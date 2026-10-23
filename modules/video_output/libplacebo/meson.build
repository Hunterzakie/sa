# Libplacebo-based video outputs

libplacebo_dep = dependency('libplacebo',
                            version: '>= 5.237',
                            required: get_option('libplacebo'))

if not libplacebo_dep.found()
    libplacebo_utils = disabler()
    subdir_done()
endif
libplacebo_utils = static_library('vlc_libplacebo_utils',
                                files('utils.c'),
                                dependencies: [libplacebo_dep],
                                include_directories: [vlc_include_dirs])

placebo_link = [libplacebo_utils]
placebo_sources = files('instance.c', 'display.c')
placebo_cargs = []
if vulkan_dep.found()
    placebo_sources += files('vk_interop.c')
    placebo_link += libvulkan_device
    placebo_cargs += '-DHAVE_VULKAN'
endif

vlc_modules += {
    'name' : 'placebo',
    'sources' : placebo_sources,
    'link_with' : placebo_link,
    'c_args' : placebo_cargs,
    'dependencies' : [libplacebo_dep],
    'plugin_dir' : 'video_output',
}

## Vulkan
if vulkan_dep.found()
    vlc_modules += {
        'name' : 'placebo_vk',
        'sources' : files('../vulkan/platform.c', 'instance_vulkan.c'),
        'link_with' : [libplacebo_utils],
        'dependencies' : [libplacebo_dep],
        'plugin_dir' : 'video_output',
    }

    vlc_modules += {
        'name' : 'placebo_sw',
        'sources' : files('interop/sw.c'),
        'link_with' : [libplacebo_utils],
        'dependencies' : [libplacebo_dep],
    }

    # NVDEC hardware interop
    placebo_nvdec_dep = dependency('ffnvcodec', version: '>= 11.1.5.1',
                                   required: false)
    if placebo_nvdec_dep.found()
        vlc_modules += {
            'name' : 'placebo_nvdec',
            'sources' : files('interop/nvdec.c'),
            'link_with' : [libvulkan_device],
            'dependencies' : [vulkan_dep, placebo_nvdec_dep, dl_lib],
        }
    endif
endif

## OpenGL
if opengl_dep.found()
    vlc_modules += {
        'name' : 'placebo_gl',
        'sources' : files('instance_opengl.c'),
        'link_with' : [libplacebo_utils],
        'dependencies' : [libplacebo_dep],
        'plugin_dir' : 'video_output',
    }
endif

if opengles2_dep.found()
    vlc_modules += {
        'name' : 'placebo_gles2',
        'sources' : files('instance_opengl.c'),
        'link_with' : [libplacebo_utils],
        'c_args' : '-DUSE_OPENGL_ES2',
        'dependencies' : [libplacebo_dep],
        'plugin_dir' : 'video_output',
    }
endif
