/*****************************************************************************
 * playlist.h
 *****************************************************************************
 * Copyright © 2022 VLC authors and VideoLAN
 *
 * Author: Lucas Ransan <lucas@ransan.tk>
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

#ifndef VLC_MPD_PLAYLIST_H
#define VLC_MPD_PLAYLIST_H

#include "mpd.h"

int mpd_playlist_callbacks_setup(intf_thread_t *intf);

#endif
