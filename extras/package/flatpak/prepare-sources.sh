#!/usr/bin/env bash
sed -i -e "s,@GIT_HASH@,$(git rev-parse HEAD),g" extras/package/flatpak/vlc-appdata-manifest-url.patch
cd extras/tools
./bootstrap
make fetch
cd ../..
tar cf extras/package/flatpak/tools.tar extras/tools
# Prefetch all contrib tarballs and create a contrib source archive
mkdir -p contrib/linux
cd contrib/linux
../bootstrap --enable-ffmpeg --enable-dav1d --enable-fluidlite
make fetch
cd ../..
tar cf extras/package/flatpak/contrib.tar contrib
find contrib/tarballs -mindepth 1 ! -name .gitignore -exec rm -rf {} +
# Create VLC source archive
# DIST_DEPENDS_ON_UPDATE_PO=no and PO_DEPENDS_ON_POT=no keep `make dist` from
# regenerating vlc.pot and msgmerge'ing every po/*.po (which would dirty the tree).
./bootstrap
QMAKE6=/usr/bin/qmake BUILDCC=/usr/bin/gcc ./configure --disable-lua --disable-avcodec
make dist DIST_DEPENDS_ON_UPDATE_PO=no PO_DEPENDS_ON_POT=no
mv vlc-*.tar.xz extras/package/flatpak/vlc.tar.xz
