/*****************************************************************************
 * filter.h
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

#ifndef VLC_MPD_FILTER_H
#define VLC_MPD_FILTER_H

#include "mpd.h"

typedef struct mpd_filter_t mpd_filter_t;
struct mpd_filter_t {
    enum {
        MPD_FILTER_TAG_EQUAL,
        MPD_FILTER_TAG_NOT_EQUAL,
        MPD_FILTER_TAG_CONTAINS,
        MPD_FILTER_TAG_NOT_CONTAINS,
        MPD_FILTER_TAG_MATCH,
        MPD_FILTER_TAG_NOT_MATCH,
        MPD_FILTER_BASE,
        MPD_FILTER_MODIFIED_SINCE,
        MPD_FILTER_AUDIO_FORMAT_EQUAL,
        MPD_FILTER_AUDIO_FORMAT_MATCH,
        MPD_FILTER_PRIO_GE,
        MPD_FILTER_NOT,
        MPD_FILTER_AND,

        MPD_FILTER_KIND_COUNT
    } kind;

    union {
        struct {
            union {
                /* MPD_FILTER_TAG_EQUAL
                   MPD_FILTER_TAG_NOT_EQUAL
                   MPD_FILTER_TAG_CONTAINS
                   MPD_FILTER_TAG_NOT_CONTAINS
                   MPD_FILTER_TAG_MATCH
                   MPD_FILTER_TAG_NOT_MATCH */
                int tag;

                /* MPD_FILTER_BASE
                   MPD_FILTER_MODIFIED_SINCE
                   MPD_FILTER_AUDIO_FORMAT_EQUAL
                   MPD_FILTER_AUDIO_FORMAT_MATCH */
                /* nothing */
            };
            char *value;
        };

        /* MPD_FILTER_PRIO_GE */
        int prio;

        /* MPD_FILTER_NOT */
        mpd_filter_t *not;

        /* MPD_FILTER_AND */
        struct VLC_VECTOR(mpd_filter_t *) and;
    };
};

typedef enum {
    MPD_FILTER_ERROR = -1,
    MPD_FILTER_NO = 0,
    MPD_FILTER_YES = 1,
    MPD_FILTER_MAYBE,
} mpd_filter_pass_t;

typedef struct mpd_filter_params_t mpd_filter_params_t;
struct mpd_filter_params_t {
    intf_thread_t *intf;
    void *item;
    char *(*get_tag)(mpd_filter_params_t *params, int tag);
    bool case_sensitive;
};

mpd_filter_t *mpd_filter_parse(const char *str);
mpd_filter_t *mpd_filter_from_tag_value(int tag, const char *value, bool strict);
void mpd_filter_optimize(mpd_filter_t **filter);
void mpd_filter_free(mpd_filter_t *filter);

mpd_filter_pass_t mpd_filter_inner(mpd_filter_t *filter, mpd_filter_params_t *params);
char *mpd_item_get_tag(mpd_filter_params_t *params, int tag);
char *mpd_media_get_tag(mpd_filter_params_t *params, int tag);
char *mpd_album_get_tag(mpd_filter_params_t *params, int tag);
char *mpd_artist_get_tag(mpd_filter_params_t *params, int tag);
char *mpd_genre_get_tag(mpd_filter_params_t *params, int tag);

#define mpd_filter(_intf, filter, what, _case_sensitive)            \
    mpd_filter_inner(filter, &(mpd_filter_params_t){                \
        .intf = _intf,                                              \
        .item = what,                                               \
        .get_tag = &_Generic((what),                                \
                             input_item_t *: mpd_item_get_tag,      \
                             vlc_ml_media_t *: mpd_media_get_tag,   \
                             vlc_ml_album_t *: mpd_album_get_tag,   \
                             vlc_ml_artist_t *: mpd_artist_get_tag, \
                             vlc_ml_genre_t *: mpd_genre_get_tag),  \
        .case_sensitive = _case_sensitive,                          \
    })

#endif
