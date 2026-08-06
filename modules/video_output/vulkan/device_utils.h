/*****************************************************************************
 * device_utils.h: Vulkan device utility helpers
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

#ifndef VLC_VULKAN_DEVICE_UTILS_H
#define VLC_VULKAN_DEVICE_UTILS_H

#include "common.h"

struct vlc_vk_device;

bool vlc_vk_device_MapMemoryTypeToIndex(const struct vlc_vk_device *dev, uint32_t type_bits,
                                        VkFlags requirements, uint32_t *type_index);

void vlc_vk_device_SetObjectName(const struct vlc_vk_device *dev,
                                 VkObjectType type, uint64_t handle, const char *name);

#endif /* VLC_VULKAN_DEVICE_UTILS_H */
