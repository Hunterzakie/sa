/*****************************************************************************
 * checkasm.c : this file uses the checkasm API to run a test suite for assembly functions in VLC,
 * and serves as a template for writing new test programs.
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

#include <vlc_cpu.h>
#include <checkasm/checkasm.h>

void test_yadif(void);
const char vlc_module_name[] = "vlc-checkasm";

int main(int argc, const char *argv[])
{
#if defined(__i386__) || defined(__x86_64__)
    // On x86, we can use the CPU detection from VLC.
    CheckasmCpuInfo cpu_flags[] = {
        {.flag = VLC_CPU_SSE2, .name = "SSE2", .suffix = "sse2"},
        {.flag = VLC_CPU_SSE3, .name = "SSE3", .suffix = "sse3"},
        {.flag = VLC_CPU_SSSE3, .name = "SSSE3", .suffix = "ssse3"},
        {.flag = VLC_CPU_SSE4_1, .name = "SSE4.1", .suffix = "sse4.1"},
        {.flag = VLC_CPU_AVX, .name = "AVX", .suffix = "avx"},
        {.flag = VLC_CPU_AVX2, .name = "AVX2", .suffix = "avx2"},
        {.flag = 0, .name = NULL}, // Sentinel
    };
#else
    CheckasmCpuInfo cpu_flags[] = {
        {.flag = 0, .name = NULL}, // Sentinel
    };
#endif
    CheckasmTest tests[] = {
        { "yadif", test_yadif }, 
        {NULL, NULL} // Sentinel
    };
    printf("Debug: Number of tests = %zu\n", sizeof(tests)/sizeof(tests[0]) - 1);
    CheckasmConfig config = {
        .cpu_flags = cpu_flags,
        .tests = tests,
        .cpu = vlc_CPU(),
        .set_cpu_flags = NULL,
    };
    return checkasm_main(&config, argc, argv);
}
