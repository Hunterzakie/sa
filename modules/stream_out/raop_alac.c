/*****************************************************************************
 * raop_alac.c: verbatim-ALAC framing helpers for the RAOP sout
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

#include "raop_alac.h"

/* The 23-bit header lives in bits 0..22 of the output bitstream: byte 0
 * holds header bits 0..7, byte 1 holds bits 8..15, byte 2's top 7 bits
 * hold the rest of the header. The PCM payload starts at bitstream bit
 * 23 (i.e. byte 2's LSB) -- so every subsequent output byte takes the low
 * 7 bits of one input byte and the high 1 bit of the next. */
void raop_alac_pack_verbatim( uint8_t *out, const uint8_t *pcm_be )
{
    const size_t n = RAOP_PCM_FRAME_BYTES;
    out[0] = 0x20;
    out[1] = 0x00;
    out[2] = 0x02 | ( pcm_be[0] >> 7 );
    for ( size_t i = 1; i < n; i++ )
    {
        out[2 + i] = (uint8_t)( ( ( pcm_be[i - 1] & 0x7F ) << 1 ) |
                                ( pcm_be[i] >> 7 ) );
    }
    out[2 + n] = (uint8_t)( ( pcm_be[n - 1] & 0x7F ) << 1 );
}

gcry_error_t raop_alac_encrypt( gcry_cipher_hd_t ctx, const uint8_t iv[16],
                                uint8_t *buf, size_t len )
{
    gcry_error_t err = gcry_cipher_reset( ctx );
    if ( err != GPG_ERR_NO_ERROR )
        return err;
    err = gcry_cipher_setiv( ctx, iv, 16 );
    if ( err != GPG_ERR_NO_ERROR )
        return err;
    return gcry_cipher_encrypt( ctx, buf, ( len / 16 ) * 16, NULL, 0 );
}
