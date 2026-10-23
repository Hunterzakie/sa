/*****************************************************************************
 * copy.c
 *****************************************************************************
 * Copyright (C) 2001, 2002, 2006 VLC authors and VideoLAN
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
 *          Eric Petit <titer@videolan.org>
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

/*****************************************************************************
 * Preamble
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_codec.h>
#include <vlc_block.h>
#include <vlc_bits.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
static int  Open ( vlc_object_t * );
static void Close( vlc_object_t * );

vlc_module_begin ()
    set_subcategory( SUBCAT_SOUT_PACKETIZER )
    set_description( N_("Copy packetizer") )
    set_capability( "video packetizer", 1 )
    set_callbacks( Open, Close )
    add_submodule()
        set_capability( "audio packetizer", 1 )
        set_callbacks( Open, Close )
    add_submodule()
        set_capability( "spu packetizer", 1 )
        set_callbacks( Open, Close )
vlc_module_end ()

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
typedef struct
{
    void (*pf_parse)( decoder_t *, block_t * );
} decoder_sys_t;

static block_t *Packetize   ( decoder_t *, block_t ** );

static void ParseWMV3( decoder_t *, block_t * );

/*****************************************************************************
 * Open: probe the packetizer and return score
 *****************************************************************************
 * Tries to launch a decoder and return score so that the interface is able
 * to choose.
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    decoder_t     *p_dec = (decoder_t*)p_this;
    decoder_sys_t *p_sys;

    if( p_dec->fmt_in->i_cat != AUDIO_ES &&
        p_dec->fmt_in->i_cat != VIDEO_ES &&
        p_dec->fmt_in->i_cat != SPU_ES )
    {
        msg_Err( p_dec, "invalid ES type" );
        return VLC_EGENERIC;
    }

    p_dec->p_sys = p_sys = malloc( sizeof(*p_sys) );
    if (unlikely(p_sys == NULL))
        return VLC_ENOMEM;

    switch( p_dec->fmt_in->i_codec )
    {
    case VLC_CODEC_WMV3:
        p_sys->pf_parse = ParseWMV3;
        break;
    default:
        p_sys->pf_parse = NULL;
        break;
    }

    vlc_fourcc_t fcc = p_dec->fmt_in->i_codec;
    /* Fix the value of the fourcc for audio */
    if( p_dec->fmt_in->i_cat == AUDIO_ES )
    {
        fcc = vlc_fourcc_GetCodecAudio( p_dec->fmt_in->i_codec,
                                                     p_dec->fmt_in->audio.i_bitspersample );
        if( !fcc )
        {
            msg_Err( p_dec, "unknown raw audio sample size" );
            free( p_sys );
            return VLC_EGENERIC;
        }
    }

    /* Create the output format */
    es_format_Copy( &p_dec->fmt_out, p_dec->fmt_in );
    p_dec->fmt_out.i_codec = fcc;
    p_dec->pf_packetize = Packetize;
    p_dec->pf_flush = NULL;
    p_dec->pf_get_cc = NULL;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    decoder_t     *p_dec = (decoder_t*)p_this;
    free( p_dec->p_sys );
}

/*****************************************************************************
 * Packetize: packetize an unit (here copy a complete block )
 *****************************************************************************/
static block_t *Packetize ( decoder_t *p_dec, block_t **pp_block )
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    if( pp_block == NULL || *pp_block == NULL )
        return NULL;

    block_t *p_block = *pp_block;
    *pp_block = NULL;

    if( p_block->i_flags&(BLOCK_FLAG_CORRUPTED) )
    {
        block_Release( p_block );
        return NULL;
    }

    if( p_block->i_dts == VLC_TICK_INVALID )
    {
        p_block->i_dts = p_block->i_pts;
    }

    if( p_block->i_dts == VLC_TICK_INVALID )
    {
        msg_Dbg( p_dec, "need valid dts" );
        block_Release( p_block );
        return NULL;
    }

    if( p_block && p_sys->pf_parse )
        p_sys->pf_parse( p_dec, p_block );

    return p_block;
}

/* Parse WMV3 packet and extract frame type information */
static void ParseWMV3( decoder_t *p_dec, block_t *p_block )
{
    bs_t s;

    /* Parse Sequence header */
    bs_init( &s, p_dec->fmt_in->p_extra, p_dec->fmt_in->i_extra );
    if( bs_read( &s, 2 ) == 3 )
        return;
    bs_skip( &s, 22 );
    const bool b_range_reduction = bs_read( &s, 1 );
    const bool b_has_frames = bs_read( &s, 3 ) > 0;
    bs_skip( &s, 2 );
    const bool b_frame_interpolation = bs_read( &s, 1 );
    if( bs_eof( &s ) )
        return;

    /* Parse frame type */
    bs_init( &s, p_block->p_buffer, p_block->i_buffer );
    bs_skip( &s, b_frame_interpolation +
                 2 +
                 b_range_reduction );

    p_block->i_flags &= ~BLOCK_FLAG_TYPE_MASK;
    if( bs_read( &s, 1 ) )
        p_block->i_flags |= BLOCK_FLAG_TYPE_P;
    else if( !b_has_frames || bs_read( &s, 1 ) )
        p_block->i_flags |= BLOCK_FLAG_TYPE_I;
    else
        p_block->i_flags |= BLOCK_FLAG_TYPE_B;
}

