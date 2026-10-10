/*****************************************************************************
 * ID3Meta.h : ID3v2 Meta Helper
 *****************************************************************************
 * Copyright (C) 2016 VLC authors and VideoLAN
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
#ifndef ID3META_H
#define ID3META_H

#include <vlc_meta.h>
#include "ID3Text.h"

#define vlc_meta_extra vlc_meta_Title
static struct
{
    uint32_t i_tag;
    vlc_meta_type_t type;
    const char *psz;
} const ID3_tag_to_metatype[] = {
    { VLC_FOURCC('T', 'A', 'L', 'B'), vlc_meta_Album,       NULL },
    { VLC_FOURCC('T', 'C', 'M', 'P'), vlc_meta_Compilation, NULL },
    { VLC_FOURCC('T', 'D', 'R', 'C'), vlc_meta_Date,        NULL },
    { VLC_FOURCC('T', 'E', 'N', 'C'), vlc_meta_extra,       "Encoder" },
    { VLC_FOURCC('T', 'I', 'T', '2'), vlc_meta_Title,       NULL },
    { VLC_FOURCC('T', 'O', 'P', 'E'), vlc_meta_extra,       "Original Artist" },
    { VLC_FOURCC('T', 'O', 'R', 'Y'), vlc_meta_extra,       "Original Release Year" },
    { VLC_FOURCC('T', 'P', 'E', '1'), vlc_meta_Artist,      NULL },
    { VLC_FOURCC('T', 'P', 'E', '2'), vlc_meta_AlbumArtist, NULL },
    { VLC_FOURCC('T', 'R', 'S', 'N'), vlc_meta_Publisher,   NULL },
    { VLC_FOURCC('T', 'R', 'S', 'O'), vlc_meta_extra,       "Radio Station Owner" },
};
#undef vlc_meta_extra

static bool ID3TextTagHandler( const uint8_t *p_buf, size_t i_buf,
                               vlc_meta_type_t type, const char *psz_extra,
                               vlc_meta_t *p_meta, bool *pb_updated )
{
    if( p_meta == NULL )
        return false;

    char *p_alloc;
    const char *psz = ID3TextConvert( p_buf, i_buf, &p_alloc );
    if( psz && *psz )
    {
        const char *psz_old = ( psz_extra ) ? vlc_meta_GetExtra( p_meta, psz_extra ):
                                              vlc_meta_Get( p_meta, type );
        if( !psz_old || strcmp( psz_old, psz ) )
        {
            if( pb_updated )
                *pb_updated = true;
            if( psz_extra )
                vlc_meta_SetExtra( p_meta, psz_extra, psz );
            else
                vlc_meta_Set( p_meta, type, psz );
        }
    }
    free( p_alloc );

    return (psz != NULL);
}

/**
 * Handle a WXXX (User defined URL link) ID3v2 frame.
 *
 * Only frames whose description starts with "artworkURL_" are
 * recognized; the URL portion is stored as vlc_meta_ArtworkURL.
 */
static bool ID3LinkFrameTagHandler( const uint8_t *p_buf, size_t i_buf,
                                    vlc_meta_t *p_meta, bool *pb_updated )
{
    if( i_buf > 13 && p_meta )
    {
        const uint8_t *p_value;
        size_t i_value;
        char *psz_alloc;
        const char *psz_desc = ID3ParseTextField( p_buf, i_buf,
                                                  &p_value, &i_value,
                                                  &psz_alloc );
        if( psz_desc && i_value > 0 && !strncmp( "artworkURL_", psz_desc, 11 ) )
        {
            /* WXXX URLs are ISO-8859-1 per the ID3v2 spec.
             * vlc_meta requires UTF-8, so convert before storing.
             */
            char *psz_url = FromCharset( "ISO_8859-1", p_value, i_value );
            if( psz_url )
            {
                const char *psz_old = vlc_meta_Get( p_meta, vlc_meta_ArtworkURL );
                if( !psz_old || strcmp( psz_old, psz_url ) )
                {
                    vlc_meta_Set( p_meta, vlc_meta_ArtworkURL, psz_url );
                    if( pb_updated )
                        *pb_updated = true;
                }
                free( psz_url );
            }
        }
        free( psz_alloc );
        return true;
    }
    return false;
}

/**
 * Dispatch an ID3v2 frame to the right handler based on its tag.
 *
 *   WXXX -> ID3LinkFrameTagHandler (artwork URL)
 *   TXXX -> store REPLAYGAIN_* keys as meta extras
 *   Tnnn -> ID3TextTagHandler via the lookup table
 */
static bool ID3HandleTag( const uint8_t *p_buf, size_t i_buf,
                          uint32_t i_tag,
                          vlc_meta_t *p_meta, bool *pb_updated )
{
    if( i_tag == VLC_FOURCC('W', 'X', 'X', 'X') )
    {
        return ID3LinkFrameTagHandler( p_buf, i_buf, p_meta, pb_updated );
    }
    else if( i_tag == VLC_FOURCC('T', 'X', 'X', 'X') )
    {
        const uint8_t *p_value;
        size_t i_value;
        char *psz_alloc_key;
        const char *psz_key = ID3ParseTextField( p_buf, i_buf,
                                                 &p_value, &i_value,
                                                 &psz_alloc_key );
        if( psz_key )
        {
            /* Only set those which are known as non binary */
            if( i_value > 0 && !strncasecmp( psz_key, "REPLAYGAIN_", 11 ) )
            {
                /* The value uses the same encoding as the description. */
                char *psz_alloc_val;
                const char *psz_val = ID3TextConv( p_value, i_value,
                                                   p_buf[0], &psz_alloc_val );
                if( psz_val )
                {
                    vlc_meta_SetExtra( p_meta, psz_key, psz_val );
                    free( psz_alloc_val );
                }
            }
            free( psz_alloc_key );
            return (vlc_meta_GetExtraCount( p_meta ) > 0);
        }
    }
    else if ( ((const char *) &i_tag)[0] == 'T' )
    {
        for( size_t i=0; i<ARRAY_SIZE(ID3_tag_to_metatype); i++ )
        {
            if( ID3_tag_to_metatype[i].i_tag == i_tag )
                return ID3TextTagHandler( p_buf, i_buf,
                                          ID3_tag_to_metatype[i].type,
                                          ID3_tag_to_metatype[i].psz,
                                          p_meta, pb_updated );
        }
    }

    return false;
}

#endif
