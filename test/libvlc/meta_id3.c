/*****************************************************************************
 * meta_id3.c - ID3 helper regression tests
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published
 * by the Free Software Foundation; either version 2.1 of the License, or
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

#include "test.h"

#include <stdint.h>
#include <string.h>

#include "../../modules/meta_engine/ID3Meta.h"

enum
{
    UTF16_LE,
    UTF16_BE,
};

static size_t append_utf16( uint8_t *p_dst, const char *psz, int i_endian,
                            bool b_bom )
{
    size_t i = 0;

    if( b_bom )
    {
        if( i_endian == UTF16_LE )
        {
            p_dst[i++] = 0xFF;
            p_dst[i++] = 0xFE;
        }
        else
        {
            p_dst[i++] = 0xFE;
            p_dst[i++] = 0xFF;
        }
    }

    while( *psz )
    {
        if( i_endian == UTF16_LE )
        {
            p_dst[i++] = (uint8_t) *psz;
            p_dst[i++] = 0x00;
        }
        else
        {
            p_dst[i++] = 0x00;
            p_dst[i++] = (uint8_t) *psz;
        }
        psz++;
    }

    return i;
}

static size_t make_txxx( uint8_t *p_dst, const char *psz_desc, const char *psz_value,
                         int i_endian, bool b_bom )
{
    size_t i = 0;
    p_dst[i++] = 0x01;
    i += append_utf16( &p_dst[i], psz_desc, i_endian, b_bom );
    p_dst[i++] = 0x00;
    p_dst[i++] = 0x00;
    i += append_utf16( &p_dst[i], psz_value, i_endian, b_bom );
    return i;
}

static size_t make_wxxx( uint8_t *p_dst, const char *psz_desc, const char *psz_url,
                         int i_endian, bool b_bom )
{
    size_t i = 0;
    p_dst[i++] = 0x01;
    i += append_utf16( &p_dst[i], psz_desc, i_endian, b_bom );
    p_dst[i++] = 0x00;
    p_dst[i++] = 0x00;
    const size_t i_url_len = strlen( psz_url );
    memcpy( &p_dst[i], psz_url, i_url_len );
    i += i_url_len;
    return i;
}

static void test_txxx( const char *psz_desc, const char *psz_value,
                       int i_endian, bool b_bom )
{
    uint8_t p_buf[256];
    const size_t i_buf = make_txxx( p_buf, psz_desc, psz_value, i_endian, b_bom );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    assert( ID3HandleTag( p_buf, i_buf, VLC_FOURCC('T','X','X','X'), p_meta, NULL ) );

    const char *psz = vlc_meta_GetExtra( p_meta, psz_desc );
    test_log( "+ got extra '%s'='%s', expecting '%s'\n",
              psz_desc, psz ? psz : "(null)", psz_value );
    assert( psz != NULL );
    assert( strcmp( psz, psz_value ) == 0 );

    vlc_meta_Delete( p_meta );
}

static void test_wxxx( const char *psz_desc, const char *psz_url,
                       int i_endian, bool b_bom )
{
    uint8_t p_buf[256];
    const size_t i_buf = make_wxxx( p_buf, psz_desc, psz_url, i_endian, b_bom );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    assert( ID3HandleTag( p_buf, i_buf, VLC_FOURCC('W','X','X','X'), p_meta, NULL ) );

    const char *psz = vlc_meta_Get( p_meta, vlc_meta_ArtworkURL );
    test_log( "+ got artwork URL '%s', expecting '%s'\n",
              psz ? psz : "(null)", psz_url );
    assert( psz != NULL );
    assert( strcmp( psz, psz_url ) == 0 );

    vlc_meta_Delete( p_meta );
}

/* Build a TXXX frame whose description fills the buffer with no NUL
 * terminator. Exercises the i_buf cap in ID3TextFieldLength: the handler
 * must not read past the input and must not store any extra.
 */
static void test_txxx_unterminated( void )
{
    uint8_t p_buf[64];
    size_t i = 0;
    p_buf[i++] = 0x01; /* UTF-16 with BOM */
    p_buf[i++] = 0xFF;
    p_buf[i++] = 0xFE;
    for( int j = 0; j < 10; j++ )
    {
        p_buf[i++] = (uint8_t) ('A' + j);
        p_buf[i++] = 0x00;
    }
    /* deliberately no NUL terminator */

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    ID3HandleTag( p_buf, i, VLC_FOURCC('T','X','X','X'), p_meta, NULL );
    assert( vlc_meta_GetExtraCount( p_meta ) == 0 );

    vlc_meta_Delete( p_meta );
}

/* TXXX with a non-REPLAYGAIN_ description must not be stored as an extra
 * (only REPLAYGAIN_* keys are treated as known-non-binary).
 */
static void test_txxx_non_replaygain( void )
{
    uint8_t p_buf[256];
    const size_t i_buf = make_txxx( p_buf, "MY_CUSTOM_KEY", "value",
                                    UTF16_LE, true );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    ID3HandleTag( p_buf, i_buf, VLC_FOURCC('T','X','X','X'), p_meta, NULL );
    assert( vlc_meta_GetExtra( p_meta, "MY_CUSTOM_KEY" ) == NULL );
    assert( vlc_meta_GetExtraCount( p_meta ) == 0 );

    vlc_meta_Delete( p_meta );
}

/* WXXX URL bytes are ISO-8859-1 per spec; the handler must convert to
 * UTF-8 before storing in vlc_meta. Input has raw 0xE9 ('é' in Latin-1);
 * stored value should have the UTF-8 sequence 0xC3 0xA9.
 */
static void test_wxxx_latin1_url( void )
{
    const char *psz_in  = "https://images.videolan.org/images/goodies/"
                          "Con\xe9" "-Audio-larg\xe9" ".png";
    const char *psz_out = "https://images.videolan.org/images/goodies/"
                          "Con\xc3\xa9" "-Audio-larg\xc3\xa9" ".png";

    uint8_t p_buf[256];
    const size_t i_buf = make_wxxx( p_buf, "artworkURL_front", psz_in,
                                    UTF16_BE, true );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    assert( ID3HandleTag( p_buf, i_buf, VLC_FOURCC('W','X','X','X'), p_meta, NULL ) );

    const char *psz = vlc_meta_Get( p_meta, vlc_meta_ArtworkURL );
    test_log( "+ got artwork URL '%s', expecting '%s'\n",
              psz ? psz : "(null)", psz_out );
    assert( psz != NULL );
    assert( strcmp( psz, psz_out ) == 0 );

    vlc_meta_Delete( p_meta );
}

/* WXXX whose description has no URL after its terminator. The
 * i_desc_len < i_buf guard in ID3LinkFrameTagHandler must prevent
 * any out-of-bounds read; no artwork URL should be stored.
 */
static void test_wxxx_empty_url( void )
{
    uint8_t p_buf[64];
    size_t i = 0;
    p_buf[i++] = 0x01; /* UTF-16 with BOM */
    i += append_utf16( &p_buf[i], "artworkURL_front", UTF16_LE, true );
    p_buf[i++] = 0x00;
    p_buf[i++] = 0x00;
    /* deliberately no URL bytes */

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    ID3HandleTag( p_buf, i, VLC_FOURCC('W','X','X','X'), p_meta, NULL );
    assert( vlc_meta_Get( p_meta, vlc_meta_ArtworkURL ) == NULL );

    vlc_meta_Delete( p_meta );
}

/* TXXX with a single-byte-encoded description (UTF-8, encoding 0x03)
 * exercises the 1-byte-delimiter branch of ID3TextFieldLength.
 */
static void test_txxx_utf8( const char *psz_desc, const char *psz_value )
{
    uint8_t p_buf[256];
    size_t i = 0;
    p_buf[i++] = 0x03; /* UTF-8 */
    memcpy( &p_buf[i], psz_desc, strlen( psz_desc ) );
    i += strlen( psz_desc );
    p_buf[i++] = 0x00;
    memcpy( &p_buf[i], psz_value, strlen( psz_value ) );
    i += strlen( psz_value );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    assert( ID3HandleTag( p_buf, i, VLC_FOURCC('T','X','X','X'), p_meta, NULL ) );

    const char *psz = vlc_meta_GetExtra( p_meta, psz_desc );
    test_log( "+ got extra '%s'='%s', expecting '%s'\n",
              psz_desc, psz ? psz : "(null)", psz_value );
    assert( psz != NULL );
    assert( strcmp( psz, psz_value ) == 0 );

    vlc_meta_Delete( p_meta );
}

/* TXXX with encoding 0x02 (UTF-16BE without BOM) exercises the
 * non-0x01 UTF-16 path in ID3TextConv.
 */
static void test_txxx_utf16be_no_bom( const char *psz_desc, const char *psz_value )
{
    uint8_t p_buf[256];
    size_t i = 0;
    p_buf[i++] = 0x02; /* UTF-16BE without BOM */
    i += append_utf16( &p_buf[i], psz_desc, UTF16_BE, false );
    p_buf[i++] = 0x00;
    p_buf[i++] = 0x00;
    i += append_utf16( &p_buf[i], psz_value, UTF16_BE, false );

    vlc_meta_t *p_meta = vlc_meta_New();
    assert( p_meta != NULL );

    assert( ID3HandleTag( p_buf, i, VLC_FOURCC('T','X','X','X'), p_meta, NULL ) );

    const char *psz = vlc_meta_GetExtra( p_meta, psz_desc );
    test_log( "+ got extra '%s'='%s', expecting '%s'\n",
              psz_desc, psz ? psz : "(null)", psz_value );
    assert( psz != NULL );
    assert( strcmp( psz, psz_value ) == 0 );

    vlc_meta_Delete( p_meta );
}

int main( void )
{
    test_init();

    test_txxx( "REPLAYGAIN_TRACK_GAIN", "+1.50 dB", UTF16_LE, true );
    test_txxx( "REPLAYGAIN_ALBUM_GAIN", "-2.00 dB", UTF16_BE, true );
    test_txxx( "REPLAYGAIN_TRACK_PEAK", "0.9876", UTF16_LE, false );
    test_wxxx( "artworkURL_front",
               "https://images.videolan.org/images/goodies/Cone-Audio-large.png",
               UTF16_BE, true );
    test_wxxx( "artworkURL_back",
               "https://images.videolan.org/images/goodies/Cone-Audio-small.png",
               UTF16_LE, true );
    test_wxxx_latin1_url();
    test_wxxx_empty_url();
    test_txxx_utf8( "REPLAYGAIN_TRACK_GAIN", "+3.25 dB" );
    test_txxx_utf16be_no_bom( "REPLAYGAIN_ALBUM_PEAK", "0.5432" );
    test_txxx_non_replaygain();
    test_txxx_unterminated();

    return 0;
}
