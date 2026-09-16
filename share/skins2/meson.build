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
