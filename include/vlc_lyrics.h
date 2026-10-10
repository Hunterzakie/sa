/*****************************************************************************
 * vlc_lyrics.h: lyrics tracks attached to a media
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

#ifndef VLC_LYRICS_H
#define VLC_LYRICS_H 1

#include <vlc_common.h>
#include <vlc_tick.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Language code for undefined language
 */
#define VLC_LYRICS_LANGUAGE_UNKNOWN "und"

/**
 * A timed word inside a lyrics line
 */
struct vlc_lyrics_word
{
    vlc_tick_t time;
    size_t offset;
    size_t length;
};

/**
 * A single lyrics line
 */
struct vlc_lyrics_line
{
    vlc_tick_t time;
    char *text; /** UTF-8 text of the line, never NULL. */
    size_t word_count; /** Number of timed words, or zero when the line has no word timing. */
    /**
     * Timed words, ordered by increasing time and by increasing offset, never
     * overlapping. NULL when @ref word_count is zero.
     */
    struct vlc_lyrics_word *words;
};

/**
 * A lyrics track, defined by its language, its description and whether it is synchronized or not
 */
struct vlc_lyrics_track
{
    /**
     * ISO 639-2 language code, lower case, or
     * VLC_LYRICS_LANGUAGE_UNKNOWN if unknown.
     */
    char *language;
    /**
     * Description of the track, can be NULL.
     */
    char *description;
    /**
     * true if every line of the track has a valid time.
     */
    bool synchronized;
    /** Number of lines, always greater than zero */
    size_t count;
    /** Lines, sorted by increasing time when the track is synchronized */
    struct vlc_lyrics_line *lines;
};

/**
 * Opaque structure holding a list of @ref vlc_lyrics_track
 *
 * @see vlc_lyrics_GetTrackCount()
 * @see vlc_lyrics_GetTrack()
 */
typedef struct vlc_lyrics vlc_lyrics_t;

/**
 * Create an empty lyrics object
 *
 * @return a lyrics object, or NULL on allocation error
 */
VLC_API vlc_lyrics_t *vlc_lyrics_New(void) VLC_USED;

/**
 * Hold a lyrics object
 *
 * @param lyrics a valid lyrics object
 * @return the same instance
 */
VLC_API vlc_lyrics_t *vlc_lyrics_Hold(vlc_lyrics_t *lyrics);

/**
 * Release a previously held lyrics object
 *
 * @param lyrics a valid lyrics object
 */
VLC_API void vlc_lyrics_Release(vlc_lyrics_t *lyrics);

/**
 * Get the index of the track matching a language, a description and a
 * synchronization, creating it if it does not exist yet
 *
 * @param lyrics a lyrics object being built
 * @param language ISO 639-2 language code
 * @param description description of the track, or NULL
 * @param synchronized whether the lines of the track carry a time
 * @return the index of the track, or -1 on allocation error
 */
VLC_API ssize_t vlc_lyrics_GetOrCreateTrack(vlc_lyrics_t *lyrics,
                                            const char *language,
                                            const char *description,
                                            bool synchronized);

/**
 * Append a line to a track
 *
 * Lines may be appended in any order: vlc_lyrics_Finish() sorts them.
 *
 * @param lyrics a lyrics object being built
 * @param index index of a track returned by vlc_lyrics_GetOrCreateTrack()
 * @param text UTF-8 text of the line; it is copied.
 * @param time time of the line, which must not be negative.
 * @return VLC_SUCCESS, VLC_EINVAL if the text is NULL or if the time is
 * negative, or VLC_ENOMEM
 */
VLC_API int vlc_lyrics_AddLine(vlc_lyrics_t *lyrics, size_t index,
                               const char *text, vlc_tick_t time);

/**
 * Append a line carrying word timing
 *
 * @param lyrics a lyrics object being built
 * @param index index of a track returned by vlc_lyrics_GetOrCreateTrack()
 * @param text UTF-8 text of the line
 * @param time time of the line, which must not be negative.
 * @param words word timing, or NULL; the array is copied. Words must be ordered
 * by increasing time and by increasing offset, must not overlap, must stay
 * within the text, and must start and end on UTF-8 character boundaries
 * @param word_count number of words, or zero
 * @return VLC_SUCCESS, VLC_EINVAL if the text is NULL, if it is empty while
 * words are given, if a time is negative, or if the words break any of the
 * rules above, or VLC_ENOMEM
 */
VLC_API int vlc_lyrics_AddTimedLine(vlc_lyrics_t *lyrics, size_t index,
                                    const char *text, vlc_tick_t time,
                                    const struct vlc_lyrics_word *words,
                                    size_t word_count);

/**
 * Finish the construction of a lyrics object
 *
 * This sorts the lines of every synchronized track by increasing time and
 * drops the tracks that ended up without any line, or that could not be sorted
 * for lack of memory. The object must not be modified afterwards.
 *
 * @param lyrics a lyrics object being built
 * @return true if at least one track remains
 */
VLC_API bool vlc_lyrics_Finish(vlc_lyrics_t *lyrics);

/**
 * Get the number of tracks of a lyrics object
 *
 * @param lyrics a valid lyrics object
 */
VLC_API size_t vlc_lyrics_GetTrackCount(const vlc_lyrics_t *lyrics);

/**
 * Get the track at a given index
 *
 * @param lyrics a valid lyrics object
 * @param index index in the range [0; count[
 * @return a valid track (can't be NULL)
 */
VLC_API const struct vlc_lyrics_track *
vlc_lyrics_GetTrack(const vlc_lyrics_t *lyrics, size_t index);

/**
 * Get the index of the line of a track that is current at a given time
 *
 * @param track a valid synchronized track
 * @param time time relative to the beginning of the media
 * @return the index of the last line whose time is lower than or equal to
 * @c time, or -1 if the track is not synchronized or if @c time is before its
 * first line
 */
VLC_API ssize_t
vlc_lyrics_track_FindLine(const struct vlc_lyrics_track *track,
                          vlc_tick_t time);

/**
 * Get the index of the word of a line that is current at a given time
 *
 * @param line a valid line
 * @param time time relative to the beginning of the media
 * @return the index of the last word whose time is lower than or equal to
 * time, or -1 if the line has no word timing or if time is before its
 * first word
 */
VLC_API ssize_t
vlc_lyrics_line_FindWord(const struct vlc_lyrics_line *line, vlc_tick_t time);

/**
 * Wrap a lyrics object into an attachment
 *
 * Lyrics travel through VLC as attachments, from the demuxer that found them
 * (see DEMUX_GET_ATTACHMENTS) to the interfaces showing them. The attachment
 * holds a reference to the lyrics object itself: it carries no data, so the
 * lyrics do not reach a preparser running in another process.
 *
 * @param lyrics a finished lyrics object
 * @return a new attachment, or NULL on allocation error
 */
VLC_API input_attachment_t *
vlc_lyrics_ToAttachment(vlc_lyrics_t *lyrics) VLC_USED;

/**
 * Get the lyrics carried by an attachment
 *
 * Any attachment can be passed: only the ones created by
 * vlc_lyrics_ToAttachment() carry lyrics, whatever the MIME type of the
 * others, so an attachment read from a file is never taken for lyrics.
 *
 * @param attachment a valid attachment
 * @return a new reference to the lyrics, to release with vlc_lyrics_Release(),
 * or NULL if the attachment carries none
 */
VLC_API vlc_lyrics_t *
vlc_lyrics_FromAttachment(const input_attachment_t *attachment) VLC_USED;

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* VLC_LYRICS_H */
