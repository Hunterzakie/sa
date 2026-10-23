echo script start

# Must be run inside flatpak
if [ -z "$FLATPAK_ID" ] && [ -z "$FLATPAK_SANDBOX_DIR" ]; then
  echo "This script must be run inside a flatpak dev environment"
  exit 1
fi


THIS_SCRIPT_DIR="$(realpath "$(dirname "$0")")"
VLC_SRCPATH="$(realpath "$THIS_SCRIPT_DIR/../../../")"

export PATH="$PATH:$FLATPAK_DEST/usr/bin"
export BUILDCC=gcc
HOST=$($BUILDCC -dumpmachine)


BUILD_DIR=$VLC_SRCPATH/flatpak-build/build
CONTRIB_BUILD_DIR=$VLC_SRCPATH/flatpak-build/contrib/contr
PREFIX=$VLC_SRCPATH/flatpak-build/${HOST}/usr/local

cd $VLC_SRCPATH
ls -al
echo flatpak-process.sh: PATH is $PATH
echo flatpak-process.sh: current dir is $PWD


echo apply patches in $THIS_SCRIPT_DIR
for patch in $THIS_SCRIPT_DIR/*.patch; do
  patch -p1 < "$patch"
done

mkdir -p $BUILD_DIR
mkdir -p $PREFIX
mkdir -p $CONTRIB_BUILD_DIR

#Build Contribs
echo flatpak-process.sh: Building Contribs
cd "$CONTRIB_BUILD_DIR" && \
   "$VLC_SRCPATH/contrib/bootstrap" --prefix="$PREFIX" --disable-libplacebo && \
   make -j6
## FIXME: libplacebo seems to have an issue linking with glslang. A temporary workaround is not building it.

#Build
echo "flatpak-process.sh: Building VLC"
cd "$BUILD_DIR"

if [ ! -f built-before ]; then
    echo "flatpak-process.sh: First time build — bootstrapping and configuring"
    "$VLC_SRCPATH/bootstrap" &&
    "$VLC_SRCPATH/configure" \
        --prefix="$PREFIX" \
        --with-contrib="$PREFIX" \
        --host=$HOST
else
    echo "flatpak-process.sh: Rebuilding VLC (incremental)"
fi

make -j6 && echo "flatpak-process.sh: VLC build successful" && touch built-before

echo flatpak-process.sh: Installing to $FLATPAK_DEST
make install -j6 && echo flatpak-process.sh: vlc installation complete

# Cleanup patches
echo cleanup applied patches
cd $VLC_SRCPATH
for patch in $THIS_SCRIPT_DIR/*.patch; do
  patch -R -p1 < "$patch"
done


echo flatpak-process.sh: fixing links
cd $FLATPAK_DEST

#bin  include  lib  libexec  share

#ln -s ./usr/local/bin
#ln -s ./usr/local/include
#ln -s ./usr/local/lib
#ln -s ./usr/local/libexec
#ln -s ./usr/local/share

cp -r $PREFIX/* $FLATPAK_DEST/

echo flatpak-process.sh: Complete
