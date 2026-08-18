/*****************************************************************************
 * lyrics.c: lyrics tracks attached to a media
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include <vlc_common.h>
#include <vlc_atomic.h>
#include <vlc_lyrics.h>
#include <vlc_vector.h>

struct lyrics_track_priv
{
    struct vlc_lyrics_track pub;
    /* pub.lines and pub.count mirror this vector */
    struct VLC_VECTOR(struct vlc_lyrics_line) lines;
};

struct vlc_lyrics
{
    vlc_atomic_rc_t rc;
    struct VLC_VECTOR(struct lyrics_track_priv) tracks;
};

/**
 * Normalize an ISO 639-2 language code.
 */
static char *
lyrics_NormalizeLanguage(const char *language)
{
    char normalized[4];

    if (language == NULL)
        return strdup(VLC_LYRICS_LANGUAGE_UNKNOWN);

    for (size_t i = 0; i < 3; i++)
    {
        unsigned char c = (unsigned char) language[i];

        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c < 'a' || c > 'z')
            return strdup(VLC_LYRICS_LANGUAGE_UNKNOWN);
        normalized[i] = (char) c;
    }
    normalized[3] = '\0';

    /* The string must be exactly three letters long */
    if (language[3] != '\0')
        return strdup(VLC_LYRICS_LANGUAGE_UNKNOWN);

    if (strcmp(normalized, "xxx") == 0)
        return strdup(VLC_LYRICS_LANGUAGE_UNKNOWN);

    return strdup(normalized);
}

static bool
lyrics_DescriptionEqual(const char *a, const char *b)
{
    if (a == NULL || b == NULL)
        return a == b;
    return strcmp(a, b) == 0;
}

static void
lyrics_TrackClean(struct lyrics_track_priv *track)
{
    for (size_t i = 0; i < track->lines.size; i++)
    {
        free(track->lines.data[i].text);
        free(track->lines.data[i].words);
    }
    vlc_vector_destroy(&track->lines);
    free(track->pub.language);
    free(track->pub.description);
}

/**
 * Check that a word array describes non-overlapping spans of a text, ordered
 * by time, and that no span cuts a UTF-8 character in half.
 *
 * The words come from a demuxer parsing an untrusted file, and the spans end up
 * being handed to text shaping, so this cannot be left to the producer.
 */
static bool
lyrics_WordsValid(const char *text, const struct vlc_lyrics_word *words,
                  size_t count)
{
    const size_t text_length = strlen(text);
    size_t end = 0;
    vlc_tick_t previous = 0;

    for (size_t i = 0; i < count; i++)
    {
        const struct vlc_lyrics_word *word = &words[i];

        if (word->time < 0 || word->time < previous)
            return false;
        previous = word->time;

        if (word->length == 0
         || word->offset < end
         || word->offset > text_length
         || word->length > text_length - word->offset)
            return false;
        end = word->offset + word->length;

        /* A continuation byte on either edge means the span cuts a character */
        if ((text[word->offset] & 0xC0) == 0x80
         || (end < text_length && (text[end] & 0xC0) == 0x80))
            return false;
    }
    return true;
}

vlc_lyrics_t *
vlc_lyrics_New(void)
{
    vlc_lyrics_t *lyrics = malloc(sizeof (*lyrics));
    if (unlikely(lyrics == NULL))
        return NULL;

    vlc_atomic_rc_init(&lyrics->rc);
    vlc_vector_init(&lyrics->tracks);
    return lyrics;
}

vlc_lyrics_t *
vlc_lyrics_Hold(vlc_lyrics_t *lyrics)
{
    vlc_atomic_rc_inc(&lyrics->rc);
    return lyrics;
}

void
vlc_lyrics_Release(vlc_lyrics_t *lyrics)
{
    if (!vlc_atomic_rc_dec(&lyrics->rc))
        return;

    for (size_t i = 0; i < lyrics->tracks.size; i++)
        lyrics_TrackClean(&lyrics->tracks.data[i]);
    vlc_vector_destroy(&lyrics->tracks);
    free(lyrics);
}

/**
 * Append an empty track, taking ownership of a normalized language
 */
static ssize_t
lyrics_AppendTrack(vlc_lyrics_t *lyrics, char *language,
                   const char *description, bool synchronized)
{
    char *description_copy = NULL;
    if (description != NULL)
    {
        description_copy = strdup(description);
        if (unlikely(description_copy == NULL))
            goto error;
    }

    const struct lyrics_track_priv track = {
        .pub = {
            .language = language,
            .description = description_copy,
            .synchronized = synchronized,
        },
        .lines = VLC_VECTOR_INITIALIZER,
    };
    if (unlikely(!vlc_vector_push(&lyrics->tracks, track)))
        goto error;

    return (ssize_t) lyrics->tracks.size - 1;

error:
    free(description_copy);
    free(language);
    return -1;
}

ssize_t
vlc_lyrics_GetOrCreateTrack(vlc_lyrics_t *lyrics, const char *language,
                            const char *description, bool synchronized)
{
    char *normalized = lyrics_NormalizeLanguage(language);
    if (unlikely(normalized == NULL))
        return -1;

    /* Make empty tracks NULL */
    if (description != NULL && *description == '\0')
        description = NULL;

    for (size_t i = 0; i < lyrics->tracks.size; i++)
    {
        const struct vlc_lyrics_track *track = &lyrics->tracks.data[i].pub;

        if (track->synchronized == synchronized
         && strcmp(track->language, normalized) == 0
         && lyrics_DescriptionEqual(track->description, description))
        {
            free(normalized);
            return (ssize_t) i;
        }
    }

    return lyrics_AppendTrack(lyrics, normalized, description, synchronized);
}

int
vlc_lyrics_AddLine(vlc_lyrics_t *lyrics, size_t index, const char *text,
                   vlc_tick_t time)
{
    return vlc_lyrics_AddTimedLine(lyrics, index, text, time, NULL, 0);
}

int
vlc_lyrics_AddTimedLine(vlc_lyrics_t *lyrics, size_t index, const char *text,
                        vlc_tick_t time,
                        const struct vlc_lyrics_word *words, size_t word_count)
{
    assert(index < lyrics->tracks.size);

    if (text == NULL || time < 0)
        return VLC_EINVAL;

    if (word_count != 0
     && (*text == '\0' || words == NULL
      || !lyrics_WordsValid(text, words, word_count)))
        return VLC_EINVAL;

    struct lyrics_track_priv *track = &lyrics->tracks.data[index];

    if (!track->pub.synchronized)
    {
        time = VLC_TICK_INVALID;
        word_count = 0;
    }

    char *text_copy = strdup(text);
    if (unlikely(text_copy == NULL))
        return VLC_ENOMEM;

    struct vlc_lyrics_word *words_copy = NULL;
    if (word_count != 0)
    {
        words_copy = vlc_alloc(word_count, sizeof (*words_copy));
        if (unlikely(words_copy == NULL))
        {
            free(text_copy);
            return VLC_ENOMEM;
        }
        memcpy(words_copy, words, word_count * sizeof (*words_copy));
    }

    const struct vlc_lyrics_line line = {
        .time = time,
        .text = text_copy,
        .word_count = word_count,
        .words = words_copy,
    };
    if (unlikely(!vlc_vector_push(&track->lines, line)))
    {
        free(words_copy);
        free(text_copy);
        return VLC_ENOMEM;
    }
    track->pub.lines = track->lines.data;
    track->pub.count = track->lines.size;

    return VLC_SUCCESS;
}

struct lyrics_sorted_line
{
    struct vlc_lyrics_line line;
    size_t order;
};

static int
lyrics_LineCompare(const void *lhs, const void *rhs)
{
    const struct lyrics_sorted_line *a = lhs, *b = rhs;

    if (a->line.time != b->line.time)
        return a->line.time < b->line.time ? -1 : 1;
    if (a->order != b->order)
        return a->order < b->order ? -1 : 1;
    return 0;
}

static bool
lyrics_TrackSort(struct vlc_lyrics_track *track)
{
    bool sorted = true;
    for (size_t i = 1; i < track->count && sorted; i++)
        sorted = track->lines[i - 1].time <= track->lines[i].time;

    if (sorted)
        return true;

    struct lyrics_sorted_line *sort =
        vlc_alloc(track->count, sizeof (*sort));
    if (unlikely(sort == NULL))
        return false;

    for (size_t i = 0; i < track->count; i++)
    {
        sort[i].line = track->lines[i];
        sort[i].order = i;
    }

    qsort(sort, track->count, sizeof (*sort), lyrics_LineCompare);

    for (size_t i = 0; i < track->count; i++)
        track->lines[i] = sort[i].line;

    free(sort);
    return true;
}

bool
vlc_lyrics_Finish(vlc_lyrics_t *lyrics)
{
    for (size_t i = 0; i < lyrics->tracks.size;)
    {
        struct lyrics_track_priv *track = &lyrics->tracks.data[i];

        if (track->pub.count == 0
         || (track->pub.synchronized && !lyrics_TrackSort(&track->pub)))
        {
            lyrics_TrackClean(track);
            vlc_vector_remove(&lyrics->tracks, i);
            continue;
        }
        i++;
    }

    return lyrics->tracks.size != 0;
}

size_t
vlc_lyrics_GetTrackCount(const vlc_lyrics_t *lyrics)
{
    return lyrics->tracks.size;
}

const struct vlc_lyrics_track *
vlc_lyrics_GetTrack(const vlc_lyrics_t *lyrics, size_t index)
{
    assert(index < lyrics->tracks.size);
    return &lyrics->tracks.data[index].pub;
}

/**
 * Index of the last entry of a sorted array whose time is lower than or equal
 * to the given one, or -1.
 */
static ssize_t
lyrics_FindTime(const void *base, size_t count, size_t size, vlc_tick_t time)
{
    size_t low = 0, high = count;

    while (low < high)
    {
        size_t mid = low + (high - low) / 2;
        const vlc_tick_t *entry =
            (const vlc_tick_t *) ((const char *) base + mid * size);

        if (*entry <= time)
            low = mid + 1;
        else
            high = mid;
    }

    return low == 0 ? -1 : (ssize_t) (low - 1);
}

ssize_t
vlc_lyrics_track_FindLine(const struct vlc_lyrics_track *track, vlc_tick_t time)
{
    static_assert(offsetof(struct vlc_lyrics_line, time) == 0,
                  "lyrics_FindTime expects the time first");

    if (!track->synchronized)
        return -1;

    return lyrics_FindTime(track->lines, track->count, sizeof (*track->lines),
                           time);
}

ssize_t
vlc_lyrics_line_FindWord(const struct vlc_lyrics_line *line, vlc_tick_t time)
{
    static_assert(offsetof(struct vlc_lyrics_word, time) == 0,
                  "lyrics_FindTime expects the time first");

    return lyrics_FindTime(line->words, line->word_count,
                           sizeof (*line->words), time);
}
