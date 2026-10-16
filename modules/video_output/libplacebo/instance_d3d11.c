/**
 * @file instance_d3d11.c
 * @brief D3D11 specific libplacebo GPU wrapper
 */
/*****************************************************************************
 * Copyright © 2023 Niklas Haas
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

#include <libplacebo/d3d11.h>

#include "instance.h"
#include "utils.h"

struct vlc_placebo_system_t {
    pl_d3d11 d3d11;
};

static void CloseInstance(vlc_placebo_t *pl);
static const struct vlc_placebo_operations instance_opts =
{
    .close = CloseInstance,
};

static int InitInstance(vlc_placebo_t *pl, const vout_display_cfg_t *cfg)
{
    vlc_window_t *window = cfg->window;
    if (window->type != VLC_WINDOW_TYPE_HWND)
        return VLC_EGENERIC;

    vlc_placebo_system_t *sys = pl->sys =
        vlc_obj_calloc(VLC_OBJECT(pl), 1, sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    // Create d3d11 device
    sys->d3d11 = pl_d3d11_create(pl->log, &(struct pl_d3d11_params) {
        .allow_software = var_InheritBool(pl, "d3d11-allow-sw"),
        .max_frame_latency = var_InheritInteger(pl, "d3d11-max-latency"),
    });
    if (!sys->d3d11)
        goto error;

    // Create swapchain
    struct pl_d3d11_swapchain_params swap_params = {
        .window = window->handle.hwnd,
    };

    pl->swapchain = pl_d3d11_create_swapchain(sys->d3d11, &swap_params);
    if (!pl->swapchain)
        goto error;

    pl->gpu = sys->d3d11->gpu;
    pl->ops = &instance_opts;
    return VLC_SUCCESS;

error:
    CloseInstance(pl);
    return VLC_EGENERIC;
}

static void CloseInstance(vlc_placebo_t *pl)
{
    vlc_placebo_system_t *sys = pl->sys;

    pl_swapchain_destroy(&pl->swapchain);
    pl_d3d11_destroy(&sys->opengl);

    vlc_obj_free(VLC_OBJECT(pl), sys);
    pl->sys = NULL;
}

#define DEBUG_TEXT "Enable API debugging"
#define DEBUG_LONGTEXT "This loads the d3d11 standard debugging layers, which can help catch runtime errors."

#define ALLOWSW_TEXT "Allow software devices"
#define ALLOWSW_LONGTEXT "If enabled, allow the use of software emulation devices, which are not real devices and therefore typically very slow."

#define MAXLATENCY_TEXT "Maximum frame latency"
#define MAXLATENCY_LONGTEXT "Affects how many frames to render/present in advance. Increasing this can improve performance at the cost of latency, by allowing better pipelining between frames. May have no effect, depending on the VLC clock settings."

vlc_module_begin()
    set_shortname("libplacebo D3D11")
    set_description(N_("D3D11-based GPU instance"))
    set_subcategory(SUBCAT_VIDEO_VOUT)
    set_capability("libplacebo gpu", 50)
    set_callback(InitInstance)
    add_shortcut("pl_d3d11")

    set_section("Device creation", NULL)
    add_bool("d3d11-debug", false, DEBUG_TEXT, DEBUG_LONGTEXT)
    add_bool("d3d11-allow-sw", pl_d3d11_default_params.allow_software,
            ALLOWSW_TEXT, ALLOWSW_LONGTEXT)
    add_integer_with_range("d3d11-max-latency", 3,
            1, 8, MAXLATENCY_TEXT, MAXLATENCY_LONGTEXT)
vlc_module_end()
