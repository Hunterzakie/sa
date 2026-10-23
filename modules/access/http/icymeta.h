/*****************************************************************************
 * icymeta.h: SHOUTcast/Icecast ICY in-band metadata
 *****************************************************************************
 * Copyright (C) 2026 Tom Taylor
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

#ifndef VLC_HTTP_ICYMETA_H
#define VLC_HTTP_ICYMETA_H 1

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <vlc_tick.h>

struct vlc_http_stream;

/* CBR audio byte offset -> playback time, or VLC_TICK_INVALID if bitrate unknown. */
static inline vlc_tick_t
vlc_icy_meta_OffsetToPts(uint64_t offset, unsigned bitrate_bps)
{
    return (bitrate_bps > 0)
         ? vlc_tick_from_samples((int64_t)offset * 8, bitrate_bps)
         : VLC_TICK_INVALID;
}

/**
 * Opens an ICY metadata stream filter.
 *
 * Wraps a body stream so that reads strip the in-band SHOUTcast/Icecast
 * metadata blocks and return only the audio payload.
 *
 * @param parent underlying body stream (consumed by the filter)
 * @param interval number of audio bytes between metadata blocks
 * @return an HTTP stream or NULL on allocation failure
 */
struct vlc_http_stream *vlc_icy_open(struct vlc_http_stream *parent,
                                     size_t interval);

/**
 * Gets the current ICY now-playing title and the de-framed audio offset at
 * which it takes effect (does not consume; repeated calls return the same
 * until the title changes). Caller frees *titlep on success.
 */
bool vlc_icy_stream_get_timed_title(struct vlc_http_stream *stream,
                                    char **restrict titlep,
                                    uint64_t *restrict offsetp);

#endif
