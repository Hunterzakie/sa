/*****************************************************************************
 * croppadd.c: Crop/Padd image filter
 *****************************************************************************
 * Copyright (C) 2008 VLC authors and VideoLAN
 *
 * Authors: Antoine Cellerier <dionoea @t videolan dot org>
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

#include <limits.h> /* INT_MAX */

#include <vlc_common.h>
#include <vlc_configuration.h>
#include <vlc_plugin.h>
#include <vlc_filter.h>
#include <vlc_picture.h>

/****************************************************************************
 * Local prototypes
 ****************************************************************************/
static int  OpenFilter ( filter_t * );

VIDEO_FILTER_WRAPPER(Filter)

#define CROPTOP_TEXT N_( "Pixels to crop from top" )
#define CROPTOP_LONGTEXT N_( \
    "Number of pixels to crop from the top of the image." )
#define CROPBOTTOM_TEXT N_( "Pixels to crop from bottom" )
#define CROPBOTTOM_LONGTEXT N_( \
    "Number of pixels to crop from the bottom of the image." )
#define CROPLEFT_TEXT N_( "Pixels to crop from left" )
#define CROPLEFT_LONGTEXT N_( \
    "Number of pixels to crop from the left of the image." )
#define CROPRIGHT_TEXT N_( "Pixels to crop from right" )
#define CROPRIGHT_LONGTEXT N_( \
    "Number of pixels to crop from the right of the image." )

#define PADDTOP_TEXT N_( "Pixels to padd to top" )
#define PADDTOP_LONGTEXT N_( \
    "Number of pixels to padd to the top of the image after cropping." )
#define PADDBOTTOM_TEXT N_( "Pixels to padd to bottom" )
#define PADDBOTTOM_LONGTEXT N_( \
    "Number of pixels to padd to the bottom of the image after cropping." )
#define PADDLEFT_TEXT N_( "Pixels to padd to left" )
#define PADDLEFT_LONGTEXT N_( \
    "Number of pixels to padd to the left of the image after cropping." )
#define PADDRIGHT_TEXT N_( "Pixels to padd to right" )
#define PADDRIGHT_LONGTEXT N_( \
    "Number of pixels to padd to the right of the image after cropping." )

#define CFG_PREFIX "croppadd-"

/*****************************************************************************
 * Module descriptor
 *****************************************************************************/
vlc_module_begin ()
    set_shortname( N_("Croppadd") )
    set_description( N_("Video cropping filter") )
    set_callback_video_filter( OpenFilter )

    set_subcategory( SUBCAT_VIDEO_VFILTER )

    set_section( N_("Crop"), NULL )
        add_integer_with_range( CFG_PREFIX "croptop", 0, 0, INT_MAX,
                                CROPTOP_TEXT, CROPTOP_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "cropbottom", 0, 0, INT_MAX,
                                CROPBOTTOM_TEXT, CROPBOTTOM_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "cropleft", 0, 0, INT_MAX,
                                CROPLEFT_TEXT, CROPLEFT_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "cropright", 0, 0, INT_MAX,
                                CROPRIGHT_TEXT, CROPRIGHT_LONGTEXT )

    set_section( N_("Padd"), NULL )
        add_integer_with_range( CFG_PREFIX "paddtop", 0, 0, INT_MAX,
                                PADDTOP_TEXT, PADDTOP_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "paddbottom", 0, 0, INT_MAX,
                                PADDBOTTOM_TEXT, PADDBOTTOM_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "paddleft", 0, 0, INT_MAX,
                                PADDLEFT_TEXT, PADDLEFT_LONGTEXT )
        add_integer_with_range( CFG_PREFIX "paddright", 0, 0, INT_MAX,
                                PADDRIGHT_TEXT, PADDRIGHT_LONGTEXT )
vlc_module_end ()

static const char *const ppsz_filter_options[] = {
    "croptop", "cropbottom", "cropleft", "cropright",
    "paddtop", "paddbottom", "paddleft", "paddright",
    NULL
};

typedef struct
{
    int i_croptop;
    int i_cropbottom;
    int i_cropleft;
    int i_cropright;
    int i_paddtop;
    int i_paddbottom;
    int i_paddleft;
    int i_paddright;
    vlc_fourcc_t i_chroma;
    unsigned pixel_size;
    bool b_bigendian;
    uint16_t padd_color[4];
} filter_sys_t;

/* Picture byte order follows the L/B suffix convention, P010/P012/P016
 * are little-endian MSB-aligned. Keep in sync with src/misc/fourcc.c. */
static bool CroppaddIsBigEndian( vlc_fourcc_t fcc )
{
    switch( fcc )
    {
        case VLC_CODEC_GBR_PLANAR_9B:
        case VLC_CODEC_GBR_PLANAR_10B:
        case VLC_CODEC_GBR_PLANAR_12B:
        case VLC_CODEC_GBR_PLANAR_14B:
        case VLC_CODEC_GBR_PLANAR_16B:
        case VLC_CODEC_GBRA_PLANAR_10B:
        case VLC_CODEC_GBRA_PLANAR_12B:
        case VLC_CODEC_GBRA_PLANAR_16B:
        case VLC_CODEC_I420_9B:
        case VLC_CODEC_I420_10B:
        case VLC_CODEC_I420_12B:
        case VLC_CODEC_I420_16B:
        case VLC_CODEC_I422_9B:
        case VLC_CODEC_I422_10B:
        case VLC_CODEC_I422_12B:
        case VLC_CODEC_I422_16B:
        case VLC_CODEC_I444_9B:
        case VLC_CODEC_I444_10B:
        case VLC_CODEC_I444_12B:
        case VLC_CODEC_I444_16B:
        case VLC_CODEC_YUVA_444_10B:
        case VLC_CODEC_YUVA_444_12B:
        case VLC_CODEC_GREY_10B:
        case VLC_CODEC_GREY_12B:
        case VLC_CODEC_GREY_16B:
            return true;
        default:
            return false;
    }
}

static void CroppaddFill( uint8_t *dst, ptrdiff_t pixels,
                          uint16_t value, unsigned pixel_size,
                          bool b_bigendian )
{
    if( pixels <= 0 )
        return;
    if( pixel_size == 1 )
    {
        memset( dst, (uint8_t)value, pixels );
        return;
    }
    if( value == 0 )
    {
        memset( dst, 0, pixels * pixel_size );
        return;
    }
    if( b_bigendian )
    {
        for( ptrdiff_t i = 0; i < pixels; i++ )
        {
            dst[0] = value >> 8;
            dst[1] = value & 0xff;
            dst += 2;
        }
        return;
    }
    for( ptrdiff_t i = 0; i < pixels; i++ )
    {
        dst[0] = value & 0xff;
        dst[1] = value >> 8;
        dst += 2;
    }
}

#define IDX_TOP 0
#define IDX_LEFT 1
#define IDX_BOTTOM 2
#define IDX_RIGHT 3

struct transform {
    unsigned idx_top;
    unsigned idx_left;
    /* idx_bottom is idx_top XOR 2
       idx_right is idx_left XOR 2 */
};

static const struct transform transforms[8] = {
    [ORIENT_TOP_LEFT]     = { IDX_TOP,    IDX_LEFT },
    [ORIENT_TOP_RIGHT]    = { IDX_TOP,    IDX_RIGHT },
    [ORIENT_BOTTOM_LEFT]  = { IDX_BOTTOM, IDX_LEFT },
    [ORIENT_BOTTOM_RIGHT] = { IDX_BOTTOM, IDX_RIGHT },
    [ORIENT_LEFT_TOP]     = { IDX_LEFT,   IDX_TOP },
    [ORIENT_LEFT_BOTTOM]  = { IDX_LEFT,   IDX_BOTTOM },
    [ORIENT_RIGHT_TOP]    = { IDX_RIGHT,  IDX_TOP },
    [ORIENT_RIGHT_BOTTOM] = { IDX_RIGHT,  IDX_BOTTOM },
};

/*****************************************************************************
 * OpenFilter: probe the filter and return score
 *****************************************************************************/
static int OpenFilter( filter_t *p_filter )
{
    filter_sys_t *p_sys;

    if( !p_filter->b_allow_fmt_out_change )
    {
        msg_Err( p_filter, "Picture format change isn't allowed" );
        return VLC_EGENERIC;
    }

    if( !video_format_IsSameChroma( &p_filter->fmt_in.video,
                                    &p_filter->fmt_out.video ) )
    {
        msg_Err( p_filter, "Input and output chromas don't match" );
        /* In fact we don't really care about this since we're allowed
         * to change the output format ... FIXME? */
        return VLC_EGENERIC;
    }

    const vlc_chroma_description_t *p_chroma =
        vlc_fourcc_GetChromaDescription( p_filter->fmt_in.video.i_chroma );
    assert( p_chroma != NULL );
    if( p_chroma->plane_count == 0 )
    {
        msg_Err( p_filter, "Unsupported input chroma %4.4s", (char*)&p_chroma->fcc );
        return VLC_EGENERIC;
    }
    if( p_chroma->subtype == VLC_CHROMA_SUBTYPE_OTHER )
    {
        msg_Err( p_filter, "Unsupported input chroma %4.4s", (char*)&p_chroma->fcc );
        return VLC_EGENERIC;
    }
    if( vlc_chroma_description_IsYUV( p_chroma )
     && p_chroma->pixel_size != 1 && p_chroma->pixel_size != 2 )
    {
        msg_Err( p_filter, "Unsupported packed high bit-depth chroma %4.4s",
                 (char*)&p_chroma->fcc );
        return VLC_EGENERIC;
    }
    if( p_chroma->plane_count == 1 && p_chroma->pixel_size == 2
     && p_chroma->color_bits > 8
     && p_chroma->subtype != VLC_CHROMA_SUBTYPE_GREY
     && p_chroma->subtype != VLC_CHROMA_SUBTYPE_RGB )
    {
        msg_Err( p_filter, "Unsupported packed high bit-depth chroma %4.4s",
                 (char*)&p_chroma->fcc );
        return VLC_EGENERIC;
    }
    if( p_chroma->pixel_size == 2
     && (p_chroma->pixel_bits < 9 || p_chroma->pixel_bits > 16) )
    {
        msg_Err( p_filter, "Unsupported bit-depth for chroma %4.4s",
                 (char*)&p_chroma->fcc );
        return VLC_EGENERIC;
    }

    p_filter->p_sys = vlc_obj_malloc( VLC_OBJECT(p_filter), sizeof( filter_sys_t ) );
    if( unlikely(!p_filter->p_sys) ) return VLC_ENOMEM;

    config_ChainParse( p_filter, CFG_PREFIX, ppsz_filter_options,
                       p_filter->p_cfg );

    p_sys = p_filter->p_sys;
#define GET_OPTION( name ) \
    p_sys->i_ ## name = var_CreateGetInteger( p_filter, CFG_PREFIX #name ); \
    if( p_sys->i_ ## name & 1 ) \
        msg_Warn( p_filter, "Using even values for `" #name "' is recommended" );
    GET_OPTION( croptop )
    GET_OPTION( cropbottom )
    GET_OPTION( cropleft )
    GET_OPTION( cropright )
    GET_OPTION( paddtop )
    GET_OPTION( paddbottom )
    GET_OPTION( paddleft )
    GET_OPTION( paddright )

    video_format_t *fmt = &p_filter->fmt_in.video;
    video_orientation_t orientation = fmt->orientation;
    const struct transform *tx = &transforms[orientation];
    /* In the same order as IDX_ constants values */
    unsigned crop[] = { p_sys->i_croptop, p_sys->i_cropleft, p_sys->i_cropbottom, p_sys->i_cropright };
    unsigned padd[] = { p_sys->i_paddtop, p_sys->i_paddleft, p_sys->i_paddbottom, p_sys->i_paddright };

    p_sys->i_croptop = crop[tx->idx_top];
    p_sys->i_cropleft = crop[tx->idx_left];
    p_sys->i_cropbottom = crop[tx->idx_top ^ 2];
    p_sys->i_cropright = crop[tx->idx_left ^ 2];

    p_sys->i_paddtop = padd[tx->idx_top];
    p_sys->i_paddleft = padd[tx->idx_left];
    p_sys->i_paddbottom = padd[tx->idx_top ^ 2];
    p_sys->i_paddright = padd[tx->idx_left ^ 2];

    /* Derive the per-plane padding color (opaque black) for this chroma. */
    p_sys->i_chroma = p_filter->fmt_in.video.i_chroma;
    p_sys->pixel_size = p_chroma->pixel_size;
    p_sys->b_bigendian = CroppaddIsBigEndian( p_sys->i_chroma );

    if( p_sys->pixel_size == 1 )
    {
        if( vlc_chroma_description_IsYUV( p_chroma )
         && p_chroma->plane_count > 1 )
        {
            p_sys->padd_color[0] = 0x00;
            p_sys->padd_color[1] = 0x80;
            p_sys->padd_color[2] = 0x80;
            p_sys->padd_color[3] = 0xff;
        }
        else
        {
            p_sys->padd_color[0] = 0x00;
            p_sys->padd_color[1] = 0x00;
            p_sys->padd_color[2] = 0x00;
            p_sys->padd_color[3] = 0xff;
        }
    }
    else if( p_sys->pixel_size > 2 )
    {
        p_sys->padd_color[0] = 0x0000;
        p_sys->padd_color[1] = 0x0000;
        p_sys->padd_color[2] = 0x0000;
        p_sys->padd_color[3] = 0x0000;
    }
    else
    {
        unsigned bits = p_chroma->pixel_bits;
        uint16_t mid = 0x8000;
        uint16_t max;
        if( p_sys->i_chroma == VLC_CODEC_P010 )
            max = 0xffc0;
        else if( p_sys->i_chroma == VLC_CODEC_P012 )
            max = 0xfff0;
        else if( p_sys->i_chroma == VLC_CODEC_P016 )
            max = 0xffff;
        else
        {
            mid = (uint16_t)(1u << (bits - 1));
            max = (bits == 16) ? 0xffff : (uint16_t)((1u << bits) - 1);
        }
        if( vlc_chroma_description_IsYUV( p_chroma ) )
        {
            p_sys->padd_color[0] = 0x0000;
            p_sys->padd_color[1] = mid;
            p_sys->padd_color[2] = mid;
            p_sys->padd_color[3] = max;
        }
        else if( p_chroma->subtype == VLC_CHROMA_SUBTYPE_GREY )
        {
            p_sys->padd_color[0] = 0x0000;
            p_sys->padd_color[1] = 0x0000;
            p_sys->padd_color[2] = 0x0000;
            p_sys->padd_color[3] = 0x0000;
        }
        else
        {
            p_sys->padd_color[0] = 0x0000;
            p_sys->padd_color[1] = 0x0000;
            p_sys->padd_color[2] = 0x0000;
            p_sys->padd_color[3] = max;
        }
    }

    p_filter->fmt_out.video.i_height =
    p_filter->fmt_out.video.i_visible_height =
        p_filter->fmt_in.video.i_visible_height
        - p_sys->i_croptop - p_sys->i_cropbottom
        + p_sys->i_paddtop + p_sys->i_paddbottom;

    p_filter->fmt_out.video.i_width =
    p_filter->fmt_out.video.i_visible_width =
        p_filter->fmt_in.video.i_visible_width
        - p_sys->i_cropleft - p_sys->i_cropright
        + p_sys->i_paddleft + p_sys->i_paddright;

    p_filter->ops = &Filter_ops;

    msg_Dbg( p_filter, "Crop: Top: %d, Bottom: %d, Left: %d, Right: %d",
             p_sys->i_croptop, p_sys->i_cropbottom, p_sys->i_cropleft,
             p_sys->i_cropright );
    msg_Dbg( p_filter, "Padd: Top: %d, Bottom: %d, Left: %d, Right: %d",
             p_sys->i_paddtop, p_sys->i_paddbottom, p_sys->i_paddleft,
             p_sys->i_paddright );
    msg_Dbg( p_filter, "%dx%d -> %dx%d",
             p_filter->fmt_in.video.i_width,
             p_filter->fmt_in.video.i_height,
             p_filter->fmt_out.video.i_width,
             p_filter->fmt_out.video.i_height );

    return VLC_SUCCESS;
}

/****************************************************************************
 * Filter: the whole thing
 ****************************************************************************/
static void Filter( filter_t *p_filter, picture_t *p_pic, picture_t *p_outpic )
{
    filter_sys_t *p_sys = p_filter->p_sys;
    int i_width, i_height, i_xcrop, i_ycrop,
        i_outwidth, i_outheight, i_xpadd, i_ypadd;
    const unsigned i_pixel_size = p_sys->pixel_size;
    const bool b_bigendian = p_sys->b_bigendian;

    for( int i_plane = 0; i_plane < p_pic->i_planes; i_plane++ )
    /* p_pic and p_outpic have the same chroma/number of planes but that's
     * about it. */
    {
        plane_t *p_plane = p_pic->p+i_plane;
        plane_t *p_outplane = p_outpic->p+i_plane;
        uint8_t *p_in = p_plane->p_pixels;
        uint8_t *p_out = p_outplane->p_pixels;
        int i_pixel_pitch = p_plane->i_pixel_pitch;
        uint16_t i_padd_color = i_plane > 3 ? p_sys->padd_color[0]
                                            : p_sys->padd_color[i_plane];

        /* These assignments assume that the first plane always has
         * a width and height equal to the picture's */
        i_width =     ( ( p_filter->fmt_in.video.i_visible_width
                          - p_sys->i_cropleft - p_sys->i_cropright )
                        * p_plane->i_visible_pitch )
                      / p_pic->p->i_visible_pitch;
        i_height =    ( ( p_filter->fmt_in.video.i_visible_height
                          - p_sys->i_croptop - p_sys->i_cropbottom )
                        * p_plane->i_visible_lines )
                      / p_pic->p->i_visible_lines;
        i_xcrop =     ( p_sys->i_cropleft * p_plane->i_visible_pitch)
                      / p_pic->p->i_visible_pitch;
        i_ycrop =     ( p_sys->i_croptop * p_plane->i_visible_lines)
                      / p_pic->p->i_visible_lines;
        i_outwidth =  ( p_filter->fmt_out.video.i_visible_width
                        * p_outplane->i_visible_pitch )
                      / p_outpic->p->i_visible_pitch;
        i_outheight = ( p_filter->fmt_out.video.i_visible_height
                        * p_outplane->i_visible_lines )
                      / p_outpic->p->i_visible_lines;
        i_xpadd =     ( p_sys->i_paddleft * p_outplane->i_visible_pitch )
                      / p_outpic->p->i_visible_pitch;
        i_ypadd =     ( p_sys->i_paddtop * p_outplane->i_visible_lines )
                       / p_outpic->p->i_visible_lines;

        /* Crop the top */
        p_in += i_ycrop * p_plane->i_pitch;

        /* Padd on the top */
        CroppaddFill( p_out, (ptrdiff_t)i_ypadd * p_outplane->i_pitch / i_pixel_size,
                      i_padd_color, i_pixel_size, b_bigendian );
        p_out += i_ypadd * p_outplane->i_pitch;

        for( int i_line = 0; i_line < i_height; i_line++ )
        {
            uint8_t *p_in_next = p_in + p_plane->i_pitch;
            uint8_t *p_out_next = p_out + p_outplane->i_pitch;

            /* Crop on the left */
            p_in += i_xcrop * i_pixel_pitch;

            /* Padd on the left */
            CroppaddFill( p_out, i_xpadd, i_padd_color, i_pixel_size,
                          b_bigendian );
            p_out += i_xpadd * i_pixel_pitch;

            /* Copy the image and crop on the right */
            memcpy( p_out, p_in, i_width * i_pixel_pitch );
            p_out += i_width * i_pixel_pitch;
            p_in += i_width * i_pixel_pitch;

            /* Padd on the right */
            CroppaddFill( p_out, i_outwidth - i_xpadd - i_width,
                          i_padd_color, i_pixel_size, b_bigendian );

            /* Got to beginning of the next line */
            p_in = p_in_next;
            p_out = p_out_next;
        }

        /* Padd on the bottom */
        CroppaddFill( p_out,
                      (ptrdiff_t)( i_outheight - i_ypadd - i_height )
                          * p_outplane->i_pitch / i_pixel_size,
                      i_padd_color, i_pixel_size, b_bigendian );
    }
}
