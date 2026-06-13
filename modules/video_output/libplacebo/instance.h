/*****************************************************************************
 * instance.h: libplacebo instance abstraction
 *****************************************************************************
 * Copyright (C) 2021 Niklas Haas
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

#ifndef VLC_LIBPLACEBO_INSTANCE_H
#define VLC_LIBPLACEBO_INSTANCE_H

#include <vlc_common.h>
#include <vlc_vout_display.h>

#include <libplacebo/swapchain.h>
#include <libplacebo/log.h>
#include <libplacebo/gpu.h>

// Extra Vulkan extensions to enable at instance / device creation
struct vlc_placebo_create_params
{
    const char * const *instance_extensions;
    size_t              instance_extension_count;
    const char * const *device_extensions;
    size_t              device_extension_count;
};

struct vlc_placebo_t;
struct vlc_placebo_vk_handles;

struct vlc_placebo_operations
{
    void (*close)(struct vlc_placebo_t *);

    // For acquiring/releasing the context on the current thread. (Optional)
    int (*make_current)(struct vlc_placebo_t *);
    void (*release_current)(struct vlc_placebo_t *);

    void (*resize)(struct vlc_placebo_t *, unsigned width, unsigned height);
    
    // Raw Vulkan handles, NULL for non-Vulkan backends (Optional)
    const struct vlc_placebo_vk_handles *(*get_vk_handles)(struct vlc_placebo_t *);
};

typedef struct vlc_placebo_system_t vlc_placebo_system_t;

// Shared struct for libplacebo context / gpu / swapchain
typedef struct vlc_placebo_t
{
    // fields internal to instance.c, should not be touched
    struct vlc_object_t obj;
    vlc_placebo_system_t *sys;
    const vout_display_cfg_t *cfg;
    char *name;

    pl_log log;
    pl_gpu gpu;
    pl_swapchain swapchain;
#if PL_API_VER >= 320
    pl_cache cache;
#endif

    const struct vlc_placebo_operations *ops;
} vlc_placebo_t;

vlc_placebo_t *vlc_placebo_New(const vout_display_cfg_t *, const char *name) VLC_USED;
int vlc_placebo_Connect(vlc_placebo_t *, const struct vlc_placebo_create_params *);
vlc_placebo_t *vlc_placebo_Create(const vout_display_cfg_t *, const char*) VLC_USED;
void vlc_placebo_Release(vlc_placebo_t *);

// Active backend's Vulkan handles, or NULL if it is not Vulkan-based
const struct vlc_placebo_vk_handles *vlc_placebo_GetVkHandles(vlc_placebo_t *);

// Needed around every `pl_gpu` / `pl_swapchain` operation
int vlc_placebo_MakeCurrent(vlc_placebo_t *);
void vlc_placebo_ReleaseCurrent(vlc_placebo_t *);

void vlc_placebo_Resize(vlc_placebo_t *, unsigned width, unsigned height);

#endif // VLC_LIBPLACEBO_INSTANCE_H
