/*****************************************************************************
 * yadif.c : tests the yadif deinterlacing filter sse2 and ssse3 optimizations using the checkasm API and verifing it against the C reference implementation.
 *
 *****************************************************************************
 * Copyright (C) 2026 VLC authors and VideoLAN
 *
 * Authors: Mazen Ewiss <mazenrory@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *****************************************************************************/
/*
 * Copyright (C) 2006 Michael Niedermayer <michaelni@gmx.at>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with FFmpeg; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <stdlib.h>
#include <vlc_cpu.h>
#include <checkasm/checkasm.h>
#include <checkasm/test.h>
#include <checkasm/utils.h>
#include "../../modules/video_filter/deinterlace/common.h"
#include "../../modules/video_filter/deinterlace/yadif.h"

#define MAX_WIDTH 256
#define STRIDE (MAX_WIDTH)
#define LINES 16

void test_yadif(void);

void test_yadif(void)
{
    BUF_RECT(uint8_t, next, STRIDE, LINES);
    BUF_RECT(uint8_t, cur, STRIDE, LINES);
    BUF_RECT(uint8_t, prev, STRIDE, LINES);
    BUF_RECT(uint8_t, ref_dst, STRIDE, LINES);
    BUF_RECT(uint8_t, opt_dst, STRIDE, LINES);

#if (defined(__i386__) || defined(__x86_64__))
    if (checkasm_get_cpu_flags() & VLC_CPU_SSE2)
    {
        declare_func(void, uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int prefs, int mrefs, int parity, int mode);
        if (checkasm_check_func(vlcpriv_yadif_filter_line_sse2, "yadif_filter_line_sse2"))
        {
            for (int i = 8; i <= MAX_WIDTH; i *= 2)
            {
                for (int j = 0; j < 2; j++)
                {
                    for (int k = 0; k < 3; k += 2)
                    {

                        INITIALIZE_BUF_RECT(next);
                        INITIALIZE_BUF_RECT(cur);
                        INITIALIZE_BUF_RECT(prev);
                        CLEAR_BUF_RECT(ref_dst);
                        CLEAR_BUF_RECT(opt_dst);
                        uint8_t *cur_mid = cur + (cur_stride * (LINES / 2));
                        uint8_t *prev_mid = prev + (prev_stride * (LINES / 2));
                        uint8_t *next_mid = next + (next_stride * (LINES / 2));
                        uint8_t *ref_dst_mid = ref_dst + (ref_dst_stride * (LINES / 2));
                        uint8_t *opt_dst_mid = opt_dst + (opt_dst_stride * (LINES / 2));
                        yadif_filter_line_c(ref_dst_mid, prev_mid, cur_mid, next_mid, i, cur_stride, -cur_stride, j, k);
                        call_new(opt_dst_mid, prev_mid, cur_mid, next_mid, i, cur_stride, -cur_stride, j, k);
                        checkasm_check1d(uint8_t, ref_dst_mid, opt_dst_mid, i, "dst");
                        bench_new(opt_dst_mid, prev_mid, cur_mid, next_mid,
                                  i, cur_stride, -cur_stride, j, k);
                    }
                }
            }
        }
    }
    if(checkasm_get_cpu_flags() & VLC_CPU_SSSE3)
    {
        declare_func(void, uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int prefs, int mrefs, int parity, int mode);
        if (checkasm_check_func(vlcpriv_yadif_filter_line_ssse3, "yadif_filter_line_ssse3"))
        {
            for (int i = 8; i <= MAX_WIDTH; i *= 2)
            {
                for (int j = 0; j < 2; j++)
                {
                    for (int k = 0; k < 3; k += 2)
                    {

                        INITIALIZE_BUF_RECT(next);
                        INITIALIZE_BUF_RECT(cur);
                        INITIALIZE_BUF_RECT(prev);
                        CLEAR_BUF_RECT(ref_dst);
                        CLEAR_BUF_RECT(opt_dst);
                        uint8_t *cur_mid = cur + (cur_stride * (LINES / 2));
                        uint8_t *prev_mid = prev + (prev_stride * (LINES / 2));
                        uint8_t *next_mid = next + (next_stride * (LINES / 2));
                        uint8_t *ref_dst_mid = ref_dst + (ref_dst_stride * (LINES / 2));
                        uint8_t *opt_dst_mid = opt_dst + (opt_dst_stride * (LINES / 2));
                        yadif_filter_line_c(ref_dst_mid, prev_mid, cur_mid, next_mid, i, cur_stride, -cur_stride, j, k);
                        call_new(opt_dst_mid, prev_mid, cur_mid, next_mid, i, cur_stride, -cur_stride, j, k);
                        checkasm_check1d(uint8_t, ref_dst_mid, opt_dst_mid, i, "dst");
                        bench_new(opt_dst_mid, prev_mid, cur_mid, next_mid,
                                  i, cur_stride, -cur_stride, j, k);
                    }
                }
            }
        }
    }
    checkasm_report("yadif_filter_line");
#endif
}