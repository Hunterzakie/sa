/*****************************************************************************
 * vk_interop.c: Vulkan hardware decoder interop
 *****************************************************************************
 * Copyright (C) 2025-2026 VideoLabs
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_modules.h>
#include <vlc_codec.h>

#include "vk_interop.h"

void vlc_vk_interop_Init(struct vlc_vk_interop *interop)
{
    /* Phase 1+2+3 common fields */
    interop->ops = NULL;

    /* Phase 2 format probe fields */
    interop->format_probe.obj = NULL;
    interop->format_probe.vctx = NULL;
    video_format_Init(&interop->format_probe.fmt_in, 0);
    interop->format_probe.dev = NULL;
    video_format_Init(&interop->format_probe.fmt_out, 0);
    interop->format_probe.vk_format = VK_FORMAT_UNDEFINED;
    interop->format_probe.tiling = VK_IMAGE_TILING_MAX_ENUM;
    interop->format_probe.sw_chroma = 0;

    /* Phase 3 interop fields */
    interop->dev = NULL;
    interop->image_count = 0;
    interop->ycbcr_conversion = VK_NULL_HANDLE;
    interop->sampler = VK_NULL_HANDLE;
    for (unsigned i = 0; i < PICTURE_PLANE_MAX; i++)
    {
        interop->plane_info[i].w.num = 0;
        interop->plane_info[i].w.den = 1;
        interop->plane_info[i].h.num = 0;
        interop->plane_info[i].h.den = 1;
        interop->plane_info[i].format = VK_FORMAT_UNDEFINED;
        interop->plane_info[i].usage = 0;
        interop->plane_info[i].tiling = VK_IMAGE_TILING_MAX_ENUM;
    }
    interop->plane_count = 0;
    interop->priv = NULL;
}

int vlc_vk_interop_Probe(struct vlc_vk_interop_probe *probe, vlc_object_t *obj,
                         struct vlc_video_context *vctx, const video_format_t *fmt)
{
    struct vlc_logger *logger = vlc_object_logger(obj);

    const vlc_chroma_description_t *desc =
        vlc_fourcc_GetChromaDescription(fmt->i_chroma);

    if (desc == NULL)
    {
        msg_Err(obj, "Unknown chroma format 0x%.8x", fmt->i_chroma);
        return VLC_EGENERIC;
    }

    probe->obj = obj;
    probe->vctx = vctx != NULL ? vlc_video_context_Hold(vctx) : NULL;
    probe->fmt_in = *fmt;
    probe->fmt_in.p_palette = NULL;

    probe->required_instance_extensions = NULL;
    probe->required_instance_extension_count = 0;
    probe->required_device_extensions = NULL;
    probe->required_device_extension_count = 0;
    probe->ops = NULL;
    probe->acquire_sem_handle_type = 0;

    module_t **mods;
    ssize_t total = vlc_module_match("vkinterop", NULL, false, &mods, NULL);
    bool found = false;

    for (ssize_t i = 0; i < total; i++)
    {
        vlc_vk_interop_open open = vlc_module_map(logger, mods[i]);

        if (open != NULL && open(probe) == VLC_SUCCESS)
        {
            assert(probe->ops != NULL);
            assert(probe->ops->probe_format != NULL);
            assert(probe->ops->load != NULL);
            assert(probe->ops->import_images != NULL);

            found = true;
            msg_Dbg(probe->obj, "Probed vkinterop module: %u instance exts, %u device exts",
                    probe->required_instance_extension_count,
                    probe->required_device_extension_count);
            break;
        }
    }

    free(mods);

    if (!found)
    {
        msg_Err(probe->obj, "Could not find compatible vkinterop module for chroma 0x%.8x",
                probe->fmt_in.i_chroma);
        if (probe->vctx != NULL)
            vlc_video_context_Release(probe->vctx);
        return VLC_EGENERIC;
    }

    msg_Dbg(probe->obj, "Probed vkinterop module successfully (Phase 1: extensions only)");
    return VLC_SUCCESS;
}

int vlc_vk_interop_ProbeFormat(struct vlc_vk_interop *interop,
                               const struct vlc_vk_interop_probe *probe,
                               struct vlc_vk_device *dev)
{
    assert(interop != NULL);
    assert(probe != NULL);
    assert(probe->ops != NULL);
    assert(probe->ops->probe_format != NULL);
    assert(dev != NULL);

    interop->ops = probe->ops;

    interop->format_probe.obj = probe->obj;
    interop->format_probe.vctx = probe->vctx;
    interop->format_probe.fmt_in = probe->fmt_in;
    interop->format_probe.dev = dev;

    video_format_Init(&interop->format_probe.fmt_out, 0);
    interop->format_probe.vk_format = VK_FORMAT_UNDEFINED;
    interop->format_probe.tiling = VK_IMAGE_TILING_MAX_ENUM;

    int ret = interop->ops->probe_format(&interop->format_probe);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(interop->format_probe.obj, "Format probe failed");
        return ret;
    }

    if (interop->format_probe.vk_format == VK_FORMAT_UNDEFINED)
    {
        /* VK_FORMAT_UNDEFINED is allowed when interop provides its own sampler
         * (e.g., Android external format where format is determined at runtime) */
        if (interop->ops->get_sampler == NULL)
        {
            msg_Err(interop->format_probe.obj, "Interop module did not set vk_format");
            return VLC_EGENERIC;
        }
        msg_Dbg(interop->format_probe.obj, "Using external format (runtime determination)");
    }

    if (interop->format_probe.tiling == VK_IMAGE_TILING_MAX_ENUM)
    {
        msg_Err(interop->format_probe.obj, "Interop module did not set tiling mode");
        return VLC_EGENERIC;
    }

    /* Clear palette pointer to avoid stale references */
    interop->format_probe.fmt_out.p_palette = NULL;

    msg_Dbg(interop->format_probe.obj, "Format probe succeeded: chroma=%.4s, vk_format=0x%x, tiling=%d",
            (char*)&interop->format_probe.fmt_out.i_chroma,
            interop->format_probe.vk_format,
            interop->format_probe.tiling);

    return VLC_SUCCESS;
}

int vlc_vk_interop_Load(struct vlc_vk_interop *interop)
{
    /* Preconditions: these are internal API calls, never NULL if code is correct */
    assert(interop != NULL);
    assert(interop->dev != NULL);
    assert(interop->dev->device != VK_NULL_HANDLE);
    assert(interop->ops != NULL);
    assert(interop->ops->load != NULL);

    /* Interop should already be populated by display.c with:
     * - interop->ops (from Phase 1)
     * - interop->dev (Vulkan context)
     * - interop->format_probe (from Phase 2)
     * - interop->ycbcr_conversion (created by display.c)
     * - interop->sampler (created by display.c)
     * Module will fill in interop->plane_count and interop->plane_info
     */

    int ret = interop->ops->load(interop);
    if (ret != VLC_SUCCESS)
    {
        msg_Err(interop->format_probe.obj, "vkinterop module load failed");
        return ret;
    }

    if (interop->plane_count == 0 || interop->plane_count > PICTURE_PLANE_MAX)
    {
        msg_Err(interop->format_probe.obj, "vkinterop module set invalid plane_count: %u",
                interop->plane_count);
        return VLC_EGENERIC;
    }

    msg_Dbg(interop->format_probe.obj, "Loaded vkinterop module with %u planes", interop->plane_count);
    return VLC_SUCCESS;
}

void vlc_vk_interop_Close(struct vlc_vk_interop *interop)
{
    /* Only call close callback if load was successful (indicated by dev being set) */
    if (interop->dev != NULL && interop->ops && interop->ops->close)
        interop->ops->close(interop);

    if (interop->format_probe.vctx)
        vlc_video_context_Release(interop->format_probe.vctx);
}
