/*****************************************************************************
 * protocol.h
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

#ifndef VLC_MPD_PROTOCOL_H
#define VLC_MPD_PROTOCOL_H

#include "mpd.h"

#define MPD_MAXTOK (MPD_TAG_COUNT * 2)

/* from https://github.com/MusicPlayerDaemon/MPD/blob/792411384d115c98fff9f59d65adbbc7e3568f6f/src/protocol/Ack.hxx#L26 */
enum {
    MPD_ERROR_NOT_LIST = 1,
    MPD_ERROR_ARG = 2,
    MPD_ERROR_PASSWORD = 3,
    MPD_ERROR_PERMISSION = 4,
    MPD_ERROR_UNKNOWN_CMD = 5,

    MPD_ERROR_NO_EXIST = 50,
    MPD_ERROR_PLAYLIST_MAX = 51,
    MPD_ERROR_SYSTEM = 52,
    MPD_ERROR_PLAYLIST_LOAD = 53,
    MPD_ERROR_UPDATE_ALREADY = 54,
    MPD_ERROR_PLAYER_SYNC = 55,
    MPD_ERROR_EXIST = 56,
};

enum {
    /* We consider "any" and "file" as tags, which simplifies the
       code. In MPD's source code they are not for some reason. */
    MPD_TAG_ANY,
    MPD_TAG_FILE,

    MPD_TAG_ARTIST,
    MPD_TAG_ARTIST_SORT,
    MPD_TAG_ALBUM,
    MPD_TAG_ALBUM_SORT,
    MPD_TAG_ALBUM_ARTIST,
    MPD_TAG_ALBUM_ARTIST_SORT,
    MPD_TAG_TITLE,
    MPD_TAG_TRACK,
    MPD_TAG_NAME,
    MPD_TAG_GENRE,
    MPD_TAG_MOOD,
    MPD_TAG_DATE,
    MPD_TAG_ORIGINAL_DATE,
    MPD_TAG_COMPOSER,
    MPD_TAG_COMPOSERSORT,
    MPD_TAG_PERFORMER,
    MPD_TAG_CONDUCTOR,
    MPD_TAG_WORK,
    MPD_TAG_MOVEMENT,
    MPD_TAG_MOVEMENTNUMBER,
    MPD_TAG_ENSEMBLE,
    MPD_TAG_LOCATION,
    MPD_TAG_GROUPING,
    MPD_TAG_COMMENT,
    MPD_TAG_DISC,
    MPD_TAG_LABEL,

    MPD_TAG_MUSICBRAINZ_ARTISTID,
    MPD_TAG_MUSICBRAINZ_ALBUMID,
    MPD_TAG_MUSICBRAINZ_ALBUMARTISTID,
    MPD_TAG_MUSICBRAINZ_TRACKID,
    MPD_TAG_MUSICBRAINZ_RELEASETRACKID,
    MPD_TAG_MUSICBRAINZ_WORKID,

    MPD_TAG_COUNT,
};

extern const char *mpd_tag_names[];

enum {
    /* Keep sorted alphabetically */
    MPD_SUBSYSTEM_DATABASE = 1 << 0,
    MPD_SUBSYSTEM_MESSAGE = 1 << 1,
    MPD_SUBSYSTEM_MIXER = 1 << 2,
    MPD_SUBSYSTEM_MOUNT = 1 << 3,
    MPD_SUBSYSTEM_NEIGHBOR = 1 << 4,
    MPD_SUBSYSTEM_OPTIONS = 1 << 5,
    MPD_SUBSYSTEM_OUTPUT = 1 << 6,
    MPD_SUBSYSTEM_PARTITION = 1 << 7,
    MPD_SUBSYSTEM_PLAYER = 1 << 8,
    MPD_SUBSYSTEM_PLAYLIST = 1 << 9,
    MPD_SUBSYSTEM_STICKER = 1 << 10,
    MPD_SUBSYSTEM_STORED_PLAYLIST = 1 << 11,
    MPD_SUBSYSTEM_SUBSCRIPTION = 1 << 12,
    MPD_SUBSYSTEM_UPDATE = 1 << 13,

    MPD_SUBSYSTEM_ALL = (1 << 14) - 1,
    MPD_SUBSYSTEM_COUNT = 14,
};

extern const char *mpd_subsystem_names[MPD_SUBSYSTEM_COUNT];

void mpd_greet_client(intf_thread_t *intf, mpd_client_t *client);
void mpd_tokenize_request(intf_thread_t *intf, char *reqbuf, char *tokens[static MPD_MAXTOK + 1]);

VLC_FORMAT(4, 5)
void mpd_send_kv(intf_thread_t *intf, mpd_client_t *client, const char *key, const char *fmt, ...);
VLC_FORMAT(4, 5)
void mpd_send_error(intf_thread_t *intf, mpd_client_t *client, int error, const char *fmt, ...);

bool mpd_send_changed_subsystems(intf_thread_t *intf, mpd_client_t *client);

bool mpd_parse_bool(const char *str, bool *val);
bool mpd_parse_int(const char *str, int64_t *val);
bool mpd_parse_uint(const char *str, uint64_t *val);
bool mpd_parse_float(const char *str, double *val);
bool mpd_parse_range(const char *str, int64_t *from, int64_t *to);
bool mpd_parse_urange(const char *str, uint64_t *from, uint64_t *to);
bool mpd_parse_tag(const char *str, int *tag);

#endif
