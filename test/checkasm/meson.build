checkasm_ext_sources = files(
    'ext/src/arm/checkasm_32.S',
    'ext/src/arm/checkasm_64.S',
    'ext/src/arm/cpu.c',
    'ext/src/checkasm.c',
    'ext/src/cpu.c',
    'ext/src/function.c',
    'ext/src/loongarch/checkasm.S',
    'ext/src/perf.c',
    'ext/src/perf/arm.c',
    'ext/src/perf/linux.c',
    'ext/src/perf/macos_kperf.c',
    'ext/src/riscv/callcheck.S',
    'ext/src/riscv/cpu.c',
    'ext/src/signal.c',
    'ext/src/stackguard.c',
    'ext/src/stats.c',
    'ext/src/utils.c',
    'ext/src/x86/cpu.c',
)

if cdata.has('HAVE_X86ASM')
    checkasm_ext_sources += files('ext/src/x86/checkasm.asm')
endif

libcheckasm = static_library(
    'checkasm_ext',
    checkasm_ext_sources,
    include_directories: [
        include_directories('ext/include', 'ext/src'),
    ],
)

libdeinterlace_merge_generic = static_library(
    'deinterlace_merge_generic',
    files('../../modules/video_filter/deinterlace/merge.c'),
    include_directories: [vlc_include_dirs],
)

checkasm_sources = files('checkasm.c', 'merge.c')

checkasm_link_with = [libvlccore, libcheckasm, libvlc, libdeinterlace_merge_generic, vlc_libcompat ]

m_dep = cc.find_library('m', required: false)
checkasm_exe = executable(
    'vlc-checkasm',
    checkasm_sources,
    link_with: checkasm_link_with,
    dependencies: [m_dep],
    c_args: ['-DTOP_BUILDDIR="' + vlc_build_root + '"'],
    include_directories: [
        vlc_include_dirs,
        include_directories('ext/include'),
    ],
    install: false,
    build_by_default: false,
)

checkasm_test_deps = [vlc_plugins_targets['deinterlace']]
if vlc_plugins_targets.has_key('deinterlace_x86')
    checkasm_test_deps += vlc_plugins_targets['deinterlace_x86']
endif

test('checkasm', checkasm_exe, suite: ['checkasm'],
    depends: checkasm_test_deps
)
benchmark(
    'checkasm-bench',
    checkasm_exe,
    suite: ['checkasm-bench'],
    args: ['--bench'],
)
