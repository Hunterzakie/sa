/*****************************************************************************
 * display.c: display stream output module
 *****************************************************************************
 * Copyright (C) 2001-2011 VLC authors and VideoLAN
 *
 * Authors: Laurent Aimar <fenrir@via.ecp.fr>
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
#include "config.h"
#endif

#include <vlc_common.h>
#include <vlc_configuration.h>

#include <vlc_decoder.h>
#include <vlc_frame.h>
#include <vlc_list.h>
#include <vlc_plugin.h>
#include <vlc_sout.h>

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
#define AUDIO_TEXT N_( "Enable audio" )
#define AUDIO_LONGTEXT N_( "Enable/disable audio rendering." )
#define VIDEO_TEXT N_( "Enable video" )
#define VIDEO_LONGTEXT N_( "Enable/disable video rendering." )
#define DELAY_TEXT N_( "Delay (ms)" )
#define DELAY_LONGTEXT N_( "Introduces a delay in the display of the stream." )

static int  Open ( vlc_object_t * );
static void Close( sout_stream_t * );

#define SOUT_CFG_PREFIX "sout-display-"

vlc_module_begin()
    set_shortname( N_("Display") )
    set_description( N_("Display stream output") )
    set_capability( "sout output", 50 )
    add_shortcut( "display" )
    set_subcategory( SUBCAT_SOUT_STREAM )

    add_bool( SOUT_CFG_PREFIX "audio", true, AUDIO_TEXT,
              AUDIO_LONGTEXT )
    add_bool( SOUT_CFG_PREFIX "video", true, VIDEO_TEXT,
              VIDEO_LONGTEXT )
    add_integer( SOUT_CFG_PREFIX "delay", 100, DELAY_TEXT,
                 DELAY_LONGTEXT )
    set_callback( Open )
vlc_module_end()


/*****************************************************************************
 * Exported prototypes
 *****************************************************************************/
static const char *const ppsz_sout_options[] = {
    "audio", "video", "delay", NULL
};

typedef struct
{
    bool b_audio;
    bool b_video;

    vlc_tick_t i_delay;
    input_resource_t *p_resource;

    struct vlc_sout_clock_bus *main_clock;
    bool first_pcr_signaled;
    vlc_tick_t start_stream_pcr;
    bool error;
    bool buffering;

    struct vlc_list decoders; /* List of sout_stream_id_sys_t */
} sout_stream_sys_t;

typedef struct
{
    vlc_input_decoder_t *dec;
    vlc_clock_t *clock;
    struct vlc_list node;
    bool waiting;
} sout_stream_id_sys_t;

static void *Add( sout_stream_t *p_stream, const es_format_t *p_fmt, const char *es_id )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;

    if( (p_fmt->i_cat == AUDIO_ES && !p_sys->b_audio) ||
        (p_fmt->i_cat == VIDEO_ES && !p_sys->b_video) )
    {
        return NULL;
    }

    sout_stream_id_sys_t *id = malloc( sizeof(*id) );
    if( unlikely(id == NULL) )
        return NULL;

    id->clock = sout_ClockCreate( p_sys->main_clock, p_fmt );
    if( unlikely(id->clock == NULL) )
    {
        free( id );
        return NULL;
    }

    id->dec = vlc_input_decoder_Create(
        VLC_OBJECT(p_stream), p_fmt, es_id, id->clock, p_sys->p_resource );
    if( id->dec == NULL )
    {
        msg_Err( p_stream, "cannot create decoder for fcc=`%4.4s'",
                 (char*)&p_fmt->i_codec );
        sout_ClockDelete( id->clock );
        free( id );
        return NULL;
    }

    if( p_sys->i_delay != VLC_TICK_INVALID )
        vlc_input_decoder_ChangeDelay( id->dec, p_sys->i_delay );

    /* Only hold the output if we are buffering, since there is no
     * other unblocking signal once started. */
    id->waiting = p_sys->buffering;
    if (id->waiting)
        vlc_input_decoder_StartWait(id->dec);

    vlc_list_append(&id->node, &p_sys->decoders);

    return id;
}

static void Del( sout_stream_t *p_stream, void *id )
{
    (void) p_stream;

    sout_stream_id_sys_t *id_sys = id;
    vlc_list_remove( &id_sys->node );

    if (id_sys->waiting)
        vlc_input_decoder_StopWait(id_sys->dec);

    vlc_input_decoder_Flush(id_sys->dec);
    vlc_input_decoder_Delete( id_sys->dec );
    sout_ClockDelete( id_sys->clock );
    free( id_sys );
}

static int Send( sout_stream_t *p_stream, void *id, vlc_frame_t *frames )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;
    sout_stream_id_sys_t *id_sys = id;

    if( !p_sys->first_pcr_signaled )
    {
        if( !p_sys->error )
        {
            msg_Err( p_stream,
                     "Missing first pcr event. Forced to drop early frames." );
            p_sys->error = true;
        }
        vlc_frame_ChainRelease( frames );
        return VLC_EGENERIC;
    }

    while( frames )
    {
        vlc_frame_t *p_next = frames->p_next;

        frames->p_next = NULL;

        if( id != NULL && frames->i_buffer > 0 )
            vlc_input_decoder_Decode( id_sys->dec, frames, !p_sys->buffering );

        frames = p_next;
    }

    return VLC_SUCCESS;
}

static void Flush( sout_stream_t *p_stream, void *id )
{
    (void) p_stream;

    sout_stream_id_sys_t *id_sys = id;
    vlc_input_decoder_Flush( id_sys->dec );
}

static int Control( sout_stream_t *p_stream, int i_query, va_list args )
{
    switch( i_query )
    {
        case SOUT_STREAM_ID_SPU_HIGHLIGHT:
        {
            sout_stream_id_sys_t *id = va_arg( args, void * );
            const vlc_spu_highlight_t *spu_hl = va_arg( args, const vlc_spu_highlight_t * );
            return vlc_input_decoder_SetSpuHighlight( id->dec, spu_hl );
        }

        case SOUT_STREAM_IS_SYNCHRONOUS:
            *va_arg( args, bool * ) = true;
            break;

        case SOUT_STREAM_SET_BUFFERING:
        {
            sout_stream_sys_t *p_sys = p_stream->p_sys;
            sout_stream_id_sys_t *id_sys;
            bool buffering = va_arg(args, int);
            if (buffering && !p_sys->buffering)
            {
                /* Entering buffering means the input pipeline was flushed
                 * and the timeline restarts from a new origin. This is
                 * similar to discontinuity detection but at the stream
                 * output level. */
                sout_ClockMainReset(p_sys->main_clock);
                p_sys->start_stream_pcr = VLC_TICK_INVALID;

                vlc_list_foreach(id_sys, &p_sys->decoders, node)
                {
                    if (id_sys->waiting)
                        continue;
                    vlc_input_decoder_StartWait(id_sys->dec);
                    id_sys->waiting = true;
                }
            }
            else if (!buffering && p_sys->buffering)
            {
                /* Buffering is finished, let's unblock the decoders. */

                vlc_list_foreach(id_sys, &p_sys->decoders, node)
                {
                    if (id_sys->waiting)
                        vlc_input_decoder_Wait(id_sys->dec);
                }

                /* Much like in the es_out when the buffering is done, restart
                 * the clock on the first PCR we got to initialize the bus. */
                if (p_sys->start_stream_pcr != VLC_TICK_INVALID)
                    sout_ClockMainSetFirstPcr(p_sys->main_clock,
                                              p_sys->start_stream_pcr);

                vlc_list_foreach(id_sys, &p_sys->decoders, node)
                {
                    if (!id_sys->waiting)
                        continue;
                    vlc_input_decoder_StopWait(id_sys->dec);
                    id_sys->waiting = false;
                }
            }
            p_sys->buffering = buffering;
            break;
        }

        default:
           return VLC_EGENERIC;
    }
    return VLC_SUCCESS;
}

static void SetPCR( sout_stream_t *p_stream, vlc_tick_t pcr )
{
    sout_stream_sys_t *sys = p_stream->p_sys;

    sys->first_pcr_signaled = true;

    /* See SOUT_STREAM_SET_BUFFERING. */
    if (sys->start_stream_pcr == VLC_TICK_INVALID)
        sys->start_stream_pcr = pcr;
}

static const struct sout_stream_operations ops = {
    .add = Add,
    .del = Del,
    .send = Send,
    .flush = Flush,
    .control = Control,
    .set_pcr = SetPCR,
    .close = Close,
};

/*****************************************************************************
 * Open:
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    sout_stream_t *p_stream = (sout_stream_t*)p_this;
    sout_stream_sys_t *p_sys;

    p_sys = malloc( sizeof(sout_stream_sys_t) );
    if( p_sys == NULL )
        return VLC_ENOMEM;

    p_sys->p_resource = input_resource_New( p_this );
    if( unlikely(p_sys->p_resource == NULL) )
    {
        free( p_sys );
        return VLC_ENOMEM;
    }

    p_sys->main_clock = sout_ClockMainCreate(p_stream);
    if( unlikely(p_sys->main_clock == NULL) )
    {
        input_resource_Release( p_sys->p_resource );
        free( p_sys );
        return VLC_ENOMEM;
    }

    config_ChainParse( p_stream, SOUT_CFG_PREFIX, ppsz_sout_options,
                       p_stream->p_cfg );

    p_sys->b_audio = var_GetBool( p_stream, SOUT_CFG_PREFIX "audio" );
    p_sys->b_video = var_GetBool( p_stream, SOUT_CFG_PREFIX "video" );
    p_sys->i_delay =
        VLC_TICK_FROM_MS( var_GetInteger(p_stream, SOUT_CFG_PREFIX "delay") );
    p_sys->first_pcr_signaled = false;
    p_sys->start_stream_pcr = VLC_TICK_INVALID;
    p_sys->error = false;
    p_sys->buffering = true;
    vlc_list_init(&p_sys->decoders);

    p_stream->ops = &ops;
    p_stream->p_sys = p_sys;
    return VLC_SUCCESS;
}

/*****************************************************************************
 * Close:
 *****************************************************************************/
static void Close( sout_stream_t *p_stream )
{
    sout_stream_sys_t *p_sys = p_stream->p_sys;

    input_resource_Release( p_sys->p_resource );
    sout_ClockMainDelete( p_sys->main_clock );
    free( p_sys );
}
