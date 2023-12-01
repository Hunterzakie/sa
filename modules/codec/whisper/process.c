// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * process.c: Whisper external process
 *****************************************************************************
 * Copyright © 2025 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdatomic.h>
#include <stdio.h>

#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_threads.h>
#include <vlc_tls.h>
#include <vlc_tick.h>
#include <vlc_interface.h>

#include "whisper_internal.h"
#include "whisper_protocol.h"

#include "../../../lib/libvlc_internal.h"

const char vlc_module_name[] = "vlc-whisper";

struct stt_whisper_process_ctx {
    vlc_object_t *obj;
    struct stt_whisper_backend *backend;

    struct vlc_tls *tls_in;
    struct vlc_tls *tls_out;
    /* Serialises whole messages on the output channel: log messages are
     * sent from any thread. */
    vlc_mutex_t send_lock;

    /* Synchronises the loader thread (which blocks in confirm()) with the
     * receive loop thread (which delivers the host's reply). */
    vlc_mutex_t confirm_lock;
    vlc_cond_t confirm_cond;
    bool confirm_pending;
    bool confirm_result;
    bool confirm_disabled; /* host gone: stop waiting, answer "cancel". */
};

static ssize_t
stt_whisper_process_Read(void *opaque, uint8_t *buf, size_t size)
{
    ssize_t ret = -1;
    if (opaque == NULL) {
        ret = read(STDIN_FILENO, buf, size);
        return ret;
    }
    struct vlc_tls *tls = opaque;
    ret = vlc_tls_Read(tls, buf, size, false);
    return ret;
}

/* The short-write retry loop lives here, under the lock, so that a whole
 * protocol message reaches the pipe without another thread interleaving one
 * of its own. */
static ssize_t
stt_whisper_process_Write(void *opaque, const uint8_t *buf, size_t size)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    size_t sent = 0;

    vlc_mutex_lock(&ctx->send_lock);
    while (sent < size) {
        ssize_t ret;
        if (ctx->tls_out == NULL) {
            ret = write(STDOUT_FILENO, buf + sent, size - sent);
        } else {
            ret = vlc_tls_Write(ctx->tls_out, buf + sent, size - sent);
        }
        if (ret <= 0) {
            vlc_mutex_unlock(&ctx->send_lock);
            return ret;
        }
        sent += ret;
    }
    vlc_mutex_unlock(&ctx->send_lock);

    return sent;
}

/**
 * Forward a log message of this process to the host.
 */
static void
stt_whisper_process_Log(void *opaque, int type, const vlc_log_t *item,
                        const char *fmt, va_list ap)
{
    assert(opaque != NULL);
    assert(item != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    char *msg;
    if (vasprintf(&msg, fmt, ap) < 0) {
        return;
    }
    stt_whisper_command_send_Log(type, item->psz_module, item->file,
                                 item->line, msg, stt_whisper_process_Write,
                                 ctx);
    free(msg);
}

static void
stt_whisper_process_Ready(void *opaque, enum vlc_stt_ctx_status status)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    stt_whisper_command_send_Ready(status, stt_whisper_process_Write,
                                   ctx);
}


static int
stt_whisper_process_SendSubtitle(void *opaque, vlc_frame_t *sub)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    stt_whisper_command_send_SendSubtitle(sub, stt_whisper_process_Write,
                                          ctx);
    vlc_frame_Release(sub);
    return VLC_SUCCESS;
}

static void
stt_whisper_process_OnProgress(void *opaque, enum vlc_stt_ctx_status status,
                               int64_t pos, int64_t total,
                               const char *description)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    stt_whisper_command_send_OnProgress(status, pos, total, description,
                                        stt_whisper_process_Write,
                                        ctx);
}

/* Loader-thread callback: forward the download confirmation to the host and
 * block until the receive loop delivers the answer (or the load is aborted /
 * the host disconnects). */
static bool
stt_whisper_process_Confirm(void *opaque, uint64_t download_size,
                            const char *path)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;
    assert(ctx->backend != NULL);
    assert(ctx->backend->type == stt_whisper_backend_thread);
    struct stt_whisper_backend_thread *bsys = ctx->backend->sys;

    vlc_mutex_lock(&ctx->confirm_lock);
    if (ctx->confirm_disabled) {
        vlc_mutex_unlock(&ctx->confirm_lock);
        return false;
    }
    ctx->confirm_pending = true;
    ctx->confirm_result = false;
    vlc_mutex_unlock(&ctx->confirm_lock);

    if (stt_whisper_command_send_Confirm(download_size, path,
                                         stt_whisper_process_Write,
                                         ctx) != VLC_SUCCESS) {
        vlc_mutex_lock(&ctx->confirm_lock);
        ctx->confirm_pending = false;
        vlc_mutex_unlock(&ctx->confirm_lock);
        return false;
    }

    vlc_mutex_lock(&ctx->confirm_lock);
    /* Poll the loader abort flag while waiting so an Abort command (or backend
     * teardown) unblocks us even if no reply ever comes. */
    while (ctx->confirm_pending && !ctx->confirm_disabled
           && !atomic_load(&bsys->abort)) {
        vlc_cond_timedwait(&ctx->confirm_cond, &ctx->confirm_lock,
                           vlc_tick_now() + VLC_TICK_FROM_MS(100));
    }
    bool accepted = !ctx->confirm_pending && ctx->confirm_result;
    ctx->confirm_pending = false;
    vlc_mutex_unlock(&ctx->confirm_lock);

    return accepted && !atomic_load(&bsys->abort);
}

/* Receive-loop callback: the host answered a pending confirm request. */
static void
stt_whisper_process_ConfirmReply(void *opaque, bool accepted)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;

    vlc_mutex_lock(&ctx->confirm_lock);
    ctx->confirm_result = accepted;
    ctx->confirm_pending = false;
    vlc_cond_signal(&ctx->confirm_cond);
    vlc_mutex_unlock(&ctx->confirm_lock);
}

/* Release any loader thread blocked in confirm() once the host is gone. */
static void
stt_whisper_process_DisableConfirm(struct stt_whisper_process_ctx *ctx)
{
    vlc_mutex_lock(&ctx->confirm_lock);
    ctx->confirm_disabled = true;
    ctx->confirm_pending = false;
    vlc_cond_signal(&ctx->confirm_cond);
    vlc_mutex_unlock(&ctx->confirm_lock);
}

static bool
stt_whisper_process_Abort(void *opaque)
{
    assert(opaque != NULL);
    struct stt_whisper_process_ctx *ctx = opaque;
    assert(ctx->backend != NULL);
    assert(ctx->backend->type == stt_whisper_backend_thread);

    struct stt_whisper_backend_thread *bsys = ctx->backend->sys;

    vlc_mutex_lock(&ctx->confirm_lock);
    bool disabled = ctx->confirm_disabled;
    vlc_mutex_unlock(&ctx->confirm_lock);

    return disabled || atomic_load(&bsys->abort);
}

static int
stt_whisper_process_StartBackend(struct stt_whisper_process_ctx *ctx)
{
    assert(ctx != NULL);

    struct stt_whisper_backend *backend = malloc(sizeof(*backend));
    if (backend == NULL) {
        return VLC_ENOMEM;
    }

    static const struct stt_whisper_backend_callbacks cbs = {
        .ready = stt_whisper_process_Ready,
        .send_subtitle = stt_whisper_process_SendSubtitle,
        .on_progress = stt_whisper_process_OnProgress,
        .confirm = stt_whisper_process_Confirm,
        .confirm_reply = stt_whisper_process_ConfirmReply,
        .abort = stt_whisper_process_Abort,
    };
    backend->params.language = NULL;
    backend->params.model_name = NULL;
    backend->decoder = NULL;
    backend->owner.cbs = &cbs;
    backend->owner.opaque = ctx;
    backend->obj = ctx->obj;

    if (stt_whisper_backend_thread_Init(backend) != VLC_SUCCESS) {
        free(backend);
        return VLC_EGENERIC;
    }
    ctx->backend = backend;
    return VLC_SUCCESS;
}

int
main(int argc, char **argv)
{
    VLC_UNUSED(argc);
    VLC_UNUSED(argv);

#ifdef TOP_BUILDDIR
    setenv("VLC_PLUGIN_PATH", TOP_BUILDDIR"/modules", 1);
    setenv("VLC_DATA_PATH", TOP_SRCDIR"/share", 1);
    setenv("VLC_LIB_PATH", TOP_BUILDDIR"/modules", 1);
#endif

#ifdef _WIN32
#include <fcntl.h>
    setmode(STDIN_FILENO, O_BINARY);
    setmode(STDOUT_FILENO, O_BINARY);
#endif

    vlc_thread_set_name("vlc-whisper");

    const char *libvlc_args[] = {
        "--vout=vdummy", "--aout=adummy", "--text-renderer=tdummy",
    };

    libvlc_instance_t *vlc = libvlc_new(ARRAY_SIZE(libvlc_args), libvlc_args);
    if (vlc == NULL) {
        return 1;
    }
    vlc_object_t *obj = VLC_OBJECT(vlc->p_libvlc_int);

    struct stt_whisper_process_ctx ctx;
    ctx.tls_in = NULL;
    ctx.tls_out = NULL;
    ctx.backend = NULL;
    ctx.obj = obj;
    vlc_mutex_init(&ctx.send_lock);
    vlc_mutex_init(&ctx.confirm_lock);
    vlc_cond_init(&ctx.confirm_cond);
    ctx.confirm_pending = false;
    ctx.confirm_result = false;
    ctx.confirm_disabled = false;

#ifndef _WIN32
    ctx.tls_in = vlc_tls_SocketOpen(STDIN_FILENO);
    if (ctx.tls_in == NULL) {
        goto end;
    }
    ctx.tls_out = vlc_tls_SocketOpen(STDOUT_FILENO);
    if (ctx.tls_out == NULL) {
        ctx.tls_in->ops->close(ctx.tls_in);
        goto end;
    }
#else
    ctx.tls_in = NULL;
    ctx.tls_out = NULL;
#endif

    static const struct vlc_logger_operations log_ops = {
        .log = stt_whisper_process_Log,
    };
    vlc_LogSet(vlc->p_libvlc_int, &log_ops, &ctx);

    if (stt_whisper_process_StartBackend(&ctx) != VLC_SUCCESS) {
        msg_Err(obj, "whisper process: backend failed to start!");
        goto end;
    }

    int status = stt_whisper_command_RecvLoop(ctx.backend,
                                              stt_whisper_process_Read,
                                              ctx.tls_in,
                                              stt_whisper_process_Write,
                                              &ctx);
    /* The host is gone: unblock a loader thread still waiting in confirm()
     * before close() joins it. */
    stt_whisper_process_DisableConfirm(&ctx);
    if (status != VLC_SUCCESS) {
        assert(ctx.backend->ops != NULL);
        assert(ctx.backend->ops->close != NULL);
        ctx.backend->ops->close(ctx.backend);
    }

    free(ctx.backend->params.model_name);
    free(ctx.backend->params.language);
    free(ctx.backend);

end:
    vlc_LogSet(vlc->p_libvlc_int, NULL, NULL);
    if (ctx.tls_in != NULL) {
        ctx.tls_in->ops->close(ctx.tls_in);
    }
    if (ctx.tls_out != NULL) {
        ctx.tls_out->ops->close(ctx.tls_out);
    }
    libvlc_release(vlc);
    return 0;
}
