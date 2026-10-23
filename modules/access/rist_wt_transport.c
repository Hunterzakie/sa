/*****************************************************************************
 * rist_wt_transport.c: RIST transport ops over a WebTransport datagram ring
 *****************************************************************************
 * Copyright (C) 2026 Sergio Ammirata
 *
 * Authors: Sergio Ammirata <sergio@ammirata.net>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

/* The transport ops (recvfrom/sendto/poll) are EM_JS thunks over
 * SharedArrayBuffer rings in WASM linear memory. The WebTransport reader/writer
 * loops live in the hosting page's JavaScript and reach the rings through the
 * exported accessors.
 *
 * Sessions are independent: one page can drive several RIST sources at once
 * (e.g. a wall of video tiles). The host creates a session with
 * rist_wt_create_session(), starts a WebTransport reader/writer over that
 * session's rings (rist_wt_get_{rx,tx}_ring), and passes the session id to the
 * matching RIST access via the rist-wt-session option. The access then claims
 * that session through rist_wt_transport_ops().
 *
 * This file is compiled only for the Emscripten target (see modules/access/
 * Makefile.am, which adds it to librist_plugin_la_SOURCES under
 * "if HAVE_EMSCRIPTEN"). It therefore carries no file-wide #ifdef. */

#include "rist_wt_transport.h"
#include <librist/transport.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <poll.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sys/socket.h>
#include <emscripten/emscripten.h>
#include <emscripten/em_js.h>

#define WT_MAX_DGRAM   1500
#define WT_RING_SLOTS  512
#define WT_HEADER_INTS 4
#define WT_SLOT_SIZE   (4 + WT_MAX_DGRAM)
#define WT_RING_BYTES  (WT_HEADER_INTS * 4 + WT_RING_SLOTS * WT_SLOT_SIZE)

/* Per-session transport context. The ring pair is owned here and reached by the
 * librist callbacks through their opaque handle, never through a global, so each
 * session's data path is independent of every other session. */
struct rist_wt_session {
    struct rist_transport_ops ops;
    uint8_t *rx_ring;
    uint8_t *tx_ring;
    atomic_int active;   /* claimed by at most one RIST access at a time */
    bool      allocated; /* false once destroyed; slot is then reusable */
};

/* Registry of sessions, indexed by the id returned from rist_wt_create_session().
 * The array holds pointers so growth never moves a live session (the librist
 * opaque handle and the rings must keep stable addresses); the session struct is
 * never freed once created, only its rings, so a callback racing teardown finds a
 * valid struct with NULL rings rather than freed memory. The lock guards registry
 * bookkeeping (create/get/claim/destroy), not the per-packet path, which uses the
 * opaque handle directly. */
static struct rist_wt_session **g_sessions;
static int g_session_count;
static int g_session_cap;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

/* Layout/ring accessors and the session lifecycle are reached from JS (EM_JS
 * bridge) only; declare them up-front to satisfy -Wmissing-prototypes since they
 * have no C caller and intentionally have no public header. */
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_max_dgram(void);
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_ring_slots(void);
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_header_ints(void);
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_slot_size(void);
EMSCRIPTEN_KEEPALIVE int      rist_wt_create_session(void);
EMSCRIPTEN_KEEPALIVE void     rist_wt_destroy_session(int session_id);
EMSCRIPTEN_KEEPALIVE void    *rist_wt_get_rx_ring(int session_id);
EMSCRIPTEN_KEEPALIVE void    *rist_wt_get_tx_ring(int session_id);

EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_max_dgram(void)   { return WT_MAX_DGRAM; }
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_ring_slots(void)  { return WT_RING_SLOTS; }
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_header_ints(void) { return WT_HEADER_INTS; }
EMSCRIPTEN_KEEPALIVE uint32_t rist_wt_slot_size(void)   { return WT_SLOT_SIZE; }

/*
 * EM_JS transport ops — return negative errno on failure (e.g. -11
 * for EAGAIN) so the C wrappers can set errno properly without
 * needing to call ___errno_location from JavaScript.
 */

EM_JS(int, rist_wt_js_recvfrom,
      (int buf, int len, int addr, int addrlen_ptr, int rx_ring),
{
    if (!rx_ring) return -11; /* EAGAIN */
    var ci = rx_ring >> 2;

    var count = Atomics.load(HEAP32, ci + 2);
    if (count <= 0) {
        var r = Atomics.wait(HEAP32, ci + 2, 0, 5);
        if (r === 'timed-out') return -11;
        count = Atomics.load(HEAP32, ci + 2);
        if (count <= 0) return -11;
    }

    var readIdx = Atomics.load(HEAP32, ci + 1);
    var off = rx_ring + 16 + readIdx * 1504; /* HEADER_INTS*4 + idx*SLOT_SIZE */
    var pktLen = HEAP32[off >> 2];
    var copyLen = Math.min(pktLen, len);

    HEAPU8.copyWithin(buf, off + 4, off + 4 + copyLen);

    if (addr) {
        HEAPU8.fill(0, addr, addr + 16);
        HEAP16[addr >> 1] = 2;              /* AF_INET */
        HEAP16[(addr >> 1) + 1] = 0x8813;   /* port 5000 network byte order */
        HEAPU8[addr + 4] = 127;
        HEAPU8[addr + 7] = 1;               /* 127.0.0.1 */
        if (addrlen_ptr) HEAP32[addrlen_ptr >> 2] = 16;
    }

    Atomics.store(HEAP32, ci + 1, (readIdx + 1) % 512);
    Atomics.sub(HEAP32, ci + 2, 1);
    return copyLen;
});

EM_JS(int, rist_wt_js_sendto,
      (int buf, int len, int tx_ring),
{
    if (!tx_ring || len > 1500) return -90; /* EMSGSIZE */
    var ci = tx_ring >> 2;

    var count = Atomics.load(HEAP32, ci + 2);
    if (count >= 512) return -11; /* EAGAIN */

    var writeIdx = Atomics.load(HEAP32, ci);
    var off = tx_ring + 16 + writeIdx * 1504;
    HEAP32[off >> 2] = len;
    HEAPU8.copyWithin(off + 4, buf, buf + len);

    Atomics.store(HEAP32, ci, (writeIdx + 1) % 512);
    Atomics.add(HEAP32, ci + 2, 1);
    Atomics.notify(HEAP32, ci + 2);
    return len;
});

/*
 * Poll override.
 *
 * Only the first pollfd is marked POLLIN — we intentionally do NOT
 * flag every registered fd just because the WT ring has data, because
 * librist may have additional fds (OOB, multicast, RTCP) that have no
 * relationship to the WebTransport ring.  Falsely marking those would
 * trigger spurious recvfrom calls that return EAGAIN, and any librist
 * release without the matching upstream fix
 * ("transport: match canonical EAGAIN errno values defensively...")
 * would then log "Receive failed: errno=11" until the handshake gave
 * up.  Returning POLLIN on a single fd keeps the load constant
 * regardless of librist version.
 */
EM_JS(int, rist_wt_js_poll,
      (int fds, int nfds, int timeout_ms, int rx_ring),
{
    if (!rx_ring || nfds <= 0) return 0;
    var ci = rx_ring >> 2;

    var count = Atomics.load(HEAP32, ci + 2);
    if (count <= 0 && timeout_ms > 0) {
        Atomics.wait(HEAP32, ci + 2, 0, timeout_ms);
        count = Atomics.load(HEAP32, ci + 2);
    }
    if (count <= 0) return 0;

    HEAP16[((fds + 6) >> 1)] = 1; /* revents = POLLIN on fds[0] only */
    return 1;
});

/* C wrappers matching struct rist_transport_ops signatures.  The session is
 * reached through the opaque handle librist passes back, not a global. */

/*
 * Set errno via __errno_location() directly to guarantee we hit the
 * same thread-local slot that librist (linked from a separate compile
 * unit) reads.  In Emscripten + pthreads builds we have observed cases
 * where librist's translation unit ends up with an EAGAIN macro that
 * does NOT match the canonical 11 used by musl at runtime, so we also
 * set errno via the local macro (which we expect to be 11 here) and
 * trust that a recent enough librist accepts both values defensively.
 * See the upstream commit "transport: match canonical EAGAIN errno
 * values defensively in rist_peer_recv".
 *
 * For older librist releases that pre-date that fix we additionally
 * limit poll() to a single fd (see rist_wt_js_poll above) so that the
 * default-socket fallback path is exercised far less often, keeping
 * the log clean even when the EAGAIN check on the receiver side mis-
 * fires.
 */
extern int *__errno_location(void);

static ssize_t wt_recvfrom(void *opaque, int fd, void *buf, size_t len,
                            int flags, struct sockaddr *addr,
                            socklen_t *addrlen)
{
    struct rist_wt_session *s = opaque;
    (void)fd; (void)flags;
    int ret = rist_wt_js_recvfrom((int)(uintptr_t)buf, (int)len,
                                   (int)(uintptr_t)addr,
                                   (int)(uintptr_t)addrlen,
                                   (int)(uintptr_t)s->rx_ring);
    /* ANY non-positive value means no data available.
     * librist callers expect ret=-1 + errno=EAGAIN, not ret=0. */
    if (ret <= 0) {
        *__errno_location() = EAGAIN;
        errno = EAGAIN;
        return -1;
    }
    return ret;
}

static ssize_t wt_sendto(void *opaque, int fd, const void *buf, size_t len,
                          int flags, const struct sockaddr *addr,
                          socklen_t addrlen)
{
    struct rist_wt_session *s = opaque;
    (void)fd; (void)flags; (void)addr; (void)addrlen;
    int ret = rist_wt_js_sendto((int)(uintptr_t)buf, (int)len,
                                 (int)(uintptr_t)s->tx_ring);
    if (ret < 0) {
        errno = -ret;
        return -1;
    }
    return ret;
}

static int wt_poll(void *opaque, struct pollfd *fds, int nfds,
                    int timeout_ms)
{
    struct rist_wt_session *s = opaque;
    return rist_wt_js_poll((int)(uintptr_t)fds, nfds, timeout_ms,
                            (int)(uintptr_t)s->rx_ring);
}

/* Caller must hold g_lock. Returns the live session for id, or NULL. */
static struct rist_wt_session *session_get(int session_id)
{
    if (session_id < 0 || session_id >= g_session_count)
        return NULL;
    struct rist_wt_session *s = g_sessions[session_id];
    return (s && s->allocated) ? s : NULL;
}

/* Reset writeIdx/readIdx/count so a fresh claim does not see leftover datagrams.
 * The slot-count field is preserved. */
static void rist_wt_reset_rings(struct rist_wt_session *s)
{
    int32_t *rx = (int32_t *)s->rx_ring;
    int32_t *tx = (int32_t *)s->tx_ring;
    atomic_store((_Atomic int32_t *)&rx[0], 0);
    atomic_store((_Atomic int32_t *)&rx[1], 0);
    atomic_store((_Atomic int32_t *)&rx[2], 0);
    atomic_store((_Atomic int32_t *)&tx[0], 0);
    atomic_store((_Atomic int32_t *)&tx[1], 0);
    atomic_store((_Atomic int32_t *)&tx[2], 0);
}

/* Allocate a session and its ring pair, returning its id (>= 0) or -1.
 * The host page calls this once per RIST source before playback, then fetches
 * the rx/tx pointers and starts that session's WebTransport reader/writer. */
EMSCRIPTEN_KEEPALIVE int rist_wt_create_session(void)
{
    pthread_mutex_lock(&g_lock);

    int id = -1;
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i] && !g_sessions[i]->allocated) {
            id = i;
            break;
        }
    }

    struct rist_wt_session *s;
    if (id >= 0) {
        s = g_sessions[id];
    } else {
        if (g_session_count == g_session_cap) {
            int ncap = g_session_cap ? g_session_cap * 2 : 4;
            struct rist_wt_session **na =
                realloc(g_sessions, (size_t)ncap * sizeof(*na));
            if (!na) {
                pthread_mutex_unlock(&g_lock);
                return -1;
            }
            g_sessions = na;
            g_session_cap = ncap;
        }
        s = calloc(1, sizeof(*s));
        if (!s) {
            pthread_mutex_unlock(&g_lock);
            return -1;
        }
        id = g_session_count++;
        g_sessions[id] = s;
    }

    s->rx_ring = calloc(1, WT_RING_BYTES);
    s->tx_ring = calloc(1, WT_RING_BYTES);
    if (!s->rx_ring || !s->tx_ring) {
        free(s->rx_ring);
        free(s->tx_ring);
        s->rx_ring = NULL;
        s->tx_ring = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
    ((int32_t *)s->rx_ring)[3] = WT_RING_SLOTS;
    ((int32_t *)s->tx_ring)[3] = WT_RING_SLOTS;

    s->ops.opaque   = s;
    s->ops.sendto   = wt_sendto;
    s->ops.recvfrom = wt_recvfrom;
    s->ops.poll     = wt_poll;
    atomic_store(&s->active, 0);
    s->allocated = true;

    pthread_mutex_unlock(&g_lock);
    return id;
}

/* Free a session's rings and mark its slot reusable. The host calls this after
 * the matching access has stopped (rist_wt_transport_ops_release). The struct is
 * kept so a late librist callback finds NULL rings (EAGAIN) rather than freed
 * memory. */
EMSCRIPTEN_KEEPALIVE void rist_wt_destroy_session(int session_id)
{
    pthread_mutex_lock(&g_lock);
    struct rist_wt_session *s = session_get(session_id);
    if (s) {
        free(s->rx_ring);
        free(s->tx_ring);
        s->rx_ring = NULL;
        s->tx_ring = NULL;
        atomic_store(&s->active, 0);
        s->allocated = false;
    }
    pthread_mutex_unlock(&g_lock);
}

EMSCRIPTEN_KEEPALIVE void *rist_wt_get_rx_ring(int session_id)
{
    pthread_mutex_lock(&g_lock);
    struct rist_wt_session *s = session_get(session_id);
    void *ring = s ? s->rx_ring : NULL;
    pthread_mutex_unlock(&g_lock);
    return ring;
}

EMSCRIPTEN_KEEPALIVE void *rist_wt_get_tx_ring(int session_id)
{
    pthread_mutex_lock(&g_lock);
    struct rist_wt_session *s = session_get(session_id);
    void *ring = s ? s->tx_ring : NULL;
    pthread_mutex_unlock(&g_lock);
    return ring;
}

const struct rist_transport_ops *rist_wt_transport_ops(int session_id)
{
    pthread_mutex_lock(&g_lock);
    struct rist_wt_session *s = session_get(session_id);
    if (!s) {
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }

    /* One access per session at a time: a second claim would mix its datagrams
     * with the first. */
    int prev = atomic_fetch_add(&s->active, 1);
    if (prev != 0) {
        atomic_fetch_sub(&s->active, 1);
        pthread_mutex_unlock(&g_lock);
        return NULL;
    }

    rist_wt_reset_rings(s);
    const struct rist_transport_ops *ops = &s->ops;
    pthread_mutex_unlock(&g_lock);
    return ops;
}

void rist_wt_transport_ops_release(int session_id)
{
    pthread_mutex_lock(&g_lock);
    struct rist_wt_session *s = session_get(session_id);
    if (s)
        atomic_fetch_sub(&s->active, 1);
    pthread_mutex_unlock(&g_lock);
}
