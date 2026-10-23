// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * stt.c: Speech To Text Loader Internal
 *****************************************************************************
 * Copyright © 2023 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>

#include <vlc_stt.h>
#include <vlc_modules.h>
#include <vlc_variables.h>

#include "stt.h"

struct vlc_stt_ctx *
vlc_stt_ctx_Loader(vlc_object_t *obj, const struct vlc_stt_ctx_callbacks *cbs,
                   void *data)
{
    assert(obj != NULL);
    assert(cbs != NULL);

    struct vlc_stt_ctx *ctx = vlc_object_create(obj, sizeof(struct vlc_stt_ctx));
    if (ctx == NULL) {
        return NULL;
    }
    ctx->owner.cbs = cbs;
    ctx->owner.data = data;
    ctx->cancelled = false;
    vlc_mutex_init(&ctx->lock);
    vlc_cond_init(&ctx->cond);

    char *impl = var_InheritString(obj, "stt-loader");
    const char *name = (impl != NULL && impl[0] != '\0') ? impl : "any";
    module_t **mods = NULL;
    size_t strict = 0;
    ssize_t n = vlc_module_match("stt loader", name, true, &mods, &strict);
    if (n <= 0) {
        msg_Err(ctx, "no stt loader module matching `%s'", name);
    }
    free(impl);

    for (ssize_t i = 0; i < n; i++) {
        vlc_stt_ctx_loader_module_t cb = vlc_module_map(vlc_object_logger(ctx),
                                                        mods[i]);
        if (cb == NULL) {
            msg_Err(ctx, "cannot map stt loader module `%s'",
                    module_get_object(mods[i]));
            continue;
        }

        ctx->sys = NULL;
        ctx->name = NULL;
        ctx->status = vlc_stt_ctx_status_loading;
        ctx->ops = NULL;

        int ret = cb(ctx);
        if (ret == VLC_SUCCESS && ctx->ops != NULL && ctx->ops->close != NULL) {
            free(mods);
            return ctx;
        }
        msg_Err(ctx, "stt loader module `%s' failed to open (%d)",
                module_get_object(mods[i]), ret);
    }
    free(mods);
    vlc_object_delete(ctx);
    return NULL;
}

void
vlc_stt_ctx_Delete(struct vlc_stt_ctx *ctx)
{
    assert(ctx != NULL);
    assert(ctx->ops != NULL);
    assert(ctx->ops->close != NULL);

    ctx->ops->close(ctx);
    vlc_object_delete(ctx);
}

void
vlc_stt_ctx_Detach(struct vlc_stt_ctx *ctx, void *data)
{
    vlc_mutex_lock(&ctx->lock);
    if (ctx->owner.data == data) {
        if (ctx->status != vlc_stt_ctx_status_done
            && ctx->status != vlc_stt_ctx_status_error) {
            ctx->cancelled = true;
            if (ctx->ops->abort != NULL) {
                ctx->ops->abort(ctx);
            }
            while (ctx->status != vlc_stt_ctx_status_done
                && ctx->status != vlc_stt_ctx_status_error) {
                vlc_cond_wait(&ctx->cond, &ctx->lock);
            }
        }
        ctx->owner.cbs = NULL;
        ctx->owner.data = NULL;
    }
    vlc_mutex_unlock(&ctx->lock);
}
