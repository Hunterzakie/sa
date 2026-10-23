/**
 * @file instance_vulkan.c
 * @brief Vulkan specific libplacebo GPU wrapper
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vector.h>

#include <libplacebo/vulkan.h>

#include "../vulkan/platform.h"
#include "instance.h"
#include "instance_vulkan.h"
#include "utils.h"

struct vlc_placebo_system_t {
    vlc_vk_platform_t *platform;
    VkSurfaceKHR surface;
    pl_vk_inst instance;
    pl_vulkan vulkan;
    struct vlc_placebo_vk_handles handles;
};

static void CloseInstance(vlc_placebo_t *pl);
static const struct vlc_placebo_vk_handles *GetVkHandles(vlc_placebo_t *pl);
static void LockQueue(vlc_placebo_t *pl, uint32_t qf, uint32_t qidx);
static void UnlockQueue(vlc_placebo_t *pl, uint32_t qf, uint32_t qidx);
static const struct vlc_placebo_operations instance_opts =
{
    .close = CloseInstance,
    .get_vk_handles = GetVkHandles,
};

static int InitInstance(vlc_placebo_t *pl, const vout_display_cfg_t *cfg,
                        const struct vlc_placebo_create_params *params)
{
    static const struct vlc_placebo_create_params no_params = {0};
    if (params == NULL)
        params = &no_params;

    vlc_placebo_system_t *sys = pl->sys =
        vlc_obj_calloc(VLC_OBJECT(pl), 1, sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    char *platform_name = var_InheritString(pl, "vk-platform");
    sys->platform = vlc_vk_platform_Create(cfg->window, platform_name);
    free(platform_name);
    if (!sys->platform)
        goto error;

    struct VLC_VECTOR(const char *) inst_ext = VLC_VECTOR_INITIALIZER;
    bool ok = vlc_vector_push(&inst_ext, VK_KHR_SURFACE_EXTENSION_NAME)
           && vlc_vector_push(&inst_ext, sys->platform->platform_ext);
    for (size_t i = 0; ok && i < params->instance_extension_count; i++)
        ok = vlc_vector_push(&inst_ext, params->instance_extensions[i]);
    if (unlikely(!ok)) {
        vlc_vector_destroy(&inst_ext);
        goto error;
    }

    sys->instance = pl_vk_inst_create(pl->log, &(struct pl_vk_inst_params) {
        .debug = var_InheritBool(pl, "vk-debug"),
        .extensions = inst_ext.data,
        .num_extensions = inst_ext.size,
    });
    vlc_vector_destroy(&inst_ext);
    if (!sys->instance)
        goto error;

    vlc_vk_instance_t inst = {
        .instance = sys->instance->instance,
        .get_proc_address = sys->instance->get_proc_addr,
    };

    // Create the platform-specific surface object
    if (vlc_vk_CreateSurface(sys->platform, &inst, &sys->surface) != VLC_SUCCESS)
        goto error;

    // Create vulkan device
    char *device_name = var_InheritString(pl, "vk-device");
    sys->vulkan = pl_vulkan_create(pl->log, &(struct pl_vulkan_params) {
        .instance = sys->instance->instance,
        .surface = sys->surface,
        .device_name = device_name,
        .allow_software = var_InheritBool(pl, "vk-allow-sw"),
        .async_transfer = var_InheritBool(pl, "vk-async-xfer"),
        .async_compute = var_InheritBool(pl, "vk-async-comp"),
        .queue_count = var_InheritInteger(pl, "vk-queue-count"),
        .extensions = params->device_extensions,
        .num_extensions = params->device_extension_count,
    });
    free(device_name);
    if (!sys->vulkan)
        goto error;

    // Create swapchain for this surface
    struct pl_vulkan_swapchain_params swap_params = {
        .surface = sys->surface,
        .present_mode = var_InheritInteger(pl, "vk-present-mode"),
        .swapchain_depth = var_InheritInteger(pl, "vk-queue-depth"),
    };

    pl->swapchain = pl_vulkan_create_swapchain(sys->vulkan, &swap_params);
    if (!pl->swapchain)
        goto error;

    VkQueue graphics_queue = VK_NULL_HANDLE;
    PFN_vkGetDeviceQueue GetDeviceQueue = (PFN_vkGetDeviceQueue)
        sys->vulkan->get_proc_addr(sys->instance->instance, "vkGetDeviceQueue");
    if (GetDeviceQueue != NULL)
        GetDeviceQueue(sys->vulkan->device, sys->vulkan->queue_graphics.index, 0,
                       &graphics_queue);

    sys->handles = (struct vlc_placebo_vk_handles) {
        .instance               = sys->vulkan->instance,
        .phys_device            = sys->vulkan->phys_device,
        .device                 = sys->vulkan->device,
        .graphics_queue_family  = sys->vulkan->queue_graphics.index,
        .get_instance_proc_addr = sys->vulkan->get_proc_addr,
        .graphics_queue         = graphics_queue,
        .graphics_queue_index   = 0,
        .lock_queue             = LockQueue,
        .unlock_queue           = UnlockQueue,
    };

    pl->gpu = sys->vulkan->gpu;
    pl->ops = &instance_opts;
    return VLC_SUCCESS;

error:
    CloseInstance(pl);
    return VLC_EGENERIC;
}

static void LockQueue(vlc_placebo_t *pl, uint32_t qf, uint32_t qidx)
{
    vlc_placebo_system_t *sys = pl->sys;
    sys->vulkan->lock_queue(sys->vulkan, qf, qidx);
}

static void UnlockQueue(vlc_placebo_t *pl, uint32_t qf, uint32_t qidx)
{
    vlc_placebo_system_t *sys = pl->sys;
    sys->vulkan->unlock_queue(sys->vulkan, qf, qidx);
}

static const struct vlc_placebo_vk_handles *GetVkHandles(vlc_placebo_t *pl)
{
    return &pl->sys->handles;
}

static void CloseInstance(vlc_placebo_t *pl)
{
    vlc_placebo_system_t *sys = pl->sys;

    pl_swapchain_destroy(&pl->swapchain);

    if (sys->surface) {
        vlc_vk_instance_t inst = {
            .instance = sys->instance->instance,
            .get_proc_address = sys->instance->get_proc_addr,
        };
        vlc_vk_DestroySurface(&inst, sys->surface);
    }

    pl_vulkan_destroy(&sys->vulkan);
    pl_vk_inst_destroy(&sys->instance);

    if (sys->platform != NULL)
        vlc_vk_platform_Release(sys->platform);

    vlc_obj_free(VLC_OBJECT(pl), sys);
    pl->sys = NULL;
}

#define PROVIDER_TEXT N_("Vulkan platform module")
#define PROVIDER_LONGTEXT N_( \
    "Which platform-specific Vulkan surface module to load.")

#define DEBUG_TEXT "Enable API debugging"
#define DEBUG_LONGTEXT "This loads the vulkan standard validation layers, which can help catch API usage errors. Comes at a small performance penalty."

#define DEVICE_TEXT "Device name override"
#define DEVICE_LONGTEXT "If set to something non-empty, only a device with this exact name will be used. To see a list of devices and their names, run vlc -v with this module active."

#define ALLOWSW_TEXT "Allow software devices"
#define ALLOWSW_LONGTEXT "If enabled, allow the use of software emulation devices, which are not real devices and therefore typically very slow. (This option has no effect if forcing a specific device name)"

#define ASYNC_XFER_TEXT "Allow asynchronous transfer"
#define ASYNC_XFER_LONGTEXT "Allows the use of an asynchronous transfer queue if the device has one. Typically this maps to a DMA engine, which can perform texture uploads/downloads without blocking the GPU's compute units. Highly recommended for 4K and above."

#define ASYNC_COMP_TEXT "Allow asynchronous compute"
#define ASYNC_COMP_LONGTEXT "Allows the use of dedicated compute queue families if the device has one. Sometimes these will schedule concurrent compute work better than the main graphics queue. Turn this off if you have any issues."

#define QUEUE_COUNT_TEXT "Queue count"
#define QUEUE_COUNT_LONGTEXT "How many queues to use on the device. Increasing this might improve rendering throughput for GPUs capable of concurrent scheduling. Increasing this past the driver's limit has no effect."

#define QUEUE_DEPTH_TEXT "Maximum frame latency"
#define QUEUE_DEPTH_LONGTEXT "Affects how many frames to render/present in advance. Increasing this can improve performance at the cost of latency, by allowing better pipelining between frames. May have no effect, depending on the VLC clock settings."

static const int present_values[] = {
    VK_PRESENT_MODE_IMMEDIATE_KHR,
    VK_PRESENT_MODE_MAILBOX_KHR,
    VK_PRESENT_MODE_FIFO_KHR,
    VK_PRESENT_MODE_FIFO_RELAXED_KHR,
};

static const char * const present_text[] = {
    "Immediate (non-blocking, tearing)",
    "Mailbox (non-blocking, non-tearing)",
    "FIFO (blocking, non-tearing)",
    "Relaxed FIFO (blocking, tearing)",
};

#define PRESENT_MODE_TEXT "Preferred present mode"
#define PRESENT_MODE_LONGTEXT "Which present mode to use when creating the swapchain. If the chosen mode is not supported, VLC will fall back to using FIFO."

vlc_module_begin()
    set_shortname("libplacebo Vulkan")
    set_description(N_("Vulkan-based GPU instance"))
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("libplacebo gpu", 50)
    set_callback(InitInstance)
    add_shortcut("pl_vulkan")
    add_module ("vk-platform", "vulkan platform", "any", PROVIDER_TEXT, PROVIDER_LONGTEXT)

    set_section("Device selection", NULL)
    add_bool("vk-debug", false, DEBUG_TEXT, DEBUG_LONGTEXT)
    add_string("vk-device", "", DEVICE_TEXT, DEVICE_LONGTEXT)
    add_bool("vk-allow-sw", pl_vulkan_default_params.allow_software,
            ALLOWSW_TEXT, ALLOWSW_LONGTEXT)

    set_section("Performance tuning", NULL)
    add_bool("vk-async-xfer", pl_vulkan_default_params.async_transfer,
            ASYNC_XFER_TEXT, ASYNC_XFER_LONGTEXT)
    add_bool("vk-async-comp", pl_vulkan_default_params.async_compute,
            ASYNC_COMP_TEXT, ASYNC_COMP_LONGTEXT)
    add_integer_with_range("vk-queue-count", pl_vulkan_default_params.queue_count,
            1, 8, QUEUE_COUNT_TEXT, QUEUE_COUNT_LONGTEXT)
    add_integer_with_range("vk-queue-depth", 3,
            1, 8, QUEUE_DEPTH_TEXT, QUEUE_DEPTH_LONGTEXT)
    add_integer("vk-present-mode", VK_PRESENT_MODE_FIFO_KHR,
            PRESENT_MODE_TEXT, PRESENT_MODE_LONGTEXT)
            change_integer_list(present_values, present_text)
vlc_module_end()
