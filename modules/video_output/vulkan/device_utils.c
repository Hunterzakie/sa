/*****************************************************************************
 * device_utils.c: Vulkan device utility helpers
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

#include "device.h"
#include "device_utils.h"

#define VK_DEV dev
#include "vk_funcs.h"

bool
vlc_vk_device_MapMemoryTypeToIndex(const struct vlc_vk_device *dev, uint32_t type_bits,
                                   VkFlags requirements, uint32_t *type_index)
{
    const VkPhysicalDeviceMemoryProperties *mem_props = &dev->gpu_mem_props;

    /* Pass 0: prefer DEVICE_LOCAL (e.g. resizable BAR for host-visible requests)
     * Pass 1: accept any matching type */
    for (unsigned pass = 0; pass < 2; pass++)
    {
        uint32_t bits = type_bits;
        for (uint32_t i = 0; i < mem_props->memoryTypeCount; i++)
        {
            if ((bits & 1) == 1)
            {
                VkFlags flags = mem_props->memoryTypes[i].propertyFlags;
                if ((flags & requirements) == requirements &&
                    (pass > 0 || (flags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)))
                {
                    *type_index = i;
                    return true;
                }
            }
            bits >>= 1;
        }
    }
    return false;
}

void
vlc_vk_device_SetObjectName(const struct vlc_vk_device *dev,
                            VkObjectType type, uint64_t handle,
                            const char *name)
{
    if (dev->vkSetObjectName == NULL)
        return;
    VkDebugUtilsObjectNameInfoEXT info = {
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };
    dev->vkSetObjectName(dev->device, &info);
}
