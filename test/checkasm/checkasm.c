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

const char vlc_module_name[] = "vlc-checkasm";

static void set_cpu_flags(CheckasmCpu new_flags)
{
    unsigned mask = (unsigned)new_flags;
    vlc_CPU_set(mask);
}

int main(int argc, const char *argv[])
{
    CheckasmCpuInfo cpu_flags[] = {
# if defined(__i386__) || defined(__x86_64__)
        {.flag = VLC_CPU_SSE2, .name = "SSE2", .suffix = "sse2"},
        {.flag = VLC_CPU_SSE3, .name = "SSE3", .suffix = "sse3"},
        {.flag = VLC_CPU_SSSE3, .name = "SSSE3", .suffix = "ssse3"},
        {.flag = VLC_CPU_SSE4_1, .name = "SSE4.1", .suffix = "sse4.1"},
        {.flag = VLC_CPU_AVX, .name = "AVX", .suffix = "avx"},
        {.flag = VLC_CPU_AVX2, .name = "AVX2", .suffix = "avx2"},
# elif defined(__aarch64__)
        {.flag = VLC_CPU_ARM_NEON, .name = "NEON", .suffix = "neon"},
        {.flag = VLC_CPU_ARM_SVE, .name = "SVE", .suffix = "sve"},
# elif defined (__arm__)
        {.flag = VLC_CPU_ARM_NEON, .name = "NEON", .suffix = "neon"}, 
        {.flag = VLC_CPU_ARMv6 , .name = "ARMv6", .suffix = "armv6"},
# elif defined (__ppc__) || defined (__ppc64__) || defined (__powerpc__)
        {.flag = VLC_CPU_ALTIVEC, .name = "AltiVec", .suffix = "altivec"},
# elif defined (__riscv)
        {.flag = VLC_CPU_RV_V, .name = "V", .suffix = "rvv"},
        {.flag = VLC_CPU_RV_B, .name = "B", .suffix = "rvb"},
# endif
        {.flag = 0, .name = NULL}, // Sentinel
    };
    CheckasmTest tests[] = {
        {0}, // Sentinel
    };
    CheckasmConfig config = {
        .cpu_flags = cpu_flags,
        .tests = tests,
        .cpu = vlc_CPU(),
        .set_cpu_flags = set_cpu_flags,
    };
    return checkasm_main(&config, argc, argv);
}
