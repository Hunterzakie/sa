// SPDX-License-Identifier: LGPL-2.1-or-later
/*****************************************************************************
 * stt.h: Speech To Text Loader Internal
 *****************************************************************************
 * Copyright © 2023 Videolabs
 *
 * Authors: Gabriel Lafond Thenaille <gabriel@videolabs.io>
 *****************************************************************************/

#ifndef VLC_STT_INTERNAL_H
#define VLC_STT_INTERNAL_H 1

#include <vlc_stt.h>

/**
 * Function that create loader structure and start a module.
 */
struct vlc_stt_ctx *
vlc_stt_ctx_Loader(vlc_object_t *obj, const struct vlc_stt_ctx_callbacks *cbs,
                   void *data);

void
vlc_stt_ctx_Delete(struct vlc_stt_ctx *ctx);

/**
 * Atomically clear cbs/data iff owner.data matches.
 */
void
vlc_stt_ctx_Detach(struct vlc_stt_ctx *ctx, void *data);

#endif
