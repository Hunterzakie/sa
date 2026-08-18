/*****************************************************************************
 * lyrics.c: test for the lyrics object
 *****************************************************************************
 * Copyright (C) 2026 VLC Authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_lyrics.h>

#include "../../libvlc/test.h"

static void test_empty(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    assert(vlc_lyrics_GetTrackCount(lyrics) == 0);
    /* A track without any line does not survive */
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true) == 0);
    assert(vlc_lyrics_Finish(lyrics) == false);
    assert(vlc_lyrics_GetTrackCount(lyrics) == 0);

    vlc_lyrics_Release(lyrics);
}

static void test_language_normalization(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    /* Case and the "xxx" placeholder must not create distinct tracks */
    ssize_t lower = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(lower == 0);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "ENG", NULL, true) == lower);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "Eng", NULL, true) == lower);

    ssize_t unknown = vlc_lyrics_GetOrCreateTrack(lyrics, "xxx", NULL, true);
    assert(unknown == 1);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, NULL, NULL, true) == unknown);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "", NULL, true) == unknown);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "en", NULL, true) == unknown);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "engl", NULL, true) == unknown);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "e1g", NULL, true) == unknown);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "und", NULL, true) == unknown);

    assert(vlc_lyrics_GetTrackCount(lyrics) == 2);
    assert(strcmp(vlc_lyrics_GetTrack(lyrics, 0)->language, "eng") == 0);
    assert(strcmp(vlc_lyrics_GetTrack(lyrics, 1)->language, "und") == 0);

    vlc_lyrics_Release(lyrics);
}

static void test_track_identity(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    /* The three components of the key each yield a distinct track */
    ssize_t a = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    ssize_t b = vlc_lyrics_GetOrCreateTrack(lyrics, "fra", NULL, true);
    ssize_t c = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", "chords", true);
    ssize_t d = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, false);

    assert(a == 0 && b == 1 && c == 2 && d == 3);
    /* ... and are stable */
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true) == a);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "eng", "chords", true) == c);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, false) == d);

    assert(vlc_lyrics_GetTrack(lyrics, c)->description != NULL);
    assert(strcmp(vlc_lyrics_GetTrack(lyrics, c)->description, "chords") == 0);
    assert(vlc_lyrics_GetTrack(lyrics, a)->description == NULL);

    vlc_lyrics_Release(lyrics);
}

static void test_line_validation(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t timed = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    ssize_t plain = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, false);
    assert(timed >= 0 && plain >= 0);

    /* No text at all is refused */
    assert(vlc_lyrics_AddLine(lyrics, timed, NULL, VLC_TICK_0) == VLC_EINVAL);
    /* So is a negative time */
    assert(vlc_lyrics_AddLine(lyrics, timed, "hey", -1) == VLC_EINVAL);

    /* An empty text is not: it marks a pause rather than something sung, which
     * LRC files use to time the instrumental breaks of a song */
    assert(vlc_lyrics_AddLine(lyrics, timed, "", VLC_TICK_FROM_MS(5000))
           == VLC_SUCCESS);

    /* There is then no text for a word to span */
    static const struct vlc_lyrics_word word = { VLC_TICK_0, 0, 1 };
    assert(vlc_lyrics_AddTimedLine(lyrics, timed, "", VLC_TICK_0, &word, 1)
           == VLC_EINVAL);

    /* A time of zero is a perfectly valid first line */
    assert(vlc_lyrics_AddLine(lyrics, timed, "at zero", 0) == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, timed, "later", VLC_TICK_FROM_MS(1000))
           == VLC_SUCCESS);

    /* An unsynchronized track ignores whatever time is passed */
    assert(vlc_lyrics_AddLine(lyrics, plain, "text", VLC_TICK_FROM_MS(1000))
           == VLC_SUCCESS);

    assert(vlc_lyrics_Finish(lyrics));
    assert(vlc_lyrics_GetTrackCount(lyrics) == 2);

    const struct vlc_lyrics_track *timed_track = vlc_lyrics_GetTrack(lyrics, 0);
    assert(timed_track->synchronized);
    assert(timed_track->count == 3);
    assert(strcmp(timed_track->lines[0].text, "at zero") == 0);
    assert(timed_track->lines[0].time == 0);
    /* The line at zero is reachable, and is current from the very start */
    assert(vlc_lyrics_track_FindLine(timed_track, 0) == 0);
    assert(vlc_lyrics_track_FindLine(timed_track, VLC_TICK_FROM_MS(999)) == 0);
    assert(vlc_lyrics_track_FindLine(timed_track, VLC_TICK_FROM_MS(1000)) == 1);

    /* The pause sorts with the rest and is current like any other line, it
     * simply has nothing to show */
    assert(strcmp(timed_track->lines[1].text, "later") == 0);
    assert(timed_track->lines[2].text[0] == '\0');
    assert(timed_track->lines[2].time == VLC_TICK_FROM_MS(5000));
    assert(timed_track->lines[2].word_count == 0);
    assert(vlc_lyrics_track_FindLine(timed_track, VLC_TICK_FROM_MS(5000)) == 2);

    const struct vlc_lyrics_track *plain_track = vlc_lyrics_GetTrack(lyrics, 1);
    assert(!plain_track->synchronized);
    assert(plain_track->lines[0].time == VLC_TICK_INVALID);

    vlc_lyrics_Release(lyrics);
}

static void test_empty_description(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    /* An empty description must not be a different track than no description */
    ssize_t none = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(none == 0);
    assert(vlc_lyrics_GetOrCreateTrack(lyrics, "eng", "", true) == none);
    assert(vlc_lyrics_GetTrackCount(lyrics) == 1);
    assert(vlc_lyrics_GetTrack(lyrics, 0)->description == NULL);

    vlc_lyrics_Release(lyrics);
}

static void test_finish_drops_empty_tracks(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    /* Empty tracks are dropped and the survivors are compacted in place */
    ssize_t empty1 = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", "empty", true);
    ssize_t kept1 = vlc_lyrics_GetOrCreateTrack(lyrics, "fra", "kept", true);
    ssize_t empty2 = vlc_lyrics_GetOrCreateTrack(lyrics, "deu", "empty", true);
    ssize_t kept2 = vlc_lyrics_GetOrCreateTrack(lyrics, "spa", "kept", false);
    assert(empty1 == 0 && kept1 == 1 && empty2 == 2 && kept2 == 3);

    assert(vlc_lyrics_AddLine(lyrics, kept1, "bonjour", VLC_TICK_FROM_MS(10))
           == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, kept2, "hola", VLC_TICK_INVALID)
           == VLC_SUCCESS);

    assert(vlc_lyrics_Finish(lyrics));
    assert(vlc_lyrics_GetTrackCount(lyrics) == 2);

    const struct vlc_lyrics_track *first = vlc_lyrics_GetTrack(lyrics, 0);
    assert(strcmp(first->language, "fra") == 0);
    assert(strcmp(first->description, "kept") == 0);
    assert(strcmp(first->lines[0].text, "bonjour") == 0);

    const struct vlc_lyrics_track *second = vlc_lyrics_GetTrack(lyrics, 1);
    assert(strcmp(second->language, "spa") == 0);
    assert(strcmp(second->description, "kept") == 0);
    assert(strcmp(second->lines[0].text, "hola") == 0);

    vlc_lyrics_Release(lyrics);
}

static void test_sort(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t idx = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(idx >= 0);

    /* Out of order, with two lines sharing a timestamp */
    assert(vlc_lyrics_AddLine(lyrics, idx, "third", VLC_TICK_FROM_MS(3000))
           == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, idx, "first", VLC_TICK_FROM_MS(1000))
           == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, idx, "second a", VLC_TICK_FROM_MS(2000))
           == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, idx, "second b", VLC_TICK_FROM_MS(2000))
           == VLC_SUCCESS);

    /* Enough lines sharing a single timestamp that an unstable sort would be
     * very unlikely to leave them in the order they were added */
    for (int i = 0; i < 200; i++)
    {
        char text[32];
        sprintf(text, "tie %03d", i);
        assert(vlc_lyrics_AddLine(lyrics, idx, text, VLC_TICK_FROM_MS(2000))
               == VLC_SUCCESS);
    }

    assert(vlc_lyrics_Finish(lyrics));

    const struct vlc_lyrics_track *track = vlc_lyrics_GetTrack(lyrics, 0);
    assert(track->synchronized);
    assert(track->count == 204);
    assert(strcmp(track->lines[0].text, "first") == 0);
    /* The sort must be stable: equal timestamps keep the insertion order */
    assert(strcmp(track->lines[1].text, "second a") == 0);
    assert(strcmp(track->lines[2].text, "second b") == 0);
    for (int i = 0; i < 200; i++)
    {
        char text[32];
        sprintf(text, "tie %03d", i);
        assert(strcmp(track->lines[3 + i].text, text) == 0);
    }
    assert(strcmp(track->lines[203].text, "third") == 0);

    vlc_lyrics_Release(lyrics);
}

static void test_find_line(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t timed = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    ssize_t plain = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, false);
    assert(timed >= 0 && plain >= 0);

    for (int i = 0; i < 5; i++)
        assert(vlc_lyrics_AddLine(lyrics, timed, "line",
                                  VLC_TICK_FROM_MS(1000 * (i + 1)))
               == VLC_SUCCESS);
    assert(vlc_lyrics_AddLine(lyrics, plain, "text", VLC_TICK_INVALID)
           == VLC_SUCCESS);
    assert(vlc_lyrics_Finish(lyrics));

    const struct vlc_lyrics_track *track = vlc_lyrics_GetTrack(lyrics, 0);
    assert(track->synchronized);

    /* Before the first line */
    assert(vlc_lyrics_track_FindLine(track, 0) == -1);
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(999)) == -1);
    /* Exactly on a line, and in between */
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(1000)) == 0);
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(1500)) == 0);
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(3000)) == 2);
    /* Past the last line */
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(5000)) == 4);
    assert(vlc_lyrics_track_FindLine(track, VLC_TICK_FROM_MS(999999)) == 4);

    /* An unsynchronized track has no current line */
    const struct vlc_lyrics_track *plain_track = vlc_lyrics_GetTrack(lyrics, 1);
    assert(!plain_track->synchronized);
    assert(vlc_lyrics_track_FindLine(plain_track, VLC_TICK_FROM_MS(1000)) == -1);

    vlc_lyrics_Release(lyrics);
}

static void test_words(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t idx = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(idx >= 0);

    /* "When the truth" — one word per span, as an A2 line would give */
    static const char *text = "When the truth";
    const struct vlc_lyrics_word words[] = {
        { VLC_TICK_FROM_MS(1000), 0, 4 },   /* When */
        { VLC_TICK_FROM_MS(2000), 5, 3 },   /* the */
        { VLC_TICK_FROM_MS(3000), 9, 5 },   /* truth */
    };

    assert(vlc_lyrics_AddTimedLine(lyrics, idx, text, VLC_TICK_FROM_MS(900),
                                   words, 3) == VLC_SUCCESS);
    /* A line without word timing keeps working */
    assert(vlc_lyrics_AddTimedLine(lyrics, idx, "plain", VLC_TICK_FROM_MS(9000),
                                   NULL, 0) == VLC_SUCCESS);
    assert(vlc_lyrics_Finish(lyrics));

    const struct vlc_lyrics_track *track = vlc_lyrics_GetTrack(lyrics, 0);
    const struct vlc_lyrics_line *line = &track->lines[0];

    assert(line->word_count == 3);
    assert(line->words[1].offset == 5 && line->words[1].length == 3);
    assert(memcmp(line->text + line->words[2].offset, "truth", 5) == 0);

    /* Before the first word, on a word, between words, and past the last */
    assert(vlc_lyrics_line_FindWord(line, 0) == -1);
    assert(vlc_lyrics_line_FindWord(line, VLC_TICK_FROM_MS(999)) == -1);
    assert(vlc_lyrics_line_FindWord(line, VLC_TICK_FROM_MS(1000)) == 0);
    assert(vlc_lyrics_line_FindWord(line, VLC_TICK_FROM_MS(1500)) == 0);
    assert(vlc_lyrics_line_FindWord(line, VLC_TICK_FROM_MS(3000)) == 2);
    assert(vlc_lyrics_line_FindWord(line, VLC_TICK_FROM_MS(99999)) == 2);

    /* A line without words has no current word */
    assert(track->lines[1].word_count == 0);
    assert(track->lines[1].words == NULL);
    assert(vlc_lyrics_line_FindWord(&track->lines[1],
                                    VLC_TICK_FROM_MS(9000)) == -1);

    vlc_lyrics_Release(lyrics);
}

static void test_words_rejected(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t idx = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(idx >= 0);

    static const char *text = "abcdef";
    const vlc_tick_t t = VLC_TICK_FROM_MS(1000);

#define REJECTED(...) do { \
    const struct vlc_lyrics_word bad[] = { __VA_ARGS__ }; \
    assert(vlc_lyrics_AddTimedLine(lyrics, idx, text, t, \
                                   bad, ARRAY_SIZE(bad)) == VLC_EINVAL); \
} while (0)

    REJECTED({ t, 0, 7 });                  /* runs past the text */
    REJECTED({ t, 6, 1 });                  /* starts past the text */
    REJECTED({ t, 0, 0 });                  /* empty span */
    REJECTED({ t, 0, 3 }, { t, 2, 2 });     /* overlapping */
    REJECTED({ t, 3, 3 }, { t, 0, 2 });     /* out of order */
    REJECTED({ VLC_TICK_FROM_MS(2000), 0, 2 },
             { VLC_TICK_FROM_MS(1000), 2, 2 });  /* times going backwards */
    REJECTED({ -1, 0, 2 });                 /* negative time */

    /* A span must not cut a UTF-8 character in half: "é" is two bytes */
    static const char *utf8 = "\xc3\xa9t\xc3\xa9";
    const struct vlc_lyrics_word cut[] = { { t, 0, 1 } };
    assert(vlc_lyrics_AddTimedLine(lyrics, idx, utf8, t, cut, 1)
           == VLC_EINVAL);
    const struct vlc_lyrics_word whole[] = { { t, 0, 2 }, { t, 2, 1 } };
    assert(vlc_lyrics_AddTimedLine(lyrics, idx, utf8, t, whole, 2)
           == VLC_SUCCESS);
#undef REJECTED

    /* Only the valid line made it in */
    assert(vlc_lyrics_Finish(lyrics));
    assert(vlc_lyrics_GetTrack(lyrics, 0)->count == 1);

    vlc_lyrics_Release(lyrics);
}

static void test_refcount(void)
{
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    ssize_t idx = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", NULL, true);
    assert(idx >= 0);
    assert(vlc_lyrics_AddLine(lyrics, idx, "hey", VLC_TICK_0) == VLC_SUCCESS);
    assert(vlc_lyrics_Finish(lyrics));

    assert(vlc_lyrics_Hold(lyrics) == lyrics);
    vlc_lyrics_Release(lyrics);

    /* Still alive and readable through the remaining reference */
    assert(vlc_lyrics_GetTrackCount(lyrics) == 1);
    assert(strcmp(vlc_lyrics_GetTrack(lyrics, 0)->lines[0].text, "hey") == 0);

    vlc_lyrics_Release(lyrics);
}

static void test_many_lines(void)
{
    /* Exercise the growth of both arrays */
    vlc_lyrics_t *lyrics = vlc_lyrics_New();
    assert(lyrics != NULL);

    for (int t = 0; t < 40; t++)
    {
        char description[16];
        sprintf(description, "track%d", t);

        ssize_t idx = vlc_lyrics_GetOrCreateTrack(lyrics, "eng", description,
                                                  true);
        assert(idx == t);

        /* Reversed, so the sort has real work to do */
        for (int i = 1000; i > 0; i--)
            assert(vlc_lyrics_AddLine(lyrics, idx, "line",
                                      VLC_TICK_FROM_MS(i)) == VLC_SUCCESS);
    }

    assert(vlc_lyrics_Finish(lyrics));
    assert(vlc_lyrics_GetTrackCount(lyrics) == 40);

    for (size_t t = 0; t < 40; t++)
    {
        const struct vlc_lyrics_track *track = vlc_lyrics_GetTrack(lyrics, t);
        assert(track->count == 1000);
        for (size_t i = 1; i < track->count; i++)
            assert(track->lines[i - 1].time <= track->lines[i].time);
    }

    vlc_lyrics_Release(lyrics);
}

int main(void)
{
    test_init();

    test_empty();
    test_language_normalization();
    test_track_identity();
    test_empty_description();
    test_finish_drops_empty_tracks();
    test_line_validation();
    test_sort();
    test_find_line();
    test_words();
    test_words_rejected();
    test_refcount();
    test_many_lines();

    return 0;
}
