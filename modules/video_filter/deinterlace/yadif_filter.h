/*****************************************************************************
 * yadif_filter.h : SIMD dispatch interface for the Yadif deinterlacer
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 * 
 * Author : Mazen Ewiss  mazenrory@gmail.com
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

#ifndef VLC_DEINTERLACE_YADIF_FILTER_H
#define VLC_DEINTERLACE_YADIF_FILTER_H 1

#include <stdint.h>

typedef void(*yadif_filter_cb)(uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int prefs, int mrefs, int parity, int mode);

struct yadif_filter_functions {
      /** Filter-line implementation, indexed by the binary order of
     * magnitude of the pixel size in bytes: 0 for 8-bit, 1 for 16-bit.
     * A NULL entry means no SIMD implementation is available for that
     * pixel size. */
    yadif_filter_cb filter[2];
};

#endif
