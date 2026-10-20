/*****************************************************************************
 * v264.c : IPROBOT input module for vlc
 *****************************************************************************
 * Copyright (C) 2023 VideoLabs, VLC authors and VideoLAN
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
#include <vlc_demux.h>
#include <vlc_plugin.h>
#include <vlc_modules.h>
#include <vlc_vector.h>

/*****************************************************************************
 * Definitions of structures used by this plugin
 *****************************************************************************/

#define V264_DEBUG

typedef struct
{
    vlc_tick_t time;
    uint64_t offset;
} index_t;

typedef struct VLC_VECTOR(index_t) vec_index_t;

struct stream_info
{
    es_out_id_t *id;
    uint8_t type;
};


typedef struct
{
    struct stream_info video, audio;

    vlc_tick_t pcr;

    vec_index_t index;
    uint64_t i_start_offset;
    uint64_t i_end_offset;

    vlc_tick_t i_first_pts;
    vlc_tick_t i_duration;

} demux_sys_t;

#define V264_HEADER_BASE_SIZE      144
#define V264_HEADER_EXTENDED       64
#define V264_SAMPLE_HEADER_BYTES   16

typedef struct
{
    uint8_t type;
    uint64_t pts;
    uint8_t flags;
    uint32_t size;
} sample_header_t;

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/

/*
    {File Header}
    |@000 140
        |@000 04    v264
        |@004 04    Fixed 0x90
        |@008 04    0x103/0x102 version ?
        |@012 04    ????
        |@016 32    metadata C string 0
        |@048 32    metadata C string 1
        |@080 04    ????
        |@084 32    metadata C string 2
        |@116 04    start time in ms
        |@120 04    end time in ms
        |@124 04    ????
        |@128 04    audio rate
        |@132 04    ????
        |@136 04    ????
        |@140 04    ????
        ( version == 0x103 )
            |@144 04    ????
            |@148 04    Samples End offset
            |@152 52    ????
            |@200 08    ???? not used in code header size ?

    {Wrapped Samples}
        |+000 01    Stream type ?
        |+000 03    ??? (Stream type is 32bits?)
        |+000 04    Sample Payload size $VAR
        |+000 04    Sample PTS
        |+000 03    ??
        |+000 01    IFRAME FLAG ?
        |+000 $VAR  Sample Payload

    {Meta structs}
        [0..N]
            |+000 04    tag
            |+000 04    size in bytes
            ( tag == 0xA1 ) // IFrame Index
                | [1..N]
                    |+000 04    Timestamp
                    |+000 04    File offset
            ( tag == 0xA2 ) // ?
                |+000 04    ?
                |+000 04    ?

    File Layout
        | {File Header}
        | {Wrapped samples}
        | {Meta structs}
*/

static int parse_sample_header(const uint8_t p[V264_SAMPLE_HEADER_BYTES], sample_header_t *unit)
{
#ifdef V264_DEBUG
    fprintf(stderr, "sample: ");
    for(int i=0; i<V264_SAMPLE_HEADER_BYTES; i++)
        fprintf(stderr, "%2.2x ", p[i]);
#endif
    unit->type = p[0];
    unit->pts = VLC_TICK_0 + vlc_tick_from_samples( GetDWLE(&p[8]), 1000);
    unit->flags = p[12];
    unit->size = GetDWLE(&p[4]);
#ifdef V264_DEBUG
        fprintf(stderr, "type %"PRIx8" pts %lu sz %u %2.2x\n", unit->type, unit->pts, unit->size, unit->flags);
#endif

    if( unit->size < 8 )
        return VLC_EGENERIC;

    return VLC_SUCCESS;
}

static enum es_format_category_e type_to_category( uint8_t type )
{
    switch( type )
    {
    case 0x02:
        return AUDIO_ES;
    case 0x0D:
        return VIDEO_ES;
    default:
        return UNKNOWN_ES;
    }
}

static struct stream_info * type_to_stream_info( demux_sys_t *p_sys, uint8_t type )
{
    enum es_format_category_e cat = type_to_category( type );
    if( cat == AUDIO_ES )
        return &p_sys->audio;
    if( cat == VIDEO_ES )
        return &p_sys->video;
    return NULL;
}

static int fill_es_format( uint8_t type, es_format_t *fmt )
{
    switch( type )
    {
        case 0x02:
            es_format_Init( fmt, AUDIO_ES, VLC_CODEC_ALAW );
            fmt->audio.i_bitspersample = 8;
            fmt->audio.i_channels = 1;
            fmt->audio.i_rate = 8000;
            break;
        case 0x0D:
            es_format_Init( fmt, VIDEO_ES, VLC_CODEC_HEVC );
            fmt->b_packetized = false;
            break;
        default:
            return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static block_t * read_v264_sample( stream_t *s, uint32_t size )
{
    block_t *p_block = vlc_stream_Block( s, size );
    if( p_block && p_block->i_buffer != size )
    {
        block_Release( p_block );
        p_block = NULL;
    }
    return p_block;
}


static int LoadIndex( stream_t *s, uint64_t offset, vec_index_t *index )
{
    uint64_t i_size;

    if( vlc_stream_GetSize( s, &i_size ) != VLC_SUCCESS ||
        vlc_stream_Seek( s, offset ) != VLC_SUCCESS )
        return VLC_EGENERIC;

    for( int i=0; i<2; i++ )
    {
        if( vlc_stream_Eof( s ) || i_size == vlc_stream_Tell( s ) )
            break;

        uint8_t bytes[8];
        if( vlc_stream_Read( s, bytes, 8 ) != 8 )
            break;

        uint32_t tag = GetDWLE( &bytes[0] );
        uint32_t payloadsize = GetDWLE( &bytes[4] );

        if( tag == 0xA1 )
        {
            msg_Dbg( s, "parsing index @%lu", vlc_stream_Tell(s) );
            if( payloadsize % 8 || payloadsize < 8 )
                break;

            if( index && !vlc_vector_reserve( index, payloadsize / 8 ) )
                break;

            for( ; payloadsize ; payloadsize -= 8 )
            {
                if( vlc_stream_Read( s, bytes, 8 ) != 8 )
                    break; // incomplete header
                index_t entry;
                entry.time = VLC_TICK_0 + vlc_tick_from_samples( GetDWLE( &bytes[0] ), 1000 );
                entry.offset = GetDWLE( &bytes[4] );
                if( index )
                    vlc_vector_push( index, entry );
            }
            return VLC_SUCCESS;
        }
        else if( tag == 0xA2 )
        {
            if( vlc_stream_Read( s, payloadsize, 8 ) != 8 )
                break;
        }
        else break;
    }

    return VLC_EGENERIC;
}

/*****************************************************************************
 * Demux: reads and demuxes data packets
 *****************************************************************************
 * Returns -1 in case of error, 0 in case of EOF, 1 otherwise
 *****************************************************************************/
static int Demux( demux_t *p_demux )
{
    demux_sys_t *p_sys  = p_demux->p_sys;

    if( vlc_stream_Tell( p_demux->s ) >= p_sys->i_end_offset )
        return VLC_DEMUXER_EOF;

    uint8_t sample_header[V264_SAMPLE_HEADER_BYTES];
    if( vlc_stream_Read( p_demux->s, sample_header, V264_SAMPLE_HEADER_BYTES )
            != V264_SAMPLE_HEADER_BYTES )
        return VLC_DEMUXER_EOF;

    sample_header_t unit = {0};
    if( parse_sample_header( sample_header, &unit ) != VLC_SUCCESS )
        return VLC_DEMUXER_EOF;

    struct stream_info *info = type_to_stream_info( p_sys, unit.type );
    if( info && info->id == NULL )
    {
        es_format_t fmt;
        fill_es_format( unit.type, &fmt );
        info->id = es_out_Add( p_demux->out, &fmt );
        es_format_Clean( &fmt );
        info->type = unit.type;
    }

    if ( p_sys->pcr == VLC_TICK_INVALID && unit.pts != VLC_TICK_INVALID )
        es_out_SetPCR( p_demux->out, unit.pts );
    p_sys->pcr = unit.pts;

    int ret;
    block_t *p_block = read_v264_sample( p_demux->s, unit.size - 8 );
    if( p_block )
    {
        if( info && info->type == unit.type && info->id )
        {
            p_block->i_pts = unit.pts;
            if( info == &p_sys->audio )
                p_block->i_dts = p_block->i_pts;
            es_out_Send( p_demux->out, info->id, p_block );
        }
        else block_Release( p_block );
        ret = VLC_DEMUXER_SUCCESS;
    }
    else ret = VLC_DEMUXER_EOF;

    if( p_sys->pcr != VLC_TICK_INVALID )
        es_out_SetPCR( p_demux->out, p_sys->pcr );

    msg_Err(p_demux, "PCR %ld PTS %ld RET %d %p", p_sys->pcr, unit.pts, ret, p_block);
    return ret;
}

static int SeekTo( demux_t *p_demux, vlc_tick_t ts, bool b_precise )
{
    demux_sys_t *p_sys  = p_demux->p_sys;

    if( p_sys->index.size == 0 )
        return VLC_EGENERIC;

    uint64_t offset = p_sys->i_start_offset;
    for( size_t i=0; i<p_sys->index.size; i++ )
    {
        if( p_sys->index.data[i].time > ts )
            break;
        offset = p_sys->index.data[i].offset;
    }

    p_sys->pcr = VLC_TICK_INVALID;
    int ret = vlc_stream_Seek( p_demux->s, offset );
    if( ret == VLC_SUCCESS && b_precise )
        es_out_SetNextDisplayTime( p_demux->out, ts );
    return ret;
}

/*****************************************************************************
 * Control:
 *****************************************************************************/
static int Control( demux_t *p_demux, int i_query, va_list args )
{
    demux_sys_t *p_sys  = p_demux->p_sys;
    switch( i_query )
    {
        case DEMUX_GET_LENGTH:
        {
            *va_arg(args, vlc_tick_t *) = p_sys->i_duration;
            return VLC_SUCCESS;
        }

        case DEMUX_GET_NORMAL_TIME:
        {
            if( p_sys->i_first_pts != VLC_TICK_INVALID )
            {
                *va_arg(args, vlc_tick_t *) = p_sys->i_first_pts;
                return VLC_SUCCESS;
            }
            return VLC_EGENERIC;
        }

        case DEMUX_GET_TIME:
            if( p_sys->pcr == VLC_TICK_INVALID )
                return VLC_EGENERIC;
            *va_arg(args, vlc_tick_t *) = p_sys->pcr;
            return VLC_SUCCESS;

        case DEMUX_SET_TIME:
        {
            vlc_tick_t ts = va_arg(args, vlc_tick_t);
            bool b_precise = va_arg( args, int );
            return SeekTo( p_demux, p_sys->i_first_pts + ts, b_precise );
        }

        case DEMUX_GET_POSITION:
        {
            if( p_sys->i_duration == 0 || p_sys->pcr == VLC_TICK_INVALID )
                return VLC_EGENERIC;
            *va_arg(args, double *) = (p_sys->pcr - p_sys->i_first_pts) / (double) p_sys->i_duration;
            return VLC_SUCCESS;
        }

        case DEMUX_SET_POSITION:
        {
            if( p_sys->i_duration == 0 )
                return VLC_EGENERIC;
            double f = va_arg(args, double);
            bool b_precise = va_arg( args, int );
            return SeekTo( p_demux, p_sys->i_first_pts + p_sys->i_duration * f, b_precise );
        }

        default:
            return demux_vaControlHelper( p_demux->s, 0, -1, -1, -1,
                                          i_query, args );
    }
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( vlc_object_t *p_this )
{
    demux_t *p_demux = (demux_t*) p_this;
    demux_sys_t *p_sys  = p_demux->p_sys;
    vlc_vector_clear( &p_sys->index );
    if( p_sys->video.id )
        es_out_Del( p_demux->out, p_sys->video.id );
    if( p_sys->audio.id )
            es_out_Del( p_demux->out, p_sys->audio.id );
    free( p_sys );
}

/*****************************************************************************
 * Open:
 *****************************************************************************/

static int Open( vlc_object_t * p_this )
{
    demux_t *p_demux = (demux_t*) p_this;
    demux_sys_t *p_sys;

    const uint8_t *p_peek;
    if( vlc_stream_Peek( p_demux->s, &p_peek, 12 ) != 12 )
        return VLC_EGENERIC;

    uint32_t version = GetDWLE(&p_peek[8]);
    if( memcmp( p_peek, "v264", 4 ) ||
       (version ^ 0x103) > 0x01) /* version 103 || 102 */
        return VLC_EGENERIC;

    p_demux->p_sys = p_sys = calloc( 1, sizeof(demux_sys_t) );
    if( !p_sys )
        return VLC_ENOMEM;

    vlc_vector_init( &p_sys->index );

    const uint32_t header_size = V264_HEADER_BASE_SIZE +
                                 (version == 0x103 ? V264_HEADER_EXTENDED : 0);

    if( vlc_stream_Peek( p_demux->s, &p_peek, header_size ) != header_size )
    {
        Close( p_this );
        return VLC_EGENERIC;
    }

    uint32_t starttime = GetDWLE( &p_peek[116] );
    uint32_t endtime = GetDWLE( &p_peek[120] );
    if( endtime > starttime && endtime )
        p_sys->i_duration = vlc_tick_from_samples( endtime - starttime, 1000 );
    p_sys->i_start_offset = header_size;

    if( version == 0x103 )
    {
        p_sys->i_end_offset = GetDWLE( &p_peek[148] );

        uint64_t filesize;
        if( vlc_stream_GetSize( p_demux->s, &filesize ) == VLC_SUCCESS )
        {
            if( p_sys->i_end_offset > filesize )
            {
                Close( p_this );
                return VLC_EGENERIC;
            }
        }
        else filesize = 0;

        if( !p_demux->b_preparsing && vlc_stream_CanSeek( p_demux->s ) && filesize )
        {
            /* Load native index from end of file */
            if( LoadIndex( p_demux->s, p_sys->i_end_offset, &p_sys->index ) != VLC_SUCCESS )
                msg_Warn( p_demux, "Could not load index. Seek will not be possible" );
        }
    }

    if( vlc_stream_Seek( p_demux->s, p_sys->i_start_offset ) != VLC_SUCCESS )
    {
        Close( p_this );
        return VLC_EGENERIC;
    }

    if( p_sys->index.size > 0 )
    {
        p_sys->i_first_pts = p_sys->index.data[0].time;
        p_sys->i_duration = p_sys->index.data[p_sys->index.size -1].time - p_sys->i_first_pts;
    }
    else
    {
        p_sys->i_first_pts = VLC_TICK_INVALID;
        p_sys->i_duration = 0;
    }

    p_demux->pf_demux   = Demux;
    p_demux->pf_control = Control;
    return VLC_SUCCESS;
}


/*****************************************************************************
 * Module descriptor
 *****************************************************************************/

vlc_module_begin ()
    set_shortname( "V264" )
    set_description( N_("V264 IPROBOT Recording") )
    set_capability( "demux", 10 )
    set_subcategory( SUBCAT_INPUT_DEMUX )
    set_callbacks( Open, Close )
vlc_module_end ()
