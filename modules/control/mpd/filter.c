/*****************************************************************************
 * filter.c
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <limits.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_strings.h>
#include <vlc_memstream.h>

#include "filter.h"
#include "protocol.h"

#define STARTS(str, with) (strncmp(str, with, strlen(with)) == 0)

/* Fake tags */
enum {
    MPD_TAG_BASE = MPD_TAG_COUNT,
    MPD_TAG_MODIFIED_SINCE,
    MPD_TAG_AUDIO_FORMAT,
    MPD_TAG_PRIORITY,
};

const char *mpd_tag_names[] = {
    [MPD_TAG_ANY] = "any",
    [MPD_TAG_FILE] = "file",

    [MPD_TAG_ARTIST] = "Artist",
    [MPD_TAG_ARTIST_SORT] = "ArtistSort",
    [MPD_TAG_ALBUM] = "Album",
    [MPD_TAG_ALBUM_SORT] = "AlbumSort",
    [MPD_TAG_ALBUM_ARTIST] = "AlbumArtist",
    [MPD_TAG_ALBUM_ARTIST_SORT] = "AlbumArtistSort",
    [MPD_TAG_TITLE] = "Title",
    [MPD_TAG_TRACK] = "Track",
    [MPD_TAG_NAME] = "Name",
    [MPD_TAG_GENRE] = "Genre",
    [MPD_TAG_MOOD] = "Mood",
    [MPD_TAG_DATE] = "Date",
    [MPD_TAG_ORIGINAL_DATE] = "OriginalDate",
    [MPD_TAG_COMPOSER] = "Composer",
    [MPD_TAG_COMPOSERSORT] = "ComposerSort",
    [MPD_TAG_PERFORMER] = "Performer",
    [MPD_TAG_CONDUCTOR] = "Conductor",
    [MPD_TAG_WORK] = "Work",
    [MPD_TAG_MOVEMENT] = "Movement",
    [MPD_TAG_MOVEMENTNUMBER] = "MovementNumber",
    [MPD_TAG_ENSEMBLE] = "Ensemble",
    [MPD_TAG_LOCATION] = "Location",
    [MPD_TAG_GROUPING] = "Grouping",
    [MPD_TAG_COMMENT] = "Comment",
    [MPD_TAG_DISC] = "Disc",
    [MPD_TAG_LABEL] = "Label",

    /* MusicBrainz tags from http://musicbrainz.org/doc/MusicBrainzTag */
    [MPD_TAG_MUSICBRAINZ_ARTISTID] = "MUSICBRAINZ_ARTISTID",
    [MPD_TAG_MUSICBRAINZ_ALBUMID] = "MUSICBRAINZ_ALBUMID",
    [MPD_TAG_MUSICBRAINZ_ALBUMARTISTID] = "MUSICBRAINZ_ALBUMARTISTID",
    [MPD_TAG_MUSICBRAINZ_TRACKID] = "MUSICBRAINZ_TRACKID",
    [MPD_TAG_MUSICBRAINZ_RELEASETRACKID] = "MUSICBRAINZ_RELEASETRACKID",
    [MPD_TAG_MUSICBRAINZ_WORKID] = "MUSICBRAINZ_WORKID",

    /* Fake tags */
    [MPD_TAG_BASE] = "base",
    [MPD_TAG_MODIFIED_SINCE] = "modified-since",
    [MPD_TAG_AUDIO_FORMAT] = "AudioFormat",
    [MPD_TAG_PRIORITY] = "prio",
};

static const char *op_names[] = {
    [MPD_FILTER_TAG_EQUAL] = "==",
    [MPD_FILTER_TAG_NOT_EQUAL] = "!=",
    [MPD_FILTER_TAG_CONTAINS] = "contains",
    [MPD_FILTER_TAG_NOT_CONTAINS] = "!contains",
    [MPD_FILTER_TAG_MATCH] = "=~",
    [MPD_FILTER_TAG_NOT_MATCH] = "!~",
};

static int parse_tag(const char **str) {
    const char *space = strchr(*str, ' ');
    if (!space)
        return -1;
    const size_t len = space - *str;

    for (size_t i = 0; i < ARRAY_SIZE(mpd_tag_names); i++) {
        if (vlc_ascii_strncasecmp(*str, mpd_tag_names[i], len) == 0) {
            *str = space + 1;
            return i;
        }
    }

    return -1;
}

static int parse_operator(const char **str) {
    const char *space = strchr(*str, ' ');
    if (!space)
        return -1;
    const size_t len = space - *str;

    for (size_t i = 0; i < ARRAY_SIZE(op_names); i++) {
        if (strncmp(*str, op_names[i], len) == 0) {
            *str = space + 1;
            return i;
        }
    }

    return -1;
}

static char *parse_quoted(const char **str) {
    char q = *(*str)++;
    if (q != '\'' && q != '"')
        return NULL;

    struct vlc_memstream stream;
    vlc_memstream_open(&stream);

    while (**str != q) {
        if (**str == '\0')
            goto error;

        if (**str == '\\')
            ++*str;

        vlc_memstream_putc(&stream, *(*str)++);
    }

    ++*str;

    if (vlc_memstream_close(&stream))
        return NULL;
    return stream.ptr;

error:
    if (!vlc_memstream_close(&stream))
        free(stream.ptr);
    return NULL;
}

#ifdef FILTER_TEST
static void test_parse_quoted(void) {
    const struct {
        const char *str, *res;
    } should_succeed[] = {
        { "\"a\"", "a" },
        { "'a'", "a" },
        { "\"foo\\'bar\\\"\"", "foo'bar\"" },
    };

    for (size_t i = 0; i < ARRAY_SIZE(should_succeed); i++) {
        const char *s = should_succeed[i].str;
        const char *r = parse_quoted(&s);
        assert(r != NULL);
        assert(*s == '\0');
        assert(strcmp(r, should_succeed[i].res) == 0);
    }
}
#endif

static int parse_prio(const char **str) {
    const char *s = *str;
    long x = strtol(s, (char **)str, 10);
    if (*str == s || x <= INT_MIN || x > INT_MAX)
        return INT_MIN;
    return (int)x;
}

static int parse_audio_format_op(const char **str) {
    if (*(*str)++ != '=')
        return -1;
    int op;
    switch (*(*str)++) {
    case '=':
        op = MPD_FILTER_AUDIO_FORMAT_EQUAL;
        break;
    case '~':
        op = MPD_FILTER_AUDIO_FORMAT_MATCH;
        break;
    default:
        return -1;
    }
    if (*(*str)++ != ' ')
        return -1;
    return op;
}

static mpd_filter_t *parse_inner(const char **str) {
    if (*(*str)++ != '(')
        return NULL;

    mpd_filter_t *filter = calloc(1, sizeof *filter);
    if (!filter)
        return NULL;

    if (**str == '!') {
        ++*str;
        filter->kind = MPD_FILTER_NOT;

        filter->not = parse_inner(str);
        if (!filter->not)
            goto error;
    } else if (**str == '(') {
        filter->kind = MPD_FILTER_AND;
        vlc_vector_init(&filter->and);

        mpd_filter_t *lhs = parse_inner(str);
        if (!lhs)
            goto error;
        vlc_vector_push(&filter->and, lhs);

        while (STARTS(*str, " AND ")) {
            *str += strlen(" AND ");
            mpd_filter_t *rhs = parse_inner(str);
            if (!rhs)
                goto error;
            vlc_vector_push(&filter->and, rhs);
        }
    } else {
        int tag = parse_tag(str);
        if (tag < 0)
            goto error;

        if (tag < MPD_TAG_COUNT) {
            filter->tag = tag;
            filter->kind = parse_operator(str);
            if (filter->kind < 0)
                goto error;
            filter->value = parse_quoted(str);
            if (!filter->value)
                goto error;
        } else if (tag == MPD_TAG_BASE || tag == MPD_TAG_MODIFIED_SINCE) {
            filter->kind = tag == MPD_TAG_BASE
                ? MPD_FILTER_BASE
                : MPD_FILTER_MODIFIED_SINCE;
            filter->value = parse_quoted(str);
            if (!filter->value)
                goto error;
        } else if (tag == MPD_TAG_PRIORITY) {
            filter->kind = MPD_FILTER_PRIO_GE;
            if (!STARTS(*str, ">= "))
                goto error;
            *str += strlen(">= ");
            filter->prio = parse_prio(str);
            if (filter->prio == INT_MIN)
                goto error;
        } else if (tag == MPD_TAG_AUDIO_FORMAT) {
            filter->kind = parse_audio_format_op(str);
            if (filter->kind < 0)
                goto error;
            filter->value = parse_quoted(str);
            if (!filter->value)
                goto error;
        }
    }

    if (*(*str)++ != ')')
        goto error;

    return filter;

error:
    mpd_filter_free(filter);
    return NULL;
}

mpd_filter_t *mpd_filter_parse(const char *str) {
    mpd_filter_t *filter = parse_inner(&str);
    if (filter && *str != '\0')
        return NULL;
    return filter;
}

mpd_filter_t *mpd_filter_from_tag_value(int tag, const char *value, bool strict) {
    mpd_filter_t *filter = calloc(1, sizeof *filter);
    if (!filter)
        goto error;

    if (tag < MPD_TAG_COUNT) {
        filter->kind = strict ? MPD_FILTER_TAG_EQUAL : MPD_FILTER_TAG_CONTAINS;
        filter->tag = tag;
        if (!(filter->value = strdup(value)))
            goto error;
    } else if (tag == MPD_TAG_BASE) {
        filter->kind = MPD_FILTER_BASE;
        if (!(filter->value = strdup(value)))
            goto error;
    } else if (tag == MPD_TAG_MODIFIED_SINCE) {
        filter->kind = MPD_FILTER_MODIFIED_SINCE;
        if (!(filter->value = strdup(value)))
            goto error;
    } else if (tag == MPD_TAG_AUDIO_FORMAT) {
        filter->kind = MPD_FILTER_AUDIO_FORMAT_EQUAL;
        if (!(filter->value = strdup(value)))
            goto error;
    } else if (tag == MPD_TAG_PRIORITY) {
        filter->kind = MPD_FILTER_PRIO_GE;
        filter->prio = parse_prio(&value);
        if (filter->prio == INT_MIN || *value != '\0')
            goto error;
    }

    return filter;

error:
    mpd_filter_free(filter);
    return NULL;
}

void mpd_filter_optimize(mpd_filter_t **filter) {
    if (!*filter)
        return;

    /* TODO: more optimizations */

    switch ((*filter)->kind) {
    case MPD_FILTER_NOT: {
        mpd_filter_t *parent = *filter;
        int subkind = parent->not->kind;
        if (MPD_FILTER_TAG_EQUAL <= subkind && subkind <= MPD_FILTER_TAG_NOT_MATCH) {
            int inverse[] = {
                [MPD_FILTER_TAG_EQUAL] = MPD_FILTER_TAG_NOT_EQUAL,
                [MPD_FILTER_TAG_NOT_EQUAL] = MPD_FILTER_TAG_EQUAL,
                [MPD_FILTER_TAG_CONTAINS] = MPD_FILTER_TAG_NOT_CONTAINS,
                [MPD_FILTER_TAG_NOT_CONTAINS] = MPD_FILTER_TAG_CONTAINS,
                [MPD_FILTER_TAG_MATCH] = MPD_FILTER_TAG_NOT_MATCH,
                [MPD_FILTER_TAG_NOT_MATCH] = MPD_FILTER_TAG_MATCH,
            };

            *filter = parent->not;
            (*filter)->kind = inverse[subkind];
            parent->not = NULL;
            mpd_filter_free(parent);
        } else if (subkind == MPD_FILTER_NOT) {
            *filter = parent->not->not;
            parent->not->not = NULL;
            mpd_filter_free(parent);
            mpd_filter_optimize(filter);
        }
        break;
    }

    case MPD_FILTER_AND:
        for (size_t i = 0; i < (*filter)->and.size; i++) {
            mpd_filter_t **subfilter = &(*filter)->and.data[i];
            /* Flatten nested AND */
            while ((*subfilter)->kind == MPD_FILTER_AND) {
                mpd_filter_t *oldfilter = *subfilter;
                /* It's not possible to parse an AND filter with less than 2 elements. */
                assert(oldfilter->and.size >= 1);

                vlc_vector_insert_hole(&(*filter)->and, i + 1, oldfilter->and.size - 1);
                memcpy(&(*filter)->and.data[i], oldfilter->and.data,
                       oldfilter->and.size * sizeof *oldfilter->and.data);

                for (size_t j = 0; j < oldfilter->and.size; j++)
                    oldfilter->and.data[j] = NULL;
                mpd_filter_free(oldfilter);

                subfilter = &(*filter)->and.data[i];
            }
            mpd_filter_optimize(subfilter);
        }

        vlc_vector_shrink_to_fit(&(*filter)->and);
        break;

    default:
        break;
    }
}

static bool str_eq(const char *a, const char *b, bool case_sensitive) {
    return case_sensitive
        ? strcmp(a, b) == 0
        : vlc_ascii_strcasecmp(a, b) == 0;
}

static bool str_contains(const char *str, const char *substr, bool case_sensitive) {
    if (case_sensitive) {
        return strstr(str, substr);
    } else {
        do {
            for (size_t i = 0;; i++) {
                if (substr[i] == '\0')
                    return true;
                if (vlc_ascii_tolower(str[i]) != vlc_ascii_tolower(substr[i]))
                    break;
            }
        } while (*str++ != '\0');
        return false;
    }
}

char *mpd_item_get_tag(mpd_filter_params_t *params, int tag) {
    input_item_t *item = params->item;

    switch (tag) {
    case MPD_TAG_FILE:
        return strdup(item->psz_uri);
    case MPD_TAG_ARTIST:
    case MPD_TAG_ARTIST_SORT:
        return input_item_GetArtist(item);
    case MPD_TAG_ALBUM:
    case MPD_TAG_ALBUM_SORT:
        return input_item_GetAlbum(item);
    case MPD_TAG_ALBUM_ARTIST:
    case MPD_TAG_ALBUM_ARTIST_SORT:
        return input_item_GetAlbumArtist(item);
    case MPD_TAG_TITLE:
    case MPD_TAG_NAME:
        return input_item_GetTitle(item);
    case MPD_TAG_TRACK:
        return input_item_GetTrackNumber(item);
    case MPD_TAG_GENRE:
        return input_item_GetGenre(item);
    case MPD_TAG_DATE:
        return input_item_GetDate(item);
    default:
        return NULL;
    }
}

char *mpd_media_get_tag(mpd_filter_params_t *params, int tag) {
    intf_sys_t *sys = params->intf->p_sys;
    vlc_ml_media_t *media = params->item;
    char *value = NULL;

    switch (tag) {
    case MPD_TAG_FILE:
        if (!media->p_files)
            return NULL;
        for (size_t i = 0; i < media->p_files->i_nb_items; i++) {
            if (media->p_files->p_items[i].i_type == VLC_ML_FILE_TYPE_MAIN) {
                value = strdup(media->p_files->p_items[i].psz_mrl);
                break;
            }
        }
        return value;
    case MPD_TAG_ARTIST:
    case MPD_TAG_ARTIST_SORT: {
        vlc_ml_artist_t *artist = vlc_ml_get_artist(sys->medialibrary, media->album_track.i_artist_id);
        if (artist)
            value = strdup(artist->psz_name);
        vlc_ml_release(artist);
        return value;
    }
    case MPD_TAG_ALBUM:
    case MPD_TAG_ALBUM_SORT: {
        vlc_ml_album_t *album = vlc_ml_get_album(sys->medialibrary, media->album_track.i_album_id);
        if (album)
            value = strdup(album->psz_title);
        vlc_ml_release(album);
        return value;
    }
    case MPD_TAG_ALBUM_ARTIST:
    case MPD_TAG_ALBUM_ARTIST_SORT: {
        vlc_ml_album_t *album = vlc_ml_get_album(sys->medialibrary, media->album_track.i_album_id);
        if (album)
            value = strdup(album->psz_artist);
        vlc_ml_release(album);
        return value;
    }
    case MPD_TAG_TITLE:
    case MPD_TAG_NAME:
        return strdup(media->psz_title);
    case MPD_TAG_TRACK: {
        int rc = asprintf(&value, "%d", media->album_track.i_track_nb);
        if (rc < 0)
            return NULL;
        return value;
    }
    case MPD_TAG_GENRE: {
        vlc_ml_genre_t *genre = vlc_ml_get_genre(sys->medialibrary, media->album_track.i_genre_id);
        if (genre)
            value = strdup(genre->psz_name);
        vlc_ml_release(genre);
        return value;
    }
    case MPD_TAG_DATE: {
        int rc = asprintf(&value, "%" PRIi32, media->i_year);
        if (rc < 0)
            return NULL;
        return value;
    }
    default:
        return NULL;
    }
}

char *mpd_album_get_tag(mpd_filter_params_t *params, int tag) {
    vlc_ml_album_t *album = params->item;

    switch (tag) {
    case MPD_TAG_ARTIST:
    case MPD_TAG_ARTIST_SORT:
    case MPD_TAG_ALBUM_ARTIST:
    case MPD_TAG_ALBUM_ARTIST_SORT:
        return strdup(album->psz_artist);
    default:
        return NULL;
    }
}

char *mpd_artist_get_tag(mpd_filter_params_t *params, int tag) {
    VLC_UNUSED(params); VLC_UNUSED(tag);
    return NULL;
}

char *mpd_genre_get_tag(mpd_filter_params_t *params, int tag) {
    VLC_UNUSED(params); VLC_UNUSED(tag);
    return NULL;
}

mpd_filter_pass_t mpd_filter_inner(mpd_filter_t *filter, mpd_filter_params_t *params) {
    switch (filter->kind) {
    case MPD_FILTER_TAG_EQUAL:
    case MPD_FILTER_TAG_NOT_EQUAL:
    case MPD_FILTER_TAG_CONTAINS:
    case MPD_FILTER_TAG_NOT_CONTAINS:
    case MPD_FILTER_TAG_MATCH:
    case MPD_FILTER_TAG_NOT_MATCH: {
        char *value = params->get_tag(params, filter->tag);
        if (!value)
            return MPD_FILTER_ERROR;

        mpd_filter_pass_t res;
        switch (filter->kind) {
        case MPD_FILTER_TAG_EQUAL:
            res = str_eq(value, filter->value, params->case_sensitive);
            break;
        case MPD_FILTER_TAG_NOT_EQUAL:
            res = !str_eq(value, filter->value, params->case_sensitive);
            break;
        case MPD_FILTER_TAG_CONTAINS:
            res = str_contains(value, filter->value, params->case_sensitive);
            break;
        case MPD_FILTER_TAG_NOT_CONTAINS:
            res = !str_contains(value, filter->value, params->case_sensitive);
            break;
        default:
            res = MPD_FILTER_ERROR;
            break;
        }

        free(value);
        return res;
    }

    case MPD_FILTER_NOT:
        switch (mpd_filter_inner(filter->not, params)) {
        case MPD_FILTER_ERROR:
            return MPD_FILTER_ERROR;
        case MPD_FILTER_NO:
            return MPD_FILTER_YES;
        case MPD_FILTER_MAYBE:
            return MPD_FILTER_MAYBE;
        case MPD_FILTER_YES:
            return MPD_FILTER_NO;
        default:
            return MPD_FILTER_ERROR;
        }

    case MPD_FILTER_AND: {
        int last = MPD_FILTER_YES;
        mpd_filter_t *f;
        vlc_vector_foreach(f, &filter->and) {
            int r = mpd_filter_inner(f, params);
            if (r <= 0)
                return r;
            last = last == MPD_FILTER_MAYBE ? MPD_FILTER_MAYBE : r;
        }
        return last;
    }

    default:
        return MPD_FILTER_MAYBE;
    }
}

void mpd_filter_free(mpd_filter_t *filter) {
    if (!filter)
        return;

    switch (filter->kind) {
    case MPD_FILTER_TAG_EQUAL:
    case MPD_FILTER_TAG_NOT_EQUAL:
    case MPD_FILTER_TAG_CONTAINS:
    case MPD_FILTER_TAG_NOT_CONTAINS:
    case MPD_FILTER_TAG_MATCH:
    case MPD_FILTER_TAG_NOT_MATCH:
    case MPD_FILTER_BASE:
    case MPD_FILTER_MODIFIED_SINCE:
    case MPD_FILTER_AUDIO_FORMAT_EQUAL:
    case MPD_FILTER_AUDIO_FORMAT_MATCH:
        free(filter->value);
        break;
    case MPD_FILTER_PRIO_GE:
        break;
    case MPD_FILTER_NOT:
        mpd_filter_free(filter->not);
        break;
    case MPD_FILTER_AND: {
        mpd_filter_t *f;
        vlc_vector_foreach(f, &filter->and)
            mpd_filter_free(f);
        vlc_vector_destroy(&filter->and);
        break;
    }
    default:
        break;
    }

    free(filter);
}

#ifdef FILTER_TEST
int main(void) {
    test_parse_quoted();

    return 0;
}
#endif
