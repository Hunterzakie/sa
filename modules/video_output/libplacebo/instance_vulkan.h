/*****************************************************************************
 * instance_vulkan.h: Vulkan handles of a libplacebo instance
 *****************************************************************************
 * Copyright (C) 2026 VideoLAN
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

#ifndef VLC_LIBPLACEBO_INSTANCE_VULKAN_H
#define VLC_LIBPLACEBO_INSTANCE_VULKAN_H

#include <vulkan/vulkan_core.h>

#include "instance.h"

struct vlc_placebo_vk_handles
{
    VkInstance               instance;
    VkPhysicalDevice         phys_device;
    VkDevice                 device;
    uint32_t                 graphics_queue_family;
    PFN_vkGetInstanceProcAddr get_instance_proc_addr;

    // libplacebo submits on this queue from its own threads and wrap any
    // vkQueueSubmit done outside libplacebo in lock_queue()/unlock_queue()
    VkQueue                  graphics_queue;
    uint32_t                 graphics_queue_index;
    void (*lock_queue)(struct vlc_placebo_t *, uint32_t qf, uint32_t qidx);
    void (*unlock_queue)(struct vlc_placebo_t *, uint32_t qf, uint32_t qidx);
};

#endif // VLC_LIBPLACEBO_INSTANCE_VULKAN_H
