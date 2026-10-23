// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * backend.c: Whisper backend functions (process/thread)
 *****************************************************************************
 * Copyright © 2025 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_interrupt.h>
#include <vlc_process.h>

#include "whisper_internal.h"
#include "whisper_protocol.h"

#ifndef _WIN32
#define STT_WHISPER_PROCESS_NAME "vlc-whisper"
#define STT_WHISPER_STATIC_PROCESS_NAME "vlc-whisper-static"
#else
#define STT_WHISPER_PROCESS_NAME "vlc-whisper.exe"
#define STT_WHISPER_STATIC_PROCESS_NAME "vlc-whisper-static.exe"
#endif

/* The short-write retry loop lives here, under the lock, so that a whole
 * protocol message reaches the pipe without another thread interleaving one
 * of its own. */
static ssize_t
stt_whisper_process_Send(void *opaque, const uint8_t *buf, size_t size)
{
    assert(opaque != NULL);
    struct stt_whisper_backend_process *sys = opaque;

    size_t sent = 0;

    vlc_mutex_lock(&sys->send_lock);
    while (sent < size) {
        ssize_t ret = vlc_process_fd_Write(sys->process, buf + sent,
                                           size - sent, -1);
        if (ret <= 0) {
            vlc_mutex_unlock(&sys->send_lock);
            return ret;
        }
        sent += ret;
    }
    vlc_mutex_unlock(&sys->send_lock);

    return sent;
}

static ssize_t
stt_whisper_process_Recv(void *opaque, uint8_t *buf, size_t size)
{
    assert(opaque != NULL);
    struct stt_whisper_backend_process *sys = opaque;

    return vlc_process_fd_Read(sys->process, buf, size, -1);
}

static int
stt_whisper_backend_process_AddAudio(struct stt_whisper_backend *backend,
                                     vlc_frame_t *frame)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (!atomic_load(&sys->running) || frame == NULL) {
        if (frame != NULL) {
            vlc_frame_Release(frame);
        }
        return VLC_EGENERIC;
    }
    int ret = stt_whisper_command_send_AddAudio(frame, stt_whisper_process_Send,
                                                sys);
    vlc_frame_Release(frame);
    return ret;
}

static int
stt_whisper_backend_process_Drain(struct stt_whisper_backend *backend,
                                  vlc_frame_t *frame)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (!atomic_load(&sys->running)) {
        if (frame != NULL) {
            vlc_frame_Release(frame);
        }
        return VLC_EGENERIC;
    }
    int ret = stt_whisper_command_send_Drain(frame, stt_whisper_process_Send,
                                             sys);
    if (frame != NULL) {
        vlc_frame_Release(frame);
    }
    return ret;
}

static int
stt_whisper_backend_process_Flush(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (!atomic_load(&sys->running)) {
        return VLC_EGENERIC;
    }
    return stt_whisper_command_send_Flush(stt_whisper_process_Send,
                                          sys);
}

static int
stt_whisper_backend_process_SetParams(struct stt_whisper_backend *backend,
                                      const struct stt_whisper_params *params)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (!atomic_load(&sys->running)) {
        return VLC_EGENERIC;
    }
    return stt_whisper_command_send_SetParams(params, stt_whisper_process_Send,
                                              sys);
}

static int
stt_whisper_backend_process_Start(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    bool running = atomic_load(&sys->running);
    if (!running) {
        return VLC_EGENERIC;
    }
    return stt_whisper_command_send_Start(stt_whisper_process_Send, sys);
}

static void
stt_whisper_backend_process_Stop(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    bool running = atomic_load(&sys->running);
    if (!running) {
        return;
    }
    stt_whisper_command_send_Stop(stt_whisper_process_Send, sys);
}

static void
stt_whisper_backend_process_RequestAbort(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (atomic_load(&sys->running)) {
        stt_whisper_command_send_Abort(stt_whisper_process_Send, sys);
    }
    vlc_interrupt_kill(sys->interrupt);
}

static void
stt_whisper_backend_process_Cancel(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    if (atomic_load(&sys->running)) {
        stt_whisper_command_send_Close(stt_whisper_process_Send, sys);
    }
}

static void
stt_whisper_backend_process_Close(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);
    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    stt_whisper_backend_process_Cancel(backend);
    vlc_interrupt_kill(sys->interrupt);
    vlc_join(sys->th, NULL);
    vlc_process_Terminate(sys->process, true);
    vlc_interrupt_destroy(sys->interrupt);
    backend->sys = NULL;
    free(sys);
}

static void *
stt_whisper_backend_process_Thread(void *data)
{
    assert(data != NULL);
    struct stt_whisper_backend *backend = data;

    assert(backend->sys != NULL);
    struct stt_whisper_backend_process *sys = backend->sys;

    vlc_interrupt_set(sys->interrupt);
    stt_whisper_command_RecvLoop(backend, stt_whisper_process_Recv, sys,
                                 stt_whisper_process_Send, sys);
    vlc_interrupt_set(NULL);
    atomic_store(&sys->running, false);

    struct vlc_stt_ctx *ctx = backend->owner.opaque;
    vlc_mutex_lock(&ctx->lock);
    enum vlc_stt_ctx_status status = ctx->status;
    vlc_mutex_unlock(&ctx->lock);

    if (backend->owner.cbs != NULL
        && status != vlc_stt_ctx_status_done
        && status != vlc_stt_ctx_status_error) {
        assert(backend->owner.cbs->ready != NULL);
        backend->owner.cbs->ready(backend->owner.opaque,
                                  vlc_stt_ctx_status_error);
    }
    return NULL;
}

int
stt_whisper_backend_process_Init(struct stt_whisper_backend *backend)
{
    assert(backend != NULL);

    backend->type = stt_whisper_backend_process;

    struct stt_whisper_backend_process *sys = malloc(sizeof(*sys));
    if (sys == NULL) {
        return VLC_ENOMEM;
    }
    backend->sys = sys;

    char *path = NULL;
    const char *lib_path = getenv("VLC_LIB_PATH");
    if (lib_path != NULL) {
        size_t len = strlen(lib_path);
        if (len > 8 && strcmp(lib_path + len - 8, "/modules") == 0) {
            if (asprintf(&path, "%.*s/bin/%s", (int)(len - 8),
                         lib_path, STT_WHISPER_STATIC_PROCESS_NAME) < 0) {
                path = NULL;
            }
        }
    }

    if (path == NULL) {
        path = config_GetSysPath(VLC_PKG_LIBEXEC_DIR,
                                 STT_WHISPER_PROCESS_NAME);
    }
    if (path == NULL) {
        free(sys);
        backend->sys = NULL;
        return VLC_EGENERIC;
    }
    sys->process = vlc_process_Spawn(path, 0, NULL);
    free(path);

    if (sys->process == NULL) {
        free(sys);
        backend->sys = NULL;
        return VLC_EGENERIC;
    }

    sys->interrupt = vlc_interrupt_create();
    if (sys->interrupt == NULL) {
        vlc_process_Terminate(sys->process, true);
        free(sys);
        backend->sys = NULL;
        return VLC_ENOMEM;
    }
    atomic_init(&sys->running, true);
    vlc_mutex_init(&sys->send_lock);

    static const struct stt_whisper_backend_operations ops = {
        .add_audio = stt_whisper_backend_process_AddAudio,
        .drain = stt_whisper_backend_process_Drain,
        .flush = stt_whisper_backend_process_Flush,
        .set_params = stt_whisper_backend_process_SetParams,
        .start = stt_whisper_backend_process_Start,
        .stop = stt_whisper_backend_process_Stop,
        .cancel = stt_whisper_backend_process_Cancel,
        .close = stt_whisper_backend_process_Close,
        .request_abort = stt_whisper_backend_process_RequestAbort,
    };
    backend->ops = &ops;

    int ret = vlc_clone(&sys->th, stt_whisper_backend_process_Thread, backend);
    if (ret != VLC_SUCCESS) {
        backend->ops = NULL;
        vlc_process_Terminate(sys->process, true);
        vlc_interrupt_destroy(sys->interrupt);
        free(sys);
        backend->sys = NULL;
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}
