#!/bin/sh
set -e

info()
{
    local green="\033[1;32m"
    local normal="\033[0m"
    echo "[${green}build${normal}] $1"
}

SCRIPTDIR=$(dirname "$0")
source "$SCRIPTDIR/env.build.sh" "none"


ARCH="x86_64"
MINIMAL_OSX_VERSION="10.7"
OSX_KERNELVERSION=`uname -r | cut -d. -f1`
SDKROOT=$(xcrun --show-sdk-path)
VLCBUILDDIR=""

CORE_COUNT=`getconf NPROCESSORS_ONLN 2>&1`
let JOBS=$CORE_COUNT+1


usage()
{
cat << EOF
usage: $0 [options]

Build vlc in the current directory

OPTIONS:
   -h            Show some help
   -q            Be quiet
   -j            Force number of cores to be used
   -r            Rebuild everything (tools, contribs, vlc)
   -c            Recompile contribs from sources
   -p            Build packages for all artifacts
   -i <n|u>      Create an installable package (n: nightly, u: unsigned stripped release archive)
   -k <sdk>      Use the specified sdk (default: $SDKROOT)
   -a <arch>     Use the specified arch (default: $ARCH)
   -C            Use the specified VLC build dir
   -b <url>      Enable breakpad support and send crash reports to this URL
EOF

}

spushd()
{
    pushd "$1" > /dev/null
}

spopd()
{
    popd > /dev/null
}

while getopts "hvrcpi:k:a:j:C:b:" OPTION
do
     case $OPTION in
         h)
             usage
             exit 1
             ;;
         q)
             set +x
             QUIET="yes"
         ;;
         r)
             REBUILD="yes"
         ;;
         c)
             CONTRIBFROMSOURCE="yes"
         ;;
         p)
             PACKAGE="yes"
         ;;
         i)
             PACKAGETYPE=$OPTARG
         ;;
         a)
             ARCH=$OPTARG
         ;;
         k)
             SDKROOT=$OPTARG
             if [ -z "$DEVELOPER_DIR" ]; then
                DEVELOPER_DIR=$(echo $SDKROOT | sed 's,/Platforms/.*,,') && test -d $DEVELOPER_DIR && export DEVELOPER_DIR
             fi
         ;;
         j)
             JOBS=$OPTARG
         ;;
         C)
             VLCBUILDDIR=$OPTARG
         ;;
         b)
             BREAKPAD=$OPTARG
         ;;
         *)
             usage
             exit 1
         ;;
     esac
done
shift $(($OPTIND - 1))

if [ "x$1" != "x" ]; then
    usage
    exit 1
fi

#
# Various initialization
#

out="/dev/stdout"
if [ "$QUIET" = "yes" ]; then
    out="/dev/null"
fi

ACTUAL_ARCH=`get_actual_arch $ARCH`
BUILD_ARCH=`get_buildsystem_arch $BUILD_ARCH`
BUILD_TRIPLET=$BUILD_ARCH-apple-darwin$OSX_KERNELVERSION
HOST_TRIPLET=$ARCH-apple-darwin$OSX_KERNELVERSION

info "Building VLC for macOS, architecture ${ACTUAL_ARCH} on a ${BUILD_ARCH} device"
export SDKROOT
vlcSetBaseEnvironment
vlcroot="$(vlcGetRootDir)"

builddir=`pwd`

info "Building in \"$builddir\""

#
# vlc/extras/tools
#

info "Building building tools"
spushd "${vlcroot}/extras/tools"
./bootstrap > $out
if [ "$REBUILD" = "yes" ]; then
    make clean
    ./bootstrap > $out
fi
make -j$JOBS > $out
spopd

#
# vlc/contribs
#

vlcSetSymbolEnvironment
vlcSetContribEnvironment "$MINIMAL_OSX_VERSION"

CONTRIBFLAGS=
if [ "$PACKAGETYPE" = "u" ]; then
    # release package should have sparkle, breakpad, growl
    CONTRIBFLAGS="$CONTRIBFLAGS --enable-sparkle --enable-breakpad --enable-growl"
elif [ "$PACKAGETYPE" = "n" ]; then
    # nightly package should have growl
    CONTRIBFLAGS="$CONTRIBFLAGS --enable-growl"
fi

info "Building contribs"
spushd "${vlcroot}/contrib"
mkdir -p contrib-$HOST_TRIPLET && cd contrib-$HOST_TRIPLET
../bootstrap --build=$BUILD_TRIPLET --host=$HOST_TRIPLET $CONTRIBFLAGS > $out
if [ "$REBUILD" = "yes" ]; then
    make clean
fi
make list
if [ "$CONTRIBFROMSOURCE" = "yes" ]; then
    make fetch
    make -j$JOBS -k || make -j1

    if [ "$PACKAGE" = "yes" ]; then
        make package
    fi

else
if [ ! -e "../$HOST_TRIPLET" ]; then
    if [ -n "$VLC_PREBUILT_CONTRIBS_URL" ]; then
        make prebuilt PREBUILT_URL="$VLC_PREBUILT_CONTRIBS_URL"
    else
        make prebuilt
    fi
    make .luac
fi
fi
spopd


vlcUnsetContribEnvironment

#
# vlc/bootstrap
#

info "Bootstrap-ing configure"
spushd "${vlcroot}"
if ! [ -e "${vlcroot}/configure" ]; then
    ${vlcroot}/bootstrap > $out
fi
spopd


if [ ! -z "$VLCBUILDDIR" ];then
    mkdir -p $VLCBUILDDIR
    pushd $VLCBUILDDIR
fi
#
# vlc/configure
#

CONFIGFLAGS=""
if [ ! -z "$BREAKPAD" ]; then
     CONFIGFLAGS="$CONFIGFLAGS --with-breakpad=$BREAKPAD"
fi
if [ "$PACKAGETYPE" = "u" ]; then
    # release package should have sparkle, breakpad, growl
    CONFIGFLAGS="$CONFIGFLAGS --enable-sparkle --enable-breakpad --enable-growl"
elif [ "$PACKAGETYPE" = "n" ]; then
    # nightly package should have growl
    CONFIGFLAGS="$CONFIGFLAGS --enable-growl"
fi

if [ "${vlcroot}/configure" -nt Makefile ]; then

  ${vlcroot}/extras/package/macosx/configure.sh \
      --build=$BUILD_TRIPLET \
      --host=$HOST_TRIPLET \
      --with-macosx-version-min=$MINIMAL_OSX_VERSION \
      --with-macosx-sdk=$SDKROOT \
      $CONFIGFLAGS \
      $VLC_CONFIGURE_ARGS > $out
fi

#
# make
#

if [ "$REBUILD" = "yes" ]; then
    info "Running make clean"
    make clean
fi

info "Running make -j$JOBS"
make -j$JOBS

info "Preparing VLC.app"
make VLC.app

# Workaround for macOS 10.7: CFNetwork only exists as part of CoreServices framework
if [ "$ARCH" = "x86_64" ]; then
    install_name_tool -change /System/Library/Frameworks/CFNetwork.framework/Versions/A/CFNetwork /System/Library/Frameworks/CoreServices.framework/Versions/A/CoreServices VLC.app/Contents/MacOS/lib/libvlccore.dylib
fi

if [ "$PACKAGETYPE" = "u" ]; then
    info "Copying app with debug symbols into VLC-debug.app and stripping"
    rm -rf VLC-debug.app
    cp -Rp VLC.app VLC-debug.app

    # Workaround for breakpad symbol parsing:
    # Symbols must be uploaded for libvlc(core).dylib, not libvlc(core).x.dylib
    (cd VLC-debug.app/Contents/MacOS/lib/ && rm libvlccore.dylib && mv libvlccore.*.dylib libvlccore.dylib)
    (cd VLC-debug.app/Contents/MacOS/lib/ && rm libvlc.dylib && mv libvlc.*.dylib libvlc.dylib)


    find VLC.app/ -name "*.dylib" -exec strip -x {} \;
    find VLC.app/ -type f -name "VLC" -exec strip -x {} \;
    find VLC.app/ -type f -name "Sparkle" -exec strip -x {} \;
    find VLC.app/ -type f -name "Growl" -exec strip -x {} \;
    find VLC.app/ -type f -name "Breakpad" -exec strip -x {} \;

    if [ "$BUILD_TRIPLET" = "$HOST_TRIPLET" ]; then
        bin/vlc-cache-gen VLC.app/Contents/MacOS/plugins
    fi

    info "Building VLC release archive"
    make package-macosx-release
    shasum -a 512 vlc-*-release.zip
elif [ "$PACKAGETYPE" = "n" -o "$PACKAGE" = "yes" ]; then
    info "Building VLC dmg package"
    make package-macosx
fi

if [ ! -z "$VLCBUILDDIR" ]; then
    popd
fi
