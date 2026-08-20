/*****************************************************************************
 * yadif.c : SIMD dispatch interface for the Yadif deinterlacer
 *****************************************************************************
 * Copyright (C) 2026 Mazen Ewiss
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
#   include "config.h"
#endif
#include <vlc_common.h>
#include <vlc_cpu.h>
#include <vlc_plugin.h>
#include "../../video_filter/deinterlace/yadif_filter.h"

void vlcpriv_yadif_filter_line_ssse3(uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int prefs, int mrefs, int parity, int mode);
void vlcpriv_yadif_filter_line_sse2(uint8_t *dst, uint8_t *prev, uint8_t *cur, uint8_t *next, int w, int prefs, int mrefs, int parity, int mode);

static void Probe(void *data)
{
    struct yadif_filter_functions *const f = data;
    if (vlc_CPU_SSSE3()) 
            f->filter[0] = vlcpriv_yadif_filter_line_ssse3;
        else if(vlc_CPU_SSE2())
            f->filter[0] = vlcpriv_yadif_filter_line_sse2;
}


vlc_module_begin()
    set_description("x86 SSE2/SSSE3 optimisation for Yadif deinterlacing")
    set_cpu_funcs("yadif filter functions", Probe, 10)
vlc_module_end()
