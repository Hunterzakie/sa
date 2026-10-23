if cc.has_header('drm/drm_fourcc.h')
    dmabuf_lib = static_library(
        'vlc_dmabuf',
        files('exporter.c'),
        include_directories: [vlc_include_dirs],
        build_by_default: false,
        install: false,
        pic: true
    )
else
    dmabuf_lib = disabler()
endif
