/*****************************************************************************
 * network.m: access module using Apple's Network.framework
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Felix Paul Kühne <fkuehne # videolan.org>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

#include <vlc_common.h>
#include <vlc_access.h>
#include <vlc_block.h>
#include <vlc_charset.h>
#include <vlc_http.h>
#include <vlc_input_item.h>
#include <vlc_keystore.h>
#include <vlc_meta.h>
#include <vlc_plugin.h>
#include <vlc_strings.h>
#include <vlc_tls.h>
#include <vlc_url.h>

#include "http/conn.h"
#include "http/message.h"

#include <sys/uio.h>

#import <TargetConditionals.h>
#import <Foundation/Foundation.h>
#import <Network/Network.h>
#import <os/lock.h>

/* Network.framework minimum deployment targets */
#define MIN_MACOS    10.14
#define MIN_IOS      12.0
#define MIN_TVOS     12.0
#define MIN_WATCHOS  5.0

/* work-around to fix compilation on older Xcode releases */
#if defined(TARGET_OS_VISION) && TARGET_OS_VISION
#define MIN_VISIONOS 1.0
#define VISIONOS_API_AVAILABLE   , visionos(MIN_VISIONOS)
#define VISIONOS_AVAILABLE_CHECK , visionOS MIN_VISIONOS
#else
#define VISIONOS_API_AVAILABLE
#define VISIONOS_AVAILABLE_CHECK
#endif

#define NW_API API_AVAILABLE(macos(MIN_MACOS), ios(MIN_IOS), tvos(MIN_TVOS), watchos(MIN_WATCHOS) VISIONOS_API_AVAILABLE)

#define CONNECT_TIMEOUT_SECS  15

@class VLCNetworkTransport;

typedef struct {
    vlc_tls_t tls;
    __unsafe_unretained VLCNetworkTransport *owner;
} nw_tls_t;

NW_API
@interface VLCNetworkTransport : NSObject
@property (nonatomic, readonly, nullable) const char *negotiatedALPN;
@property (nonatomic, readonly, nullable) vlc_tls_t *tlsStream;
/* Read end of the wakeup pipe, used by NWShimGetFD as the pollable fd. */
@property (nonatomic, readonly) int pipeReadFD;

- (instancetype)initWithAccess:(stream_t *)access
                          host:(NSString *)host
                          port:(uint16_t)port
                           tls:(BOOL)tls;

- (BOOL)waitReadyWithTimeout:(NSTimeInterval)timeout;
- (BOOL)send:(const void *)buf length:(size_t)len;

/* Returns bytes copied (>0), 0 on EOF, -1 if no data is available yet. */
- (ssize_t)receiveInto:(void *)buf length:(size_t)maxLen;
- (void)cancel;
@end

NW_API
extern const struct vlc_tls_operations nw_tls_ops;

/* Boolean wakeup pipe: signal writes one byte, drain empties the fifo. */
static inline void pipe_signal(int fd)
{
    uint8_t byte = 1;
    (void)write(fd, &byte, 1);
}

static inline void pipe_drain(int fd)
{
    uint8_t buf[64];
    while (read(fd, buf, sizeof(buf)) > 0);
}

NW_API
@implementation VLCNetworkTransport
{
    stream_t *_access;
    nw_connection_t _connection;
    dispatch_queue_t _queue;

    dispatch_semaphore_t _readySem;
    os_unfair_lock _lock;

    BOOL _ready;
    BOOL _failed;
    BOOL _cancelled;

    nw_tls_t *_shim;
    char *_negotiatedALPN;

    /* Wakeup pipe (pollable fd) and chunk queue. Chunks are kept as
     * dispatch_data_t with a head offset so receiveInto: copies straight
     * into the caller's buffer. _receiveInFlight gates on-demand
     * scheduling: at most one nw_connection_receive is outstanding, which
     * gives us TCP-level backpressure for free. Protected by _recvLock. */
    int                                 _pipefds[2];
    NSMutableArray<dispatch_data_t>    *_recvBufs;
    size_t                              _recvBufOffset;
    os_unfair_lock                      _recvLock;
    BOOL                                _recvEOF;
    BOOL                                _receiveInFlight;
}

- (instancetype)initWithAccess:(stream_t *)access
                          host:(NSString *)host
                          port:(uint16_t)port
                           tls:(BOOL)tls
{
    self = [super init];
    if (!self)
        return nil;

    _shim = malloc(sizeof(*_shim));
    if (!_shim)
        return nil;
    _shim->tls.ops = &nw_tls_ops;
    _shim->tls.p   = NULL;
    _shim->owner   = self;

    _access = access;
    _lock = OS_UNFAIR_LOCK_INIT;
    _readySem = dispatch_semaphore_create(0);

    _pipefds[0] = _pipefds[1] = -1;
    if (pipe(_pipefds) != 0) {
        free(_shim);
        return nil;
    }
    fcntl(_pipefds[0], F_SETFL, O_NONBLOCK);
    fcntl(_pipefds[1], F_SETFL, O_NONBLOCK);

    _recvBufs        = [NSMutableArray new];
    _recvBufOffset   = 0;
    _recvLock        = OS_UNFAIR_LOCK_INIT;
    _recvEOF         = NO;
    _receiveInFlight = NO;

    NSString *portStr = [NSString stringWithFormat:@"%u", (unsigned)port];
    nw_endpoint_t endpoint =
        nw_endpoint_create_host([host UTF8String], [portStr UTF8String]);

    nw_parameters_t params;
    if (tls) {
        /* Advertise HTTP/2 and HTTP/1.1 via TLS ALPN so the server can
         * pick the multiplexed protocol when supported */
        params = nw_parameters_create_secure_tcp(
            ^(nw_protocol_options_t tls_options) {
                sec_protocol_options_t sec =
                    nw_tls_copy_sec_protocol_options(tls_options);
                sec_protocol_options_add_tls_application_protocol(sec, "h2");
                sec_protocol_options_add_tls_application_protocol(sec,
                                                                  "http/1.1");
            },
            NW_PARAMETERS_DEFAULT_CONFIGURATION);
    } else {
        params = nw_parameters_create_secure_tcp(
            NW_PARAMETERS_DISABLE_PROTOCOL,
            NW_PARAMETERS_DEFAULT_CONFIGURATION);
    }

    _connection = nw_connection_create(endpoint, params);
    _queue = dispatch_queue_create("org.videolan.vlc.access.network",
                                   DISPATCH_QUEUE_SERIAL);
    nw_connection_set_queue(_connection, _queue);

    __weak __typeof(self) weakSelf = self;
    nw_connection_set_state_changed_handler(_connection,
        ^(nw_connection_state_t state, nw_error_t error) {
            __strong __typeof(weakSelf) strong = weakSelf;
            if (!strong)
                return;
            [strong handleStateChange:state error:error];
        });

    nw_connection_start(_connection);
    return self;
}

- (void)dealloc
{
    if (_connection) {
        nw_connection_cancel(_connection);
        _connection = nil;
    }
    if (_pipefds[0] >= 0) close(_pipefds[0]);
    if (_pipefds[1] >= 0) close(_pipefds[1]);
    free(_shim);
    free(_negotiatedALPN);
}

- (int)pipeReadFD
{
    return _pipefds[0];
}

- (vlc_tls_t *)tlsStream
{
    return &_shim->tls;
}

- (void)handleStateChange:(nw_connection_state_t)state error:(nw_error_t)error
{
    stream_t *access = _access;
    switch (state) {
        case nw_connection_state_ready: {
            nw_protocol_definition_t tls_def = nw_protocol_copy_tls_definition();
            nw_protocol_metadata_t meta =
                nw_connection_copy_protocol_metadata(_connection, tls_def);
            if (meta != NULL) {
                sec_protocol_metadata_t sec = (sec_protocol_metadata_t)meta;
                const char *proto =
                    sec_protocol_metadata_get_negotiated_protocol(sec);
                if (proto != NULL && *proto != '\0')
                    _negotiatedALPN = strdup(proto);
            }
            if (access)
                msg_Dbg(access, "connection ready (alpn=%s)",
                        _negotiatedALPN ? _negotiatedALPN : "none");
            os_unfair_lock_lock(&_lock);
            _ready = YES;
            os_unfair_lock_unlock(&_lock);
            dispatch_semaphore_signal(_readySem);
            break;
        }
        case nw_connection_state_failed: {
            int code = error ? nw_error_get_error_code(error) : 0;
            if (access) msg_Err(access, "connection failed (%d)", code);
            os_unfair_lock_lock(&_lock);
            _failed = YES;
            os_unfair_lock_unlock(&_lock);
            os_unfair_lock_lock(&_recvLock);
            _recvEOF = YES;
            os_unfair_lock_unlock(&_recvLock);
            pipe_signal(_pipefds[1]);
            dispatch_semaphore_signal(_readySem);
            break;
        }
        case nw_connection_state_cancelled:
            if (access) msg_Warn(access, "connection cancelled");
            os_unfair_lock_lock(&_lock);
            _cancelled = YES;
            os_unfair_lock_unlock(&_lock);
            os_unfair_lock_lock(&_recvLock);
            _recvEOF = YES;
            os_unfair_lock_unlock(&_recvLock);
            pipe_signal(_pipefds[1]);
            dispatch_semaphore_signal(_readySem);
            break;
        case nw_connection_state_waiting:
            if (access) msg_Dbg(access, "connection waiting");
            break;
        default:
            break;
    }
}

- (void)scheduleReceive
{
    __weak __typeof(self) weakSelf = self;
    nw_connection_receive(_connection, 1, 65536,
        ^(dispatch_data_t content, nw_content_context_t __unused ctx,
          bool is_complete, nw_error_t error) {
            __strong __typeof(weakSelf) strong = weakSelf;
            if (!strong)
                return;
            [strong handleReceive:content complete:is_complete error:error];
        });
}

- (void)handleReceive:(dispatch_data_t)content
             complete:(bool)is_complete
                error:(nw_error_t)error
{
    BOOL eof = (is_complete || error != NULL);
    if (error != NULL && _access)
        msg_Warn(_access, "receive error (%d)",
                 nw_error_get_error_code(error));

    os_unfair_lock_lock(&_recvLock);
    if (content != NULL && dispatch_data_get_size(content) > 0)
        [_recvBufs addObject:content];
    if (eof)
        _recvEOF = YES;
    _receiveInFlight = NO;
    os_unfair_lock_unlock(&_recvLock);

    pipe_signal(_pipefds[1]);
}

- (BOOL)waitReadyWithTimeout:(NSTimeInterval)timeout
{
    dispatch_time_t deadline = dispatch_time(DISPATCH_TIME_NOW,
        (int64_t)(timeout * NSEC_PER_SEC));
    if (dispatch_semaphore_wait(_readySem, deadline) != 0)
        return NO;
    os_unfair_lock_lock(&_lock);
    BOOL ready = _ready && !_failed && !_cancelled;
    os_unfair_lock_unlock(&_lock);
    return ready;
}

- (BOOL)send:(const void *)buf length:(size_t)len
{
    if (!buf || len == 0)
        return YES;

    dispatch_data_t data = dispatch_data_create(buf, len, _queue,
                                                DISPATCH_DATA_DESTRUCTOR_DEFAULT);
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    __block BOOL ok = YES;
    nw_connection_send(_connection, data, NW_CONNECTION_DEFAULT_MESSAGE_CONTEXT,
                       true, ^(nw_error_t error) {
        if (error != NULL)
            ok = NO;
        dispatch_semaphore_signal(done);
    });
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    return ok;
}

- (ssize_t)receiveInto:(void *)dst length:(size_t)maxLen
{
    pipe_drain(_pipefds[0]);

    os_unfair_lock_lock(&_recvLock);

    if (_recvBufs.count == 0) {
        BOOL eof = _recvEOF;
        BOOL needSchedule = !eof && !_receiveInFlight;
        if (needSchedule)
            _receiveInFlight = YES;
        os_unfair_lock_unlock(&_recvLock);
        if (needSchedule)
            [self scheduleReceive];
        return eof ? 0 : -1;
    }

    dispatch_data_t head = _recvBufs[0];
    size_t headLen = dispatch_data_get_size(head);
    size_t avail   = headLen - _recvBufOffset;
    size_t take    = (avail < maxLen) ? avail : maxLen;

    /* One memcpy: walk the scatter/gather regions of the head chunk,
     * skip _recvBufOffset bytes, copy `take` bytes into dst. */
    __block size_t copied = 0;
    __block size_t skip   = _recvBufOffset;
    dispatch_data_apply(head,
        ^bool(dispatch_data_t __unused rgn, size_t __unused o,
              const void *bytes, size_t size) {
            if (skip >= size) { skip -= size; return true; }
            const uint8_t *src = (const uint8_t *)bytes + skip;
            size_t region = size - skip;
            skip = 0;
            size_t want  = take - copied;
            size_t chunk = (region < want) ? region : want;
            memcpy((uint8_t *)dst + copied, src, chunk);
            copied += chunk;
            return copied < take;
        });

    if (copied == avail) {
        [_recvBufs removeObjectAtIndex:0];
        _recvBufOffset = 0;
    } else {
        _recvBufOffset += copied;
    }

    if (_recvBufs.count > 0 || _recvEOF)
        pipe_signal(_pipefds[1]);

    os_unfair_lock_unlock(&_recvLock);

    return (ssize_t)copied;
}

- (void)cancel
{
    os_unfair_lock_lock(&_lock);
    _cancelled = YES;
    os_unfair_lock_unlock(&_lock);

    os_unfair_lock_lock(&_recvLock);
    _recvEOF = YES;
    os_unfair_lock_unlock(&_recvLock);
    pipe_signal(_pipefds[1]);

    if (_connection)
        nw_connection_cancel(_connection);
    dispatch_semaphore_signal(_readySem);
}
@end

/* vlc_tls_t ops: non-blocking readv backed by the wakeup pipe; writev stays
 * synchronous since NW.framework's send queue never returns EAGAIN. */

NW_API
static VLCNetworkTransport *TransportFromTLS(vlc_tls_t *tls)
{
    return container_of(tls, nw_tls_t, tls)->owner;
}

NW_API
static int NWShimGetFD(vlc_tls_t *tls, short *restrict events)
{
    (void)events;
    return TransportFromTLS(tls).pipeReadFD;
}

NW_API
static ssize_t NWShimReadv(vlc_tls_t *tls, struct iovec *iov, unsigned niov)
{
    if (niov == 0 || iov[0].iov_len == 0)
        return 0;

    ssize_t r = [TransportFromTLS(tls) receiveInto:iov[0].iov_base
                                            length:iov[0].iov_len];
    if (r < 0) {
        errno = EAGAIN;
        return -1;
    }
    return r;
}

NW_API
static ssize_t NWShimWritev(vlc_tls_t *tls, const struct iovec *iov,
                            unsigned niov)
{
    if (niov == 0 || iov[0].iov_len == 0)
        return 0;
    if (![TransportFromTLS(tls) send:iov[0].iov_base length:iov[0].iov_len]) {
        errno = ECONNRESET;
        return -1;
    }
    return (ssize_t)iov[0].iov_len;
}

NW_API
static int NWShimShutdown(vlc_tls_t *tls, bool duplex)
{
    (void)duplex;
    [TransportFromTLS(tls) cancel];
    return 0;
}

NW_API
static void NWShimClose(vlc_tls_t *tls)
{
    (void)tls; /* storage and the transport's nw_connection are freed
                * by the transport's dealloc. */
}

NW_API
const struct vlc_tls_operations nw_tls_ops = {
    .get_fd   = NWShimGetFD,
    .readv    = NWShimReadv,
    .writev   = NWShimWritev,
    .shutdown = NWShimShutdown,
    .close    = NWShimClose,
};

typedef struct NW_API
{
    /* libvlc_http drives I/O through the transport's vlc_tls_t shim.
     * The conn is either an HTTP/2 or an HTTP/1.1 conn depending on ALPN */
    VLCNetworkTransport      *transport;
    struct vlc_http_conn     *http_conn;
    struct vlc_http_msg      *http_resp;    /* owns the active stream */
    block_t                  *http_pending; /* leftover bytes from last read */

    vlc_url_t url;
    bool      tls;

    /* Response state (reset on every request) */
    int       status;
    char     *mime;
    char     *location;    /* heap: redirect target */
    uint64_t  content_length;
    bool      has_content_length;
    uint64_t  body_remaining;  /* bytes left in current response body;
                                * UINT64_MAX = read until the server closes */
    bool      icecast;
    bool      seekable;

    /* Resource-level state (set once on initial open, preserved on seek) */
    uint64_t  total_size;
    bool      has_total_size;
    uint64_t  offset;          /* current byte offset in the resource */

    /* ICY */
    int       icy_metaint;       /* 0 = no ICY metadata framing */
    int       icy_remaining;     /* bytes until next metadata block */

    vlc_http_auth_t auth;
    char     *psz_username;
    char     *psz_password;

    vlc_http_cookie_jar_t *jar;
} access_sys_t NW_API;

/* Forward seeks within this many bytes of the current position are
 * served by draining the existing response instead of opening a new
 * connection */
#define SKIP_FORWARD_MAX      (256 * 1024)

static void ApplyIcyMeta(stream_t *access, vlc_meta_type_t type, char *value)
{
    if (!access->p_input_item || !value || !*value)
        return;
    if (EnsureUTF8(value) == NULL)
        return;
    vlc_xml_decode(value);
    input_item_SetMeta(access->p_input_item, type, value);
}

/*****************************************************************************
 * HTTP path (via libvlc_http over the vlc_tls shim; protocol-agnostic:
 * h1 and h2 share the same vlc_http_conn / vlc_http_stream / block_t
 * API - the backend is chosen at conn-creation time based on ALPN).
 *****************************************************************************/

NW_API
static void TeardownHTTP(access_sys_t *sys)
{
    if (sys->http_resp) {
        /* Destroying the response also closes the attached stream. */
        vlc_http_msg_destroy(sys->http_resp);
        sys->http_resp = NULL;
    }
    if (sys->http_pending) {
        block_Release(sys->http_pending);
        sys->http_pending = NULL;
    }
    if (sys->http_conn) {
        vlc_http_conn_release(sys->http_conn);
        sys->http_conn = NULL;
    }
    sys->transport = nil;
}

NW_API
static int OpenHTTPStream(stream_t *access, uint64_t offset)
{
    access_sys_t *sys = access->p_sys;

    /* Destroy the previous response - its auto-close terminates the
     * attached stream (RST_STREAM on h2, socket kill on h1 if body
     * wasn't fully consumed) without touching any other streams on
     * the conn. */
    if (sys->http_resp) {
        vlc_http_msg_destroy(sys->http_resp);
        sys->http_resp = NULL;
    }
    if (sys->http_pending) {
        block_Release(sys->http_pending);
        sys->http_pending = NULL;
    }

    free(sys->mime);       sys->mime = NULL;
    free(sys->location);   sys->location = NULL;
    sys->status             = 0;
    sys->content_length     = 0;
    sys->has_content_length = false;
    sys->body_remaining     = 0;
    sys->icecast            = false;
    sys->icy_metaint        = 0;
    sys->icy_remaining      = 0;

    const char *raw_path = sys->url.psz_path;
    if (!raw_path || !*raw_path) raw_path = "/";

    char *full_path = NULL;
    const char *path = raw_path;
    if (sys->url.psz_option) {
        if (asprintf(&full_path, "%s?%s", raw_path, sys->url.psz_option) < 0)
            return VLC_EGENERIC;
        path = full_path;
    }

    char *authority = vlc_http_authority(sys->url.psz_host, sys->url.i_port);
    if (!authority) {
        free(full_path);
        return VLC_EGENERIC;
    }

    struct vlc_http_msg *req = vlc_http_req_create(
        "GET", sys->tls ? "https" : "http", authority, path);
    free(authority);
    free(full_path);
    if (!req) return VLC_EGENERIC;

    char *ua = var_InheritString(access, "http-user-agent");
    vlc_http_msg_add_agent(req, ua ? ua : PACKAGE_NAME "/" PACKAGE_VERSION);
    free(ua);

    if (offset > 0)
        vlc_http_msg_add_header(req, "Range", "bytes=%llu-",
                                (unsigned long long)offset);
    vlc_http_msg_add_header(req, "Icy-MetaData", "1");
    vlc_http_msg_add_cookies(req, sys->jar);

    if (sys->psz_username != NULL && sys->psz_password != NULL) {
        char *authhdr = vlc_http_auth_FormatAuthorizationHeader(
            VLC_OBJECT(access), &sys->auth, "GET",
            sys->url.psz_path ? sys->url.psz_path : "/",
            sys->psz_username, sys->psz_password);
        if (authhdr != NULL) {
            vlc_http_msg_add_header(req, "Authorization", "%s", authhdr);
            free(authhdr);
        }
    }

    msg_Dbg(access, "open stream offset=%llu", (unsigned long long)offset);

    struct vlc_http_stream *stream =
        vlc_http_stream_open(sys->http_conn, req, false);
    vlc_http_msg_destroy(req);
    if (!stream) {
        msg_Err(access, "failed to open HTTP stream");
        return VLC_EGENERIC;
    }

    /* get_initial takes ownership of the stream: on success it attaches
     * it to the returned message (auto-closed when msg is destroyed);
     * on failure it closes the stream itself. Same contract for
     * get_final, which may iterate past 1xx continues. */
    struct vlc_http_msg *resp = vlc_http_msg_get_initial(stream);
    resp = resp ? vlc_http_msg_get_final(resp) : NULL;
    if (!resp) {
        msg_Err(access, "failed to read HTTP response headers");
        return VLC_EGENERIC;
    }

    sys->status = vlc_http_msg_get_status(resp);

    const char *val;
    if ((val = vlc_http_msg_get_header(resp, "Content-Type")) != NULL) {
        free(sys->mime);
        sys->mime = strdup(val);
    }
    if ((val = vlc_http_msg_get_header(resp, "Location")) != NULL) {
        free(sys->location);
        if (val[0] == '/') {
            if (asprintf(&sys->location, "%s://%s:%u%s",
                         sys->tls ? "https" : "http",
                         sys->url.psz_host, sys->url.i_port, val) < 0)
                sys->location = NULL;
        } else {
            sys->location = strdup(val);
        }
    }
    if ((val = vlc_http_msg_get_header(resp, "Accept-Ranges")) != NULL &&
        strcasestr(val, "bytes"))
        sys->seekable = true;
    if ((val = vlc_http_msg_get_header(resp, "Content-Range")) != NULL) {
        sys->seekable = true;
        const char *slash = strchr(val, '/');
        if (slash && slash[1] != '*' && slash[1] != '\0') {
            uint64_t total = strtoull(slash + 1, NULL, 10);
            if (total > 0) {
                sys->total_size     = total;
                sys->has_total_size = true;
            }
        }
    }

    uintmax_t body_size = vlc_http_msg_get_size(resp);
    if (body_size != (uintmax_t)-1) {
        sys->content_length     = body_size;
        sys->has_content_length = true;
        sys->body_remaining     = body_size;
    } else {
        sys->body_remaining = UINT64_MAX;
    }

    if ((val = vlc_http_msg_get_header(resp, "Icy-MetaInt")) != NULL) {
        int m = atoi(val);
        if (m > 0) {
            sys->icy_metaint   = m;
            sys->icy_remaining = m;
            sys->icecast       = true;
        }
    }
    if ((val = vlc_http_msg_get_header(resp, "Icy-Name")) != NULL) {
        sys->icecast = true;
        char *copy = strdup(val);
        if (copy) { ApplyIcyMeta(access, vlc_meta_Title, copy); free(copy); }
    }
    if ((val = vlc_http_msg_get_header(resp, "Icy-Genre")) != NULL) {
        char *copy = strdup(val);
        if (copy) { ApplyIcyMeta(access, vlc_meta_Genre, copy); free(copy); }
    }
    if ((val = vlc_http_msg_get_header(resp, "WWW-Authenticate")) != NULL)
        vlc_http_auth_ParseWwwAuthenticateHeader(
            VLC_OBJECT(access), &sys->auth, val);
    if ((val = vlc_http_msg_get_header(resp, "Authentication-Info")) != NULL)
        vlc_http_auth_ParseAuthenticationInfoHeader(
            VLC_OBJECT(access), &sys->auth, val, "GET",
            sys->url.psz_path ? sys->url.psz_path : "/",
            sys->psz_username, sys->psz_password);

    if ((val = vlc_http_msg_get_header(resp, "Server")) != NULL &&
        (strncasecmp(val, "Icecast", 7) == 0 ||
         strncasecmp(val, "Nanocaster", 10) == 0))
        sys->icecast = true;

    vlc_http_msg_get_cookies(resp, sys->jar, sys->url.psz_host,
                             sys->url.psz_path ? sys->url.psz_path : "/");

    sys->http_resp = resp;
    sys->offset  = offset;
    return VLC_SUCCESS;
}

/* Pulls exactly n bytes from the current response body, draining the
 * pending block before touching the stream. Used to collect ICY
 * metadata bursts that the server interleaves into the audio payload.
 * Returns the number of bytes actually read (< n on EOF/error). */
NW_API
static size_t HTTPReadExact(access_sys_t *sys, void *out, size_t n)
{
    uint8_t *p = out;
    size_t got = 0;
    while (got < n) {
        block_t *b = sys->http_pending;
        if (b == NULL) {
            if (!sys->http_resp)
                break;
            b = vlc_http_msg_read(sys->http_resp);
            if (b == NULL || b == vlc_http_error)
                break;
        } else {
            sys->http_pending = NULL;
        }
        size_t take = b->i_buffer < (n - got) ? b->i_buffer : (n - got);
        memcpy(p + got, b->p_buffer, take);
        got += take;
        if (take < b->i_buffer) {
            b->p_buffer += take;
            b->i_buffer -= take;
            sys->http_pending = b;
        } else {
            block_Release(b);
        }
    }
    return got;
}

NW_API
static void HTTPReadIcyMetadata(stream_t *access)
{
    access_sys_t *sys = access->p_sys;
    uint8_t sz;
    if (HTTPReadExact(sys, &sz, 1) != 1)
        return;
    size_t meta_len = (size_t)sz * 16;
    if (meta_len == 0)
        return;

    char *meta = malloc(meta_len + 1);
    if (!meta)
        return;
    if (HTTPReadExact(sys, meta, meta_len) == meta_len) {
        meta[meta_len] = '\0';
        /* StreamTitle='...';StreamUrl='...'; NUL padding */
        char *p = strcasestr(meta, "StreamTitle=");
        if (p) {
            p += sizeof("StreamTitle=") - 1;
            char quote = (*p == '\'' || *p == '"') ? *p++ : '\0';
            char *end = quote ? strchr(p, quote) : strchr(p, ';');
            if (end) *end = '\0';
            ApplyIcyMeta(access, vlc_meta_NowPlaying, p);
        }
    }
    free(meta);
}

NW_API
static block_t *ReadBlock(stream_t *access, bool *restrict eof)
{
    access_sys_t *sys = access->p_sys;

    block_t *b = sys->http_pending;
    if (b != NULL) {
        sys->http_pending = NULL;
    } else {
        if (!sys->http_resp) {
            *eof = true;
            return NULL;
        }
        b = vlc_http_msg_read(sys->http_resp);
        if (b == vlc_http_error) {
            msg_Err(access, "HTTP stream read failed");
            *eof = true;
            return NULL;
        }
        if (b == NULL) {
            /* Body drained. If the resource has more bytes than the
             * server just delivered (short response, or seek to
             * partial range), reopen at the current offset on the
             * same conn and continue. */
            if (sys->has_total_size && sys->offset < sys->total_size) {
                if (OpenHTTPStream(access, sys->offset) != VLC_SUCCESS ||
                    sys->status != 206) {
                    *eof = true;
                    return NULL;
                }
                b = vlc_http_msg_read(sys->http_resp);
                if (b == NULL || b == vlc_http_error) {
                    *eof = true;
                    return NULL;
                }
            } else {
                *eof = true;
                return NULL;
            }
        }
    }

    /* ICY: the server interleaves a metadata burst every icy_metaint
     * bytes. Slice the block at the boundary so the caller only sees
     * audio; stash the rest as pending; then pull the metadata
     * (1 length byte + n*16 bytes) out of the stream and parse it. */
    if (sys->icy_metaint > 0 && b->i_buffer > (size_t)sys->icy_remaining) {
        size_t data_part = (size_t)sys->icy_remaining;
        size_t rest = b->i_buffer - data_part;
        block_t *tail = block_Alloc(rest);
        if (tail) {
            memcpy(tail->p_buffer, b->p_buffer + data_part, rest);
            if (sys->http_pending)
                block_Release(sys->http_pending);
            sys->http_pending = tail;
        }
        b->i_buffer = data_part;
    }
    if (sys->icy_metaint > 0) {
        sys->icy_remaining -= (int)b->i_buffer;
        if (sys->icy_remaining == 0) {
            HTTPReadIcyMetadata(access);
            sys->icy_remaining = sys->icy_metaint;
        }
    }

    sys->offset += b->i_buffer;
    if (sys->body_remaining != UINT64_MAX)
        sys->body_remaining -= b->i_buffer;
    return b;
}

NW_API
static int Seek(stream_t *access, uint64_t pos)
{
    access_sys_t *sys = access->p_sys;
    if (!sys->seekable || !sys->has_total_size)
        return VLC_EGENERIC;

    /* Small forward seeks: drain bytes on the current stream instead
     * of opening a new one */
    if (sys->http_resp && pos > sys->offset && sys->icy_metaint == 0) {
        uint64_t gap = pos - sys->offset;
        if (gap <= SKIP_FORWARD_MAX && gap <= sys->body_remaining) {
            msg_Dbg(access, "skip-forward %llu bytes to offset %llu",
                    (unsigned long long)gap, (unsigned long long)pos);
            while (gap > 0) {
                block_t *b = sys->http_pending;
                if (b != NULL) {
                    sys->http_pending = NULL;
                } else {
                    b = vlc_http_msg_read(sys->http_resp);
                    if (b == NULL || b == vlc_http_error)
                        break;
                }
                size_t drain = b->i_buffer < gap ? b->i_buffer : (size_t)gap;
                sys->offset += drain;
                if (sys->body_remaining != UINT64_MAX)
                    sys->body_remaining -= drain;
                gap -= drain;
                if (drain < b->i_buffer) {
                    b->p_buffer += drain;
                    b->i_buffer -= drain;
                    sys->http_pending = b;
                } else {
                    block_Release(b);
                }
            }
            if (gap == 0)
                return VLC_SUCCESS;
            /* Partial skip - current stream died. Fall through and
             * open a fresh one at the target position. */
        }
    }

    if (OpenHTTPStream(access, pos) != VLC_SUCCESS)
        return VLC_EGENERIC;

    if (pos == 0) {
        if (sys->status != 200 && sys->status != 206) {
            msg_Err(access, "unexpected status %d at offset 0",
                    sys->status);
            return VLC_EGENERIC;
        }
    } else if (sys->status != 206) {
        msg_Err(access, "server did not honour Range request (status %d)",
                sys->status);
        return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

NW_API
static int Control(stream_t *access, int query, va_list args)
{
    access_sys_t *sys = access->p_sys;

    switch (query) {
        case STREAM_CAN_SEEK:
        case STREAM_CAN_FASTSEEK:
            *va_arg(args, bool *) = sys->seekable && sys->has_total_size;
            break;
        case STREAM_CAN_PAUSE:
        case STREAM_CAN_CONTROL_PACE:
            *va_arg(args, bool *) = true;
            break;
        case STREAM_GET_SIZE:
            if (!sys->has_total_size)
                return VLC_EGENERIC;
            *va_arg(args, uint64_t *) = sys->total_size;
            break;
        case STREAM_GET_PTS_DELAY:
            *va_arg(args, vlc_tick_t *) = VLC_TICK_FROM_MS(
                var_InheritInteger(access, "network-caching"));
            break;
        case STREAM_GET_CONTENT_TYPE: {
            char **type = va_arg(args, char **);
            if (sys->icecast && !sys->mime)
                *type = strdup("audio/mpeg");
            else if (strcasecmp(access->psz_name, "itpc") == 0)
                *type = strdup("application/rss+xml");
            else if (sys->mime)
                *type = strdup(sys->mime);
            else
                return VLC_EGENERIC;
            break;
        }
        case STREAM_SET_PAUSE_STATE:
            break;
        default:
            return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}


NW_API
static int TryOpen(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;
    access_sys_t *sys;
    int ret = VLC_EGENERIC;

    sys = calloc(1, sizeof(*sys));
    if (!sys)
        return VLC_ENOMEM;
    access->p_sys = sys;

    if (vlc_UrlParseFixup(&sys->url, access->psz_url) != 0 ||
        sys->url.psz_host == NULL || *sys->url.psz_host == '\0') {
        msg_Err(access, "invalid URL: %s", access->psz_url);
        goto error;
    }

    /* Scheme handling */
    const char *scheme = sys->url.psz_protocol ? sys->url.psz_protocol
                                               : access->psz_name;
    if (strcasecmp(scheme, "https") == 0) {
        sys->tls = true;
        if (sys->url.i_port == 0) sys->url.i_port = 443;
    } else {
        sys->tls = false;
        if (sys->url.i_port == 0) sys->url.i_port = 80;
    }

    sys->jar = var_InheritAddress(access, "http-cookies");

    vlc_credential crd;
    vlc_credential_init(&crd, &sys->url);

    int cret = vlc_credential_get(&crd, obj, NULL, NULL, NULL, NULL);
    if (cret == -EINTR) {
        vlc_credential_clean(&crd);
        goto error;
    }
    if (cret == 0) {
        sys->psz_username = strdup(crd.psz_username);
        sys->psz_password = strdup(crd.psz_password);
    }

    /* Establish the transport and let libvlc_http's h1 or h2 connection
     * state machine drive requests over the transport's vlc_tls shim.
     * TLS ALPN picks the version; plaintext HTTP always uses h1. */
    {
        NSString *host = [NSString stringWithUTF8String:sys->url.psz_host];
        sys->transport = [[VLCNetworkTransport alloc]
            initWithAccess:access
                      host:host
                      port:(uint16_t)sys->url.i_port
                       tls:sys->tls];
        if (!sys->transport ||
            ![sys->transport waitReadyWithTimeout:CONNECT_TIMEOUT_SECS]) {
            vlc_credential_clean(&crd);
            goto error;
        }

        vlc_tls_t *shim = sys->transport.tlsStream;
        const char *alpn = sys->transport.negotiatedALPN;
        if (sys->tls && alpn && strcmp(alpn, "h2") == 0)
            sys->http_conn = vlc_h2_conn_create(vlc_object_logger(access), shim);
        else
            sys->http_conn = vlc_h1_conn_create(vlc_object_logger(access), shim, false);
        if (!sys->http_conn) {
            vlc_credential_clean(&crd);
            goto error;
        }
        if (OpenHTTPStream(access, 0) != VLC_SUCCESS) {
            vlc_credential_clean(&crd);
            goto error;
        }
    }

    while (sys->status == 401) {
        if (sys->auth.psz_realm == NULL) {
            msg_Err(access, "authentication failed without realm");
            break;
        }
        crd.psz_realm = sys->auth.psz_realm;
        crd.psz_authtype = sys->auth.psz_nonce ? "Digest" : "Basic";

        if (vlc_credential_get(&crd, obj, NULL, NULL,
                               _("HTTP authentication"),
                               _("Please enter a valid login name and a "
                                 "password for realm %s."),
                               sys->auth.psz_realm) != 0)
            break;

        free(sys->psz_username);
        free(sys->psz_password);
        sys->psz_username = strdup(crd.psz_username);
        sys->psz_password = strdup(crd.psz_password);

        if (OpenHTTPStream(access, 0) != VLC_SUCCESS) {
            vlc_credential_clean(&crd);
            goto error;
        }
        msg_Dbg(access, "HTTP status %d (after auth retry)", sys->status);
    }

    if (sys->status != 401)
        vlc_credential_store(&crd, obj);
    vlc_credential_clean(&crd);

    msg_Dbg(access, "HTTP status %d", sys->status);

    if (sys->status >= 300 && sys->status < 400 && sys->location != NULL) {
        /* VLC core still owns the previous psz_url and stores it in its
         * redirect chain to free later. We must only assign, never free. */
        access->psz_url = sys->location;
        sys->location = NULL;
        ret = VLC_ACCESS_REDIRECT;
        goto error;
    }
    if (sys->status >= 400) {
        msg_Err(access, "HTTP error %d", sys->status);
        goto error;
    }

    /* Content-Range (for 206 responses) already sets total_size during
     * header parsing. Fall back to Content-Length only when the server
     * returned a full 200 response without Content-Range. */
    if (!sys->has_total_size && sys->has_content_length) {
        sys->total_size     = sys->content_length;
        sys->has_total_size = true;
    }

    access->pf_read    = NULL;
    access->pf_block   = ReadBlock;
    access->pf_seek    = (sys->seekable && sys->has_total_size) ? Seek : NULL;
    access->pf_control = Control;
    return VLC_SUCCESS;

error:
    TeardownHTTP(sys);
    vlc_http_auth_Deinit(&sys->auth);
    free(sys->psz_username);
    free(sys->psz_password);
    free(sys->mime);
    free(sys->location);
    vlc_UrlClean(&sys->url);
    free(sys);
    access->p_sys = NULL;
    return ret;
}

NW_API
static void TryClose(vlc_object_t *obj)
{
    stream_t *access = (stream_t *)obj;
    access_sys_t *sys = access->p_sys;
    if (!sys) return;

    TeardownHTTP(sys);
    vlc_http_auth_Deinit(&sys->auth);
    free(sys->psz_username);
    free(sys->psz_password);
    free(sys->mime);
    free(sys->location);
    vlc_UrlClean(&sys->url);
    free(sys);
}

/* Runtime-version-checking */
static int Open(vlc_object_t *obj)
{
    if (@available(macOS MIN_MACOS, iOS MIN_IOS, tvOS MIN_TVOS,
                   watchOS MIN_WATCHOS VISIONOS_AVAILABLE_CHECK, *))
        return TryOpen(obj);

    msg_Err((stream_t *)obj,
            "Network.framework unavailable on this OS");
    return VLC_EGENERIC;
}

static void Close(vlc_object_t *obj)
{
    if (@available(macOS MIN_MACOS, iOS MIN_IOS, tvOS MIN_TVOS,
                   watchOS MIN_WATCHOS VISIONOS_AVAILABLE_CHECK, *))
        TryClose(obj);
}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin()
    set_shortname("Network.framework access")
    set_description(N_("Access based on Network.framework"))
    set_subcategory(SUBCAT_INPUT_ACCESS)
    set_capability("access", 3)
    add_shortcut("http", "https", "itpc")
    set_callbacks(Open, Close)
vlc_module_end()
