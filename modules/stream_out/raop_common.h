/*****************************************************************************
 * raop_common.h: shared interface between the RAOP sout and demux filter
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
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

#ifndef VLC_RAOP_COMMON_H
#define VLC_RAOP_COMMON_H

#include <vlc_input.h>
#include <vlc_meta.h>

#define RAOP_SHARED_VAR_NAME "raop_sout"

typedef struct
{
    void *p_opaque;

    /* Takes ownership of p_meta. NULL clears any previously-stored meta. */
    void (*pf_set_meta)( void *opaque, vlc_meta_t *p_meta );

} raop_common;

#endif
