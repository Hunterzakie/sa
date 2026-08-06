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
placebo_cargs = []
if vulkan_dep.found()
    placebo_link += libvulkan_device
    placebo_cargs += '-DHAVE_VULKAN'
endif

vlc_modules += {
    'name' : 'placebo',
    'sources' : files('instance.c', 'display.c'),
    'link_with' : placebo_link,
    'c_args' : placebo_cargs,
    'dependencies' : [libplacebo_dep],
}

## Vulkan
if vulkan_dep.found()
    vlc_modules += {
        'name' : 'placebo_vk',
        'sources' : files('../vulkan/platform.c', 'instance_vulkan.c'),
        'link_with' : [libplacebo_utils],
        'dependencies' : [libplacebo_dep],
    }
endif

## OpenGL
if opengl_dep.found()
    vlc_modules += {
        'name' : 'placebo_gl',
        'sources' : files('instance_opengl.c'),
        'link_with' : [libplacebo_utils],
        'dependencies' : [libplacebo_dep],
    }
endif

if opengles2_dep.found()
    vlc_modules += {
        'name' : 'placebo_gles2',
        'sources' : files('instance_opengl.c'),
        'link_with' : [libplacebo_utils],
        'c_args' : '-DUSE_OPENGL_ES2',
        'dependencies' : [libplacebo_dep],
    }
endif
