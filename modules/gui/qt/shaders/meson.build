# Shader compilation and resource generation

qt6 = import('qt6')

env = environment()
# For qsb to locate spirv-opt:
env.append('PATH', contrib_dir + '/bin')

shader_sources = [
    'FadingEdge.frag',
    'FadingEdgeX.vert',
    'FadingEdgeY.vert',
    'Noise.frag',
    'RectFilter.frag',
    'SubTexture.vert',
    'SDFAARoundedTexture.frag',
    'SDFAARoundedTexture_cropsupport_bordersupport.frag',
    'DitheredTexture.frag',
    'FastBlend.frag',
    'FastBlend_additive.frag',
    'FastBlend_multiply.frag',
    'FastBlend_screen.frag',
    'RoundedRectangleShadow.frag',
    'RoundedRectangleShadow_hollow.frag',
    'VoronoiSnow.frag',
    'DualKawaseBlur_downsample.frag',
    'DualKawaseBlur_upsample.frag',
    'DualKawaseBlur_upsample_postprocess.frag',
    'GaussianBlur.comp',
]

shader_files = files(shader_sources)

qt_bin_directory = qt6_dep.get_variable(pkgconfig: 'bindir', configtool: 'QT_HOST_BINS')
qsb = find_program('qsb', dirs: qt_bin_directory, required: true)

qsb_params_common = ['--batchable', '-O']
qsb_params = ['--glsl=300 es,130,150'] + qsb_params_common
qsb_params_comp = ['--glsl="310 es,430'] + qsb_params_common
if host_system == 'windows'
    qsb_params_windows = ['--hlsl=50', '-c']
    qsb_params += qsb_params_windows
    qsb_params_comp += qsb_params_windows
elif host_system == 'darwin'
    qsb_params_darwin += '--msl=12'
    qsb_params += qsb_params_darwin
    qsb_params_comp += qsb_params_darwin
endif

shader_targets = []
foreach shader: shader_sources
    shader_target_name = shader + '.qsb'

    if shader.endswith('.comp')
        target = custom_target(shader_target_name,
          input: shader,
          output: shader_target_name,
          command: [qsb, qsb_params_comp, '--output', '@OUTPUT@', '@INPUT@']
        )
    else
        target = custom_target(shader_target_name,
          depend_files: ['Common.glsl'],
          input: shader,
          output: shader_target_name,
          command: [qsb, qsb_params, '--output', '@OUTPUT@', '@INPUT@']
        )
    endif

    shader_targets += [target]
endforeach

build_shaders = configure_file(input: 'shaders.qrc', output: 'shaders.qrc', copy: true)

shaders_processed = qt6.preprocess(
        qresources: build_shaders,
        dependencies: qt6_dep)
