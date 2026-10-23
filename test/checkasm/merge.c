/*****************************************************************************
 * merge.c: checkasm tests for VLC deinterlace merge routines
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

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <vlc/vlc.h>
#include <vlc_common.h>
#include <vlc_cpu.h>
#include <checkasm/checkasm.h>
#include <checkasm/test.h>
#include <checkasm/utils.h>
#include "../../modules/video_filter/deinterlace/merge.h"

/* This file only goes through vlc_CPU_functions_init()
 * against the "deinterlace functions" capability. Whichever architecture's
 * Probe() is linked into the checkasm binary (x86 today, others later) is
 * exercised identically, with no per-arch code needed in this file. */

#define MAX_BUF_BYTES 256

static const size_t test_sizes[] = {1, 8, 15, 16, 17, 31, 32, 63, 65, 127, MAX_BUF_BYTES};
#define NUM_TEST_SIZES (sizeof(test_sizes) / sizeof(test_sizes[0]))

void checkasm_check_merge(void);

static void get_merge_functions(struct deinterlace_functions *funcs)
{

    memset(funcs, 0, sizeof(*funcs));
    vlc_CPU_functions_init("deinterlace functions", funcs);
}
static void check_merge8(struct deinterlace_functions *funcs)
{
    if (funcs->merges[0] == NULL) 
        return;

    CHECKASM_ALIGN(uint8_t s1[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint8_t s2[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint8_t dest_c[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint8_t dest_a[MAX_BUF_BYTES]);
    checkasm_declare(void, void *d, const void *s1, const void *s2, size_t len); 

    if (checkasm_check_func(Merge8BitGeneric, "merge8")) 
    {
        INITIALIZE_BUF(s1);
        INITIALIZE_BUF(s2);
        CLEAR_BUF(dest_c);

        checkasm_call_new(dest_c, s1, s2, MAX_BUF_BYTES);
        checkasm_bench_new(dest_c, s1, s2, MAX_BUF_BYTES);
    }

    if (checkasm_check_func(funcs->merges[0], "merge8"))
    {
        for (size_t i = 0; i < NUM_TEST_SIZES; i++)
        {
            const size_t len = test_sizes[i];
            INITIALIZE_BUF(s1);
            INITIALIZE_BUF(s2);
            CLEAR_BUF(dest_a);
            CLEAR_BUF(dest_c);

            checkasm_call_ref(dest_c, s1, s2, len);
            checkasm_call_new(dest_a, s1, s2, len);

            checkasm_check1d(uint8_t, dest_a, dest_c, len, "dst");
        }
        checkasm_bench_new(dest_a, s1, s2, MAX_BUF_BYTES);
    }
    checkasm_report("merge8");
}

static void check_merge16(struct deinterlace_functions *funcs)
{
    if (funcs->merges[1] == NULL)
        return;
    CHECKASM_ALIGN(uint16_t s1[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint16_t s2[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint16_t dest_c[MAX_BUF_BYTES]);
    CHECKASM_ALIGN(uint16_t dest_a[MAX_BUF_BYTES]);
    checkasm_declare(void, void *d, const void *s1, const void *s2, size_t len);

    if (checkasm_check_func(Merge16BitGeneric, "merge16"))
    {
        INITIALIZE_BUF(s1);
        INITIALIZE_BUF(s2);
        CLEAR_BUF(dest_c);

        checkasm_call_new(dest_c, s1, s2, MAX_BUF_BYTES);
        checkasm_bench_new(dest_c, s1, s2, MAX_BUF_BYTES);
    }

    if (checkasm_check_func(funcs->merges[1], "merge16"))
    {
        for (size_t i = 0; i < NUM_TEST_SIZES; i++)
        {
            const size_t len_bytes = test_sizes[i] & ~(size_t)1;

            if (len_bytes == 0)
                continue;

            INITIALIZE_BUF(s1);
            INITIALIZE_BUF(s2);
            CLEAR_BUF(dest_a);
            CLEAR_BUF(dest_c);

            checkasm_call_ref(dest_c, s1, s2, len_bytes);
            checkasm_call_new(dest_a, s1, s2, len_bytes);

            checkasm_check1d(uint16_t, dest_a, dest_c, len_bytes / 2, "dst");
        }

        checkasm_bench_new(dest_a, s1, s2, MAX_BUF_BYTES);
    }

    checkasm_report("merge16");
}
void checkasm_check_merge(void)
{
    struct deinterlace_functions funcs;
    get_merge_functions(&funcs);
    check_merge8(&funcs);
    check_merge16(&funcs);
}
