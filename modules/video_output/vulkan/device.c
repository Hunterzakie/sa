/*****************************************************************************
 * device.c: Vulkan device creation and management
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

#include <vlc_common.h>

#include "device.h"

static int
LoadInstanceFunctions(struct vlc_vk_device *dev)
{
#define VK_LOAD(name) \
    dev->name = (PFN_##name)dev->inst.get_proc_address(dev->inst.instance, #name); \
    if (!dev->name) { \
        VK_LOG_ERR(dev, "Failed to load " #name); \
        return VLC_EGENERIC; \
    }
    VK_INSTANCE_FUNCS(VK_LOAD)
#undef VK_LOAD
    return VLC_SUCCESS;
}

static int
LoadDeviceFunctions(struct vlc_vk_device *dev)
{
    PFN_vkGetDeviceProcAddr gdpa = dev->vkGetDeviceProcAddr;
    VkDevice device = dev->device;
#define VK_LOAD(name) \
    dev->name = (PFN_##name)gdpa(device, #name); \
    if (!dev->name) { \
        VK_LOG_ERR(dev, "Failed to load " #name); \
        return VLC_EGENERIC; \
    }
    VK_DEVICE_FUNCS(VK_LOAD)
#undef VK_LOAD
    return VLC_SUCCESS;
}

void
vlc_vk_device_Init(struct vlc_vk_device *dev, struct vlc_logger *logger)
{
    dev->logger = logger;
    dev->inst.instance = VK_NULL_HANDLE;
    dev->inst.get_proc_address = NULL;
    dev->debug_messenger = VK_NULL_HANDLE;
    dev->gpu = VK_NULL_HANDLE;
    dev->device = VK_NULL_HANDLE;
    dev->queue_family_index = 0;

#define VK_PFN_INIT(name) dev->name = NULL;
    VK_INSTANCE_FUNCS(VK_PFN_INIT)
    VK_DEVICE_FUNCS(VK_PFN_INIT)
#undef VK_PFN_INIT

    dev->vkSetHdrMetadataEXT = NULL;
    dev->vkGetSemaphoreFd = NULL;
    dev->vkSetObjectName = NULL;
    dev->owns_instance = false;
    dev->owns_device = false;
}

#define VK_DEV dev
#include "vk_funcs.h"

int vlc_vk_device_SetExternal(struct vlc_vk_device *dev,
                              VkPhysicalDevice physical_device,
                              VkDevice device,
                              uint32_t queue_family)
{
    /* inst must already be populated by caller */
    assert(dev->inst.instance != VK_NULL_HANDLE);
    assert(dev->inst.get_proc_address != NULL);

    dev->gpu = physical_device;
    dev->queue_family_index = queue_family;

    /* Owned by the caller */
    dev->owns_instance = false;
    dev->owns_device = false;

    if (LoadInstanceFunctions(dev) != VLC_SUCCESS)
        return VLC_EGENERIC;

    dev->device = device;
    if (LoadDeviceFunctions(dev) != VLC_SUCCESS)
        return VLC_EGENERIC;

    vkGetPhysicalDeviceMemoryProperties(dev->gpu, &dev->gpu_mem_props);

    dev->vkSetHdrMetadataEXT = (PFN_vkSetHdrMetadataEXT)
        vkGetDeviceProcAddr(dev->device, "vkSetHdrMetadataEXT");

    /* Load semaphore export function (for read fence support) */
    dev->vkGetSemaphoreFd = (PFN_vkGetSemaphoreFdKHR)
        vkGetDeviceProcAddr(dev->device, "vkGetSemaphoreFdKHR");

    dev->vkSetObjectName = (PFN_vkSetDebugUtilsObjectNameEXT)
        dev->inst.get_proc_address(dev->inst.instance, "vkSetDebugUtilsObjectNameEXT");

    VK_LOG_INFO(dev, "External Vulkan device initialized successfully");
    return VLC_SUCCESS;
}

void vlc_vk_device_Destroy(struct vlc_vk_device *dev)
{
    /* Only external handles are supported for now, the caller frees them */
    assert(!dev->owns_instance && !dev->owns_device);
    dev->device = VK_NULL_HANDLE;
    dev->inst.instance = VK_NULL_HANDLE;
}
