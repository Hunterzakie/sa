/*****************************************************************************
 * nvdec.c: Vulkan NVDEC opaque converter
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
#ifdef _WIN32
# include <windows.h>
# define VK_USE_PLATFORM_WIN32_KHR
#else
# include <unistd.h>
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>

#include "hw/nvdec/nvdec_fmt.h"
#include "hw/nvdec/nvdec_priv.h"
#include "../../vulkan/interop.h"
#include "../../vulkan/device_utils.h"

#define VK_DEV dev
#include "../../vulkan/vk_funcs.h"

#ifdef _WIN32
typedef HANDLE                            vk_ext_handle_t;
typedef PFN_vkGetMemoryWin32HandleKHR     PFN_vkGetMemoryHandleKHR;
typedef PFN_vkGetSemaphoreWin32HandleKHR  PFN_vkGetSemaphoreHandleKHR;

# define VK_EXT_HANDLE_INVALID    INVALID_HANDLE_VALUE
# define VK_EXT_MEM_HANDLE_TYPE   VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32_BIT
# define VK_EXT_SEM_HANDLE_TYPE   VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_WIN32_BIT
# define VK_EXT_MEM_EXT_NAME      VK_KHR_EXTERNAL_MEMORY_WIN32_EXTENSION_NAME
# define VK_EXT_SEM_EXT_NAME      VK_KHR_EXTERNAL_SEMAPHORE_WIN32_EXTENSION_NAME
# define VK_GET_MEM_PROC_NAME     "vkGetMemoryWin32HandleKHR"
# define VK_GET_SEM_PROC_NAME     "vkGetSemaphoreWin32HandleKHR"
# define CUDA_EXT_MEM_HANDLE_TYPE CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_WIN32
# define CUDA_EXT_SEM_HANDLE_TYPE CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_WIN32
# define CLOSE_EXT_HANDLE(h)      CloseHandle(h)
# define EXT_HANDLE_IS_VALID(h)   ((h) != INVALID_HANDLE_VALUE)
#else
typedef int                          vk_ext_handle_t;
typedef PFN_vkGetMemoryFdKHR        PFN_vkGetMemoryHandleKHR;
typedef PFN_vkGetSemaphoreFdKHR     PFN_vkGetSemaphoreHandleKHR;

# define VK_EXT_HANDLE_INVALID    (-1)
# define VK_EXT_MEM_HANDLE_TYPE   VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT
# define VK_EXT_SEM_HANDLE_TYPE   VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT
# define VK_EXT_MEM_EXT_NAME      VK_KHR_EXTERNAL_MEMORY_FD_EXTENSION_NAME
# define VK_EXT_SEM_EXT_NAME      VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME
# define VK_GET_MEM_PROC_NAME     "vkGetMemoryFdKHR"
# define VK_GET_SEM_PROC_NAME     "vkGetSemaphoreFdKHR"
# define CUDA_EXT_MEM_HANDLE_TYPE CU_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD
# define CUDA_EXT_SEM_HANDLE_TYPE CU_EXTERNAL_SEMAPHORE_HANDLE_TYPE_TIMELINE_SEMAPHORE_FD
# define CLOSE_EXT_HANDLE(h)      close(h)
# define EXT_HANDLE_IS_VALID(h)   ((h) >= 0)
#endif

/* File-specific VK_CHECK macro - assumes 'interop' variable is available */
#define VK_CHECK(x, msg) \
    if (vlc_vk_check(interop->dev, x, msg, __FILE__, __LINE__)) goto error

#define CALL_CUDA(vd, priv, func, ...) \
    CudaCheckErr(VLC_OBJECT(vd), (priv)->cudaFunctions, \
                 (priv)->cudaFunctions->func(__VA_ARGS__), #func)

struct vk_nvdec_plane {
    VkImage image;
    VkDeviceMemory memory;
    vk_ext_handle_t memory_handle;
    CUexternalMemory cuda_ext_mem;
    CUmipmappedArray cuda_mipmap;
    CUarray cuda_array;             /* not owned, do NOT destroy */
    VkExtent2D copy_extent;
    unsigned bytes_per_pixel;
};

struct nvdec_image_slot {
    struct vk_nvdec_plane planes[3];  /* 4:2:0: [Y, UV], 4:4:4: [Y, U, V] */
};

struct priv {
    vlc_decoder_device *decoder_device;
    CUcontext cuCtx;
    CudaFunctions *cudaFunctions;

    CUexternalSemaphore cuda_sem;

    struct nvdec_image_slot *slots;
    uint32_t slot_count;

    unsigned width;
    unsigned height;
    vlc_fourcc_t chroma;
    const vlc_chroma_description_t *desc;

    PFN_vkGetMemoryHandleKHR vkGetMemoryHandle;
    PFN_vkGetSemaphoreHandleKHR vkGetSemaphoreHandle;
};

static VkFormat
PlaneVkFormat(unsigned pixel_size, bool interleaved)
{
    if (pixel_size == 1)
        return interleaved ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM;
    return interleaved ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16_UNORM;
}

static int
ImportImages(const struct vlc_vk_interop *interop,
            VkImage *out_images,
            struct vlc_vk_interop_sync *sync,
            picture_t *pic,
            uint32_t image_index)
{
    struct priv *priv = interop->priv;
    vlc_object_t *o = interop->format_probe.obj;
    pic_context_nvdec_t *nvdec_ctx = NVDEC_PICCONTEXT_FROM_PICCTX(pic->context);

    struct nvdec_image_slot *slot = &priv->slots[image_index];

    sync->signal_value = 0;

    int result;

    result = CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
    if (result != VLC_SUCCESS) {
        msg_Err(o, "Failed to push CUDA context");
        return result;
    }

    /* Block the copy until the core has released every plane of this slot */
    if (priv->cuda_sem)
    {
        CUDA_EXTERNAL_SEMAPHORE_WAIT_PARAMS wait_params = {
            .params.fence.value = sync->wait_value,
        };
        result = CALL_CUDA(o, priv, cuWaitExternalSemaphoresAsync,
                           &priv->cuda_sem, &wait_params, 1, 0);
        if (result != VLC_SUCCESS)
        {
            msg_Err(o, "Failed to wait on the timeline for slot %u", image_index);
            CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
            return result;
        }
    }

    size_t srcY = 0;
    for (unsigned i = 0; i < priv->desc->plane_count; i++) {
        struct vk_nvdec_plane *plane = &slot->planes[i];

        CUDA_MEMCPY2D copy = {
            .srcMemoryType = CU_MEMORYTYPE_DEVICE,
            .srcDevice = nvdec_ctx->devicePtr,
            .srcPitch = nvdec_ctx->bufferPitch,
            .srcY = srcY,
            .dstMemoryType = CU_MEMORYTYPE_ARRAY,
            .dstArray = plane->cuda_array,
            .WidthInBytes = plane->copy_extent.width * plane->bytes_per_pixel,
            .Height = plane->copy_extent.height,
        };

        result = CALL_CUDA(o, priv, cuMemcpy2DAsync, &copy, 0);
        if (result != VLC_SUCCESS) {
            msg_Err(o, "Failed to copy NVDEC plane %u to Vulkan", i);
            CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
            return result;
        }

        srcY += nvdec_ctx->bufferHeight;
    }

    /* Vulkan waits on this semaphore before sampling */
    if (priv->cuda_sem)
    {
        CUDA_EXTERNAL_SEMAPHORE_SIGNAL_PARAMS signal_params = {
            .params.fence.value = sync->wait_value + 1,
        };
        result = CALL_CUDA(o, priv, cuSignalExternalSemaphoresAsync,
                           &priv->cuda_sem, &signal_params, 1, 0);
        if (result != VLC_SUCCESS)
        {
            msg_Err(o, "Failed to signal external semaphore for slot %u", image_index);
            CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
            return result;
        }
        sync->signal_value = sync->wait_value + 1;
    }

    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);

    for (unsigned i = 0; i < priv->desc->plane_count; i++)
        out_images[i] = slot->planes[i].image;

    return VLC_SUCCESS;
}

static int
SetupAcquireSem(struct vlc_vk_interop *interop,
                VkSemaphore sem, uint32_t image_index)
{
    const struct vlc_vk_device *dev = interop->dev;
    struct priv *priv = interop->priv;
    vlc_object_t *o = interop->format_probe.obj;
    int result;

    if (priv->cuda_sem != NULL)
        return VLC_SUCCESS;

    result = CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
    if (result != VLC_SUCCESS)
        return result;

#ifdef _WIN32
    VkSemaphoreGetWin32HandleInfoKHR get_handle_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_WIN32_HANDLE_INFO_KHR,
#else
    VkSemaphoreGetFdInfoKHR get_handle_info = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
#endif
        .semaphore = sem,
        .handleType = VK_EXT_SEM_HANDLE_TYPE,
    };

    vk_ext_handle_t handle = VK_EXT_HANDLE_INVALID;
    VkResult res = priv->vkGetSemaphoreHandle(dev->device, &get_handle_info, &handle);
    if (res != VK_SUCCESS)
    {
        msg_Err(o, "vkGetSemaphoreHandle failed for slot %u: %d", image_index, res);
        CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
        return VLC_EGENERIC;
    }

    CUDA_EXTERNAL_SEMAPHORE_HANDLE_DESC sem_desc = {
        .type = CUDA_EXT_SEM_HANDLE_TYPE,
#ifdef _WIN32
        .handle.win32.handle = handle,
#else
        .handle.fd = handle,
#endif
    };

    result = CALL_CUDA(o, priv, cuImportExternalSemaphore, &priv->cuda_sem, &sem_desc);
    if (result != VLC_SUCCESS)
    {
        msg_Err(o, "cuImportExternalSemaphore failed for slot %u", image_index);
        CLOSE_EXT_HANDLE(handle);
        CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
        return VLC_EGENERIC;
    }

#ifdef _WIN32
    /* On Windows, CUDA duplicates the HANDLE and the caller must close its copy */
    CLOSE_EXT_HANDLE(handle);
#endif

    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);

    return VLC_SUCCESS;
}

static void
Close(struct vlc_vk_interop *interop)
{
    const struct vlc_vk_device *dev = interop->dev;
    struct priv *priv = interop->priv;
    vlc_object_t *o = interop->format_probe.obj;

    if (priv == NULL)
        return;

    if (dev != NULL && dev->device != VK_NULL_HANDLE) {
        VkDevice device = dev->device;

        vkDeviceWaitIdle(device);

        if (priv->cuda_sem && priv->cuCtx) {
            CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
            CALL_CUDA(o, priv, cuDestroyExternalSemaphore, priv->cuda_sem);
            CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
            priv->cuda_sem = NULL;
        }

        for (uint32_t s = 0; s < priv->slot_count; s++) {
            struct nvdec_image_slot *slot = &priv->slots[s];

            for (unsigned i = 0; i < 3; i++) {
                /* cuda_array is a reference into cuda_mipmap — do NOT destroy it */
                if (slot->planes[i].cuda_mipmap && priv->cuCtx) {
                    CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
                    CALL_CUDA(o, priv, cuMipmappedArrayDestroy, slot->planes[i].cuda_mipmap);
                    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
                }
                if (slot->planes[i].cuda_ext_mem && priv->cuCtx) {
                    CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
                    CALL_CUDA(o, priv, cuDestroyExternalMemory, slot->planes[i].cuda_ext_mem);
                    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);
                }
                if (slot->planes[i].memory != VK_NULL_HANDLE)
                    vkFreeMemory(device, slot->planes[i].memory, NULL);
                if (EXT_HANDLE_IS_VALID(slot->planes[i].memory_handle))
                    CLOSE_EXT_HANDLE(slot->planes[i].memory_handle);
                if (slot->planes[i].image != VK_NULL_HANDLE)
                    vkDestroyImage(device, slot->planes[i].image, NULL);
            }
        }
    }

    free(priv->slots);

    if (priv->decoder_device)
        vlc_decoder_device_Release(priv->decoder_device);

    free(priv);

    interop->priv = NULL;
}

static int
ProbeFormat(struct vlc_vk_interop_format_probe *format_probe)
{
    (void)format_probe->dev;

    format_probe->fmt_out = format_probe->fmt_in;
    vlc_fourcc_t chroma = NVDECToVlcChroma(format_probe->fmt_in.i_chroma);

    format_probe->tiling = VK_IMAGE_TILING_OPTIMAL;

    format_probe->vk_format = vlc_vk_GetVulkanFormat(chroma);
    if (format_probe->vk_format == VK_FORMAT_UNDEFINED)
    {
        msg_Err(format_probe->obj, "No VkFormat mapping for NVDEC chroma %.4s",
                (char*)&chroma);
        return VLC_EGENERIC;
    }

    format_probe->sw_chroma = chroma;

    return VLC_SUCCESS;
}

static int
Load(struct vlc_vk_interop *interop)
{
    const struct vlc_vk_device *dev = interop->dev;
    vlc_object_t *o = interop->format_probe.obj;

    vlc_decoder_device *device = vlc_video_context_HoldDevice(interop->format_probe.vctx);
    if (device == NULL) {
        msg_Err(o, "Failed to get decoder device");
        return VLC_EGENERIC;
    }

    decoder_device_nvdec_t *devsys = GetNVDECOpaqueDevice(device);
    if (!devsys) {
        msg_Err(o, "Failed to get NVDEC device");
        vlc_decoder_device_Release(device);
        return VLC_EGENERIC;
    }

    struct priv *priv = malloc(sizeof(*priv));
    if (priv == NULL) {
        vlc_decoder_device_Release(device);
        return VLC_ENOMEM;
    }

    priv->decoder_device = device;
    priv->cuCtx = devsys->cuCtx;
    priv->cudaFunctions = devsys->cudaFunctions;
    priv->cuda_sem = NULL;
    priv->slots = NULL;
    priv->slot_count = 0;
    priv->width = interop->format_probe.fmt_in.i_visible_width;
    priv->height = interop->format_probe.fmt_in.i_visible_height;
    priv->chroma = interop->format_probe.fmt_in.i_chroma;
    priv->desc = vlc_fourcc_GetChromaDescription(
        NVDECToVlcChroma(priv->chroma));

    priv->vkGetMemoryHandle = (PFN_vkGetMemoryHandleKHR)
        vkGetDeviceProcAddr(dev->device, VK_GET_MEM_PROC_NAME);
    priv->vkGetSemaphoreHandle = (PFN_vkGetSemaphoreHandleKHR)
        vkGetDeviceProcAddr(dev->device, VK_GET_SEM_PROC_NAME);

    if (!priv->vkGetMemoryHandle || !priv->vkGetSemaphoreHandle) {
        msg_Err(o, "Required Vulkan extension functions not available");
        free(priv);
        vlc_decoder_device_Release(device);
        return VLC_EGENERIC;
    }

    if (!priv->cudaFunctions->cuExternalMemoryGetMappedMipmappedArray
     || !priv->cudaFunctions->cuMipmappedArrayGetLevel
     || !priv->cudaFunctions->cuMipmappedArrayDestroy)
    {
        msg_Err(o, "Required CUDA mipmapped array functions not available");
        free(priv);
        vlc_decoder_device_Release(device);
        return VLC_EGENERIC;
    }

    if (!priv->cudaFunctions->cuImportExternalSemaphore
     || !priv->cudaFunctions->cuDestroyExternalSemaphore
     || !priv->cudaFunctions->cuWaitExternalSemaphoresAsync
     || !priv->cudaFunctions->cuSignalExternalSemaphoresAsync)
    {
        msg_Err(o, "Required CUDA external semaphore functions not available");
        free(priv);
        vlc_decoder_device_Release(device);
        return VLC_EGENERIC;
    }

    uint32_t image_count = interop->image_count;
    priv->slots = vlc_alloc(image_count, sizeof(*priv->slots));
    if (priv->slots == NULL) {
        free(priv);
        vlc_decoder_device_Release(device);
        return VLC_ENOMEM;
    }
    priv->slot_count = image_count;

    for (uint32_t s = 0; s < image_count; s++) {
        struct nvdec_image_slot *slot = &priv->slots[s];

        for (unsigned i = 0; i < 3; i++) {
            slot->planes[i].image = VK_NULL_HANDLE;
            slot->planes[i].memory = VK_NULL_HANDLE;
            slot->planes[i].memory_handle = VK_EXT_HANDLE_INVALID;
            slot->planes[i].cuda_ext_mem = NULL;
            slot->planes[i].cuda_mipmap = NULL;
            slot->planes[i].cuda_array = NULL;
            slot->planes[i].copy_extent.width = 0;
            slot->planes[i].copy_extent.height = 0;
            slot->planes[i].bytes_per_pixel = 0;
        }
    }

    int result;

    result = CALL_CUDA(o, priv, cuCtxPushCurrent, priv->cuCtx);
    if (result != VLC_SUCCESS) {
        msg_Err(o, "Failed to push CUDA context");
        goto error_nopush;
    }

    const vlc_chroma_description_t *desc = priv->desc;
    unsigned num_planes = desc->plane_count;

    /* CUDA array format must match the per-plane pixel layout */
    CUarray_format cuda_formats[3];
    unsigned cuda_channels[3];
    unsigned plane_bpp[3];
    for (unsigned i = 0; i < num_planes; i++)
    {
        bool interleaved = (num_planes == 2 && i > 0);
        plane_bpp[i] = interleaved ? desc->pixel_size * 2 : desc->pixel_size;
        if (desc->pixel_size == 1) {
            cuda_formats[i] = CU_AD_FORMAT_UNSIGNED_INT8;
            cuda_channels[i] = interleaved ? 2 : 1;
        } else {
            cuda_formats[i] = CU_AD_FORMAT_UNSIGNED_INT16;
            cuda_channels[i] = interleaved ? 2 : 1;
        }
    }

    for (uint32_t s = 0; s < image_count; s++) {
        struct nvdec_image_slot *slot = &priv->slots[s];
        VkResult res;

        /* Chroma subsampling determines per-plane dimensions */
        for (unsigned i = 0; i < num_planes; i++) {
            struct vk_nvdec_plane *plane = &slot->planes[i];
            bool interleaved = (num_planes == 2 && i > 0);
            plane->copy_extent.width = priv->width * desc->p[i].w.num / desc->p[i].w.den;
            plane->copy_extent.height = priv->height * desc->p[i].h.num / desc->p[i].h.den;
            if (interleaved)
                plane->copy_extent.width /= 2;
            plane->bytes_per_pixel = plane_bpp[i];

            /* Describe each plane to the renderer once */
            if (s == 0) {
                interop->plane_info[i] = (struct vlc_vk_plane_info) {
                    .w = { plane->copy_extent.width, priv->width },
                    .h = { plane->copy_extent.height, priv->height },
                    .format = PlaneVkFormat(desc->pixel_size, interleaved),
                    .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                    .tiling = VK_IMAGE_TILING_OPTIMAL,
                };
            }
        }

        /* Make each plane a single-plane Vulkan image with its own dedicated memory */
        for (unsigned i = 0; i < num_planes; i++) {
            struct vk_nvdec_plane *plane = &slot->planes[i];
            bool interleaved = (num_planes == 2 && i > 0);

            VkExternalMemoryImageCreateInfo ext_mem_info = {
                .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
                .handleTypes = VK_EXT_MEM_HANDLE_TYPE,
            };
            VkImageCreateInfo img_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                .pNext = &ext_mem_info,
                .imageType = VK_IMAGE_TYPE_2D,
                .format = PlaneVkFormat(desc->pixel_size, interleaved),
                .extent = { plane->copy_extent.width, plane->copy_extent.height, 1 },
                .mipLevels = 1,
                .arrayLayers = 1,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .tiling = VK_IMAGE_TILING_OPTIMAL,
                .usage = VK_IMAGE_USAGE_SAMPLED_BIT,
                .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            };

            res = vkCreateImage(dev->device, &img_info, NULL, &plane->image);
            VK_CHECK(res, "vkCreateImage");

            char name[64];
            snprintf(name, sizeof(name), "VLC NVDEC Plane Image %u:%u", s, i);
            vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_IMAGE,
                                        (uint64_t)plane->image, name);

            VkImageMemoryRequirementsInfo2 req_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                .image = plane->image,
            };
            VkMemoryRequirements2 mem_req2 = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            };
            vkGetImageMemoryRequirements2(dev->device, &req_info, &mem_req2);
            VkMemoryRequirements mem_req = mem_req2.memoryRequirements;

            VkMemoryDedicatedAllocateInfo dedicated_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
                .image = plane->image,
            };
            VkExportMemoryAllocateInfo export_info = {
                .sType = VK_STRUCTURE_TYPE_EXPORT_MEMORY_ALLOCATE_INFO,
                .pNext = &dedicated_info,
                .handleTypes = VK_EXT_MEM_HANDLE_TYPE,
            };
            VkMemoryAllocateInfo alloc_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
                .pNext = &export_info,
                .allocationSize = mem_req.size,
            };

            if (!vlc_vk_device_MapMemoryTypeToIndex(interop->dev, mem_req.memoryTypeBits,
                                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                                     &alloc_info.memoryTypeIndex)) {
                msg_Err(o, "Failed to find suitable memory type for slot %u plane %u", s, i);
                goto error;
            }

            res = vkAllocateMemory(dev->device, &alloc_info, NULL, &plane->memory);
            VK_CHECK(res, "vkAllocateMemory");

            snprintf(name, sizeof(name), "VLC NVDEC Plane Memory %u:%u", s, i);
            vlc_vk_device_SetObjectName(interop->dev, VK_OBJECT_TYPE_DEVICE_MEMORY,
                                        (uint64_t)plane->memory, name);

            VkBindImageMemoryInfo bind_info = {
                .sType = VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO,
                .image = plane->image,
                .memory = plane->memory,
                .memoryOffset = 0,
            };
            res = vkBindImageMemory2(dev->device, 1, &bind_info);
            VK_CHECK(res, "vkBindImageMemory2");
        }

        for (unsigned i = 0; i < num_planes; i++) {
            struct vk_nvdec_plane *plane = &slot->planes[i];

            /* CUDA import needs the exact allocation size */
            VkImageMemoryRequirementsInfo2 req_info = {
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
                .image = plane->image,
            };
            VkMemoryRequirements2 mem_req2 = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
            };
            vkGetImageMemoryRequirements2(dev->device, &req_info, &mem_req2);

#ifdef _WIN32
            VkMemoryGetWin32HandleInfoKHR get_handle_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_GET_WIN32_HANDLE_INFO_KHR,
#else
            VkMemoryGetFdInfoKHR get_handle_info = {
                .sType = VK_STRUCTURE_TYPE_MEMORY_GET_FD_INFO_KHR,
#endif
                .memory = plane->memory,
                .handleType = VK_EXT_MEM_HANDLE_TYPE,
            };

            res = priv->vkGetMemoryHandle(dev->device, &get_handle_info, &plane->memory_handle);
            VK_CHECK(res, "vkGetMemoryHandle");

            CUDA_EXTERNAL_MEMORY_HANDLE_DESC ext_mem_desc = {
                .type = CUDA_EXT_MEM_HANDLE_TYPE,
#ifdef _WIN32
                .handle.win32.handle = plane->memory_handle,
#else
                .handle.fd = plane->memory_handle,
#endif
                .size = mem_req2.memoryRequirements.size,
            };

            result = CALL_CUDA(o, priv, cuImportExternalMemory,
                              &plane->cuda_ext_mem, &ext_mem_desc);
            if (result != VLC_SUCCESS) {
                msg_Err(o, "Failed to import external memory into CUDA for slot %u plane %u", s, i);
                goto error;
            }

            /* cuImportExternalMemory takes ownership of the fd and closes it when the external memory is destroyed,
            * but on Windows, the HANDLE is duplicated and must be closed by the caller. */
#ifdef _WIN32
            CLOSE_EXT_HANDLE(plane->memory_handle);
#endif
            plane->memory_handle = VK_EXT_HANDLE_INVALID;

            /* Mipmapped array lets CUDA respect the driver's OPTIMAL tiling */
            CUDA_EXTERNAL_MEMORY_MIPMAPPED_ARRAY_DESC mipmap_desc = {
                .offset = 0,
                .arrayDesc = {
                    .Width = plane->copy_extent.width,
                    .Height = plane->copy_extent.height,
                    .Depth = 0,  /* 2D array */
                    .Format = cuda_formats[i],
                    .NumChannels = cuda_channels[i],
                    .Flags = 0,
                },
                .numLevels = 1,
            };

            result = CALL_CUDA(o, priv, cuExternalMemoryGetMappedMipmappedArray,
                              &plane->cuda_mipmap, plane->cuda_ext_mem, &mipmap_desc);
            if (result != VLC_SUCCESS) {
                msg_Err(o, "Failed to map CUDA mipmapped array for slot %u plane %u", s, i);
                goto error;
            }

            /* Level-0 reference — owned by cuda_mipmap, not freed separately */
            result = CALL_CUDA(o, priv, cuMipmappedArrayGetLevel,
                              &plane->cuda_array, plane->cuda_mipmap, 0);
            if (result != VLC_SUCCESS) {
                msg_Err(o, "Failed to get mipmap level 0 for slot %u plane %u", s, i);
                goto error;
            }
        }

    }

    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);

    /* Preserve HDR/colorspace metadata from the decoder */
    interop->format_probe.fmt_out.space = interop->format_probe.fmt_in.space;
    interop->format_probe.fmt_out.primaries = interop->format_probe.fmt_in.primaries;
    interop->format_probe.fmt_out.transfer = interop->format_probe.fmt_in.transfer;
    interop->format_probe.fmt_out.color_range = interop->format_probe.fmt_in.color_range;
    interop->format_probe.fmt_out.chroma_location = interop->format_probe.fmt_in.chroma_location;

    // set the plane count for the interop to match the NVDEC format
    assert(priv->desc->plane_count <= PICTURE_PLANE_MAX);
    interop->plane_count = priv->desc->plane_count;

    interop->priv = priv;

    msg_Info(o, "NVDEC Vulkan interop loaded for %ux%u %.4s (%u slots)",
             priv->width, priv->height, (char*)&priv->chroma, priv->slot_count);

    return VLC_SUCCESS;

error:
    CALL_CUDA(o, priv, cuCtxPopCurrent, NULL);

error_nopush:
    /* Close() handles partial-init cleanup */
    interop->priv = priv;
    Close(interop);
    return VLC_EGENERIC;
}

static int
Open(struct vlc_vk_interop_probe *probe)
{
    if (!is_nvdec_opaque(probe->fmt_in.i_chroma))
        return VLC_EGENERIC;

    if (probe->vctx == NULL
     || vlc_video_context_GetType(probe->vctx) != VLC_VIDEO_CONTEXT_NVDEC)
        return VLC_EGENERIC;

    probe->required_instance_extensions = NULL;
    probe->required_instance_extension_count = 0;

    static const char *required_device_extensions[] = {
        VK_EXT_MEM_EXT_NAME,
        VK_EXT_SEM_EXT_NAME,
    };

    probe->required_device_extensions = required_device_extensions;
    probe->required_device_extension_count = ARRAY_SIZE(required_device_extensions);

    static const struct vlc_vk_interop_ops ops = {
        .probe_format = ProbeFormat,
        .load = Load,
        .import_images = ImportImages,
        .setup_acquire_sem = SetupAcquireSem,
        .close = Close,
    };
    probe->ops = &ops;
    probe->acquire_sem_handle_type = VK_EXT_SEM_HANDLE_TYPE;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_description("NVDEC Vulkan surface converter")
    set_capability("vkinterop", 2)
    set_callback(Open)
    set_subcategory(SUBCAT_VIDEO_VOUT)
    add_shortcut("nvdec")
vlc_module_end()
