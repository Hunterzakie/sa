# Data files installed with VLC.

fs = import('fs')

#
# Lua scripts
#

# Byte-compiled and installed in the architecture-specific directory
lua_modules = files(
    'lua/extensions/VLSub.lua',
    'lua/intf/cli.lua',
    'lua/intf/dummy.lua',
    'lua/intf/dumpmeta.lua',
    'lua/intf/http.lua',
    'lua/intf/luac.lua',
    'lua/intf/modules/host.lua',
    'lua/intf/modules/httprequests.lua',
    'lua/intf/telnet.lua',
    'lua/meta/art/02_frenchtv.lua',
    'lua/meta/art/03_lastfm.lua',
    'lua/meta/art/01_googleimage.lua',
    'lua/meta/art/00_musicbrainz.lua',
    'lua/meta/reader/filename.lua',
    'lua/modules/common.lua',
    'lua/modules/dkjson.lua',
    'lua/modules/sandbox.lua',
    'lua/modules/simplexml.lua',
    'lua/playlist/anevia_streams.lua',
    'lua/playlist/anevia_xml.lua',
    'lua/playlist/bbc_co_uk.lua',
    'lua/playlist/cue.lua',
    'lua/playlist/dailymotion.lua',
    'lua/playlist/extreme.lua',
    'lua/playlist/france2.lua',
    'lua/playlist/jamendo.lua',
    'lua/playlist/katsomo.lua',
    'lua/playlist/koreus.lua',
    'lua/playlist/lelombrik.lua',
    'lua/playlist/mpora.lua',
    'lua/playlist/newgrounds.lua',
    'lua/playlist/pinkbike.lua',
    'lua/playlist/rockbox_fm_presets.lua',
    'lua/playlist/soundcloud.lua',
    'lua/playlist/vimeo.lua',
    'lua/playlist/vocaroo.lua',
    'lua/playlist/twitch.lua',
    'lua/playlist/zapiks.lua',
    'lua/sd/fmc.lua',
    'lua/sd/jamendo.lua',
)

# Installed as-is in the architecture-independent directory
lua_pkgdata = files(
    'lua/http/custom.lua',
    'lua/http/view.html',
    'lua/http/mobile.html',
    'lua/http/dialogs/create_stream.html',
    'lua/http/dialogs/equalizer_window.html',
    'lua/http/dialogs/mosaic_window.html',
    'lua/http/dialogs/batch_window.html',
    'lua/http/dialogs/error_window.html',
    'lua/http/dialogs/browse_window.html',
    'lua/http/dialogs/stream_window.html',
    'lua/http/dialogs/offset_window.html',
    'lua/http/dialogs/stream_config_window.html',
    'lua/http/vlm.html',
    'lua/http/images/buttons.png',
    'lua/http/images/speaker-32.png',
    'lua/http/images/vlc-48.png',
    'lua/http/images/Audio-48.png',
    'lua/http/images/Folder-48.png',
    'lua/http/images/Other-48.png',
    'lua/http/images/Back-48.png',
    'lua/http/images/Video-48.png',
    'lua/http/images/vlc16x16.png',
    'lua/http/mobile_equalizer.html',
    'lua/http/vlm_export.html',
    'lua/http/mobile_browse.html',
    'lua/http/favicon.ico',
    'lua/http/mobile_view.html',
    'lua/http/requests/playlist.xml',
    'lua/http/requests/playlist.json',
    'lua/http/requests/README.txt',
    'lua/http/requests/playlist_jstree.xml',
    'lua/http/requests/browse.xml',
    'lua/http/requests/browse.json',
    'lua/http/requests/vlm_cmd.xml',
    'lua/http/requests/status.xml',
    'lua/http/requests/status.json',
    'lua/http/requests/rd.json',
    'lua/http/requests/rd.xml',
    'lua/http/requests/vlm.xml',
    'lua/http/index.html',
    'lua/http/css/ui-lightness/jquery-ui-1.8.13.custom.css',
    'lua/http/css/ui-lightness/images/ui-bg_diagonals-thick_20_666666_40x40.png',
    'lua/http/css/ui-lightness/images/ui-bg_flat_10_000000_40x100.png',
    'lua/http/css/ui-lightness/images/ui-icons_ffffff_256x240.png',
    'lua/http/css/ui-lightness/images/ui-bg_gloss-wave_35_f6a828_500x100.png',
    'lua/http/css/ui-lightness/images/ui-bg_glass_65_ffffff_1x400.png',
    'lua/http/css/ui-lightness/images/ui-icons_222222_256x240.png',
    'lua/http/css/ui-lightness/images/ui-bg_glass_100_f6f6f6_1x400.png',
    'lua/http/css/ui-lightness/images/ui-bg_highlight-soft_100_eeeeee_1x100.png',
    'lua/http/css/ui-lightness/images/ui-bg_highlight-soft_75_ffe45c_1x100.png',
    'lua/http/css/ui-lightness/images/ui-bg_diagonals-thick_18_b81900_40x40.png',
    'lua/http/css/ui-lightness/images/ui-icons_ffd27a_256x240.png',
    'lua/http/css/ui-lightness/images/ui-icons_228ef1_256x240.png',
    'lua/http/css/ui-lightness/images/ui-icons_ef8c08_256x240.png',
    'lua/http/css/ui-lightness/images/ui-bg_glass_100_fdf5ce_1x400.png',
    'lua/http/css/main.css',
    'lua/http/css/mobile.css',
    'lua/http/js/ui.js',
    'lua/http/js/common.js',
    'lua/http/js/jquery.jstree.js',
    'lua/http/js/controllers.js',
)

# Installed as documentation only, together with a few sample scripts
lua_doc = files(
    'lua/README.txt',
    'lua/extensions/README.txt',
    'lua/http/requests/README.txt',
    'lua/intf/README.txt',
    'lua/intf/dumpmeta.lua',
    'lua/meta/README.txt',
    'lua/meta/art/README.txt',
    'lua/meta/art/01_googleimage.lua',
    'lua/meta/fetcher/README.txt',
    'lua/meta/reader/README.txt',
    'lua/meta/reader/filename.lua',
    'lua/playlist/README.txt',
    'lua/sd/README.txt',
    'lua/sd/icecast.lua',
    'lua/sd/icast.lua',
)

if lua_enabled
    luac = find_program('luac')

    if host_system == 'windows'
        # VLC looks for everything next to the executable on Windows
        luac_install_dir = prefix_path
        lua_pkgdata_install_dir = prefix_path
    else
        luac_install_dir = pkglibexecdir_path
        lua_pkgdata_install_dir = pkgdatadir_path
    endif

    # Meson refuses build subdirectories that also exist in the source tree,
    # so the byte-compiled scripts are kept apart from the sources
    foreach script : lua_modules
        script_path = fs.relative_to(script, meson.current_source_dir())
        script_dir = fs.parent(script_path)
        custom_target(script_path.underscorify(),
            input: script,
            output: fs.stem(script_path) + '.luac',
            build_subdir: 'luac' / script_dir,
            command: [luac, '-o', '@OUTPUT@', '@INPUT@'],
            build_by_default: true,
            install: true,
            install_dir: luac_install_dir / script_dir)
    endforeach

    install_data(lua_pkgdata,
        install_dir: lua_pkgdata_install_dir,
        preserve_path: true)

    # The installation tree is the package itself on Windows, where the
    # documentation is not shipped
    if host_system != 'windows'
        install_data(lua_doc,
            install_dir: docdir_path,
            preserve_path: true)
    endif
endif

#
# Skins
#

if skins2_feasible
    subdir('skins2')
endif

#
# Icons and helper scripts
#

if host_system != 'windows'
    hicolor_dir = sysdatadir_path / 'icons' / 'hicolor'

    install_data('icons/vlc.ico', install_dir: pkgdatadir_path)
    install_data(
        'icons/16x16/vlc.png',
        'icons/16x16/vlc.xpm',
        install_dir: hicolor_dir / '16x16' / 'apps')
    install_data(
        'icons/32x32/vlc.png',
        'icons/32x32/vlc.xpm',
        'icons/32x32/vlc-xmas.xpm',
        'icons/32x32/vlc-aprilfools.xpm',
        install_dir: hicolor_dir / '32x32' / 'apps')
    install_data(
        'icons/48x48/vlc.png',
        'icons/48x48/vlc-xmas.png',
        'icons/48x48/vlc-aprilfools.png',
        install_dir: hicolor_dir / '48x48' / 'apps')
    install_data(
        'icons/128x128/vlc.png',
        'icons/128x128/vlc-xmas.png',
        'icons/128x128/vlc-kb.png',
        'icons/128x128/vlc-aprilfools.png',
        install_dir: hicolor_dir / '128x128' / 'apps')
    install_data(
        'icons/256x256/vlc.png',
        install_dir: hicolor_dir / '256x256' / 'apps')

    install_data(
        'utils/audio-vlc-default.sh',
        'utils/video-vlc-default.sh',
        install_dir: pkgdatadir_path / 'utils',
        install_mode: 'rwxr-xr-x')
endif

#
# freedesktop.org integration
#

have_freedesktop = host_system not in ['windows', 'darwin', 'android', 'emscripten']

if have_freedesktop
    # Desktop entries
    desktop_conf = configuration_data()
    desktop_conf.set('bindir', bindir_path)

    mimetypes = []
    foreach line : fs.read('vlc.desktop.mimetypes').split('\n')
        mimetype = line.split('#')[0].strip()
        if mimetype != ''
            mimetypes += mimetype
        endif
    endforeach
    desktop_conf.set('MIMETYPES', ';'.join(mimetypes) + ';')

    desktop_files = []
    foreach name : ['vlc', 'vlc-openbd', 'vlc-opendvd', 'vlc-openvcd', 'vlc-opencda']
        desktop_files += configure_file(
            input: name + '.desktop.in',
            output: name + '.desktop',
            configuration: desktop_conf,
            install: true,
            install_dir: sysdatadir_path / 'applications')
    endforeach

    desktop_file_validate = find_program('desktop-file-validate', required: false)
    if desktop_file_validate.found() and get_option('tests').allowed()
        test('desktop-file-validate', desktop_file_validate,
            args: desktop_files,
            suite: 'share')
    endif

    # AppStream metadata
    appdata_conf = configuration_data()
    appdata_conf.set('VERSION', vlc_version_full)
    appdata_conf.set('VERSION_DATE', vlc_version_date)
    appdata_conf.set('LIBVLC_ABI_MAJOR', libvlc_abi_version_major)

    appdata_template = configure_file(
        input: 'org.videolan.vlc.appdata.xml.in.in',
        output: 'org.videolan.vlc.appdata.xml.in',
        configuration: appdata_conf)

    msgfmt = find_program('msgfmt', version: '>= 0.19.7', required: false, native: true)
    if msgfmt.found()
        i18n = import('i18n')
        i18n.merge_file(
            input: appdata_template,
            output: 'org.videolan.vlc.appdata.xml',
            type: 'xml',
            po_dir: vlc_src_root / 'po',
            install: true,
            install_dir: sysdatadir_path / 'metainfo')
    else
        configure_file(
            input: appdata_template,
            output: 'org.videolan.vlc.appdata.xml',
            copy: true,
            install: true,
            install_dir: sysdatadir_path / 'metainfo')
    endif
endif

# KDE Solid actions
kde_solid_option = get_option('kde_solid').disable_auto_if(not have_freedesktop)
if kde_solid_option.allowed()
    kde_solid_dir = get_option('kde_solid_dir')
    if kde_solid_dir == ''
        kde_solid_dir = sysdatadir_path / 'solid' / 'actions'
    endif

    install_data(
        'solid/vlc-openbd.desktop',
        'solid/vlc-opencda.desktop',
        'solid/vlc-opendvd.desktop',
        'solid/vlc-openvcd.desktop',
        install_dir: kde_solid_dir)
endif
