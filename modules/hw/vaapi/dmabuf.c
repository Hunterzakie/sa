/*****************************************************************************
 * dmabuf.c: Export VA-API surfaces as dmabuf descriptors
 *****************************************************************************
 * Copyright (C) 2017-2026 VLC authors and VideoLAN
 * Copyright (C) 2026 Alexandre Janniaux <ajanni@videolabs.io>
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

#include <strings.h>
#include <unistd.h>

#include <va/va.h>
#include <va/va_drmcommon.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include "vlc_vaapi.h"
#include "../dmabuf/vlc_dmabuf.h"

/**
 * Describe a VLC VAAPI fourcc for libva and how the planes are setup
 */
static int DescribeChroma(vlc_fourcc_t chroma, struct vlc_dmabuf_format *format)
{
    switch (chroma)
    {
        case VLC_CODEC_VAAPI_420:
            *format = (struct vlc_dmabuf_format) {
                .drm_fourcc = DRM_FORMAT_NV12,
                .chroma = VLC_CODEC_NV12,
                .plane_count = 2,
                .planes = {
                    { DRM_FORMAT_R8,   { 1, 1 }, { 1, 1 } },
                    { DRM_FORMAT_GR88, { 1, 2 }, { 1, 2 } },
                },
            };
            break;
        case VLC_CODEC_VAAPI_420_10BPP:
        case VLC_CODEC_VAAPI_420_12BPP:
            *format = (struct vlc_dmabuf_format) {
                .drm_fourcc = chroma == VLC_CODEC_VAAPI_420_10BPP
                    ? DRM_FORMAT_P010 : DRM_FORMAT_P012,
                .chroma = chroma == VLC_CODEC_VAAPI_420_10BPP
                    ? VLC_CODEC_P010 : VLC_CODEC_P012,
                .plane_count = 2,
                .planes = {
                    { DRM_FORMAT_R16,    { 1, 1 }, { 1, 1 } },
                    { DRM_FORMAT_GR1616, { 1, 2 }, { 1, 2 } },
                },
            };
            break;
        case VLC_CODEC_VAAPI_422_10BPP:
        case VLC_CODEC_VAAPI_422_12BPP:
            *format = (struct vlc_dmabuf_format) {
                .drm_fourcc = chroma == VLC_CODEC_VAAPI_422_10BPP
                    ? DRM_FORMAT_Y210 : DRM_FORMAT_Y212,
                .chroma = chroma == VLC_CODEC_VAAPI_422_10BPP
                    ? VLC_CODEC_Y210 : VLC_CODEC_Y212,
                .plane_count = 1,
                .planes = {
                    { DRM_FORMAT_ABGR16161616, { 1, 2 }, { 1, 1 } },
                },
            };
            break;
        case VLC_CODEC_VAAPI_444:
            *format = (struct vlc_dmabuf_format) {
                .drm_fourcc = DRM_FORMAT_XYUV8888,
                .chroma = VLC_CODEC_VUYX,
                .plane_count = 1,
                .planes = {
                    { DRM_FORMAT_XYUV8888, { 1, 1 }, { 1, 1 } },
                },
            };
            break;
        case VLC_CODEC_VAAPI_444_10BPP:
        case VLC_CODEC_VAAPI_444_12BPP:
        {
            const uint32_t drm_fourcc = chroma == VLC_CODEC_VAAPI_444_10BPP
                ? DRM_FORMAT_Y410 : DRM_FORMAT_Y412;

            *format = (struct vlc_dmabuf_format) {
                .drm_fourcc = drm_fourcc,
                .chroma = chroma == VLC_CODEC_VAAPI_444_10BPP
                    ? VLC_CODEC_Y410 : VLC_CODEC_Y412,
                .plane_count = 1,
                .planes = { { drm_fourcc, { 1, 1 }, { 1, 1 } } },
            };
            break;
        }
        default:
            /* Not a vaapi chroma */
            return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

struct exporter_sys
{
    vlc_decoder_device *dec_device;
    VADisplay dpy;
};

static void ReleaseFrame(struct vlc_dmabuf_frame *frame)
{
    for (size_t i = 0; i < frame->plane_count; i++)
        close(frame->planes[i].fd);

    frame->plane_count = 0;
}

static int FillFrame(struct vlc_dmabuf_exporter *exporter,
                     const VADRMPRIMESurfaceDescriptor *desc,
                     struct vlc_dmabuf_frame *frame)
{
    if (desc->num_layers != exporter->format.plane_count)
    {
        /* Wrong picture / context ? */
        msg_Warn(exporter, "VA surface exported with %u planes, %u expected",
                 desc->num_layers, exporter->format.plane_count);
        return VLC_EGENERIC;
    }

    const uint64_t modifier = desc->objects[0].drm_format_modifier;

    /* Also check the layout if the producer doesn't behave properly */
    for (unsigned i = 1; i < desc->num_objects; i++)
    {
        if (desc->objects[i].drm_format_modifier != modifier)
        {
            msg_Warn(exporter, "VA surface objects have differing layouts");
            return VLC_EGENERIC;
        }
    }

    *frame = (struct vlc_dmabuf_frame) {
        .drm_fourcc = exporter->format.drm_fourcc,
        .modifier = modifier,
        .plane_count = 0,
    };

    for (unsigned i = 0; i < desc->num_layers; i++)
    {
        if (desc->layers[i].num_planes != 1)
        {
            msg_Warn(exporter, "VA surface layer %u holds %u planes", i,
                     desc->layers[i].num_planes);
            ReleaseFrame(frame);
            return VLC_EGENERIC;
        }

        if (desc->layers[i].drm_format != exporter->format.planes[i].drm_fourcc)
            msg_Dbg(exporter,
                     "VA surface plane %u exported as %4.4s, %4.4s announced",
                     i, (const char *)&desc->layers[i].drm_format,
                     (const char *)&exporter->format.planes[i].drm_fourcc);

        const uint32_t object = desc->layers[i].object_index[0];

        if (object >= desc->num_objects)
        {
            ReleaseFrame(frame);
            return VLC_EGENERIC;
        }

        const int fd = dup(desc->objects[object].fd);
        if (fd == -1)
        {
            ReleaseFrame(frame);
            return VLC_EGENERIC;
        }

        const vlc_rational_t h = exporter->format.planes[i].height;

        frame->planes[i] = (struct vlc_dmabuf_plane) {
            .fd = fd,
            .offset = desc->layers[i].offset[0],
            .lines = (desc->height * h.num + h.den - 1) / h.den,
            .pitch = desc->layers[i].pitch[0],
        };
        frame->plane_count = i + 1;
    }

    return VLC_SUCCESS;
}

static int ExportSurface(struct vlc_dmabuf_exporter *exporter,
                         VASurfaceID surface, struct vlc_dmabuf_frame *frame)
{
    struct exporter_sys *sys = exporter->sys;

    VADRMPRIMESurfaceDescriptor desc;

    if (vlc_vaapi_ExportSurfaceHandle(VLC_OBJECT(exporter), sys->dpy, surface,
                                      VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                      VA_EXPORT_SURFACE_READ_ONLY
                                      | VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                      &desc))
        return VLC_EGENERIC;

    if (desc.num_objects == 0 || desc.num_objects > VLC_DMABUF_MAX_PLANES)
    {
        /* should probably never happen */

        msg_Warn(exporter, "VA surface exported as %u objects",
                 desc.num_objects);

        for (unsigned i = 0; i < desc.num_objects; i++)
            close(desc.objects[i].fd);

        return VLC_EGENERIC;
    }

    const int ret = FillFrame(exporter, &desc, frame);

    for (unsigned i = 0; i < desc.num_objects; i++)
        close(desc.objects[i].fd);

    return ret;
}

static int Export(struct vlc_dmabuf_exporter *exporter, picture_t *pic,
                  struct vlc_dmabuf_frame *frame)
{
    struct exporter_sys *sys = exporter->sys;
    VASurfaceID surface = vlc_vaapi_PicGetSurface(pic);

    if (ExportSurface(exporter, surface, frame) != VLC_SUCCESS)
        return VLC_EGENERIC;

    VAStatus status = vaSyncSurface(sys->dpy, surface);
    if (status != VA_STATUS_SUCCESS) // non-fatal. ex: VA_STATUS_ERROR_DECODING_ERROR
        msg_Warn(exporter, "vaSyncSurface: 0x%x %s", status,
                 vaErrorStr(status));

    return VLC_SUCCESS;
}

static void Close(struct vlc_dmabuf_exporter *exporter)
{
    struct exporter_sys *sys = exporter->sys;

    vlc_decoder_device_Release(sys->dec_device);
    free(sys);
}

static const struct vlc_dmabuf_exporter_operations ops =
{
    .export_frame = Export,
    .close = Close,
};

static int strcasecmp_void(const void *a, const void *b)
{
    return strcasecmp(a, b);
}

static int
tc_va_check_interop_blacklist(struct vlc_dmabuf_exporter *exporter, VADisplay dpy)
{
    const char *vendor = vaQueryVendorString(dpy);
    if (vendor == NULL)
        return VLC_SUCCESS;

#define BL_SIZE_MAX 19
    static const char blacklist_prefix[][BL_SIZE_MAX] = {
        /* XXX: case insensitive and alphabetical order */
        "mesa gallium vaapi",
    };

    char vendor_prefix[BL_SIZE_MAX];
    strncpy(vendor_prefix, vendor, sizeof(vendor_prefix) - 1);
    vendor_prefix[sizeof(vendor_prefix) - 1] = '\0';

    const char *found = bsearch(vendor_prefix, blacklist_prefix,
                                ARRAY_SIZE(blacklist_prefix),
                                BL_SIZE_MAX, strcasecmp_void);
    if (found != NULL)
    {
        msg_Warn(exporter, "The '%s' driver is blacklisted: no interop", found);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static int tc_va_check_derive_image(struct vlc_dmabuf_exporter *exporter)
{
    struct exporter_sys *sys = exporter->sys;
    VASurfaceID *va_surface_ids;

    picture_pool_t *pool = vlc_vaapi_PoolNew(VLC_OBJECT(exporter),
                                             exporter->vctx, sys->dpy, 1,
                                             &va_surface_ids,
                                             &exporter->fmt);
    if (!pool)
        return VLC_EGENERIC;

    struct vlc_dmabuf_frame frame;
    int ret = ExportSurface(exporter, va_surface_ids[0], &frame);
    if (ret == VLC_SUCCESS)
        ReleaseFrame(&frame);

    picture_pool_Release(pool);
    return ret;
}

static int Open(struct vlc_dmabuf_exporter *exporter)
{
    if (vlc_video_context_GetType(exporter->vctx) != VLC_VIDEO_CONTEXT_VAAPI)
        return VLC_EGENERIC;

    /* Non-vaapi chroma are filtered out here */
    if (DescribeChroma(exporter->fmt.i_chroma, &exporter->format) != VLC_SUCCESS)
        return VLC_EGENERIC;

    vlc_decoder_device *dec_device =
        vlc_video_context_HoldDevice(exporter->vctx);
    if (dec_device == NULL)
        return VLC_EGENERIC;

    if (dec_device->type != VLC_DECODER_DEVICE_VAAPI
     || dec_device->opaque == NULL)
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_EGENERIC;
    }

    if (tc_va_check_interop_blacklist(exporter, dec_device->opaque) != VLC_SUCCESS)
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_EGENERIC;
    }

    struct exporter_sys *sys = malloc(sizeof (*sys));
    if (unlikely(sys == NULL))
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_ENOMEM;
    }

    sys->dec_device = dec_device;
    sys->dpy = dec_device->opaque;

    exporter->sys = sys;

    if (tc_va_check_derive_image(exporter) != VLC_SUCCESS)
    {
        vlc_decoder_device_Release(dec_device);
        free(sys);
        return VLC_EGENERIC;
    }

    exporter->ops = &ops;
    return VLC_SUCCESS;
}

vlc_module_begin()
    set_description("VA-API dmabuf exporter")
    set_capability("dmabuf exporter", 10)
    set_callback_dmabuf_exporter(Open)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("vaapi")
vlc_module_end()
