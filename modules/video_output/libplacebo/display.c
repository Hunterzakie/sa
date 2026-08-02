/**
 * @file display.c
 * @brief libplacebo video output module
 */
/*****************************************************************************
 * Copyright © 2021 Niklas Haas
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

#include <stdlib.h>
#include <assert.h>

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_fs.h>
#include <vlc_subpicture.h>

#include "utils.h"
#include "instance.h"
#ifdef HAVE_VULKAN
# include "instance_vulkan.h"
# include "vk_interop.h"
# include <libplacebo/vulkan.h>
#endif

#include <libplacebo/renderer.h>
#include <libplacebo/utils/upload.h>
#include <libplacebo/swapchain.h>
#include <libplacebo/shaders/lut.h>

#define INTEROP_MAX_SLOTS 4

typedef struct vout_display_sys_t
{
    vlc_placebo_t *pl;
    pl_renderer renderer;
    pl_tex plane_tex[4];
    pl_tex fbo;

    // Pool of textures for the subpictures
    struct pl_overlay *overlays;
    struct pl_overlay_part *overlay_parts;
    pl_tex *overlay_tex;
    int num_overlays;

    // Storage for rendering parameters
    struct pl_filter_config upscaler;
    struct pl_filter_config downscaler;
    struct pl_deband_params deband;
    struct pl_sigmoid_params sigmoid;
    struct pl_color_map_params color_map;
    struct pl_dither_params dither;
    struct pl_render_params params;
    struct pl_color_space target;
    uint64_t target_icc_signature;
    struct pl_peak_detect_params peak_detect;
    enum pl_chroma_location yuv_chroma_loc;
    int dither_depth;

    struct pl_custom_lut *lut;
    char *lut_path;
    int lut_mode;

    const struct pl_hook *hook;
    char *hook_path;

    struct pl_dovi_metadata dovi_metadata;

#ifdef HAVE_VULKAN
    struct vlc_vk_device vk_dev;
    bool vk_dev_active;
    struct vlc_vk_interop interop;
    bool interop_active;
    
    /* Keep a cached pl_tex for each plane of each interop slot,
    so that we can reuse it across frames */
    pl_tex interop_slot_tex[INTEROP_MAX_SLOTS][PICTURE_PLANE_MAX];
    uint32_t interop_next_slot;

    /* One timeline shared by every slot and plane */
    VkSemaphore interop_sem;
    uint64_t interop_sem_value;
#endif
} vout_display_sys_t;

// Display callbacks
static void PictureRender(vout_display_t *, picture_t *, const vlc_render_subpicture *, vlc_tick_t);
static void PictureDisplay(vout_display_t *, picture_t *);
static int SetDisplaySize(vout_display_t *, unsigned width, unsigned height);
static void Close(vout_display_t *);
static void UpdateParams(vout_display_t *);
static void UpdateColorspaceHint(vout_display_t *, const video_format_t *);
static void UpdateIccProfile(vout_display_t *, const vlc_icc_profile_t *);

static const struct vlc_display_operations ops = {
    .close = Close,
    .prepare = PictureRender,
    .display = PictureDisplay,
    .set_display_size = SetDisplaySize,
    .set_icc_profile = UpdateIccProfile,
};

/* Software upload path (pl_upload_plane) */
static int OpenUpload(vout_display_t *vd, vout_display_sys_t *sys,
                      video_format_t *fmt)
{
    char *name = var_InheritString(vd, "pl-gpu");
    sys->pl = vlc_placebo_Create(vd->cfg, name);
    free(name);
    if (sys->pl == NULL)
        return VLC_EGENERIC;

    /* From now on, the error label will reset this to NULL. */
    vd->sys = sys;

    if (vlc_placebo_MakeCurrent(sys->pl) != VLC_SUCCESS)
        goto error;

    // Set colorspace hint *before* first swapchain resize
    UpdateColorspaceHint(vd, fmt);

    // Set initial framebuffer size
    int width = (int) vd->cfg->display.width;
    int height = (int) vd->cfg->display.height;
    if (!pl_swapchain_resize(sys->pl->swapchain, &width, &height))
        goto error;

    pl_gpu gpu = sys->pl->gpu;
    sys->renderer = pl_renderer_create(sys->pl->log, gpu);
    if (!sys->renderer)
        goto error;

    vlc_placebo_ReleaseCurrent(sys->pl);

    // Attempt using the input format as the display format
    if (vlc_placebo_FormatSupported(gpu, vd->source->i_chroma)) {
        fmt->i_chroma = vd->source->i_chroma;
    } else {
        fmt->i_chroma = 0;
        vlc_fourcc_t *list = vlc_fourcc_GetFallback(vd->source->i_chroma);
        if (list != NULL)
        {
            for (const vlc_fourcc_t *fcc = list; *fcc; fcc++) {
                if (vlc_placebo_FormatSupported(gpu, *fcc)) {
                    fmt->i_chroma = *fcc;
                    break;
                }
            }
            free(list);
        }

        if (fmt->i_chroma == 0) {
            fmt->i_chroma = VLC_CODEC_RGBA;
            msg_Warn(vd, "Failed picking any suitable input format, falling "
                     "back to RGBA for sanity!");
        }
    }
    sys->yuv_chroma_loc = vlc_fourcc_IsYUV(fmt->i_chroma) ?
                          vlc_placebo_ChromaLoc(fmt) : PL_CHROMA_UNKNOWN;

    // Hard-coded list of supported subtitle chromas (non-planar only!)
    static const vlc_fourcc_t subfmts[] = {
        VLC_CODEC_RGBA,
        VLC_CODEC_BGRA,
        VLC_CODEC_RGB555LE,
        VLC_CODEC_RGB565LE,
        VLC_CODEC_RGB24,
        VLC_CODEC_XRGB,
        VLC_CODEC_RGB233,
        VLC_CODEC_GREY,
        0
    };

    vd->info.subpicture_chromas = subfmts;
    vd->ops = &ops;

    UpdateParams(vd);
    return VLC_SUCCESS;

error:
    pl_renderer_destroy(&sys->renderer);
    vlc_placebo_Release(sys->pl);
    vd->sys = NULL;
    return VLC_EGENERIC;
}

#ifdef HAVE_VULKAN
static int CheckInteropPlanes(vout_display_t *vd,
                              const struct vlc_vk_interop *interop)
{
    video_format_t fmt = interop->format_probe.fmt_out;
    fmt.i_chroma = interop->format_probe.sw_chroma;

    struct pl_plane planes[4];
    int count = vlc_placebo_PlaneComponents(&fmt, planes);

    if (count != (int) interop->plane_count) {
        msg_Err(vd, "Interop declared %u planes, but %4.4s has %d",
                interop->plane_count, (const char *) &fmt.i_chroma, count);
        return VLC_EGENERIC;
    }

    return VLC_SUCCESS;
}

static int CreateInteropSem(vout_display_t *vd, vout_display_sys_t *sys,
                            VkExternalSemaphoreHandleTypeFlagBits handle_type)
{
    struct vlc_vk_interop *interop = &sys->interop;

    if (interop->ops->setup_acquire_sem == NULL)
        return VLC_SUCCESS;

    VkExportSemaphoreCreateInfo export_info = {
        .sType = VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO,
        .handleTypes = handle_type,
    };
    VkSemaphoreTypeCreateInfo type_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
        .pNext = &export_info,
        .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
        .initialValue = 0,
    };
    VkSemaphoreCreateInfo sem_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
        .pNext = &type_info,
    };

    VkResult res = sys->vk_dev.vkCreateSemaphore(sys->vk_dev.device, &sem_info, NULL, &sys->interop_sem);
    if (res != VK_SUCCESS) {
        msg_Err(vd, "Failed creating the interop timeline semaphore: %d", res);
        sys->interop_sem = VK_NULL_HANDLE;
        return VLC_EGENERIC;
    }

    sys->interop_sem_value = 0;

    for (uint32_t i = 0; i < interop->image_count; i++) {
        if (interop->ops->setup_acquire_sem(interop, sys->interop_sem, i)
                != VLC_SUCCESS) {
            msg_Err(vd, "Failed handing the timeline to interop slot %u", i);
            sys->vk_dev.vkDestroySemaphore(sys->vk_dev.device, sys->interop_sem, NULL);
            sys->interop_sem = VK_NULL_HANDLE;
            return VLC_EGENERIC;
        }
    }

    return VLC_SUCCESS;
}

/* Hardware zero-copy path, fallbacks to software upload */
static int OpenVkInterop(vout_display_t *vd, vout_display_sys_t *sys,
                       video_format_t *fmt, vlc_video_context *vctx)
{
    /* Phase 1 */
    struct vlc_vk_interop_probe probe;
    if (vlc_vk_interop_Probe(&probe, VLC_OBJECT(vd), vctx, fmt) != VLC_SUCCESS)
        return VLC_EGENERIC;

    /* Create the libplacebo instance and connect it to the Vulkan device. */
    char *name = var_InheritString(vd, "pl-gpu");
    sys->pl = vlc_placebo_New(vd->cfg, name);
    free(name);
    if (sys->pl == NULL)
        goto error_probe;

    const struct vlc_placebo_create_params params = {
        .instance_extensions      = probe.required_instance_extensions,
        .instance_extension_count = probe.required_instance_extension_count,
        .device_extensions        = probe.required_device_extensions,
        .device_extension_count   = probe.required_device_extension_count,
    };
    if (vlc_placebo_Connect(sys->pl, &params) != VLC_SUCCESS)
        goto error_pl;

    const bool needs_vk = probe.ops->upload_planes == NULL;
    const struct vlc_placebo_vk_handles *vk = vlc_placebo_GetVkHandles(sys->pl);
    if (vk == NULL && needs_vk)
        goto error_pl;

    if (vk != NULL) {
        vlc_vk_device_Init(&sys->vk_dev, vlc_object_logger(VLC_OBJECT(vd)));
        sys->vk_dev.inst.instance = vk->instance;
        sys->vk_dev.inst.get_proc_address = vk->get_instance_proc_addr;
        if (vlc_vk_device_SetExternal(&sys->vk_dev, vk->phys_device, vk->device,
                                      vk->graphics_queue_family) != VLC_SUCCESS)
            goto error_pl;
        sys->vk_dev_active = true;
    }

    struct vlc_vk_device *dev = sys->vk_dev_active ? &sys->vk_dev : NULL;

    /* Phase 2 */
    vlc_vk_interop_Init(&sys->interop);
    if (vlc_vk_interop_ProbeFormat(&sys->interop, &probe, dev, sys->pl->gpu)
            != VLC_SUCCESS)
        goto error_interop;

    /* Phase 3 */
    sys->interop.dev = dev;
    sys->interop.image_count = INTEROP_MAX_SLOTS;
    if (vlc_vk_interop_Load(&sys->interop) != VLC_SUCCESS)
        goto error_interop;

    /* Upload modules fill in their own planes, so there is no wrap to check */
    if (needs_vk && CheckInteropPlanes(vd, &sys->interop) != VLC_SUCCESS)
        goto error_interop;

    if (probe.acquire_sem_handle_type != 0 && CreateInteropSem(vd, sys, probe.acquire_sem_handle_type) != VLC_SUCCESS)
        goto error_interop;

    // Set the output format to the one that was probed and accepted by the interop
    *fmt = sys->interop.format_probe.fmt_out;

    if (vlc_placebo_MakeCurrent(sys->pl) != VLC_SUCCESS)
        goto error_interop;

    // Set colorspace hint *before* first swapchain resize
    UpdateColorspaceHint(vd, fmt);

    // Set initial framebuffer size
    int width = (int) vd->cfg->display.width;
    int height = (int) vd->cfg->display.height;
    if (!pl_swapchain_resize(sys->pl->swapchain, &width, &height))
        goto error_interop;

    sys->renderer = pl_renderer_create(sys->pl->log, sys->pl->gpu);
    vlc_placebo_ReleaseCurrent(sys->pl);

    if (sys->renderer == NULL)
        goto error_interop;

    static const vlc_fourcc_t subfmts[] = {
        VLC_CODEC_RGBA, VLC_CODEC_BGRA, VLC_CODEC_RGB555LE, VLC_CODEC_RGB565LE,
        VLC_CODEC_RGB24, VLC_CODEC_XRGB, VLC_CODEC_RGB233, VLC_CODEC_GREY, 0,
    };
    vd->info.subpicture_chromas = subfmts;
    vd->ops = &ops;
    UpdateParams(vd);

    sys->interop_active = true;
    return VLC_SUCCESS;

error_interop:
    pl_renderer_destroy(&sys->renderer);
    if (sys->interop_sem != VK_NULL_HANDLE) {
        sys->vk_dev.vkDestroySemaphore(sys->vk_dev.device, sys->interop_sem, NULL);
        sys->interop_sem = VK_NULL_HANDLE;
    }
    vlc_vk_interop_Close(&sys->interop);
    if (sys->vk_dev_active) {
        vlc_vk_device_Destroy(&sys->vk_dev);
        sys->vk_dev_active = false;
    }
    vlc_placebo_Release(sys->pl);
    sys->pl = NULL;
    return VLC_EGENERIC;

error_pl:
    if (sys->vk_dev_active) {
        vlc_vk_device_Destroy(&sys->vk_dev);
        sys->vk_dev_active = false;
    }
    vlc_placebo_Release(sys->pl);
    sys->pl = NULL;
error_probe:
    if (probe.vctx != NULL)
        vlc_video_context_Release(probe.vctx);
    return VLC_EGENERIC;
}
#endif

static int Open(vout_display_t *vd,
                video_format_t *fmt, vlc_video_context *vctx)
{
    vout_display_sys_t *sys =
        vlc_obj_calloc(VLC_OBJECT(vd), 1, sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;
    vd->sys = sys;

#ifdef HAVE_VULKAN
    if (OpenVkInterop(vd, sys, fmt, vctx) == VLC_SUCCESS)
        return VLC_SUCCESS;
    msg_Dbg(vd, "no usable vkinterop, falling back to software upload");
#else
    (void) vctx;
#endif

    return OpenUpload(vd, sys, fmt);
}

static void Close(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    pl_gpu gpu = sys->pl->gpu;

    if (vlc_placebo_MakeCurrent(sys->pl) == VLC_SUCCESS) {
        for (int i = 0; i < 4; i++)
            pl_tex_destroy(gpu, &sys->plane_tex[i]);
        for (int i = 0; i < sys->num_overlays; i++)
            pl_tex_destroy(gpu, &sys->overlay_tex[i]);
#ifdef HAVE_VULKAN
        for (int s = 0; s < INTEROP_MAX_SLOTS; s++)
            for (int i = 0; i < PICTURE_PLANE_MAX; i++)
                pl_tex_destroy(gpu, &sys->interop_slot_tex[s][i]);

        if (sys->interop_active)
            vlc_vk_interop_Close(&sys->interop);
#endif
        pl_renderer_destroy(&sys->renderer);
        vlc_placebo_ReleaseCurrent(sys->pl);
    }

    if (sys->overlays) {
        free(sys->overlays);
        free(sys->overlay_parts);
        free(sys->overlay_tex);
    }

    pl_lut_free(&sys->lut);
    free(sys->lut_path);

    pl_mpv_user_shader_destroy(&sys->hook);
    free(sys->hook_path);

#ifdef HAVE_VULKAN
    if (sys->interop_active && sys->interop_sem != VK_NULL_HANDLE)
        sys->vk_dev.vkDestroySemaphore(sys->vk_dev.device, sys->interop_sem, NULL);
    if (sys->vk_dev_active)
        vlc_vk_device_Destroy(&sys->vk_dev);
#endif

    vlc_placebo_Release(sys->pl);
}

#ifdef HAVE_VULKAN
static void ReleasePlanes(pl_gpu gpu, pl_tex *slot_tex, unsigned count)
{
    for (unsigned i = 0; i < count; i++)
        pl_vulkan_release_ex(gpu, pl_vulkan_release_params(
            .tex = slot_tex[i],
            .layout = VK_IMAGE_LAYOUT_GENERAL,
            .qf = VK_QUEUE_FAMILY_IGNORED,
        ));
}

static int RenderInteropUpload(vout_display_t *vd, struct pl_frame *img,
                               picture_t *pic)
{
    vout_display_sys_t *sys = vd->sys;
    const struct vlc_vk_interop *interop = &sys->interop;
    unsigned planes = 0;

    if (interop->ops->upload_planes(interop, img->planes, &planes, pic) != VLC_SUCCESS)
        return VLC_EGENERIC;

    img->num_planes = planes;
    return VLC_SUCCESS;
}

/* One single-plane image per plane, each wrapped once into a cached pl_tex and
 * reused across frames. The interop copies the new frame into the same images
 * each time, so the wrap stays valid. */
static int RenderInteropImport(vout_display_t *vd, struct pl_frame *img,
                               picture_t *pic, const video_format_t *cfmt)
{
    vout_display_sys_t *sys = vd->sys;
    pl_gpu gpu = sys->pl->gpu;
    struct vlc_vk_interop *interop = &sys->interop;
    const unsigned planes = interop->plane_count;

    // Round-robin across image_count slots
    const uint32_t slot = sys->interop_next_slot;
    sys->interop_next_slot = (slot + 1) % interop->image_count;
    pl_tex *slot_tex = sys->interop_slot_tex[slot];

    const bool wrapped = slot_tex[0] != NULL;
    const uint64_t base = sys->interop_sem_value;
    VkImage images[PICTURE_PLANE_MAX] = { VK_NULL_HANDLE };
    struct vlc_vk_interop_sync sync = { .sem = sys->interop_sem };
    unsigned held = 0;
    int ret = VLC_EGENERIC;

    if (wrapped && sys->interop_sem != VK_NULL_HANDLE) {
        while (held < planes
            && pl_vulkan_hold_ex(gpu, pl_vulkan_hold_params(
                   .tex = slot_tex[held],
                   .layout = VK_IMAGE_LAYOUT_GENERAL,
                   .qf = VK_QUEUE_FAMILY_IGNORED,
                   .semaphore = { sys->interop_sem, base + held + 1 },
               )))
            held++;
    }

    sync.wait_value = base + held;

    if (wrapped && sys->interop_sem != VK_NULL_HANDLE && held < planes) {
        msg_Err(vd, "pl_vulkan_hold_ex failed for plane %u", held);
        ReleasePlanes(gpu, slot_tex, held);
        goto done;
    }

    if (interop->ops->import_images(interop, images, &sync, pic, slot) != VLC_SUCCESS) {
        ReleasePlanes(gpu, slot_tex, held);
        goto done;
    }

    img->num_planes = planes;
    for (unsigned i = 0; i < planes; i++) {
        const struct vlc_vk_plane_info *info = &interop->plane_info[i];

        // Wrap the VkImage into a pl_tex if it hasn't been wrapped yet. The wrap is cached and reused across frames.
        if (!wrapped) {
            slot_tex[i] = pl_vulkan_wrap(gpu, pl_vulkan_wrap_params(
                .image  = images[i],
                .width  = vd->source->i_width * info->w.num / info->w.den,
                .height = vd->source->i_height * info->h.num / info->h.den,
                .format = info->format,
                .usage  = VK_IMAGE_USAGE_SAMPLED_BIT,
            ));

            if (slot_tex[i] == NULL) {
                msg_Err(vd, "pl_vulkan_wrap failed for plane %u", i);
                for (unsigned j = 0; j < i; j++)
                    pl_tex_destroy(gpu, &slot_tex[j]);
                goto done;
            }
        }

        img->planes[i] = (struct pl_plane){ .texture = slot_tex[i] };
    }

    vlc_placebo_PlaneComponents(cfmt, img->planes);

    /* First frame the image is still UNDEFINED for Vulkan, so we declare
     * UNDEFINED, later frames are held and released in GENERAL */
    const VkImageLayout layout = wrapped ? VK_IMAGE_LAYOUT_GENERAL
                                         : VK_IMAGE_LAYOUT_UNDEFINED;

    const pl_vulkan_sem release_sem = sync.signal_value
        ? (pl_vulkan_sem) { sync.sem, sync.signal_value }
        : (pl_vulkan_sem) { VK_NULL_HANDLE, 0 };

    for (unsigned i = 0; i < planes; i++)
        pl_vulkan_release_ex(gpu, pl_vulkan_release_params(
            .tex = slot_tex[i],
            .layout = layout,
            .qf = VK_QUEUE_FAMILY_IGNORED,
            .semaphore = release_sem,
        ));

    ret = VLC_SUCCESS;

done:
    sys->interop_sem_value = sync.signal_value ? sync.signal_value : sync.wait_value;
    return ret;
}

static int RenderInterop(vout_display_t *vd, struct pl_frame *img,
                         picture_t *pic, const video_format_t *cfmt)
{
    vout_display_sys_t *sys = vd->sys;
    const struct vlc_vk_interop *interop = &sys->interop;

    if (interop->ops->upload_planes != NULL)
        return RenderInteropUpload(vd, img, pic);

    return RenderInteropImport(vd, img, pic, cfmt);
}
#endif

static void PictureRender(vout_display_t *vd, picture_t *pic,
                          const vlc_render_subpicture *subpicture,
                          vlc_tick_t date)
{
    VLC_UNUSED(date);
    vout_display_sys_t *sys = vd->sys;
    pl_gpu gpu = sys->pl->gpu;
    bool failed = false;

    if (vlc_placebo_MakeCurrent(sys->pl) != VLC_SUCCESS)
        return;

    struct pl_swapchain_frame frame;
    if (!pl_swapchain_start_frame(sys->pl->swapchain, &frame)) {
        vlc_placebo_ReleaseCurrent(sys->pl);
        return; // Probably benign error, ignore it
    }
    sys->fbo = frame.fbo;

#if PL_API_VER >= 199
    bool need_vflip = false;
#else
    bool need_vflip = frame.flipped;
#endif

    /* The opaque hw chroma has no chroma description, so colour/repr must be
     * derived from the underlying software chroma on the interop path */
    const video_format_t *cfmt = vd->fmt;
#ifdef HAVE_VULKAN
    video_format_t hw_fmt;
    if (sys->interop_active) {
        hw_fmt = *vd->fmt;
        hw_fmt.i_chroma = sys->interop.format_probe.sw_chroma;
        cfmt = &hw_fmt;
    }
#endif

    struct pl_frame img = {
        .num_planes = pic->i_planes,
        .color      = vlc_placebo_ColorSpace(cfmt),
        .repr       = vlc_placebo_ColorRepr(cfmt),
        .crop = {
            .x0 = pic->format.i_x_offset,
            .y0 = pic->format.i_y_offset,
            .x1 = pic->format.i_x_offset + pic->format.i_visible_width,
            .y1 = pic->format.i_y_offset + pic->format.i_visible_height,
        },
    };

    vlc_placebo_frame_DoviMetadata(&img, pic, &sys->dovi_metadata);

    struct vlc_ancillary *iccp = picture_GetAncillary(pic, VLC_ANCILLARY_ID_ICC);
    if (iccp) {
        vlc_icc_profile_t *icc = vlc_ancillary_GetData(iccp);
        img.profile.data = icc->data;
        img.profile.len = icc->size;
        pl_icc_profile_compute_signature(&img.profile);
    }

    struct vlc_ancillary *hdrplus = picture_GetAncillary(pic, VLC_ANCILLARY_ID_HDR10PLUS);
    if (hdrplus) {
        vlc_video_hdr_dynamic_metadata_t *hdm = vlc_ancillary_GetData(hdrplus);
        vlc_placebo_HdrMetadata(hdm, &img.color.hdr);
    }

#ifdef HAVE_VULKAN
    if (sys->interop_active) {
        if (RenderInterop(vd, &img, pic, cfmt) != VLC_SUCCESS) {
            failed = true;
            goto done;
        }
    } else
#endif
    {
        // Upload the image data for each plane
        struct pl_plane_data data[4];
        if (!vlc_placebo_PlaneData(pic, data)) {
            // This should never happen, in theory
            assert(!"Failed processing the picture_t into pl_plane_data!?");
        }

        for (int i = 0; i < pic->i_planes; i++) {
            struct pl_plane *plane = &img.planes[i];
            if (!pl_upload_plane(gpu, plane, &sys->plane_tex[i], &data[i])) {
                msg_Err(vd, "Failed uploading image data!");
                failed = true;
                goto done;
            }

            // Matches only the chroma planes, never luma or alpha
            if (sys->yuv_chroma_loc != PL_CHROMA_UNKNOWN && i != 0 && i != 3)
                pl_chroma_location_offset(sys->yuv_chroma_loc, &plane->shift_x,
                                          &plane->shift_y);
        }
    }

    struct pl_frame target;
    pl_frame_from_swapchain(&target, &frame);
    if (vd->cfg->icc_profile) {
        target.profile.data = vd->cfg->icc_profile->data;
        target.profile.len = vd->cfg->icc_profile->size;
        target.profile.signature = sys->target_icc_signature;
        if (!target.profile.signature) {
            pl_icc_profile_compute_signature(&target.profile);
            sys->target_icc_signature = target.profile.signature;
        }
    }

    // Set the target crop dynamically based on the swapchain flip state
    vout_display_place_t place = *vd->place;
    if (need_vflip)
    {
        place.y = place.height + place.y;
        place.height = -place.height;
    }

#define SWAP(a, b) { float _tmp = (a); (a) = (b); (b) = _tmp; }
    switch (vd->fmt->orientation) {
    case ORIENT_HFLIPPED:
        SWAP(img.crop.x0, img.crop.x1);
        break;
    case ORIENT_VFLIPPED:
        SWAP(img.crop.y0, img.crop.y1);
        break;
    case ORIENT_ROTATED_90:
        img.rotation = PL_ROTATION_90;
        break;
    case ORIENT_ROTATED_180:
        img.rotation = PL_ROTATION_180;
        break;
    case ORIENT_ROTATED_270:
        img.rotation = PL_ROTATION_270;
        break;
    case ORIENT_TRANSPOSED:
        img.rotation = PL_ROTATION_90;
        SWAP(img.crop.y0, img.crop.y1);
        break;
    case ORIENT_ANTI_TRANSPOSED:
        img.rotation = PL_ROTATION_90;
        SWAP(img.crop.x0, img.crop.x1);
    default:
        break;
    }

    target.crop = (struct pl_rect2df) {
        place.x, place.y, place.x + place.width, place.y + place.height,
    };

    // Override the target colorimetry only if the user requests it
    if (sys->target.primaries)
        target.color.primaries = sys->target.primaries;
    if (sys->target.transfer)
        target.color.transfer = sys->target.transfer;
    if (sys->dither_depth > 0) {
        // override the sample depth without affecting the color encoding
        struct pl_bit_encoding *bits = &target.repr.bits;
        float scale = bits->color_depth / bits->sample_depth;
        bits->sample_depth = sys->dither_depth;
        bits->color_depth = scale * sys->dither_depth;
    }

    if (subpicture) {
        int num_regions = subpicture->regions.size;
        const struct subpicture_region_rendered *r;

        // Grow the overlays array if needed
        if (num_regions > sys->num_overlays) {
            sys->overlays = realloc(sys->overlays, num_regions * sizeof(struct pl_overlay));
            sys->overlay_parts = realloc(sys->overlay_parts, num_regions * sizeof(struct pl_overlay_part));
            sys->overlay_tex = realloc(sys->overlay_tex, num_regions * sizeof(pl_tex));
            if (!sys->overlays || !sys->overlay_parts || !sys->overlay_tex) {
                // Unlikely OOM, just do whatever
                sys->num_overlays = 0;
                failed = true;
                goto done;
            }
            // Clear the newly added texture pointers for pl_upload_plane
            for (int i = sys->num_overlays; i < num_regions; i++)
                sys->overlay_tex[i] = NULL;
            sys->num_overlays = num_regions;
        }

        // Upload all of the regions
        int i = 0;
        vlc_vector_foreach(r, &subpicture->regions) {
            assert(r->p_picture->i_planes == 1);
            struct pl_plane_data subdata[4];
            if (!vlc_placebo_PlaneData(r->p_picture, subdata))
                assert(!"Failed processing the subpicture_t into pl_plane_data!?");

            if (!pl_upload_plane(gpu, NULL, &sys->overlay_tex[i], subdata)) {
                msg_Err(vd, "Failed uploading subpicture region!");
                num_regions = i; // stop here
                break;
            }

            int ysign = need_vflip ? (-1) : 1;
            sys->overlays[i] = (struct pl_overlay) {
                .tex   = sys->overlay_tex[i],
                .mode  = PL_OVERLAY_NORMAL,
                .color = vlc_placebo_ColorSpace(&r->p_picture->format),
                .repr  = vlc_placebo_ColorRepr(&r->p_picture->format),
                .parts = &sys->overlay_parts[i],
                .num_parts = 1,
            };

            sys->overlay_parts[i] = (struct pl_overlay_part) {
                .src = {
                    .x1 = r->p_picture->format.i_visible_width,
                    .y1 = r->p_picture->format.i_visible_height,
                },
                .dst = {
                    .x0 = r->place.x,
                    .y0 = r->place.y * ysign,
                    .x1 = (r->place.x + r->place.width ),
                    .y1 = (r->place.y + r->place.height) * ysign,
                },
            };
            i++;
        }

        // Update the target information to reference the subpictures
        target.overlays = sys->overlays;
        target.num_overlays = num_regions;
    }

    // If we don't cover the entire output, clear it first
    // struct pl_rect2d full = {0, 0, frame.fbo->params.w, frame.fbo->params.h };
    // struct pl_rect2d norm = {place.x, place.y, place.x + place.width, place.y + place.height };
    // pl_rect2d_normalize(&norm);
    // if (!pl_rect2d_eq(norm, full)) {
        // TODO: make background color configurable?
        pl_tex_clear(gpu, frame.fbo, (float[4]){ 0.0, 0.0, 0.0, 0.0 });
    // }

    switch (sys->lut_mode) {
    case LUT_DECODING:
        img.lut_type = PL_LUT_CONVERSION;
        img.lut = sys->lut;
        break;
    case LUT_ENCODING:
        target.lut_type = PL_LUT_CONVERSION;
        target.lut = sys->lut;
        break;
    }

    // Dispatch the actual image rendering with the pre-configured parameters
    if (!pl_render_image(sys->renderer, &img, &target, &sys->params)) {
        msg_Err(vd, "Failed rendering frame!");
        failed = true;
        goto done;
    }

done:

    if (failed)
        pl_tex_clear(gpu, frame.fbo, (float[4]){ 1.0, 0.0, 0.0, 1.0 });

    pl_gpu_flush(gpu);

    vlc_placebo_ReleaseCurrent(sys->pl);
}

static void PictureDisplay(vout_display_t *vd, picture_t *pic)
{
    VLC_UNUSED(pic);
    vout_display_sys_t *sys = vd->sys;
    if (!sys->fbo)
        return;

    sys->fbo = NULL;
    if (vlc_placebo_MakeCurrent(sys->pl) == VLC_SUCCESS) {
        if (!pl_swapchain_submit_frame(sys->pl->swapchain))
            msg_Err(vd, "Failed rendering frame!");
        pl_swapchain_swap_buffers(sys->pl->swapchain);
        vlc_placebo_ReleaseCurrent(sys->pl);
    }
}

static void UpdateColorspaceHint(vout_display_t *vd, const video_format_t *fmt)
{
    vout_display_sys_t *sys = vd->sys;
    struct pl_swapchain_colors hint = {0};

    switch (var_InheritInteger(vd, "pl-output-hint")) {
    case OUTPUT_AUTO: ;
        const struct pl_color_space csp = vlc_placebo_ColorSpace(fmt);
        hint.primaries = csp.primaries;
        hint.transfer = csp.transfer;
        hint.hdr = (struct pl_hdr_metadata) {
            .prim.green.x   = fmt->mastering.primaries[0] / 50000.0f,
            .prim.green.y   = fmt->mastering.primaries[1] / 50000.0f,
            .prim.blue.x    = fmt->mastering.primaries[2] / 50000.0f,
            .prim.blue.y    = fmt->mastering.primaries[3] / 50000.0f,
            .prim.red.x     = fmt->mastering.primaries[4] / 50000.0f,
            .prim.red.y     = fmt->mastering.primaries[5] / 50000.0f,
            .prim.white.x   = fmt->mastering.white_point[0] / 50000.0f,
            .prim.white.y   = fmt->mastering.white_point[1] / 50000.0f,
            .max_luma       = fmt->mastering.max_luminance / 10000.0f,
            .min_luma       = fmt->mastering.min_luminance / 10000.0f,
            .max_cll        = fmt->lighting.MaxCLL,
            .max_fall       = fmt->lighting.MaxFALL,
        };
        break;
    case OUTPUT_SDR:
        break;
    case OUTPUT_HDR10:
        hint.primaries = PL_COLOR_PRIM_BT_2020;
        hint.transfer = PL_COLOR_TRC_PQ;
        break;
    case OUTPUT_HLG:
        hint.primaries = PL_COLOR_PRIM_BT_2020;
        hint.transfer = PL_COLOR_TRC_HLG;
        break;
    }

    pl_swapchain_colorspace_hint(sys->pl->swapchain, &hint);
}

static void UpdateIccProfile(vout_display_t *vd, const vlc_icc_profile_t *prof)
{
    vout_display_sys_t *sys = vd->sys;
    sys->target_icc_signature = 0; /* recompute signature on next PictureRender */
    (void) prof; /* we get the current value from vout_display_cfg_t */
}

static int SetDisplaySize(vout_display_t *vd, unsigned width_, unsigned height_)
{
    vout_display_sys_t *sys = vd->sys;

    /* The following resize should be automatic on most platforms but can
     * trigger bugs on some platform with some drivers, that have been seen
     * on Windows in particular. Doing it right now enforces the correct
     * behavior and prevents these bugs.
     * In addition, platforms like Wayland need the call as the size of the
     * window is defined by the size of the content, and not the opposite.
     * The swapchain creation won't be done twice with this call. */
    int width = (int) width_;
    int height = (int) height_;
    if (vlc_placebo_MakeCurrent(sys->pl) != VLC_SUCCESS)
        return VLC_SUCCESS; // ignore errors

    vlc_placebo_Resize(sys->pl, width_, height_);
    pl_swapchain_resize(sys->pl->swapchain, &width, &height);
    vlc_placebo_ReleaseCurrent(sys->pl);

    /* NOTE: We currently ignore resizing failures that are transient
     * on X11. Maybe improving resizing might fix that, but we don't
     * implement reset_pictures anyway.
    if (width != (int) width_ || height != (int) height_)
        return VLC_EGENERIC;
    */
    return VLC_SUCCESS;
}

static void LoadCustomLUT(vout_display_sys_t *sys, const char *filepath)
{
    if (!filepath || !*filepath) {
        pl_lut_free(&sys->lut);
        return;
    }

    if (sys->lut_path && strcmp(filepath, sys->lut_path) == 0)
        return; // same LUT

    char *lut_file = NULL;
    FILE *fs = NULL;

    free(sys->lut_path);
    sys->lut_path = strdup(filepath);

    fs = vlc_fopen(filepath, "rb");
    if (!fs)
        goto error;
    int ret = fseek(fs, 0, SEEK_END);
    if (ret == -1)
        goto error;
    long length = ftell(fs);
    if (length < 0)
        goto error;
    rewind(fs);

    lut_file = vlc_alloc(length, sizeof(*lut_file));
    if (!lut_file)
        goto error;
    ret = fread(lut_file, length, 1, fs);
    if (ret != 1)
        goto error;
    sys->lut = pl_lut_parse_cube(sys->pl->log, lut_file, length);
    if (!sys->lut)
        goto error;

    fclose(fs);
    free(lut_file);
    return;

error:
    free(lut_file);
    if (fs)
        fclose(fs);
    return;
}

static void LoadUserShader(vout_display_sys_t *sys, const char *filepath)
{
    if (!filepath || !*filepath) {
        pl_mpv_user_shader_destroy(&sys->hook);
        return;
    }

    if (sys->hook_path && strcmp(filepath, sys->hook_path) == 0)
        return; // same shader

    char *shader_str = NULL;
    FILE *fs = NULL;

    free(sys->hook_path);
    sys->hook_path = strdup(filepath);

    fs = vlc_fopen(filepath, "rb");
    int ret = fseek(fs, 0, SEEK_END);
    if (ret == -1)
        goto error;
    long length = ftell(fs);
    if (length < 0)
        goto error;
    rewind(fs);

    shader_str = vlc_alloc(length, sizeof(*shader_str));
    if (!shader_str)
        goto error;
    ret = fread(shader_str, length, 1, fs);
    if (ret != 1)
        goto error;
    sys->hook = pl_mpv_user_shader_parse(sys->pl->gpu, shader_str, length);
    if (!sys->hook)
        goto error;

    fclose(fs);
    free(shader_str);
    return;

error:
    free(shader_str);
    if (fs)
        fclose(fs);
    return;
}

// Options

#define PROVIDER_TEXT N_("GPU instance provider")
#define PROVIDER_LONGTEXT N_( \
    "Extension which provides the GPU instance to use.")

vlc_module_begin ()
    set_shortname ("libplacebo")
    set_description (N_("libplacebo video output"))
    set_subcategory (SUBCAT_VIDEO_VOUT)
    set_callback_display(Open, 0)
    add_shortcut ("libplacebo", "pl")
    add_module ("pl-gpu", "libplacebo gpu", "any", PROVIDER_TEXT, PROVIDER_LONGTEXT)

    set_section("Custom shaders", NULL)
    add_loadfile("pl-user-shader", NULL, USER_SHADER_FILE_TEXT, USER_SHADER_FILE_LONGTEXT)

    set_section("Scaling", NULL)
    add_integer("pl-upscaler-preset", SCALE_BUILTIN,
            UPSCALER_PRESET_TEXT, SCALER_PRESET_LONGTEXT)
            change_integer_list(scale_values, scale_text)
    add_integer("pl-downscaler-preset", SCALE_BUILTIN,
            DOWNSCALER_PRESET_TEXT, SCALER_PRESET_LONGTEXT)
            change_integer_list(scale_values, scale_text)
    add_integer_with_range("pl-lut-entries", 64, 16, 256,
            LUT_ENTRIES_TEXT, LUT_ENTRIES_LONGTEXT)
    add_float_with_range("pl-antiringing", 0.0,
            0.0, 1.0, ANTIRING_TEXT, ANTIRING_LONGTEXT)
    add_bool("pl-sigmoid", !!pl_render_default_params.sigmoid_params,
            SIGMOID_TEXT, SIGMOID_LONGTEXT)
    add_float_with_range("pl-sigmoid-center", pl_sigmoid_default_params.center,
            0., 1., SIGMOID_CENTER_TEXT, SIGMOID_CENTER_LONGTEXT)
    add_float_with_range("pl-sigmoid-slope", pl_sigmoid_default_params.slope,
            1., 20., SIGMOID_SLOPE_TEXT, SIGMOID_SLOPE_LONGTEXT)

    set_section("Debanding", NULL)
    add_bool("pl-debanding", false, DEBAND_TEXT, DEBAND_LONGTEXT)
    add_integer("pl-iterations", pl_deband_default_params.iterations,
            DEBAND_ITER_TEXT, DEBAND_ITER_LONGTEXT)
    add_float("pl-threshold", pl_deband_default_params.threshold,
            DEBAND_THRESH_TEXT, DEBAND_THRESH_LONGTEXT)
    add_float("pl-radius", pl_deband_default_params.radius,
            DEBAND_RADIUS_TEXT, DEBAND_RADIUS_LONGTEXT)
    add_float("pl-grain", pl_deband_default_params.grain,
            DEBAND_GRAIN_TEXT, DEBAND_GRAIN_LONGTEXT)

    set_section("Colorspace conversion", NULL)
    add_integer("pl-output-hint", true, OUTPUT_HINT_TEXT, OUTPUT_HINT_LONGTEXT)
            change_integer_list(output_values, output_text)
    add_placebo_color_map_opts("pl")
    add_integer("pl-target-prim", PL_COLOR_PRIM_UNKNOWN, PRIM_TEXT, PRIM_LONGTEXT)
            change_integer_list(prim_values, prim_text)
    add_integer("pl-target-trc", PL_COLOR_TRC_UNKNOWN, TRC_TEXT, TRC_LONGTEXT)
            change_integer_list(trc_values, trc_text)

    add_loadfile("pl-lut-file", NULL, LUT_FILE_TEXT, LUT_FILE_LONGTEXT)
    add_integer("pl-lut-mode", LUT_DISABLED, LUT_MODE_TEXT, LUT_MODE_LONGTEXT)
            change_integer_list(lut_mode_values, lut_mode_text)

    // TODO: support for ICC profiles

    add_float_with_range("pl-peak-period", pl_peak_detect_default_params.smoothing_period,
            0., 1000., PEAK_PERIOD_TEXT, PEAK_PERIOD_LONGTEXT)
    add_float("pl-scene-threshold-low", pl_peak_detect_default_params.scene_threshold_low,
            SCENE_THRESHOLD_LOW_TEXT, SCENE_THRESHOLD_LOW_LONGTEXT)
    add_float("pl-scene-threshold-high", pl_peak_detect_default_params.scene_threshold_high,
            SCENE_THRESHOLD_HIGH_TEXT, SCENE_THRESHOLD_HIGH_LONGTEXT)

#if PL_API_VER >= 285
    add_float_with_range("pl-contrast-recovery", pl_color_map_default_params.contrast_recovery,
            0., 3., CONTRAST_RECOVERY_TEXT, CONTRAST_RECOVERY_LONGTEXT)
    add_float_with_range("pl-contrast-smoothness", pl_color_map_default_params.contrast_smoothness,
            0., 10., CONTRAST_SMOOTHNESS_TEXT, CONTRAST_SMOOTHNESS_LONGTEXT)
#endif

    set_section("Dithering", NULL)
    add_integer("pl-dither", -1,
            DITHER_TEXT, DITHER_LONGTEXT)
            change_integer_list(dither_values, dither_text)
    add_integer_with_range("pl-dither-size", pl_dither_default_params.lut_size,
            1, 8, DITHER_SIZE_TEXT, DITHER_SIZE_LONGTEXT)
    add_bool("pl-temporal-dither", pl_dither_default_params.temporal,
            TEMPORAL_DITHER_TEXT, TEMPORAL_DITHER_LONGTEXT)
    add_integer_with_range("pl-dither-depth", 0,
            0, 16, DITHER_DEPTH_TEXT, DITHER_DEPTH_LONGTEXT)

    set_section("Custom upscaler (when preset = custom)", NULL)
    add_integer("pl-upscaler-kernel", FILTER_BOX,
            KERNEL_TEXT, KERNEL_LONGTEXT)
            change_integer_list(filter_values, filter_text)
    add_integer("pl-upscaler-window", FILTER_NONE,
            WINDOW_TEXT, WINDOW_LONGTEXT)
            change_integer_list(filter_values, filter_text)
    add_bool("pl-upscaler-polar", false, POLAR_TEXT, POLAR_LONGTEXT)
    add_float_with_range("pl-upscaler-clamp", 0.0,
            0.0, 1.0, CLAMP_TEXT, CLAMP_LONGTEXT)
    add_float_with_range("pl-upscaler-blur", 1.0,
            0.0, 100.0, BLUR_TEXT, BLUR_LONGTEXT)
    add_float_with_range("pl-upscaler-taper", 0.0,
            0.0, 10.0, TAPER_TEXT, TAPER_LONGTEXT)

    set_section("Custom downscaler (when preset = custom)", NULL)
    add_integer("pl-downscaler-kernel", FILTER_BOX,
            KERNEL_TEXT, KERNEL_LONGTEXT)
            change_integer_list(filter_values, filter_text)
    add_integer("pl-downscaler-window", FILTER_NONE,
            WINDOW_TEXT, WINDOW_LONGTEXT)
            change_integer_list(filter_values, filter_text)
    add_bool("pl-downscaler-polar", false, POLAR_TEXT, POLAR_LONGTEXT)
    add_float_with_range("pl-downscaler-clamp", 0.0,
            0.0, 1.0, CLAMP_TEXT, CLAMP_LONGTEXT)
    add_float_with_range("pl-downscaler-blur", 1.0,
            0.0, 100.0, BLUR_TEXT, BLUR_LONGTEXT)
    add_float_with_range("pl-downscaler-taper", 0.0,
            0.0, 10.0, TAPER_TEXT, TAPER_LONGTEXT)

    set_section("Performance tweaks / debugging", NULL)
    add_bool("pl-skip-aa", false, SKIP_AA_TEXT, SKIP_AA_LONGTEXT)
    add_float_with_range("pl-polar-cutoff", 0.001,
            0., 1., POLAR_CUTOFF_TEXT, POLAR_CUTOFF_LONGTEXT)
    add_bool("pl-overlay-direct", false, OVERLAY_DIRECT_TEXT, OVERLAY_DIRECT_LONGTEXT)
    add_bool("pl-disable-linear", false, DISABLE_LINEAR_TEXT, DISABLE_LINEAR_LONGTEXT)
    add_bool("pl-force-general", false, FORCE_GENERAL_TEXT, FORCE_GENERAL_LONGTEXT)
    add_bool("pl-delayed-peak", false, DELAYED_PEAK_TEXT, DELAYED_PEAK_LONGTEXT)

vlc_module_end ()

// Update the renderer settings based on the current configuration.
//
// XXX: This could be called every time the parameters change, but currently
// VLC does not allow that - so we're stuck with doing it once on Open().
// Should be changed as soon as it's possible!
static void UpdateParams(vout_display_t *vd)
{
    vout_display_sys_t *sys = vd->sys;
    vlc_placebo_ColorMapParams(VLC_OBJECT(vd), "pl", &sys->color_map);

    sys->deband = pl_deband_default_params;
    sys->deband.iterations = var_InheritInteger(vd, "pl-iterations");
    sys->deband.threshold = var_InheritFloat(vd, "pl-threshold");
    sys->deband.radius = var_InheritFloat(vd, "pl-radius");
    sys->deband.grain = var_InheritFloat(vd, "pl-grain");
    bool use_deband = sys->deband.iterations > 0 || sys->deband.grain > 0;
    use_deband &= var_InheritBool(vd, "pl-debanding");

    sys->sigmoid = pl_sigmoid_default_params;
    sys->sigmoid.center = var_InheritFloat(vd, "pl-sigmoid-center");
    sys->sigmoid.slope = var_InheritFloat(vd, "pl-sigmoid-slope");
    bool use_sigmoid = var_InheritBool(vd, "pl-sigmoid");

    sys->dither = pl_dither_default_params;
    int method = var_InheritInteger(vd, "pl-dither");
    bool use_dither = method >= 0;
    sys->dither.method = use_dither ? method : 0;
    sys->dither.lut_size = var_InheritInteger(vd, "pl-dither-size");
    sys->dither.temporal = var_InheritBool(vd, "pl-temporal-dither");

    sys->params = pl_render_default_params;
    sys->params.deband_params = use_deband ? &sys->deband : NULL;
    sys->params.sigmoid_params = use_sigmoid ? &sys->sigmoid : NULL;
    sys->params.color_map_params = &sys->color_map;
    sys->params.dither_params = use_dither ? &sys->dither : NULL;
    sys->params.lut_entries = var_InheritInteger(vd, "pl-lut-entries");
    sys->params.antiringing_strength = var_InheritFloat(vd, "pl-antiringing");
    sys->params.skip_anti_aliasing = var_InheritBool(vd, "pl-skip-aa");
    sys->params.polar_cutoff = var_InheritFloat(vd, "pl-polar-cutoff");
    sys->params.disable_linear_scaling = var_InheritBool(vd, "pl-disable-linear");
    sys->params.disable_builtin_scalers = var_InheritBool(vd, "pl-force-general");

    sys->peak_detect.smoothing_period = var_InheritFloat(vd, "pl-peak-period");
    sys->peak_detect.scene_threshold_low = var_InheritFloat(vd, "pl-scene-threshold-low");
    sys->peak_detect.scene_threshold_high = var_InheritFloat(vd, "pl-scene-threshold-high");
#if PL_API_VER >= 254
    sys->peak_detect.allow_delayed = var_InheritBool(vd, "pl-delayed-peak");
#else
    sys->params.allow_delayed_peak_detect = var_InheritBool(vd, "pl-delayed-peak");
#endif
    if (sys->peak_detect.smoothing_period > 0.0)
        sys->params.peak_detect_params = &sys->peak_detect;

#if PL_API_VER >= 285
    sys->color_map.contrast_recovery = var_InheritFloat(vd, "pl-contrast-recovery");
    sys->color_map.contrast_smoothness = var_InheritFloat(vd, "pl-contrast-smoothness");
#endif

    int preset = var_InheritInteger(vd, "pl-upscaler-preset");
    sys->params.upscaler = scale_config[preset];
    if (preset == SCALE_CUSTOM) {
        sys->params.upscaler = &sys->upscaler;
        sys->upscaler = (struct pl_filter_config) {
            .kernel = filter_fun[var_InheritInteger(vd, "pl-upscaler-kernel")],
            .window = filter_fun[var_InheritInteger(vd, "pl-upscaler-window")],
            .clamp  = var_InheritFloat(vd, "pl-upscaler-clamp"),
            .blur   = var_InheritFloat(vd, "pl-upscaler-blur"),
            .taper  = var_InheritFloat(vd, "pl-upscaler-taper"),
            .polar  = var_InheritBool(vd, "pl-upscaler-polar"),
        };

        if (!sys->upscaler.kernel) {
            msg_Err(vd, "Tried specifying a custom upscaler with no kernel!");
            sys->params.upscaler = NULL;
        }
    };

    preset = var_InheritInteger(vd, "pl-downscaler-preset");
    sys->params.downscaler = scale_config[preset];
    if (preset == SCALE_CUSTOM) {
        sys->params.downscaler = &sys->downscaler;
        sys->downscaler = (struct pl_filter_config) {
            .kernel = filter_fun[var_InheritInteger(vd, "pl-downscaler-kernel")],
            .window = filter_fun[var_InheritInteger(vd, "pl-downscaler-window")],
            .clamp  = var_InheritFloat(vd, "pl-downscaler-clamp"),
            .blur   = var_InheritFloat(vd, "pl-downscaler-blur"),
            .taper  = var_InheritFloat(vd, "pl-downscaler-taper"),
            .polar  = var_InheritBool(vd, "pl-downscaler-polar"),
        };

        if (!sys->downscaler.kernel) {
            msg_Err(vd, "Tried specifying a custom downscaler with no kernel!");
            sys->params.downscaler = NULL;
        }
    };

    sys->dither_depth = var_InheritInteger(vd, "pl-dither-depth");
    sys->target = (struct pl_color_space) {
        .primaries = var_InheritInteger(vd, "pl-target-prim"),
        .transfer = var_InheritInteger(vd, "pl-target-trc"),
    };

    sys->lut_mode = var_InheritInteger(vd, "pl-lut-mode");
    char *lut_file = var_InheritString(vd, "pl-lut-file");
    LoadCustomLUT(sys, lut_file);
    free(lut_file);
    if (sys->lut) {
        sys->params.lut = sys->lut;
        switch (sys->lut_mode) {
        case LUT_NATIVE:     sys->params.lut_type = PL_LUT_NATIVE; break;
        case LUT_LINEAR:     sys->params.lut_type = PL_LUT_NORMALIZED; break;
        case LUT_CONVERSION: sys->params.lut_type = PL_LUT_CONVERSION; break;
        default:
            sys->params.lut = NULL; // the others need to be applied elsewhere
            break;
        }
    }

    char *shader_file = var_InheritString(vd, "pl-user-shader");
    LoadUserShader(sys, shader_file);
    free(shader_file);
    if (sys->hook) {
        sys->params.hooks = &sys->hook;
        sys->params.num_hooks = 1;
    } else {
        sys->params.num_hooks = 0;
    }
}
