/*****************************************************************************
 * raop_alac.h: verbatim-ALAC framing helpers for the RAOP sout
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

#ifndef VLC_RAOP_ALAC_H
#define VLC_RAOP_ALAC_H

#include <stddef.h>
#include <stdint.h>

#include <gcrypt.h>

/* 352 samples / packet keeps the verbatim ALAC payload at ~1408 bytes,
 * fitting one IP MTU. Larger chunks (e.g. 4096) work for compressed ALAC
 * but force IP fragmentation that many embedded receivers drop. */
#define RAOP_FRAMES_PER_PACKET 352

/* Verbatim (uncompressed) ALAC frame, stereo 16-bit:
 *   3 bits  channels        = 1  (CPE / stereo)
 *   4 bits  unused          = 0
 *  12 bits  unused          = 0
 *   1 bit   hassize         = 0
 *   2 bits  uncompressed    = 0
 *   1 bit   isnotcompressed = 1
 *   For each sample frame: 16 bits L (BE), 16 bits R (BE)
 * Total: 23 header bits + N*32 sample bits, packed MSB-first.
 * For RAOP_FRAMES_PER_PACKET=352 stereo s16: ceil((23 + 352*32) / 8) = 1411 bytes.
 */
#define RAOP_ALAC_HEADER_BITS 23
#define RAOP_PCM_FRAME_BYTES (RAOP_FRAMES_PER_PACKET * 2 * sizeof(int16_t))
#define RAOP_ALAC_FRAME_BYTES \
    ((RAOP_ALAC_HEADER_BITS + RAOP_FRAMES_PER_PACKET * 32 + 7) / 8)

/* Bit-pack RAOP_PCM_FRAME_BYTES of stereo s16-BE samples at pcm_be into a
 * verbatim ALAC frame of RAOP_ALAC_FRAME_BYTES bytes at out. */
void raop_alac_pack_verbatim( uint8_t *out, const uint8_t *pcm_be );

/* AES-CBC encrypt the first (len/16)*16 bytes of buf in place. ctx must
 * already have the session key set; iv is reset before each call so callers
 * can reuse the same per-session IV across packets. */
gcry_error_t raop_alac_encrypt( gcry_cipher_hd_t ctx, const uint8_t iv[16],
                                uint8_t *buf, size_t len );

#endif
