/*****************************************************************************
 * sw.c: software picture interop
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
#include <vlc_plugin.h>
#include <vlc_picture.h>

#include "../vk_interop.h"
#include "../utils.h"

/* The planes are simply uploaded to libplacebo  */

/**
 * SOFTWARE VS HARDWARE INTEROPS:
 * - Software interop should try fallback formats here if needed
 * - Hardware interops should pick format based on decoder output
 *
 * INTERNAL VS EXTERNAL CONVERSIONS:
 *
 * The module must decide whether to handle format conversion internally or
 * request VLC core to insert a converter filter:
 *
 * 1. INTERNAL CONVERSION (module handles):
 *    - Use when conversion is cheap and module-specific (e.g., plane reorganization)
 *    - Set: format_probe->fmt_out.i_chroma = format_probe->fmt_in.i_chroma
 *           format_probe->vk_format = <actual Vulkan texture format>
 *    - Result: No VLC filter inserted, module converts during import_images()
 *    - Example: I420_10L → P010 (3-plane to 2-plane, same bit depth)
 *              fmt_out.i_chroma = VLC_CODEC_I420_10L (no filter)
 *              vk_format = VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16 (P010)
 *
 * 2. EXTERNAL CONVERSION (VLC core handles):
 *    - Use when Vulkan format is unsupported and VLC must convert
 *    - Set: format_probe->fmt_out.i_chroma = fallback_format
 *           format_probe->vk_format = <Vulkan format for fallback>
 *    - Result: VLC core inserts converter filter
 *    - Example: I420_10L → NV12 (10-bit to 8-bit when device lacks 10-bit support)
 *              fmt_out.i_chroma = VLC_CODEC_NV12 (VLC inserts filter)
 *              vk_format = VK_FORMAT_G8_B8R8_2PLANE_420_UNORM (NV12)
 *
 * RULE OF THUMB:
 * - Same bit depth + cheap operation → internal (keep fmt_out = fmt_in)
 * - Different bit depth or expensive → external (change fmt_out to fallback)
 */

struct priv {
    pl_tex tex[PICTURE_PLANE_MAX];
    enum pl_chroma_location chroma_loc;
};

static int
UploadPlanes(const struct vlc_vk_interop *interop,
             struct pl_plane *out_planes, unsigned *out_plane_count,
             picture_t *pic)
{
    struct priv *priv = interop->priv;
    pl_gpu gpu = interop->format_probe.gpu;
    vlc_object_t *o = interop->format_probe.obj;

    struct pl_plane_data data[4];
    int planes = vlc_placebo_PlaneData(pic, data);
    if (planes == 0) {
        msg_Err(o, "Failed to get plane data for %.4s", (char *)&pic->format.i_chroma);
        return VLC_EGENERIC;
    }

    for (int i = 0; i < planes; i++) {
        struct pl_plane *plane = &out_planes[i];

        if (!pl_upload_plane(gpu, plane, &priv->tex[i], &data[i])) {
            msg_Err(o, "Failed uploading plane %d", i);
            return VLC_EGENERIC;
        }

        if (priv->chroma_loc != PL_CHROMA_UNKNOWN && i != 0 && i != 3)
            pl_chroma_location_offset(priv->chroma_loc, &plane->shift_x, &plane->shift_y);
    }

    *out_plane_count = planes;
    return VLC_SUCCESS;
}

static void
Close(struct vlc_vk_interop *interop)
{
    struct priv *priv = interop->priv;
    pl_gpu gpu = interop->format_probe.gpu;

    for (unsigned i = 0; i < PICTURE_PLANE_MAX; i++)
        pl_tex_destroy(gpu, &priv->tex[i]);

    free(priv);
    interop->priv = NULL;
}

static int
ProbeFormat(struct vlc_vk_interop_format_probe *format_probe)
{
    const vlc_fourcc_t in = format_probe->fmt_in.i_chroma;
    pl_gpu gpu = format_probe->gpu;
    vlc_fourcc_t chroma = 0;

    // Check if the input format is supported by libplacebo, or find a fallback
    if (vlc_placebo_FormatSupported(gpu, in)) {
        chroma = in;
    } else {
        vlc_fourcc_t *list = vlc_fourcc_GetFallback(in);
        if (list != NULL) {
            for (const vlc_fourcc_t *fcc = list; *fcc; fcc++) {
                if (vlc_placebo_FormatSupported(gpu, *fcc)) {
                    chroma = *fcc;
                    break;
                }
            }
            free(list);
        }
    }

    if (chroma == 0) {
        msg_Dbg(format_probe->obj, "No supported format found for %.4s", (char *)&in);
        return VLC_EGENERIC;
    }

    format_probe->fmt_out = format_probe->fmt_in;
    format_probe->fmt_out.i_chroma = chroma;
    format_probe->sw_chroma = chroma;

    return VLC_SUCCESS;
}

static int
Load(struct vlc_vk_interop *interop)
{
    const video_format_t *fmt = &interop->format_probe.fmt_out;

    const vlc_chroma_description_t *desc = vlc_fourcc_GetChromaDescription(fmt->i_chroma);
    if (desc == NULL || desc->plane_count == 0) {
        msg_Err(interop->format_probe.obj, "No plane description for %.4s", (char *)&fmt->i_chroma);
        return VLC_EGENERIC;
    }

    struct priv *priv = calloc(1, sizeof(*priv));
    if (unlikely(priv == NULL))
        return VLC_ENOMEM;

    priv->chroma_loc = vlc_fourcc_IsYUV(fmt->i_chroma) ?
                       vlc_placebo_ChromaLoc(fmt) : PL_CHROMA_UNKNOWN;

    interop->plane_count = desc->plane_count;
    interop->priv = priv;

    msg_Dbg(interop->format_probe.obj, "Software interop loaded for %.4s (%u planes)",
            (char *)&fmt->i_chroma, interop->plane_count);

    return VLC_SUCCESS;
}

static int
Open(struct vlc_vk_interop_probe *probe)
{
    const vlc_chroma_description_t *desc = vlc_fourcc_GetChromaDescription(probe->fmt_in.i_chroma);
    if (desc == NULL || desc->plane_count == 0)
        return VLC_EGENERIC;

    static const struct vlc_vk_interop_ops ops = {
        .probe_format = ProbeFormat,
        .load = Load,
        .upload_planes = UploadPlanes,
        .close = Close,
    };
    probe->ops = &ops;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_description("Software picture interop")
    set_capability("vkinterop", 1)
    set_callback(Open)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("sw")
vlc_module_end()
