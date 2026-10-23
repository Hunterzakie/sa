/*****************************************************************************
 * vaapi.c: Vulkan VAAPI opaque converter
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
#include <sys/stat.h>

#include <va/va_drmcommon.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include "hw/vaapi/vlc_vaapi.h"
#include "../vk_interop.h"
#include "../../vulkan/device_utils.h"

#define VK_DEV dev
#include "../../vulkan/vk_funcs.h"

struct vaapi_image_slot
{
    VkImage images[PICTURE_PLANE_MAX];
    VkDeviceMemory memories[PICTURE_PLANE_MAX];
    unsigned plane_count;

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

    unsigned width;
    unsigned height;
    vlc_fourcc_t chroma;
};

static VkFormat
PlaneVkFormat(unsigned pixel_size, bool interleaved)
{
    if (pixel_size == 1)
        return interleaved ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM;
    return interleaved ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16_UNORM;
}

static void
DestroySlotResources(struct vaapi_image_slot *slot, const struct vlc_vk_device *dev)
{
    VkDevice device = dev->device;

    for (unsigned i = 0; i < slot->plane_count; i++)
    {
        if (slot->images[i] != VK_NULL_HANDLE)
        {
            vkDestroyImage(device, slot->images[i], NULL);
            slot->images[i] = VK_NULL_HANDLE;
        }

        if (slot->memories[i] != VK_NULL_HANDLE)
        {
            /* This will also close the imported DMA-BUF FD */
            vkFreeMemory(device, slot->memories[i], NULL);
            slot->memories[i] = VK_NULL_HANDLE;
        }
    }

    slot->plane_count = 0;
    slot->surface_id = VA_INVALID_ID;
}


static int
ImportImages(const struct vlc_vk_interop *interop,
            VkImage *out_images,
            struct vlc_vk_interop_sync *sync,
            picture_t *pic,
            uint32_t image_index)
{
    (void)image_index;
    sync->signal_value = 0;

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

    /* Plane count doesn't always match the number of layers */
    if (surface_desc.num_layers != interop->plane_count)
    {
        VK_LOG_ERR(interop->dev, "Expected %u layers, got %u",
                   interop->plane_count, surface_desc.num_layers);
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

    /* TODO: I will just make the CPU wait for the GPU to finish rendering the surface for now */
    va_status = vaSyncSurface(priv->va_dpy, surface);
    if (va_status != VA_STATUS_SUCCESS)
        VK_LOG_WARN(interop->dev, "vaSyncSurface failed: 0x%x %s", va_status, vaErrorStr(va_status));

    for (unsigned i = 0; i < POOL_SIZE; i++)
    {
        if (free_slot == NULL && priv->image_cache[i].surface_id == VA_INVALID_ID)
        {
            free_slot = &priv->image_cache[i];
        }
        else if (priv->image_cache[i].surface_id == surface && priv->image_cache[i].images[0] != VK_NULL_HANDLE)
        {
            if (priv->image_cache[i].backing_dev == st.st_dev &&
                priv->image_cache[i].backing_ino == st.st_ino)
            {
                slot = &priv->image_cache[i];
                slot->last_used = priv->frame_counter;

                for (unsigned j = 0; j < surface_desc.num_objects; ++j)
                    close(surface_desc.objects[j].fd);

                for (unsigned j = 0; j < slot->plane_count; j++)
                    out_images[j] = slot->images[j];
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
            if (priv->image_cache[i].images[0] != VK_NULL_HANDLE)
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

    slot->plane_count = surface_desc.num_layers;

    for (unsigned i = 0; i < surface_desc.num_layers; i++)
    {
        const uint32_t obj_idx = surface_desc.layers[i].object_index[0];
        const struct vlc_vk_plane_info *info = &interop->plane_info[i];

        VkSubresourceLayout plane_layout =
        {
            .offset = surface_desc.layers[i].offset[0],
            .size = 0,  /* Unknown, driver will determine */
            .rowPitch = surface_desc.layers[i].pitch[0],
            .arrayPitch = 0,
            .depthPitch = 0,
        };

        VkImageDrmFormatModifierExplicitCreateInfoEXT drm_mod_info =
        {
            .sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
            .pNext = NULL,
            .drmFormatModifier = surface_desc.objects[obj_idx].drm_format_modifier,
            .drmFormatModifierPlaneCount = 1,
            .pPlaneLayouts = &plane_layout,
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
            .format = info->format,
            .extent = {
                priv->width * info->w.num / info->w.den,
                priv->height * info->h.num / info->h.den,
                1,
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
            .usage = info->usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };

        VkResult res = vkCreateImage(dev->device, &img_info, NULL, &slot->images[i]);
        if (res != VK_SUCCESS)
        {
            VK_LOG_ERR(interop->dev, "Failed to create Vulkan image from DMA-BUF: %d", res);
            goto error;
        }

        unsigned slot_idx = (unsigned)(slot - priv->image_cache);
        char name[64];
        snprintf(name, sizeof(name), "VLC VAAPI Image %u plane %u", slot_idx, i);
        vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_IMAGE,
                                    (uint64_t)slot->images[i], name);

        VkMemoryRequirements mem_req;
        vkGetImageMemoryRequirements(dev->device, slot->images[i], &mem_req);

        VkMemoryDedicatedAllocateInfo dedicated_info =
        {
            .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
            .image = slot->images[i],
        };

        /* The driver may have already allocated the memory for the image */
        int import_fd = dup(surface_desc.objects[obj_idx].fd);
        if (import_fd < 0)
        {
            VK_LOG_ERR(interop->dev, "Failed to duplicate DMA-BUF FD: %m");
            goto error;
        }

        VkImportMemoryFdInfoKHR import_fd_info =
        {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
            .pNext = &dedicated_info,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
            .fd = import_fd,
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
            close(import_fd);
            goto error;
        }

        res = vkAllocateMemory(dev->device, &alloc_info, NULL, &slot->memories[i]);
        if (res != VK_SUCCESS)
        {
            VK_LOG_ERR(interop->dev, "Failed to allocate Vulkan memory: %d", res);
            close(import_fd);
            goto error;
        }

        snprintf(name, sizeof(name), "VLC VAAPI Memory %u plane %u", slot_idx, i);
        vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_DEVICE_MEMORY,
                                    (uint64_t)slot->memories[i], name);

        res = vkBindImageMemory(dev->device, slot->images[i], slot->memories[i], 0);
        if (res != VK_SUCCESS)
        {
            VK_LOG_ERR(interop->dev, "Failed to bind Vulkan image memory: %d", res);
            goto error;
        }
    }

    for (unsigned i = 0; i < surface_desc.num_objects; ++i)
        close(surface_desc.objects[i].fd);

    slot->surface_id = surface;
    slot->backing_dev = st.st_dev;
    slot->backing_ino = st.st_ino;
    slot->last_used = priv->frame_counter;

    for (unsigned i = 0; i < slot->plane_count; i++)
        out_images[i] = slot->images[i];

    return VLC_SUCCESS;

error:
    DestroySlotResources(slot, dev);
    for (unsigned i = 0; i < surface_desc.num_objects; ++i)
        close(surface_desc.objects[i].fd);
    return VLC_EGENERIC;
#endif
}

static int
SetupAcquireSem(struct vlc_vk_interop *interop, VkSemaphore sem, uint32_t image_index)
{
    (void)interop; (void)sem; (void)image_index;
    return VLC_SUCCESS;
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

    format_probe->sw_chroma = sw_chroma;

    msg_Dbg(format_probe->obj, "VAAPI format: %.4s → %.4s (VkFormat 0x%x), tiling=DRM_MODIFIER",
            (char*)&format_probe->fmt_in.i_chroma, (char*)&sw_chroma, format_probe->vk_format);

    return VLC_SUCCESS;
}

static int
Load(struct vlc_vk_interop *interop)
{
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
    priv->width = interop->format_probe.fmt_in.i_visible_width;
    priv->height = interop->format_probe.fmt_in.i_visible_height;
    priv->chroma = interop->format_probe.fmt_in.i_chroma;

    for (unsigned i = 0; i < POOL_SIZE; i++)
    {
        for (unsigned j = 0; j < PICTURE_PLANE_MAX; j++)
        {
            priv->image_cache[i].images[j] = VK_NULL_HANDLE;
            priv->image_cache[i].memories[j] = VK_NULL_HANDLE;
        }
        priv->image_cache[i].plane_count = 0;
        priv->image_cache[i].surface_id = VA_INVALID_ID;
        priv->image_cache[i].backing_dev = 0;
        priv->image_cache[i].backing_ino = 0;
        priv->image_cache[i].last_used = 0;
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

    const vlc_chroma_description_t *desc =
        vlc_fourcc_GetChromaDescription(interop->format_probe.sw_chroma);
    if (desc == NULL || desc->plane_count == 0)
    {
        VK_LOG_ERR(interop->dev, "No chroma description for %.4s",
                   (char*)&interop->format_probe.sw_chroma);
        vlc_decoder_device_Release(dec_device);
        free(priv);
        return VLC_EGENERIC;
    }

    assert(desc->plane_count <= PICTURE_PLANE_MAX);
    interop->plane_count = desc->plane_count;
    for (unsigned i = 0; i < desc->plane_count; i++)
    {
        bool interleaved = (desc->plane_count == 2 && i > 0);
        unsigned plane_w = priv->width * desc->p[i].w.num / desc->p[i].w.den;
        unsigned plane_h = priv->height * desc->p[i].h.num / desc->p[i].h.den;
        if (interleaved)
            plane_w /= 2;

        interop->plane_info[i] = (struct vlc_vk_plane_info)
        {
            .w = { plane_w, priv->width },
            .h = { plane_h, priv->height },
            .format = PlaneVkFormat(desc->pixel_size, interleaved),
            .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
            .tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
        };
    }

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
        .setup_acquire_sem = SetupAcquireSem,
        .close = Close,
    };
    probe->ops = &ops;
    probe->acquire_sem_handle_type = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT;

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
