/*****************************************************************************
 * windows_audio_common.h: Windows Audio common code
 *****************************************************************************
 * Copyright (C) 2001-2013 VideoLAN
 *
 * Authors: Denis Charmet <typx@videolan.org>
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
#ifndef DMX_WINDOWS_AUDIO_COMMONS_H
#define DMX_WINDOWS_AUDIO_COMMONS_H

#include <vlc_aout.h>
#include <vlc_codecs.h>

static const uint32_t pi_channels_src[] = {
    WAVE_SPEAKER_FRONT_LEFT, WAVE_SPEAKER_FRONT_RIGHT,
    WAVE_SPEAKER_FRONT_CENTER, WAVE_SPEAKER_LOW_FREQUENCY,
    WAVE_SPEAKER_BACK_LEFT, WAVE_SPEAKER_BACK_RIGHT, WAVE_SPEAKER_BACK_CENTER,
    WAVE_SPEAKER_SIDE_LEFT, WAVE_SPEAKER_SIDE_RIGHT,
    0
};

static const uint32_t pi_channels_aout[] = {
    AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT,
    AOUT_CHAN_CENTER, AOUT_CHAN_LFE,
    AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT, AOUT_CHAN_REARCENTER,
    AOUT_CHAN_MIDDLELEFT, AOUT_CHAN_MIDDLERIGHT,
    0
};

static inline unsigned getChannelMask( uint32_t * wvfextChannelMask, int i_channels, int * i_match )
{
    unsigned i_channel_mask = 0;
    *i_match = 0;
    for( unsigned i = 0;
         i < sizeof(pi_channels_src)/sizeof(*pi_channels_src) &&
         *i_match < i_channels; i++ )
    {
        if( *wvfextChannelMask & pi_channels_src[i] )
        {
            if( !( i_channel_mask & pi_channels_aout[i] ) )
                 *i_match += 1;

            *wvfextChannelMask &= ~pi_channels_src[i];
            i_channel_mask |= pi_channels_aout[i];
        }
    }
    return i_channel_mask;
}

static inline int ParseWaveFmt( vlc_object_t *o,
                                const void *p_data, size_t i_size,
                                es_format_t *p_fmt, const char **ppsz_name )
{
    const uint8_t *data = (const uint8_t *)p_data;
    size_t i_extended = 0;
    uint32_t physical_channels_mask = 0;

    if ( unlikely(i_size < sizeof(WAVEFORMATEX)) )
    {
        msg_Err( o, "missing WAVEFORMATEX" );
        return VLC_EINVAL;
    }
    uint16_t wFormatTag = GetWLE( &data[0] /* wFormatTag */ );
    if( wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        unlikely(i_size < sizeof( WAVEFORMATEXTENSIBLE )) )
    {
        msg_Err( o, "missing WAVEFORMATEXTENSIBLE" );
        return VLC_EINVAL;
    }

    wf_tag_to_fourcc( wFormatTag, &p_fmt->i_codec,
                      &p_fmt->i_original_fourcc, ppsz_name );
    p_fmt->audio.i_channels      = GetWLE ( &data[2] /* nChannels */ );
    p_fmt->audio.i_rate          = GetDWLE( &data[4] /* nSamplesPerSec */ );
    p_fmt->audio.i_blockalign    = GetWLE( &data[12] /* nBlockAlign */ );
    p_fmt->i_bitrate             = GetDWLE( &data[8] /* nAvgBytesPerSec */ ) * 8;
    p_fmt->audio.i_bitspersample = GetWLE( &data[14] /* wBitsPerSample */ );
    if( i_size >= sizeof(WAVEFORMATEX) )
        p_fmt->i_extra = __MIN( GetWLE( &data[16] /* cbSize */ ), i_size - sizeof(WAVEFORMATEX) );
    else if ( GetWLE( &data[16] ) != 0)
    {
        msg_Dbg( o, "extra WAVEFORMATEX missing" );
        return VLC_EINVAL;
    }
    else
        p_fmt->i_extra = 0;

    /* Handle new WAVE_FORMAT_EXTENSIBLE wav files */
    /* see the following link for more information:
     * http://www.microsoft.com/whdc/device/audio/multichaud.mspx#EFAA */
    if( wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
        ( p_fmt->i_extra >= sizeof( WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX ) ) )
    {
        uint32_t dwChannelMask;
        GUID guid_subformat;
        const WAVEFORMATEXTENSIBLE *p_wf_ext = (const WAVEFORMATEXTENSIBLE *)p_data;

        guid_subformat = p_wf_ext->SubFormat;
        guid_subformat.Data1 = GetDWLE( &p_wf_ext->SubFormat.Data1 );
        guid_subformat.Data2 = GetWLE( &p_wf_ext->SubFormat.Data2 );
        guid_subformat.Data3 = GetWLE( &p_wf_ext->SubFormat.Data3 );

        sf_tag_to_fourcc( &guid_subformat, &p_fmt->i_codec, ppsz_name );

        msg_Dbg( o, "extensible format guid " GUID_FMT, GUID_PRINT(guid_subformat) );

        i_extended = sizeof( WAVEFORMATEXTENSIBLE ) - sizeof( WAVEFORMATEX );
        p_fmt->i_extra -= i_extended;

        dwChannelMask = GetDWLE( &p_wf_ext->dwChannelMask );
        if( dwChannelMask )
        {
            int i_match = 0;
            physical_channels_mask = getChannelMask( &dwChannelMask, p_fmt->audio.i_channels, &i_match );
            if( dwChannelMask )
                msg_Warn( o, "Some channels are unrecognized or uselessly specified (0x%x)", dwChannelMask );
            if( i_match < p_fmt->audio.i_channels )
            {
                int i_missing = p_fmt->audio.i_channels - i_match;
                msg_Warn( o, "Trying to fill up unspecified position for %d channels", p_fmt->audio.i_channels - i_match );

                static const uint32_t pi_pair[] = { AOUT_CHAN_REARLEFT|AOUT_CHAN_REARRIGHT,
                                                    AOUT_CHAN_MIDDLELEFT|AOUT_CHAN_MIDDLERIGHT,
                                                    AOUT_CHAN_LEFT|AOUT_CHAN_RIGHT };
                /* FIXME: Unused yet
                static const uint32_t pi_center[] = { AOUT_CHAN_REARCENTER,
                                                      0,
                                                      AOUT_CHAN_CENTER }; */

                /* Try to complete with pair */
                for( unsigned i = 0; i < ARRAY_SIZE(pi_pair); i++ )
                {
                    if( i_missing >= 2 && !(physical_channels_mask & pi_pair[i] ) )
                    {
                        i_missing -= 2;
                        physical_channels_mask |= pi_pair[i];
                    }
                }
                /* Well fill up with what we can */
                for( unsigned i = 0; i < ARRAY_SIZE(pi_channels_aout) && i_missing > 0; i++ )
                {
                    if( !( physical_channels_mask & pi_channels_aout[i] ) )
                    {
                        physical_channels_mask |= pi_channels_aout[i];
                        i_missing--;

                        if( i_missing <= 0 )
                            break;
                    }
                }

                i_match = p_fmt->audio.i_channels - i_missing;
            }
            if( i_match < p_fmt->audio.i_channels )
            {
                msg_Err( o, "Invalid/unsupported channel mask" );
                physical_channels_mask = 0;
            }
        }
    }

    switch( p_fmt->i_codec )
    {
    case VLC_FOURCC( 'a', 'r', 'a', 'w' ):
    case VLC_FOURCC( 'a', 'f', 'l', 't' ):
    case VLC_FOURCC( 'u', 'l', 'a', 'w' ):
    case VLC_CODEC_ALAW:
    case VLC_CODEC_MULAW:
        p_fmt->i_codec =
            vlc_fourcc_GetCodecAudio( p_fmt->i_codec,
                                        p_fmt->audio.i_bitspersample );
        if( p_fmt->i_codec == 0 ) {
            msg_Err( o, "Unrecognized codec" );
            return VLC_EINVAL;
        }
        break;
    }

    if( p_fmt->i_codec == VLC_CODEC_UNKNOWN )
        msg_Err( o, "Unrecognized wf tag: 0x%x", wFormatTag );

    if( p_fmt->i_extra > 0 )
    {
        p_fmt->p_extra = malloc( p_fmt->i_extra );
        if( unlikely(!p_fmt->p_extra) )
        {
            p_fmt->i_extra = 0;
            return VLC_ENOMEM;
        }
        memcpy( p_fmt->p_extra, (uint8_t *)p_data + sizeof( WAVEFORMATEX ) + i_extended,
                p_fmt->i_extra );
    }

    if( physical_channels_mask == 0 && p_fmt->audio.i_channels > 2
     && p_fmt->audio.i_channels <= AOUT_CHAN_MAX )
    {
        /* A dwChannelMask of 0 tells the audio device to render the first
         * channel to the first port on the device, the second channel to the
         * second port on the device, and so on. pi_default_channels is
         * different than pi_channels_aout. Indeed FLC/FRC must be treated a
         * SL/SR in that case. See "Default Channel Ordering" and "Details
         * about dwChannelMask" from msdn */

        static const uint32_t pi_default_channels[] = {
            AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT, AOUT_CHAN_CENTER,
            AOUT_CHAN_LFE, AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT,
            AOUT_CHAN_MIDDLELEFT, AOUT_CHAN_MIDDLERIGHT, AOUT_CHAN_REARCENTER };

        for( unsigned i = 0; i < p_fmt->audio.i_channels &&
             i < ARRAY_SIZE(pi_default_channels);
             i++ )
            physical_channels_mask |= pi_default_channels[i];
    }

    p_fmt->audio.i_physical_channels = physical_channels_mask;

    return VLC_SUCCESS;
}

#endif /*DMX_WINDOWS_AUDIO_COMMONS_H*/
