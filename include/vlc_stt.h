// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * vlc_stt.h: Speech To Text Loader API
 *****************************************************************************
 * Copyright © 2023 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_STT_H
#define VLC_STT_H 1

#include <assert.h>
#include <stdbool.h>

#include <vlc_tick.h>
#include <vlc_threads.h>

/**
 * @defgroup stt Speech To Text
 * @ingroup input
 * @{
 * @file
 * This file declares vlc speech to text loader API
 * @defgroup stt_loader Speech To Text Loader
 * @{
 */

struct vlc_stt_ctx;

/**
 * Context status
 */
enum vlc_stt_ctx_status {
    /** context loaded */
    vlc_stt_ctx_status_done = 0,
    /** downloading the model */
    vlc_stt_ctx_status_downloading = 1,
    /** extracting the model */
    vlc_stt_ctx_status_extracting = 2,
    /** loading the model */
    vlc_stt_ctx_status_loading = 3,
    /** context error */
    vlc_stt_ctx_status_error = 4,
    /** hashing the model */
    vlc_stt_ctx_status_hashing = 5,
};

/**
 * Context callbacks
 */
struct vlc_stt_ctx_callbacks {
    /**
     * Called when the context has been loaded or if an error occurs in the
     * loader.
     *
     * @note Mandatory.
     * @note Must be called with the ctx lock held; must not re-acquire it.
     *
     * @param ctx loaded context
     * @param status the context status
     * @param opaque user data
     */
    void
    (*loaded)(struct vlc_stt_ctx *ctx, enum vlc_stt_ctx_status status,
              void *opaque);

    /**
     * Called to report status progress.
     *
     * @note Optional.
     * @note Called from the loader thread without the ctx lock held.
     *
     * @param ctx context structure
     * @param status the context status
     * @param position current position in bytes
     * @param total total size
     * @param description information text
     * @param opaque user data
     */
    void
    (*on_progress)(struct vlc_stt_ctx *ctx, enum vlc_stt_ctx_status status,
                   int64_t position, int64_t total, const char *description,
                   void *opaque);

    /**
     * Called to confirm a model download.
     *
     * @note Optional. A NULL callback means proceed with the download.
     * @note Called from the loader thread without the ctx lock held; may
     *       block until the user answers.
     *
     * @param ctx context structure
     * @param download_size size of the download in bytes
     * @param path destination path of the download
     * @param opaque user data
     * @return true to proceed with the download, false to cancel
     */
    bool (*confirm)(struct vlc_stt_ctx *ctx, uint64_t download_size,
                    const char *path, void *opaque);

    /**
     * Called to query whether the current loading should be aborted.
     *
     * @note Optional. A NULL callback means never abort.
     * @note Called from the loader thread without the ctx lock held.
     *
     * @param ctx context structure
     * @param opaque user data
     * @return true if the loading was cancelled and must be aborted
     */
    bool (*abort)(struct vlc_stt_ctx *ctx, void *opaque);
};

/**
 * Context operations
 */
struct vlc_stt_ctx_operations {
    /**
     * Free the context and close all internal thread or process.
     *
     * @note Mandatory
     *
     * @param ctx context structure
     */
    void
    (*close)(struct vlc_stt_ctx *ctx);

    /**
     * Request the in-progress load to abort, without freeing the context.
     *
     * Called (with the ctx lock held) when the owner cancels, e.g. from
     * vlc_stt_ctx_Detach(), so a loader running in another thread or process
     * reaches a terminal status promptly instead of finishing the download.
     *
     * @note Optional. Loaders whose abort callback is polled in-process do not
     *       need it.
     *
     * @param ctx context structure
     */
    void
    (*abort)(struct vlc_stt_ctx *ctx);
};

/**
 * Speech to text context
 */
struct vlc_stt_ctx {
    vlc_object_t obj;
    vlc_mutex_t lock;

    /**
     * Set by vlc_stt_ctx_Detach() to request the loader to abort the current
     * loading. Protected by @ref lock.
     */
    bool cancelled;

    /**
     * Signaled when @ref status reaches a terminal value (done or error).
     * Used by vlc_stt_ctx_Detach() to wait for the loader to settle. Protected
     * by @ref lock.
     */
    vlc_cond_t cond;

    /**
     * Internal structure used by the module.
     */
    void *sys;

    /**
     * The module name
     */
    const char *name;

    /**
     * The status of the context
     */
    enum vlc_stt_ctx_status status;

    /**
     * Pts delay requested by the module
     */
    vlc_tick_t delay;

    /**
     * The callbacks must be call from a different thread than the thread used
     * to create the context.
     */
    struct {
        const struct vlc_stt_ctx_callbacks *cbs;
        void *data;
    } owner;

    /** Used by the caller to perform actions with the ctx */
    const struct vlc_stt_ctx_operations *ops;
};

typedef int (*vlc_stt_ctx_loader_module_t)(struct vlc_stt_ctx *ctx);

#define set_callback_stt_ctx_loader(activate, priority) \
    { \
        vlc_stt_ctx_loader_module_t open__ = activate; \
        (void) open__; \
        set_callback(activate) \
    } \
    set_capability("stt loader", priority)

/**
 * Fire the `loaded` callback on @p ctx.
 *
 * Must be called by the loader module.
 *
 * @param ctx context structure
 * @param status terminal status
 */
static inline void
vlc_stt_ctx_Loaded(struct vlc_stt_ctx *ctx, enum vlc_stt_ctx_status status)
{
    vlc_mutex_lock(&ctx->lock);
    ctx->status = status;
    const struct vlc_stt_ctx_callbacks *cbs = ctx->owner.cbs;
    void *data = ctx->owner.data;

    /* When the owner is detaching it waits for a terminal status; don't call
     * back into a possibly torn-down owner, only wake the waiter. */
    if (!ctx->cancelled && cbs != NULL) {
        assert(cbs->loaded != NULL);
        cbs->loaded(ctx, status, data);
    }
    vlc_cond_broadcast(&ctx->cond);
    vlc_mutex_unlock(&ctx->lock);
}

/**
 * Fire the `on_progress` callback on @p ctx.
 *
 * Must be called by the loader module.
 *
 * @param ctx context structure
 * @param status the context status
 * @param position current position in bytes
 * @param total total size
 * @param description information text
 */
static inline void
vlc_stt_ctx_Progress(struct vlc_stt_ctx *ctx, enum vlc_stt_ctx_status status,
                     int64_t position, int64_t total, const char *description)
{
    vlc_mutex_lock(&ctx->lock);
    const struct vlc_stt_ctx_callbacks *cbs = ctx->owner.cbs;
    void *data = ctx->owner.data;
    if (cbs != NULL && cbs->on_progress != NULL) {
        cbs->on_progress(ctx, status, position, total, description, data);
    }
    vlc_mutex_unlock(&ctx->lock);
}

/** @} */
/** @} */

#endif
