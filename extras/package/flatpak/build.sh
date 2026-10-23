#!/bin/bash

set -e

## TODO: Add more usability args
for arg in "$@"; do
  if [ "$arg" = "-f" ]; then
    FORCE_CLEAN="--force-clean"
    break
  fi
done

# Check if flatpak-builder is available
if ! command -v flatpak-builder >/dev/null 2>&1; then
  echo "Missing required tool: flatpak-builder"
  echo ""
  echo "Please install flatpak and flatpak-builder."
  exit 1
fi

# If flatpak-builder exists, check for Flathub remote
if ! flatpak remote-list | grep -q "^flathub"; then
  echo "Flathub remote is required."
  echo "Please run the following to add it:"
  echo " flatpak --user remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo"
  exit 1
fi

THIS_SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
export VLC_SRC="$(realpath "$THIS_SCRIPT_DIR/../../../")"


BUILD_DIR=$VLC_SRC/flatpak-build
mkdir -p $BUILD_DIR/build $BUILD_DIR/contrib

envsubst < "$VLC_SRC/extras/package/flatpak/vlc-flatpak.yaml.in" > "$VLC_SRC/extras/package/flatpak/vlc-flatpak.yaml"


if [ -n "$(find "$BUILD_DIR/flatpak-build-dir" -mindepth 1 -print -quit 2>/dev/null)" ] \
   && [ -z "$FORCE_CLEAN" ]; then
    echo "App dir '$BUILD_DIR/flatpak-build-dir' is not empty. Please delete the existing contents or use -f" >&2
    exit 1
fi


date > $THIS_SCRIPT_DIR/timestamp

flatpak-builder --user \
  --state-dir="$BUILD_DIR/flatpak-state-dir" \
  --install-deps-from=flathub \
  --install \
  $FORCE_CLEAN \
  "$BUILD_DIR/flatpak-build-dir" \
  "$VLC_SRC/extras/package/flatpak/vlc-flatpak.yaml"

