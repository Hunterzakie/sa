# Data files of the skins2 interface

if host_system == 'windows'
    # VLC looks for the skins next to the executable on Windows
    skins2_install_dir = prefix_path / 'skins'
else
    skins2_install_dir = pkgdatadir_path / 'skins2'
endif

install_data(
    'fonts/FreeSans.ttf',
    'fonts/FreeSansBold.ttf',
    'skin.dtd',
    'skin.catalog',
    'winamp2.xml',
    install_dir: skins2_install_dir,
    preserve_path: true)

# Content of the default skin archive, in the order it is stored
default_vlt_files = [
    'default/subX/about.png',
    'default/subX/eq.png',
    'default/subX/font.otf',
    'default/subX/main.png',
    'default/subX/pl.png',
    'default/subX/playtreeglyphs.png',
    'default/subX/sysbuttons.png',
    'default/subX/vol_anim.png',
    'default/subX/vol_slider.png',
    'default/theme.xml',
]

# Store root as the owner of the files like the autotools build. This is not
# supported by the tar of macOS.
tar = find_program('tar')
if build_machine.system() == 'darwin'
    tar_owner_args = []
else
    tar_owner_args = ['--owner=root', '--group=root']
endif

custom_target('default.vlt',
    input: files(default_vlt_files),
    output: 'default.vlt',
    command: [tar, '-c', '-z', '-f', '@OUTPUT@', '--format=ustar', tar_owner_args,
              '-C', meson.current_source_dir(), default_vlt_files],
    install: true,
    install_dir: skins2_install_dir)
