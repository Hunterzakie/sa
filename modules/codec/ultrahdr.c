/*****************************************************************************
 * ultrahdr.c: ultraHDR decoder module using libultrahdr
 *****************************************************************************
 * Copyright (C) 2025 VLC authors and VideoLAN
 *
 * Authors: Ojus Chugh <ojuschugh@gmail.com>
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
#include <vlc_codec.h>

#include <ultrahdr_api.h>

typedef struct
{
    uhdr_codec_private_t *p_decoder;
} decoder_sys_t;

static void CloseDecoder(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;
    decoder_sys_t *p_sys = p_dec->p_sys;

    uhdr_release_decoder(p_sys->p_decoder);
    free(p_sys);
}

/* Only RGB output formats are supported for decode; YCbCr (P010, I420) and
 * half-float have no usable VLC equivalent. */
static vlc_fourcc_t UhdrFmtToVlcChroma(uhdr_img_fmt_t fmt)
{
    switch (fmt)
    {
        case UHDR_IMG_FMT_32bppRGBA1010102: return VLC_CODEC_RGBA10LE;
        case UHDR_IMG_FMT_32bppRGBA8888:    return VLC_CODEC_RGBA;
        default:                            return 0;
    }
}

static plane_t MakeSrcPlane(const void *p_pixels, unsigned stride_px,
                             unsigned width_px, unsigned height, int bpp)
{
    return (plane_t){
        .p_pixels        = (uint8_t *)p_pixels,
        .i_lines         = (int)height,
        .i_pitch         = (int)(stride_px * (unsigned)bpp),
        .i_pixel_pitch   = bpp,
        .i_visible_lines = (int)height,
        .i_visible_pitch = (int)(width_px  * (unsigned)bpp),
    };
}

static bool TryOutputFormat(uhdr_codec_private_t *dec,
                            uhdr_img_fmt_t fmt, uhdr_color_transfer_t ct)
{
    if (uhdr_dec_set_out_img_format(dec, fmt).error_code != UHDR_CODEC_OK)
        return false;
    return uhdr_dec_set_out_color_transfer(dec, ct).error_code == UHDR_CODEC_OK;
}

static int DecodeBlock(decoder_t *p_dec, block_t *p_block)
{
    decoder_sys_t *p_sys = p_dec->p_sys;

    if (p_block == NULL)
        return VLCDEC_SUCCESS;

    if (!is_uhdr_image(p_block->p_buffer, p_block->i_buffer))
    {
        msg_Dbg(p_dec, "not a valid UltraHDR image");
        block_Release(p_block);
        return VLCDEC_SUCCESS;
    }

    uhdr_compressed_image_t compressed_img = {
        .data     = p_block->p_buffer,
        .data_sz  = p_block->i_buffer,
        .capacity = p_block->i_buffer,
        .cg       = UHDR_CG_UNSPECIFIED,
        .ct       = UHDR_CT_UNSPECIFIED,
        .range    = UHDR_CR_UNSPECIFIED,
    };

    uhdr_error_info_t status = uhdr_dec_set_image(p_sys->p_decoder, &compressed_img);
    if (status.error_code != UHDR_CODEC_OK)
    {
        msg_Err(p_dec, "could not set image: %s", status.detail);
        goto error;
    }

    /* Prefer PQ then HLG for HDR output; fall back to sRGB if both fail. */
    if (!TryOutputFormat(p_sys->p_decoder,
                         UHDR_IMG_FMT_32bppRGBA1010102, UHDR_CT_PQ) &&
        !TryOutputFormat(p_sys->p_decoder,
                         UHDR_IMG_FMT_32bppRGBA1010102, UHDR_CT_HLG))
    {
        uhdr_reset_decoder(p_sys->p_decoder);
        status = uhdr_dec_set_image(p_sys->p_decoder, &compressed_img);
        if (status.error_code != UHDR_CODEC_OK)
        {
            msg_Err(p_dec, "could not re-set image: %s", status.detail);
            goto error;
        }
        if (!TryOutputFormat(p_sys->p_decoder,
                             UHDR_IMG_FMT_32bppRGBA8888, UHDR_CT_SRGB))
        {
            msg_Err(p_dec, "no supported output format available");
            goto error;
        }
    }

    status = uhdr_decode(p_sys->p_decoder);
    if (status.error_code != UHDR_CODEC_OK)
    {
        msg_Err(p_dec, "decode error: %s", status.detail);
        goto error;
    }

    uhdr_raw_image_t *decoded_img = uhdr_get_decoded_image(p_sys->p_decoder);
    if (decoded_img == NULL)
    {
        msg_Err(p_dec, "no decoded image");
        goto error;
    }

    vlc_fourcc_t vlc_chroma = UhdrFmtToVlcChroma(decoded_img->fmt);
    if (vlc_chroma == 0)
    {
        msg_Err(p_dec, "unsupported output format %d", (int)decoded_img->fmt);
        goto error;
    }

    p_dec->fmt_out.video.i_width          = decoded_img->w;
    p_dec->fmt_out.video.i_visible_width  = decoded_img->w;
    p_dec->fmt_out.video.i_height         = decoded_img->h;
    p_dec->fmt_out.video.i_visible_height = decoded_img->h;
    p_dec->fmt_out.i_codec                = vlc_chroma;
    p_dec->fmt_out.video.i_chroma         = vlc_chroma;

    switch (decoded_img->ct)
    {
        case UHDR_CT_PQ:  p_dec->fmt_out.video.transfer = TRANSFER_FUNC_SMPTE_ST2084; break;
        case UHDR_CT_HLG: p_dec->fmt_out.video.transfer = TRANSFER_FUNC_HLG;          break;
        default:          p_dec->fmt_out.video.transfer = TRANSFER_FUNC_SRGB;         break;
    }

    if (decoder_UpdateVideoFormat(p_dec) != 0)
    {
        msg_Err(p_dec, "cannot update format");
        goto error;
    }

    picture_t *p_pic = decoder_NewPicture(p_dec);
    if (p_pic == NULL)
    {
        msg_Err(p_dec, "out of memory");
        goto error;
    }

    /* All supported output formats are packed (single plane). */
    {
        int bpp = p_pic->p[0].i_pixel_pitch;
        plane_t src = MakeSrcPlane(decoded_img->planes[UHDR_PLANE_PACKED],
                                   decoded_img->stride[UHDR_PLANE_PACKED],
                                   decoded_img->w, decoded_img->h, bpp);
        plane_CopyPixels(&p_pic->p[0], &src);
    }

    p_pic->date          = p_block->i_pts;
    p_pic->b_progressive = true;

    uhdr_reset_decoder(p_sys->p_decoder);
    block_Release(p_block);
    decoder_QueueVideo(p_dec, p_pic);

    return VLCDEC_SUCCESS;

error:
    uhdr_reset_decoder(p_sys->p_decoder);
    block_Release(p_block);
    return VLCDEC_SUCCESS;
}

static int OpenDecoder(vlc_object_t *p_this)
{
    decoder_t *p_dec = (decoder_t *)p_this;

    if (p_dec->fmt_in->i_codec != VLC_CODEC_UHDR)
        return VLC_EGENERIC;

    decoder_sys_t *p_sys = malloc(sizeof(*p_sys));
    if (p_sys == NULL)
        return VLC_ENOMEM;

    p_sys->p_decoder = uhdr_create_decoder();
    if (p_sys->p_decoder == NULL)
    {
        msg_Err(p_dec, "cannot create decoder");
        free(p_sys);
        return VLC_EGENERIC;
    }

    p_dec->p_sys     = p_sys;
    p_dec->pf_decode = DecodeBlock;

    p_dec->fmt_out.video.transfer    = TRANSFER_FUNC_SMPTE_ST2084;
    p_dec->fmt_out.video.space       = COLOR_SPACE_BT2020;
    p_dec->fmt_out.video.primaries   = COLOR_PRIMARIES_BT2020;
    p_dec->fmt_out.video.color_range = COLOR_RANGE_FULL;

    return VLC_SUCCESS;
}

vlc_module_begin()
    set_subcategory(SUBCAT_INPUT_VCODEC)
    set_description(N_("UltraHDR image decoder"))
    set_capability("video decoder", 50)
    set_callbacks(OpenDecoder, CloseDecoder)
    add_shortcut("ultrahdr", "uhdr")
vlc_module_end()
