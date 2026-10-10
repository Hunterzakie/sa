/*****************************************************************************
 * ID3Text.h : ID3v2 Text Helper
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
#ifndef ID3TEXT_H
#define ID3TEXT_H

#include <vlc_charset.h>

/**
 * Decode raw bytes to UTF-8 per the ID3v2 encoding byte.
 *
 *   0x00 = ISO-8859-1
 *   0x01 = UTF-16 (with BOM)
 *   0x02 = UTF-16BE (no BOM)
 *   0x03 = UTF-8
 */
static const char * ID3TextConv( const uint8_t *p_buf, size_t i_buf,
                                 uint8_t i_charset, char **ppsz_allocated )
{
    char *p_alloc = NULL;
    const char *psz = p_alloc;
    if( i_buf > 0 && i_charset < 0x04 )
    {
        switch( i_charset )
        {
            case 0x00:
                psz = p_alloc = FromCharset( "ISO_8859-1", p_buf, i_buf );
                break;
            case 0x01:
                /* Encoding 0x01: ID3v2 strings start with a UTF-16 BOM.
                 * - 0xFE 0xFF -> strip BOM, decode as UTF-16BE.
                 * - 0xFF 0xFE -> strip BOM, decode as UTF-16LE.
                 * - No BOM (non-spec frame) -> decode the full buffer as
                 *   UTF-16LE (legacy behaviour for non-spec encoders).
                 * - Strip the BOM here, not via iconv -- otherwise the
                 *   decoded text retains a leading BOM, prefixed on the
                 *   key.
                 */
            {
                const char *psz_codeset = "UTF-16LE";
                size_t i_skip = 0;

                if( i_buf >= 2 )
                {
                    if( !memcmp( p_buf, "\xFE\xFF", 2 ) )
                    {
                        psz_codeset = "UTF-16BE";
                        i_skip = 2;
                    }
                    else if( !memcmp( p_buf, "\xFF\xFE", 2 ) )
                    {
                        i_skip = 2;
                    }
                }

                psz = p_alloc = FromCharset( psz_codeset,
                                             p_buf + i_skip,
                                             i_buf - i_skip );
                break;
            }
            case 0x02:
                psz = p_alloc = FromCharset( "UTF-16BE", p_buf, i_buf );
                break;
            default:
            case 0x03:
                if( p_buf[ i_buf - 1 ] != 0x00 )
                {
                    psz = p_alloc = (char *) malloc( i_buf + 1 );
                    if( p_alloc )
                    {
                        memcpy( p_alloc, p_buf, i_buf );
                        p_alloc[i_buf] = '\0';
                    }
                }
                else
                {
                    psz = (const char *) p_buf;
                }
                break;
        }
    }
    *ppsz_allocated = p_alloc;
    return psz;
}

/**
 * Wrapper for ID3TextConv that takes a buffer starting with the
 * encoding byte (typical layout of an ID3v2 text frame body).
 */
static inline const char * ID3TextConvert( const uint8_t *p_buf, size_t i_buf,
                                           char **ppsz_allocated )
{
    if( i_buf == 0 )
    {
        *ppsz_allocated = NULL;
        return NULL;
    }
    return ID3TextConv( &p_buf[1], i_buf - 1, p_buf[0], ppsz_allocated );
}

/**
 * Terminator width for an ID3v2 string field.
 * 2 for UTF-16 (encoding 0x01/0x02)
 * 1 for ISO-8859-1 (0x00) and UTF-8 (0x03)
 */
static inline size_t ID3TextDelimiterWidth( uint8_t i_charset )
{
    return (i_charset == 0x01 || i_charset == 0x02) ? 2 : 1;
}

/**
 * Byte length of the leading nul-terminated string in p_buf,
 * terminator included. Capped at i_buf if no terminator is found.
 */
static size_t ID3TextFieldLength( const uint8_t *p_buf, size_t i_buf,
                                         uint8_t i_charset )
{
    const size_t i_delim = ID3TextDelimiterWidth( i_charset );
    size_t i_len = 0;

    if( i_delim == 2 )
    {
        while( i_len + 1 < i_buf && (p_buf[i_len] || p_buf[i_len + 1]) )
            i_len += i_delim;
    }
    else
    {
        while( i_len < i_buf && p_buf[i_len] )
            i_len += i_delim;
    }
    i_len += i_delim;

    return i_len > i_buf ? i_buf : i_len;
}

/**
 * Decode the leading nul-terminated string from an ID3 text-frame body.
 *
 * Layout: [encoding][string][terminator][remainder]
 */
static const char * ID3ParseTextField( const uint8_t *p_buf, size_t i_buf,
                                         const uint8_t **pp_value,
                                         size_t *pi_value,
                                         char **ppsz_allocated )
{
    if( i_buf < 1 )
    {
        *ppsz_allocated = NULL;
        *pp_value = p_buf;
        *pi_value = 0;
        return NULL;
    }

    const uint8_t  i_charset = p_buf[0];
    const uint8_t *p_body    = p_buf + 1; /* skip encoding byte */
    const size_t   i_body    = i_buf - 1; /* skip encoding byte */
    const size_t   i_delim   = ID3TextDelimiterWidth( i_charset );
    const size_t   i_field   = ID3TextFieldLength( p_body, i_body, i_charset );
    const size_t   i_text    = i_field >= i_delim ? i_field - i_delim : 0;

    *pp_value = p_body + i_field;
    *pi_value = i_body - i_field;

    return ID3TextConv( p_body, i_text, i_charset, ppsz_allocated );
}

#endif
