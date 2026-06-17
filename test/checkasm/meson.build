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

checkasm_sources = files(
    'checkasm.c',
)
m_dep = cc.find_library('m', required: false)
checkasm_exe = executable(
    'vlc-checkasm',
    checkasm_sources,
    link_with: [libvlccore, libcheckasm],
    dependencies:  [m_dep],
    include_directories: [vlc_include_dirs, include_directories('ext/include')],
    install: false,
    build_by_default: false,
)

test('checkasm', checkasm_exe, suite: ['checkasm'])
benchmark(
    'checkasm-bench',
    checkasm_exe,
    suite: ['checkasm-bench'],
    args: ['--bench'],
)
