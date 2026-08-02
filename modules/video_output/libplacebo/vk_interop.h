/*****************************************************************************
 * vk_interop.h: Vulkan hardware decoder interop interface
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

#ifndef VLC_PLACEBO_VK_INTEROP_H
#define VLC_PLACEBO_VK_INTEROP_H

#include <libplacebo/renderer.h>

#include "../vulkan/interop.h"

/**
 * Initialize interop structure
 *
 * Explicitly initializes all fields to safe defaults.
 *
 * \param interop the interop structure to initialize
 */
void vlc_vk_interop_Init(struct vlc_vk_interop *interop);

/**
 * Probe for compatible interop module (Phase 1)
 *
 * Searches for a vkinterop module that can handle the given format/context.
 * Calls the module's open() callback to check compatibility and get required
 * instance and device extensions.
 *
 * This function is called BEFORE instance creation. The module returns its
 * extension requirements without access to Vulkan handles.
 *
 * After instance/device creation, call vlc_vk_interop_ProbeFormat() to negotiate
 * format, then vlc_vk_interop_Load() to complete initialization.
 *
 * \param probe probe structure to fill
 * \param obj parent VLC object for logging
 * \param vctx video context (holds decoder device), or NULL for software rendering
 * \param fmt video format (contains opaque chroma)
 * \return VLC_SUCCESS if compatible module found, VLC_EGENERIC otherwise
 */
int vlc_vk_interop_Probe(struct vlc_vk_interop_probe *probe, vlc_object_t *obj,
                         struct vlc_video_context *vctx, const video_format_t *fmt);

/**
 * Negotiate format with device capabilities (Phase 2)
 *
 * Calls the interop's probe_format() callback to select the best format for the device.
 * Must be called after vlc_vk_interop_Probe() and device creation, before Load().
 *
 * Populates interop with Phase 1 probe results (ops) and format_probe fields
 * (obj, vctx, fmt_in, phys_dev) before calling the interop's probe_format() callback.
 *
 * \param interop interop structure to populate with ops and format_probe fields
 * \param probe Phase 1 probe results (provides obj, vctx, fmt_in, ops)
 * \param dev Vulkan device for format queries
 * \param gpu libplacebo GPU
 * \return VLC_SUCCESS if format negotiated, VLC_EGENERIC otherwise
 */
int vlc_vk_interop_ProbeFormat(struct vlc_vk_interop *interop,
                               const struct vlc_vk_interop_probe *probe,
                               struct vlc_vk_device *dev, pl_gpu gpu);

/**
 * Complete interop initialization with device handles (Phase 3)
 *
 * Populates interop with Vulkan device and calls the module's load() callback.
 * Display.c must populate dev, ycbcr_conversion, sampler before calling this function.
 *
 * \param interop the interop (format_probe fields and dev/sampler must be valid)
 * \return VLC_SUCCESS or VLC error
 */
int vlc_vk_interop_Load(struct vlc_vk_interop *interop);

/**
 * Close a Vulkan interop
 *
 * Calls the interop's close callback and releases resources.
 *
 * \param interop the interop to close
 */
void vlc_vk_interop_Close(struct vlc_vk_interop *interop);

#endif /* VLC_PLACEBO_VK_INTEROP_H */
