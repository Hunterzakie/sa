/*****************************************************************************
 * interop.h: Vulkan hardware decoder interop interface
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

#ifndef VLC_VULKAN_INTEROP_H
#define VLC_VULKAN_INTEROP_H

#include <vlc_common.h>
#include <vlc_picture.h>
#include <vlc_codec.h>

#include "device.h"

/* Used through a pointer here, because pl_plane is defined in libplacebo and we don't want to include it here
* It is used in the upload_planes callback in the libplacebo interop implementation */
struct pl_plane;

/**
 * Vulkan hardware decoder interop interface
 *
 * This interface allows hardware-specific modules (NVDEC, VAAPI, etc.) to
 * import decoded GPU frames into Vulkan images for zero-copy rendering.
 *
 * Similar to the OpenGL glinterop system (see vlc_opengl_interop.h), but
 * adapted for Vulkan's explicit memory management and synchronization model.
 */

struct vlc_vk_interop;
struct vlc_vk_interop_probe;
struct vlc_vk_interop_format_probe;

/**
 * One timeline semaphore is shared by every slot and every plane. The core
 * raises it once per plane when it hands the images over, and the backend
 * raises it once more when its writes are done
 */
struct vlc_vk_interop_sync
{
    VkSemaphore sem;
    uint64_t wait_value;
    uint64_t signal_value;
};

/**
 * Plane configuration
 *
 * Similar to vlc_gl_tex_cfg in OpenGL interop, but for Vulkan images.
 */
struct vlc_vk_plane_info
{
    /* Plane width ratio (e.g., {1,1} for Y, {1,2} for UV) */
    vlc_rational_t w;
    /* Plane height ratio (e.g., {1,1} for Y, {1,2} for UV) */
    vlc_rational_t h;
    /* VK_FORMAT_R8_UNORM, VK_FORMAT_R8G8_UNORM, etc. */
    VkFormat format;
    /* VK_IMAGE_USAGE_SAMPLED_BIT | ... */
    VkImageUsageFlags usage;
    /* VK_IMAGE_TILING_OPTIMAL or LINEAR */
    VkImageTiling tiling;
};

/**
 * Phase 1: Probe configuration (extensions only)
 *
 * Passed to Open() callback. Module fills in required extensions and callbacks.
 * NO FORMAT INFORMATION - that comes from probe_format().
 */
struct vlc_vk_interop_probe
{
    /* Inputs (set by caller before open) */

    /* For logging */
    vlc_object_t *obj;
    /* Decoder device context */
    struct vlc_video_context *vctx;
    /* Opaque input format */
    video_format_t fmt_in;

    /* Outputs (set by module during open) */

    const char **required_instance_extensions;
    uint32_t required_instance_extension_count;
    const char **required_device_extensions;
    uint32_t required_device_extension_count;
    const struct vlc_vk_interop_ops *ops;
    /* 0 = none */
    VkExternalSemaphoreHandleTypeFlagBits acquire_sem_handle_type;
};

/**
 * Phase 2: Format probe configuration
 *
 * Passed to probe_format() callback. Module fills in negotiated format.
 * Contains only the subset of Phase 1 fields needed for format negotiation.
 */
struct vlc_vk_interop_format_probe
{
    /* Inputs (set by caller before probe_format) */

    /* For logging */
    vlc_object_t *obj;
    /* Decoder device context */
    struct vlc_video_context *vctx;
    /* Opaque input format */
    video_format_t fmt_in;
    const struct vlc_vk_device *dev;
    /* Renderer GPU handle, same type as pl_gpu. Used in upload_planes in the libplacebo interop implementation */
    const struct pl_gpu_t *gpu;

    /* Outputs (set by module during probe_format) */

    /* Requested picture format from VLC core
     * - Same as fmt_in: module handles conversion internally
     * - Different: VLC core inserts converter filter
     * See probe_format() docs for details */
    video_format_t fmt_out;
    /* Actual Vulkan texture format module will create
     * MUST be set explicitly (not VK_FORMAT_UNDEFINED)
     * May differ from fmt_out for internal conversions */
    VkFormat vk_format;
    /* VK_IMAGE_TILING_OPTIMAL or LINEAR */
    VkImageTiling tiling;
    /* Software chroma the imported planes hold */
    vlc_fourcc_t sw_chroma;
};

/**
 * Vulkan interop object (embeds Phase 2)
 *
 * Only exists after vlc_vk_interop_Load(). All fields are valid.
 * Fields are populated in phases:
 * - Phase 1 (probe): Extension requirements (stored separately), ops
 * - Phase 2 (probe_format): obj, vctx, fmt_in, fmt_out, vk_format, tiling, sw_chroma
 * - Phase 3 (load): dev, sampler, plane_info, priv
 */
struct vlc_vk_interop
{
    /* Phase 1+2+3 common fields */
    const struct vlc_vk_interop_ops *ops;

    /* Phase 2 data */
    struct vlc_vk_interop_format_probe format_probe;

    /* Phase 3 specific fields (valid after load) */

    /* NULL if the backend is not Vulkan */
    const struct vlc_vk_device *dev;
    VkSamplerYcbcrConversion ycbcr_conversion;
    VkSampler sampler;
    uint32_t image_count;
    struct vlc_vk_plane_info plane_info[PICTURE_PLANE_MAX];
    unsigned plane_count;
    /* Module private data (allocated during load) */
    void *priv;
    bool loaded;
};

/**
 * Interop operations callbacks
 */
struct vlc_vk_interop_ops
{
    /**
     * Phase 2: Negotiate format with device capabilities
     *
     * Called AFTER Vulkan device creation. Module can now query device format support
     * using vkGetPhysicalDeviceFormatProperties() and choose the best format.
     *
     * \param format_probe format probe structure with phys_dev input and format outputs
     * \return VLC_SUCCESS if format found, VLC_EGENERIC if no compatible format
     */
    int (*probe_format)(struct vlc_vk_interop_format_probe *format_probe);

    /**
     * Phase 3: Complete initialization with device handles
     *
     * Called AFTER format negotiation. Module should initialize hardware-specific
     * resources (CUDA context, VA-API display, etc.) and configure plane_info in interop.
     *
     * \param interop the Vulkan interop (Vulkan context is now valid)
     * \return VLC_SUCCESS or VLC error
     */
    int (*load)(struct vlc_vk_interop *interop);

    /**
     * Import picture into Vulkan image
     *
     * Called for each frame to import decoded GPU memory into Vulkan images.
     * The implementation should use external memory APIs (e.g., CUDA-Vulkan
     * interop for NVDEC, DMA-BUF import for VAAPI) to enable zero-copy access.
     *
     * \param interop the Vulkan interop
     * \param[out] out_images arrayfilled with one single-plane VkImage per plane, in plane order
     * \param sync in/out timeline handoff for this frame
     * \param pic the picture to import (contains hardware-specific context)
     * \param image_index the render slot index (enables 1:1 slot mapping with render images)
     * \return VLC_SUCCESS or VLC error
     */
    int (*import_images)(const struct vlc_vk_interop *interop,
                        VkImage *out_images,
                        struct vlc_vk_interop_sync *sync,
                        picture_t *pic,
                        uint32_t image_index);

    /**
     * Upload a host-memory picture into module-owned textures
     *
     * The module owns the destination textures and keeps them
     * across frames. It must fill in every plane it reports, including the
     * component mapping and any chroma siting shift.
     *
     * \param interop the Vulkan interop
     * \param[out] out_planes planes to sample, filled by the module
     * \param[out] out_plane_count number of planes filled
     * \param pic the picture to upload
     * \return VLC_SUCCESS or VLC error
     */
    int (*upload_planes)(const struct vlc_vk_interop *interop,
                         struct pl_plane *out_planes,
                         unsigned *out_plane_count,
                         picture_t *pic);

    /**
     * One-time per-slot setup after Load (optional)
     *
     * Called once per image_index after Load completes. Allows the interop
     * to import the core-owned acquire semaphore into its own domain
     * (e.g., CUDA import of Vulkan semaphore).
     *
     * \param interop the Vulkan interop
     * \param sem core-owned semaphore for this image_index
     * \param image_index the slot index
     * \return VLC_SUCCESS or VLC error
     */
    int (*setup_acquire_sem)(struct vlc_vk_interop *interop,
                             VkSemaphore sem, uint32_t image_index);

    /**
     * Get sampler for rendering (optional)
     *
     * For interops that create their own YCbCr sampler (e.g., Android with
     * external format), this returns the sampler to use for rendering.
     * If NULL or returns VK_NULL_HANDLE, display.c creates a standard sampler.
     *
     * May return different sampler on format changes (e.g., adaptive streaming).
     * Caller should compare with previous value and recreate pipeline if changed.
     *
     * \param interop the Vulkan interop
     * \return VkSampler to use, or VK_NULL_HANDLE for default
     */
    VkSampler (*get_sampler)(const struct vlc_vk_interop *interop);

    /**
     * Set read fence for current frame (optional)
     *
     * Called after GPU commands are submitted to signal when buffer can be reused.
     * Notifies the decoder when the GPU is done reading the buffer.
     *
     * \param interop the Vulkan interop
     * \param pic the picture being rendered
     * \param fd sync file descriptor that signals when GPU read completes
     *           (ownership transferred to interop, interop must close on error)
     */
    void (*set_read_fence)(const struct vlc_vk_interop *interop,
                           picture_t *pic, int fd);

    /**
     * Cleanup callback
     *
     * Called when the interop is being destroyed. Should release all
     * hardware-specific resources.
     *
     * \param interop the Vulkan interop
     */
    void (*close)(struct vlc_vk_interop *interop);
};

/**
 * Activation function for Vulkan interop modules (Phase 1)
 *
 * Called when loading a vkinterop module. The module should check if it can
 * handle the given decoder context and input format, then fill in the probe
 * structure with required extensions and callbacks.
 *
 * \param probe probe structure to fill with extensions and callbacks
 * \return VLC_SUCCESS if compatible, VLC_EGENERIC otherwise
 */
typedef int (*vlc_vk_interop_open)(struct vlc_vk_interop_probe *probe);



/**
 * Map VLC chroma fourcc to Vulkan format
 *
 * \param sw_chroma VLC chroma fourcc (e.g., VLC_CODEC_NV12, VLC_CODEC_P010)
 * \return VkFormat, or VK_FORMAT_UNDEFINED if no mapping exists
 */
static inline VkFormat vlc_vk_GetVulkanFormat(vlc_fourcc_t sw_chroma)
{
    switch (sw_chroma)
    {
        /* 4:2:0 3-plane */
        case VLC_CODEC_I420:
        case VLC_CODEC_YV12:
            return VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM;
        case VLC_CODEC_I420_12L:
        case VLC_CODEC_I420_12B:
            return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_420_UNORM_3PACK16;
        case VLC_CODEC_I420_16L:
        case VLC_CODEC_I420_16B:
            return VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM;

        /* 4:2:0 2-plane */
        case VLC_CODEC_NV12:
            return VK_FORMAT_G8_B8R8_2PLANE_420_UNORM;
        case VLC_CODEC_P010:
            return VK_FORMAT_G10X6_B10X6R10X6_2PLANE_420_UNORM_3PACK16;
        case VLC_CODEC_P012:
            return VK_FORMAT_G12X4_B12X4R12X4_2PLANE_420_UNORM_3PACK16;
        case VLC_CODEC_P016:
            return VK_FORMAT_G16_B16R16_2PLANE_420_UNORM;

        /* 4:2:2 3-plane */
        case VLC_CODEC_I422:
            return VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM;
        case VLC_CODEC_I422_10L:
        case VLC_CODEC_I422_10B:
            return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_422_UNORM_3PACK16;
        case VLC_CODEC_I422_12L:
        case VLC_CODEC_I422_12B:
            return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_422_UNORM_3PACK16;
        case VLC_CODEC_I422_16L:
        case VLC_CODEC_I422_16B:
            return VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM;

        /* 4:2:2 2-plane */
        case VLC_CODEC_NV16:
            return VK_FORMAT_G8_B8R8_2PLANE_422_UNORM;

        /* 4:4:4 3-plane */
        case VLC_CODEC_I444:
            return VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM;
        case VLC_CODEC_I444_10L:
        case VLC_CODEC_I444_10B:
            return VK_FORMAT_G10X6_B10X6_R10X6_3PLANE_444_UNORM_3PACK16;
        case VLC_CODEC_I444_12L:
        case VLC_CODEC_I444_12B:
            return VK_FORMAT_G12X4_B12X4_R12X4_3PLANE_444_UNORM_3PACK16;
        case VLC_CODEC_I444_16L:
        case VLC_CODEC_I444_16B:
            return VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM;

        /* 4:4:4 2-plane */
        case VLC_CODEC_NV24:
            return VK_FORMAT_G8_B8R8_2PLANE_444_UNORM;

        default:
            return VK_FORMAT_UNDEFINED;
    }
}
#endif /* VLC_VULKAN_INTEROP_H */
