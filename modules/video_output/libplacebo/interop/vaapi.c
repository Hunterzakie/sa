/*****************************************************************************
 * interop_vaapi.c: Vulkan VAAPI opaque converter
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

#include <assert.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/stat.h>

#include <va/va_drmcommon.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include "interop.h"
#include "device_utils.h"
#include "../../hw/vaapi/vlc_vaapi.h"

#define VK_DEV dev
#include "vk_funcs.h"

/* DMA-BUF sync file support for explicit synchronization (kernel 5.20+) */
#ifndef DMA_BUF_IOCTL_EXPORT_SYNC_FILE
struct dma_buf_export_sync_file {
    uint32_t flags;
    int32_t fd;
};
#define DMA_BUF_SYNC_READ  (1 << 0)
#define DMA_BUF_SYNC_WRITE (2 << 0)
#define DMA_BUF_BASE 'b'
#define DMA_BUF_IOCTL_EXPORT_SYNC_FILE _IOWR(DMA_BUF_BASE, 2, struct dma_buf_export_sync_file)
#endif

struct vaapi_image_slot
{
    VkImage image;
    VkDeviceMemory memory;
    VkImageView image_view;

    VASurfaceID surface_id;

    dev_t backing_dev;
    ino_t backing_ino;

    uint64_t last_used;
};

/* Match typical decoder DPB size (H.264/HEVC high-profile: 16-32 surfaces).
 * Each slot is ~120 bytes of metadata; actual GPU memory is owned by decoder.
 * This eliminates cache thrashing and repeated vkAllocateMemory() calls. */
#define POOL_SIZE 64

struct priv
{
    VADisplay va_dpy;

    struct vaapi_image_slot image_cache[POOL_SIZE];
    uint64_t frame_counter;

    PFN_vkImportSemaphoreFdKHR vkImportSemaphoreFd;

    unsigned width;
    unsigned height;
    vlc_fourcc_t chroma;
};

static void
DestroySlotResources(struct vaapi_image_slot *slot, const struct vlc_vk_device *dev)
{
    VkDevice device = dev->device;

    if (slot->image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, slot->image_view, NULL);
        slot->image_view = VK_NULL_HANDLE;
    }

    if (slot->image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, slot->image, NULL);
        slot->image = VK_NULL_HANDLE;
    }

    if (slot->memory != VK_NULL_HANDLE)
    {
        /* This will also close the imported DMA-BUF FD */
        vkFreeMemory(device, slot->memory, NULL);
        slot->memory = VK_NULL_HANDLE;
    }

    slot->surface_id = VA_INVALID_ID;
}


static int
ImportImages(const struct vlc_vk_interop *interop,
            VkImageView *image_view,
            VkSemaphore acquire_sem,
            uint64_t *acquire_sem_value,
            picture_t *pic,
            uint32_t image_index)
{
    (void)image_index;
    *acquire_sem_value = 0;

    const struct vlc_vk_device *dev = interop->dev;
    struct priv *priv = interop->priv;

#if !VA_CHECK_VERSION(1, 1, 0)
    VK_LOG_ERR(interop->dev, "VAAPI 1.1+ required for DRM PRIME export");
    return VLC_EGENERIC;
#else

    struct vaapi_image_slot *slot = NULL, *free_slot = NULL;
    priv->frame_counter++;

    VASurfaceID surface = vlc_vaapi_PicGetSurface(pic);

    /* Always export surface to get fresh DMA-BUF FD.
     * This allows us to verify that cached resources still point to the same
     * backing memory (VA-API may reallocate backing memory after seek/flush). */
    VADRMPRIMESurfaceDescriptor surface_desc;
    VAStatus va_status = vaExportSurfaceHandle(priv->va_dpy, surface,
                                     VA_SURFACE_ATTRIB_MEM_TYPE_DRM_PRIME_2,
                                     VA_EXPORT_SURFACE_READ_ONLY | VA_EXPORT_SURFACE_SEPARATE_LAYERS,
                                     &surface_desc);
    if (va_status != VA_STATUS_SUCCESS)
    {
        VK_LOG_ERR(interop->dev, "vaExportSurfaceHandle failed: 0x%x %s", va_status, vaErrorStr(va_status));
        return VLC_EGENERIC;
    }

    /* Expecting single memory object for NV12/P010/P012 (2-plane formats) */
    if (surface_desc.num_objects != 1)
    {
        VK_LOG_ERR(interop->dev, "Expected 1 memory object, got %u", surface_desc.num_objects);
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    struct stat st;
    if (fstat(surface_desc.objects[0].fd, &st) != 0)
    {
        VK_LOG_ERR(interop->dev, "fstat failed on DMA-BUF FD: %m");
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    /* Must be done BEFORE cache lookup so both cache hits and misses get proper sync */
    {
        int sync_fd = -1;
        struct dma_buf_export_sync_file sync_file_export =
        {
            .flags = DMA_BUF_SYNC_READ,
            .fd = -1,
        };

        if (ioctl(surface_desc.objects[0].fd, DMA_BUF_IOCTL_EXPORT_SYNC_FILE,
                  &sync_file_export) == 0)
        {
            sync_fd = sync_file_export.fd;
        }
        else
        {
            VK_LOG_DBG(interop->dev, "DMA_BUF_IOCTL_EXPORT_SYNC_FILE not supported, CPU sync fallback");
            va_status = vaSyncSurface(priv->va_dpy, surface);
            if (va_status != VA_STATUS_SUCCESS)
                VK_LOG_WARN(interop->dev, "vaSyncSurface failed: 0x%x %s", va_status, vaErrorStr(va_status));
            /* sync_fd stays -1: immediately signaled per SYNC_FD spec */
        }

        if (acquire_sem != VK_NULL_HANDLE && priv->vkImportSemaphoreFd != NULL)
        {
            /* fd=-1 is valid for SYNC_FD import: means immediately signaled per spec */
            VkImportSemaphoreFdInfoKHR import_info =
            {
                .sType = VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR,
                .semaphore = acquire_sem,
                .flags = VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
                .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
                .fd = sync_fd,
            };

            VkResult vk_res = priv->vkImportSemaphoreFd(dev->device, &import_info);
            if (vk_res != VK_SUCCESS)
            {
                VK_LOG_WARN(interop->dev, "Failed to import sync FD into semaphore: %d", vk_res);
                if (sync_fd >= 0)
                    close(sync_fd);
            }
            /* else: FD consumed by import */
        }
        else
        {
            if (sync_fd >= 0)
                close(sync_fd);
        }
    }

    for (unsigned i = 0; i < POOL_SIZE; i++)
    {
        if (free_slot == NULL && priv->image_cache[i].surface_id == VA_INVALID_ID)
        {
            free_slot = &priv->image_cache[i];
        }
        else if (priv->image_cache[i].surface_id == surface && priv->image_cache[i].image != VK_NULL_HANDLE)
        {
            if (priv->image_cache[i].backing_dev == st.st_dev &&
                priv->image_cache[i].backing_ino == st.st_ino)
            {
                slot = &priv->image_cache[i];
                slot->last_used = priv->frame_counter;

                for (unsigned j = 0; j < surface_desc.num_objects; ++j)
                    close(surface_desc.objects[j].fd);

                *image_view = slot->image_view;
                return VLC_SUCCESS;
            }
            else
            {
                /* VA-API may reallocate backing memory after seek/flush,
                 * so force cache flush by leaving slot = NULL */
                VK_LOG_DBG(interop->dev, "Backing memory changed");
                free_slot = NULL;
                break;
            }
        }
    }

    if (free_slot == NULL)
    {
        VK_LOG_WARN(interop->dev, "Flushing entire cache - waiting for GPU idle");
        vkDeviceWaitIdle(dev->device);

        for (unsigned i = 0; i < POOL_SIZE; i++)
        {
            if (priv->image_cache[i].image != VK_NULL_HANDLE)
            {
                DestroySlotResources(&priv->image_cache[i], dev);
            }
        }

        slot = &priv->image_cache[0];
    }
    else
    {
        slot = free_slot;
    }

    VkSubresourceLayout plane_layouts[2];
    for (unsigned i = 0; i < surface_desc.num_layers; i++)
    {
        plane_layouts[i].offset = surface_desc.layers[i].offset[0];
        plane_layouts[i].size = 0;  /* Unknown, driver will determine */
        plane_layouts[i].rowPitch = surface_desc.layers[i].pitch[0];
        plane_layouts[i].arrayPitch = 0;
        plane_layouts[i].depthPitch = 0;
    }

    VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info =
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
        .pNext = NULL,
        .drmFormatModifier = surface_desc.objects[0].drm_format_modifier,
        .drmFormatModifierPlaneCount = surface_desc.num_layers,
        .pPlaneLayouts = plane_layouts,
    };

    VkExternalMemoryImageCreateInfo ext_mem_img_info =
    {
        .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
        .pNext = &drm_mod_info,
        .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
    };

    VkImageCreateInfo img_info =
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .pNext = &ext_mem_img_info,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = interop->format_probe.vk_format,
        .extent = { priv->width, priv->height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };

    VkResult res = vkCreateImage(dev->device, &img_info, NULL, &slot->image);
    if (res != VK_SUCCESS)
    {
        VK_LOG_ERR(interop->dev, "Failed to create Vulkan image from DMA-BUF: %d", res);
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    unsigned slot_idx = (unsigned)(slot - priv->image_cache);
    char name[64];
    snprintf(name, sizeof(name), "VLC VAAPI Image %u", slot_idx);
    vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_IMAGE,
                                (uint64_t)slot->image, name);

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(dev->device, slot->image, &mem_req);

    VkMemoryDedicatedAllocateInfo dedicated_info =
    {
        .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
        .image = slot->image,
    };

    VkImportMemoryFdInfoKHR import_fd_info =
    {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
        .pNext = &dedicated_info,
        .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
        .fd = surface_desc.objects[0].fd,
    };

    VkMemoryAllocateInfo alloc_info =
    {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_fd_info,
        .allocationSize = mem_req.size,
    };

    if (!vlc_vk_device_MapMemoryTypeToIndex(interop->dev, mem_req.memoryTypeBits,
                             VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                             &alloc_info.memoryTypeIndex))
    {
        VK_LOG_ERR(interop->dev, "Failed to find suitable memory type");
        vkDestroyImage(dev->device, slot->image, NULL);
        slot->image = VK_NULL_HANDLE;
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    res = vkAllocateMemory(dev->device, &alloc_info, NULL, &slot->memory);
    if (res != VK_SUCCESS)
    {
        VK_LOG_ERR(interop->dev, "Failed to allocate Vulkan memory: %d", res);
        vkDestroyImage(dev->device, slot->image, NULL);
        slot->image = VK_NULL_HANDLE;
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    snprintf(name, sizeof(name), "VLC VAAPI Memory %u", slot_idx);
    vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_DEVICE_MEMORY,
                                (uint64_t)slot->memory, name);

    res = vkBindImageMemory(dev->device, slot->image, slot->memory, 0);
    if (res != VK_SUCCESS)
    {
        VK_LOG_ERR(interop->dev, "Failed to bind Vulkan image memory: %d", res);
        vkFreeMemory(dev->device, slot->memory, NULL);
        slot->memory = VK_NULL_HANDLE;
        vkDestroyImage(dev->device, slot->image, NULL);
        slot->image = VK_NULL_HANDLE;
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    VkSamplerYcbcrConversionInfo ycbcr_info =
    {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO,
        .conversion = interop->ycbcr_conversion,
    };

    VkImageViewCreateInfo view_info =
    {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .pNext = &ycbcr_info,
        .image = slot->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = interop->format_probe.vk_format,
        .subresourceRange =
        {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = 0,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    res = vkCreateImageView(dev->device, &view_info, NULL, &slot->image_view);
    if (res != VK_SUCCESS)
    {
        VK_LOG_ERR(interop->dev, "Failed to create Vulkan image view: %d", res);
        vkFreeMemory(dev->device, slot->memory, NULL);
        slot->memory = VK_NULL_HANDLE;
        vkDestroyImage(dev->device, slot->image, NULL);
        slot->image = VK_NULL_HANDLE;
        for (unsigned i = 0; i < surface_desc.num_objects; ++i)
            close(surface_desc.objects[i].fd);
        return VLC_EGENERIC;
    }

    snprintf(name, sizeof(name), "VLC VAAPI View %u", slot_idx);
    vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_IMAGE_VIEW,
                                (uint64_t)slot->image_view, name);

    slot->surface_id = surface;
    slot->backing_dev = st.st_dev;
    slot->backing_ino = st.st_ino;
    slot->last_used = priv->frame_counter;

    *image_view = slot->image_view;

    return VLC_SUCCESS;
#endif
}

static void
Close(struct vlc_vk_interop *interop)
{
    const struct vlc_vk_device *dev = interop->dev;
    struct priv *priv = interop->priv;

    for (unsigned i = 0; i < POOL_SIZE; i++)
    {
        DestroySlotResources(&priv->image_cache[i], dev);
    }

    free(priv);
}

static void
GetChromaVaFourcc(vlc_fourcc_t opaque_chroma, int *va_fourcc,
                  vlc_fourcc_t *sw_chroma)
{
    switch (opaque_chroma)
    {
        case VLC_CODEC_VAAPI_420:
            *va_fourcc = VA_FOURCC_NV12;
            *sw_chroma = VLC_CODEC_NV12;
            break;
        case VLC_CODEC_VAAPI_420_10BPP:
            *va_fourcc = VA_FOURCC_P010;
            *sw_chroma = VLC_CODEC_P010;
            break;
        case VLC_CODEC_VAAPI_420_12BPP:
            *va_fourcc = VA_FOURCC_P012;
            *sw_chroma = VLC_CODEC_P012;
            break;
        default:
            vlc_assert_unreachable();
    }
}

static int
ProbeFormat(struct vlc_vk_interop_format_probe *format_probe)
{
    (void)format_probe->dev;

    int va_fourcc;
    vlc_fourcc_t sw_chroma;
    GetChromaVaFourcc(format_probe->fmt_in.i_chroma, &va_fourcc, &sw_chroma);

    format_probe->fmt_out = format_probe->fmt_in;

    /* VAAPI always uses DRM format modifiers for importing DMA-BUF */
    format_probe->tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT;

    format_probe->vk_format = vlc_vk_GetVulkanFormat(sw_chroma);
    if (format_probe->vk_format == VK_FORMAT_UNDEFINED)
    {
        msg_Err(format_probe->obj, "No VkFormat mapping for VAAPI chroma %.4s", (char*)&sw_chroma);
        return VLC_EGENERIC;
    }

    msg_Dbg(format_probe->obj, "VAAPI format: %.4s → %.4s (VkFormat 0x%x), tiling=DRM_MODIFIER",
            (char*)&format_probe->fmt_in.i_chroma, (char*)&sw_chroma, format_probe->vk_format);

    return VLC_SUCCESS;
}

static int
Load(struct vlc_vk_interop *interop)
{
    const struct vlc_vk_device *dev = interop->dev;
    vlc_decoder_device *dec_device = vlc_video_context_HoldDevice(interop->format_probe.vctx);
    if (dec_device == NULL)
    {
        VK_LOG_ERR(interop->dev, "Failed to get decoder device");
        return VLC_EGENERIC;
    }

    struct priv *priv = malloc(sizeof(*priv));
    if (priv == NULL)
    {
        vlc_decoder_device_Release(dec_device);
        return VLC_ENOMEM;
    }

    priv->va_dpy = dec_device->opaque;
    if (priv->va_dpy == NULL)
    {
        VK_LOG_ERR(interop->dev, "Invalid VA display");
        vlc_decoder_device_Release(dec_device);
        free(priv);
        return VLC_EGENERIC;
    }

    priv->frame_counter = 0;
    priv->vkImportSemaphoreFd = NULL;
    priv->width = interop->format_probe.fmt_in.i_visible_width;
    priv->height = interop->format_probe.fmt_in.i_visible_height;
    priv->chroma = interop->format_probe.fmt_in.i_chroma;

    for (unsigned i = 0; i < POOL_SIZE; i++)
    {
        priv->image_cache[i].image = VK_NULL_HANDLE;
        priv->image_cache[i].memory = VK_NULL_HANDLE;
        priv->image_cache[i].image_view = VK_NULL_HANDLE;
        priv->image_cache[i].surface_id = VA_INVALID_ID;
        priv->image_cache[i].backing_dev = 0;
        priv->image_cache[i].backing_ino = 0;
        priv->image_cache[i].last_used = 0;
    }

    priv->vkImportSemaphoreFd = (PFN_vkImportSemaphoreFdKHR)
        vkGetDeviceProcAddr(dev->device, "vkImportSemaphoreFdKHR");
    if (!priv->vkImportSemaphoreFd)
    {
        VK_LOG_WARN(interop->dev, "vkImportSemaphoreFdKHR not available, "
                                  "will use implicit sync (may cause corruption)");
    }

    interop->format_probe.fmt_out.space = interop->format_probe.fmt_in.space;
    interop->format_probe.fmt_out.primaries = interop->format_probe.fmt_in.primaries;
    interop->format_probe.fmt_out.transfer = interop->format_probe.fmt_in.transfer;
    interop->format_probe.fmt_out.color_range = interop->format_probe.fmt_in.color_range;
    interop->format_probe.fmt_out.chroma_location = interop->format_probe.fmt_in.chroma_location;

    if (interop->format_probe.fmt_in.transfer == TRANSFER_FUNC_SMPTE_ST2084 ||
        interop->format_probe.fmt_in.transfer == TRANSFER_FUNC_HLG)
    {
        memcpy(&interop->format_probe.fmt_out.mastering, &interop->format_probe.fmt_in.mastering,
               sizeof(interop->format_probe.fmt_in.mastering));
        memcpy(&interop->format_probe.fmt_out.lighting, &interop->format_probe.fmt_in.lighting,
               sizeof(interop->format_probe.fmt_in.lighting));
        VK_LOG_DBG(interop->dev, "HDR metadata copied: MaxCLL=%u, MaxFALL=%u",
                   interop->format_probe.fmt_in.lighting.MaxCLL, interop->format_probe.fmt_in.lighting.MaxFALL);
    }

    /* Single multi-planar image: YCbCr sampler handles plane separation */
    interop->plane_count = 1;
    interop->plane_info[0] = (struct vlc_vk_plane_info)
    {
        .w = { 1, 1 },
        .h = { 1, 1 },
        .format = interop->format_probe.vk_format,
        .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
        .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
    };

    interop->priv = priv;

    vlc_decoder_device_Release(dec_device);

    VK_LOG_INFO(interop->dev, "VAAPI Vulkan interop loaded for %ux%u %.4s",
                priv->width, priv->height, (char*)&priv->chroma);

    return VLC_SUCCESS;
}

static int
Open(struct vlc_vk_interop_probe *probe)
{
#if !VA_CHECK_VERSION(1, 1, 0)
    msg_Err(probe->obj, "VAAPI 1.1+ required for DRM PRIME export");
    return VLC_EGENERIC;
#endif

    if (probe->vctx == NULL
     || vlc_video_context_GetType(probe->vctx) != VLC_VIDEO_CONTEXT_VAAPI)
        return VLC_EGENERIC;

    if (!vlc_vaapi_IsChromaOpaque(probe->fmt_in.i_chroma))
    {
        msg_Dbg(probe->obj, "Not a VAAPI opaque chroma: %.4s",
                (char*)&probe->fmt_in.i_chroma);
        return VLC_EGENERIC;
    }

    probe->required_instance_extensions = NULL;
    probe->required_instance_extension_count = 0;

    static const char *required_device_extensions[] =
    {
        VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME,
        VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,

        VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
        VK_EXT_EXTERNAL_MEMORY_DMA_BUF_EXTENSION_NAME,
        VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
    };

    probe->required_device_extensions = required_device_extensions;
    probe->required_device_extension_count = ARRAY_SIZE(required_device_extensions);

    static const struct vlc_vk_interop_ops ops =
    {
        .probe_format = ProbeFormat,
        .load = Load,
        .import_images = ImportImages,
        .close = Close,
    };
    probe->ops = &ops;
    probe->acquire_sem_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

    msg_Dbg(probe->obj, "VAAPI interop probe: %u instance exts, %u device exts",
            probe->required_instance_extension_count,
            probe->required_device_extension_count);

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_description("VA-API Vulkan surface converter")
    set_capability("vkinterop", 2)
    set_callback(Open)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("vaapi")
vlc_module_end()
