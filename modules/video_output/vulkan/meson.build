if vulkan_dep.found()
    libvulkan_device = static_library('vlc_vulkan_device',
        files('common.c',
              'device.c',
              'device_utils.c'),
        dependencies: [vulkan_dep],
        include_directories: [vlc_include_dirs])
endif
