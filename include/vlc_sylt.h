/*****************************************************************************
 * vlc_sylt.h : synchronized lyrics (ID3v2 SYLT) common code
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

#ifndef VLC_SYLT_H
#define VLC_SYLT_H 1

#include <vlc_common.h>
#include <vlc_tick.h>

/**
 * \file vlc_sylt.h
 * \defgroup sylt Synchronized lyrics
 * \ingroup input
 * Typed container for timed lyric lines (e.g. parsed from an ID3v2 SYLT
 * frame). Carried through es_format_t for the audio elementary stream, in the
 * same spirit as ::audio_replay_gain_t.
 *
 * @{
 */

/**
 * A single timed lyric line.
 */
typedef struct
{
    vlc_tick_t  i_time;     /**< playback time at which the line starts */
    char       *psz_text;   /**< UTF-8 lyric text (owned) */
} vlc_sylt_entry_t;

/**
 * Synchronized lyrics: an ordered set of timed lines.
 */
typedef struct
{
    size_t            i_count;     /**< number of entries */
    vlc_sylt_entry_t *p_entries;   /**< array of \ref i_count entries (owned) */
} vlc_sylt_t;

/**
 * Initializes an empty synchronized lyrics structure.
 *
 * \param p_sylt structure to initialize
 */
static inline void vlc_sylt_Init( vlc_sylt_t *p_sylt )
{
    p_sylt->i_count = 0;
    p_sylt->p_entries = NULL;
}

/**
 * Releases the entries held by a synchronized lyrics structure and resets it
 * to the empty state. Safe to call multiple times.
 *
 * \param p_sylt structure to clean (may be NULL)
 */
static inline void vlc_sylt_Clean( vlc_sylt_t *p_sylt )
{
    if( p_sylt == NULL )
        return;

    for( size_t i = 0; i < p_sylt->i_count; i++ )
        free( p_sylt->p_entries[i].psz_text );
    free( p_sylt->p_entries );

    p_sylt->p_entries = NULL;
    p_sylt->i_count = 0;
}

/**
 * Deep-copies a synchronized lyrics structure.
 *
 * \param p_dst destination structure, always initialized by this call
 * \param p_src source structure (may be NULL or empty)
 * \return VLC_SUCCESS on success (including the empty case),
 *         VLC_ENOMEM on allocation failure (p_dst left empty)
 */
static inline int vlc_sylt_Copy( vlc_sylt_t *p_dst, const vlc_sylt_t *p_src )
{
    vlc_sylt_Init( p_dst );

    if( p_src == NULL || p_src->i_count == 0 )
        return VLC_SUCCESS;

    p_dst->p_entries = (vlc_sylt_entry_t *)
                       calloc( p_src->i_count, sizeof(*p_dst->p_entries) );
    if( unlikely(p_dst->p_entries == NULL) )
        return VLC_ENOMEM;

    for( size_t i = 0; i < p_src->i_count; i++ )
    {
        p_dst->p_entries[i].i_time = p_src->p_entries[i].i_time;
        if( p_src->p_entries[i].psz_text != NULL )
        {
            p_dst->p_entries[i].psz_text = strdup( p_src->p_entries[i].psz_text );
            if( unlikely(p_dst->p_entries[i].psz_text == NULL) )
            {
                p_dst->i_count = i; /* free what was copied so far */
                vlc_sylt_Clean( p_dst );
                return VLC_ENOMEM;
            }
        }
    }
    p_dst->i_count = p_src->i_count;
    return VLC_SUCCESS;
}

/**
 * Merges synchronized lyrics: copies the source entries into the destination
 * only when the destination is still empty, mirroring ::replay_gain_Merge.
 *
 * \param p_dst destination structure
 * \param p_src source structure
 */
static inline void vlc_sylt_Merge( vlc_sylt_t *p_dst, const vlc_sylt_t *p_src )
{
    if( p_dst == NULL || p_src == NULL )
        return;

    if( p_dst->i_count == 0 && p_src->i_count > 0 )
        vlc_sylt_Copy( p_dst, p_src );
}

/** @} */

#endif /* VLC_SYLT_H */
