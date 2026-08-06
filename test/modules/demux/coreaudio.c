/*****************************************************************************
 * coreaudio.c: CoreAudio channel layout tests
 *****************************************************************************
 * Copyright (C) 2026 VideoLabs, VLC authors and VideoLAN
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
#include <vlc_aout.h>

#include "../../../modules/demux/mp4/coreaudio.h"

#define ASSERT(a) do { \
    if( !(a) ) { \
        fprintf( stderr, "failed line %d\n", __LINE__ ); \
        return 1; \
    } \
} while( 0 )

static int CheckLayout( const struct CoreAudio_layout_s *layout,
                        uint16_t expected_bitmap, uint8_t expected_channels,
                        const uint32_t *expected_order,
                        const uint8_t *expected_reorder )
{
    uint32_t order[AOUT_CHAN_MAX + 1];
    uint8_t reorder[AOUT_CHAN_MAX];
    uint16_t bitmap;
    uint8_t channels;

    ASSERT( CoreAudio_Layout_to_vlc( layout, &bitmap, &channels, order )
            == VLC_SUCCESS );
    ASSERT( bitmap == expected_bitmap );
    ASSERT( channels == expected_channels );
    ASSERT( memcmp( order, expected_order,
                    (expected_channels + 1) * sizeof(*order) ) == 0 );

    /* already in VLC order: aout_CheckChannelReorder() reports 0, not a count */
    if( expected_reorder == NULL )
        ASSERT( aout_CheckChannelReorder( order, NULL, bitmap, reorder ) == 0 );
    else
    {
        ASSERT( aout_CheckChannelReorder( order, NULL, bitmap, reorder )
                == expected_channels );
        ASSERT( memcmp( reorder, expected_reorder, expected_channels ) == 0 );
    }
    return 0;
}

static int CheckFailure( enum CoreAudio_Layout tag, uint32_t channels_bitmap )
{
    const struct CoreAudio_layout_s layout = {
        .i_channels_layout_tag = tag,
        .i_channels_bitmap = channels_bitmap,
    };
    uint32_t order[AOUT_CHAN_MAX + 1] = { UINT32_MAX };
    uint16_t bitmap = UINT16_MAX;
    uint8_t channels = UINT8_MAX;

    ASSERT( CoreAudio_Layout_to_vlc( &layout, &bitmap, &channels, order )
            == VLC_EGENERIC );
    ASSERT( bitmap == 0 );
    ASSERT( channels == 0 );
    ASSERT( order[0] == 0 );
    return 0;
}

int main( void )
{
    static const uint32_t mpeg_5_1_a_order[] = {
        AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT, AOUT_CHAN_CENTER, AOUT_CHAN_LFE,
        AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT, 0,
    };
    static const uint8_t mpeg_5_1_a_reorder[] = {
        0, 1, 4, 5, 2, 3,
    };
    static const uint32_t mpeg_7_1_c_order[] = {
        AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT, AOUT_CHAN_CENTER, AOUT_CHAN_LFE,
        AOUT_CHAN_MIDDLELEFT, AOUT_CHAN_MIDDLERIGHT,
        AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT, 0,
    };
    static const uint8_t mpeg_7_1_c_reorder[] = {
        0, 1, 6, 7, 2, 3, 4, 5,
    };
    static const uint32_t wave_7_1_order[] = {
        AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT, AOUT_CHAN_CENTER, AOUT_CHAN_LFE,
        AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT,
        AOUT_CHAN_MIDDLELEFT, AOUT_CHAN_MIDDLERIGHT, 0,
    };
    static const uint8_t wave_7_1_reorder[] = {
        0, 1, 6, 7, 4, 5, 2, 3,
    };
    static const uint32_t dvd_18_order[] = {
        AOUT_CHAN_LEFT, AOUT_CHAN_RIGHT,
        AOUT_CHAN_REARLEFT, AOUT_CHAN_REARRIGHT, AOUT_CHAN_LFE, 0,
    };

    struct CoreAudio_layout_s layout = {
        .i_channels_layout_tag = CoreAudio_Layout_MPEG_5_1_A,
    };
    ASSERT( CheckLayout( &layout, AOUT_CHANS_5_1, 6,
                         mpeg_5_1_a_order, mpeg_5_1_a_reorder ) == 0 );

    layout.i_channels_layout_tag = CoreAudio_Layout_MPEG_7_1_C;
    ASSERT( CheckLayout( &layout, AOUT_CHANS_7_1, 8,
                         mpeg_7_1_c_order, mpeg_7_1_c_reorder ) == 0 );

    layout.i_channels_layout_tag = CoreAudio_Layout_WAVE_7_1;
    ASSERT( CheckLayout( &layout, AOUT_CHANS_7_1, 8,
                         wave_7_1_order, wave_7_1_reorder ) == 0 );

    /* Lc/Rc have no VLC position and land on the middle pair; what is pinned
     * here is that the shared order no longer expands to ten entries. */
    layout.i_channels_layout_tag = CoreAudio_Layout_MPEG_7_1_A;
    ASSERT( CheckLayout( &layout, AOUT_CHANS_7_1, 8,
                         wave_7_1_order, wave_7_1_reorder ) == 0 );

    layout.i_channels_layout_tag = CoreAudio_Layout_DVD_18;
    ASSERT( CheckLayout( &layout, AOUT_CHANS_4_1, 5, dvd_18_order, NULL ) == 0 );

    layout.i_channels_layout_tag = CoreAudio_Layout_BITMAP;
    layout.i_channels_bitmap =
        CoreAudio_Bitmap_LEFT | CoreAudio_Bitmap_RIGHT |
        CoreAudio_Bitmap_CENTER | CoreAudio_Bitmap_LFESCREEN |
        CoreAudio_Bitmap_LEFTSURROUND | CoreAudio_Bitmap_RIGHTSURROUND;
    ASSERT( CheckLayout( &layout, AOUT_CHANS_5_1, 6,
                         mpeg_5_1_a_order, mpeg_5_1_a_reorder ) == 0 );

    ASSERT( CheckFailure( CoreAudio_Layout_BITMAP, 0 ) == 0 );
    ASSERT( CheckFailure( CoreAudio_Layout_BITMAP,
                          CoreAudio_Bitmap_LEFT |
                          CoreAudio_Bitmap_LEFTCENTER ) == 0 );
    ASSERT( CheckFailure( CoreAudio_Layout_BITMAP,
                          CoreAudio_Bitmap_TOPCENTERSURROUND ) == 0 );
    ASSERT( CheckFailure( CoreAudio_Layout_BITMAP, UINT32_C(1) << 31 ) == 0 );
    ASSERT( CheckFailure( CoreAudio_Layout_Unknown, 0 ) == 0 );

    /* a bitmap disagreeing with the tag channel count is dropped, not mapped */
    for( size_t i = 0; i < ARRAY_SIZE(CoreAudio_Layout_mapping); i++ )
    {
        const enum CoreAudio_Layout tag = CoreAudio_Layout_mapping[i].layout;
        const struct CoreAudio_layout_s entry = {
            .i_channels_layout_tag = tag,
        };
        uint32_t order[AOUT_CHAN_MAX + 1];
        uint16_t bitmap;
        uint8_t channels;

        if( CoreAudio_Layout_to_vlc( &entry, &bitmap, &channels, order )
            != VLC_SUCCESS )
        {
            fprintf( stderr, "layout tag %u (%u channels) not translated\n",
                     (unsigned) tag >> 16, (unsigned) tag & 0xFF );
            return 1;
        }
        ASSERT( channels == ((unsigned) tag & 0xFF) );
        ASSERT( bitmap == CoreAudio_Layout_mapping[i].i_vlc_bitmap );
    }
    return 0;
}
