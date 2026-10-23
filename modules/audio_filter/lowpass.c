/*****************************************************************************
 * lowpass.c : lowpass filter
 *****************************************************************************
 * Copyright © 2026 VLC authors and VideoLAN
 *
 * Authors: Alex Park <alexhojinpark@gmail.com>
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

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_aout.h>
#include <math.h>

/*****************************************************************************
 * Structures
 *****************************************************************************/
 typedef struct
{
    float f_alpha;
    float *p_previous_samples;
    size_t i_channels;
} filter_sys_t;

/*****************************************************************************
 * Process
 *****************************************************************************/
static block_t *Process( filter_t *p_filter, block_t *p_block )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    float *p_samples = (float *)p_block->p_buffer;
    unsigned i_samples_count = p_block->i_nb_samples;
    size_t i_channels = p_sys->i_channels;
    float alpha = p_sys->f_alpha;

    // Iterate through samples. 
    // VLC interleaves channels: [L, R, L, R, L, R]
    for( unsigned i = 0; i < i_samples_count; i++ )
    {
        for( unsigned c = 0; c < i_channels; c++ )
        {
            float f_input = p_samples[i * i_channels + c];
            float f_output = ( alpha * f_input ) + 
                             ( ( 1.0f - alpha ) * p_sys->p_previous_samples[c] );
            
            p_samples[i * i_channels + c] = f_output;
            p_sys->p_previous_samples[c] = f_output;
        }
    }
    return p_block;
}

/*****************************************************************************
 * Open
 *****************************************************************************/
static int Open( vlc_object_t *p_this )
{
    filter_t *p_filter = (filter_t *)p_this;

    if( p_filter->fmt_in.audio.i_format != VLC_CODEC_FL32 )
    {
        msg_Err( p_filter, "LPF requires Float32 audio format" );
        return VLC_EGENERIC;
    }
    
    // Allocate the filter structure
    filter_sys_t *p_sys = vlc_obj_calloc( p_this, 1, sizeof( *p_sys ) );
    if ( unlikely(!p_sys) )
        return VLC_ENOMEM;

    p_filter->p_sys = p_sys;
    p_sys->i_channels = aout_FormatNbChannels( &p_filter->fmt_in.audio );

    // Allocate the samples array
    p_sys->p_previous_samples = vlc_obj_calloc( p_this, p_sys->i_channels, sizeof(float) );
    if( unlikely(!p_sys->p_previous_samples) )
        return VLC_ENOMEM;

    // Calculate Alpha
    float f_cutoff = var_InheritFloat( p_filter, "lpf-cutoff" );
    float f_sample_rate = p_filter->fmt_in.audio.i_rate;

    // Guard against invalid cutoff values
    VLC_CLIP(f_cutoff, 1.0f, f_sample_rate / 2.0f);

    float dt = 1.0f / f_sample_rate;
    float rc = 1.0f / ( 2.0f * (float)M_PI * f_cutoff );
    p_sys->f_alpha = dt / ( rc + dt );

    static const struct vlc_filter_operations filter_ops =
        { .filter_audio = Process };
    p_filter->ops = &filter_ops;

    return VLC_SUCCESS;
}

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin()
    set_description( N_("Low Pass Filter") )
    set_capability( "audio filter", 0 )
    set_subcategory( SUBCAT_AUDIO_AFILTER )
    add_float_with_range( "lpf-cutoff", 1000.0, 0.0, 20000.0,
        N_("Cutoff Frequency (Hz)"), NULL)
    set_callback( Open )
vlc_module_end()