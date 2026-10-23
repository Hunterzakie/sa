#!/bin/bash

THIS_SCRIPT_DIR="$(realpath "$(dirname "$0")")"
VLC_SRCPATH="$(realpath "$THIS_SCRIPT_DIR/../../../")"
FLATPAK_BUILD_DIR="$VLC_SRCPATH/flatpak-build"


apply_patches() {
  applied_any=false
  for patch in "$THIS_SCRIPT_DIR"/*.patch; do
    if patch -p1 < "$patch" > /dev/null 2>&1; then
      applied_any=true
    else
      echo "Failed to apply patch: $patch" >&2
    fi
  done

  if $applied_any; then
    echo "Patches applied from $THIS_SCRIPT_DIR"
  fi
}

unapply_patches() {
  reverted_any=false
  cd "$VLC_SRCPATH" || exit 1
  for patch in "$THIS_SCRIPT_DIR"/*.patch; do
    if patch -R -p1 < "$patch" > /dev/null 2>&1; then
      reverted_any=true
    else
      echo "Failed to revert patch: $patch" >&2
    fi
  done

  if $reverted_any; then
    echo "Patches reverted from $THIS_SCRIPT_DIR"
  fi
}


run_command() {
  apply_patches
  flatpak-builder --run \
    --socket=pulseaudio \
    --device=all \
    "$FLATPAK_BUILD_DIR/flatpak-build-dir" \
    "$THIS_SCRIPT_DIR/vlc-flatpak.yaml" \
    "$@"
  unapply_patches
}

print_help() {
  echo "Usage: $0 <subcommand> [options]"
  echo
  echo "Subcommands:"
  echo "  install-build            Run initial Flatpak build and install (required for running"
  echo "                           rest of the commands)"
  echo "                           Next time builds must be confirmed with -f to clean flatpak-build-dir"
  echo "                           directory."
  echo "  "
  echo "  shell [-c <command>]     Run an interactive shell or execute command inside Flatpak sandbox"
  echo "  contrib-make [args...]   Run make inside the contrib directory (e.g., contrib-make -j8)"
  echo "  make [args...]           Run make inside the build directory (e.g., make -j8)"
  echo "  run                      Run the already made vlc instance"
  echo "  clean -f                 Delete the build directory (must be confirmed with -f)"
  echo "  help                     Show this help message"
}


check_built() {
  if [[ ! -d "$FLATPAK_BUILD_DIR" ]]; then
    echo "Error: Build directory does not exist."
    echo "You need to run '$0 install-build' first."
    exit 1
  fi
}

subcommand="$1"
shift || true

case "$subcommand" in
  shell)
    if [[ "$1" == "-c" ]]; then
      shift
      run_command sh -c "$*"
    else
      run_command sh -c 'PS1="[\[\e[38;5;208m\]➤ vlc \[\e[32m\]\W\[\e[0m\]] \$ " exec bash --noprofile --norc'
    fi
    ;;
  install-build)
    if [[ "$1" == "-f" ]]; then
      $THIS_SCRIPT_DIR/build.sh -f
    fi
    $THIS_SCRIPT_DIR/build.sh
    ;;
  clean)
    if [[ "$1" != "-f" ]]; then
      echo "Error: 'clean' must be followed by -f to confirm deletion."
      echo "Usage: $0 clean -f"
      exit 1
    fi
    echo "Removing build directory: $FLATPAK_BUILD_DIR"
    rm -rf "$FLATPAK_BUILD_DIR"
    ;;
  make)
    check_built
    run_command sh -c "cd '$FLATPAK_BUILD_DIR/build' && make $*"
    ;;
  contrib-make)
    check_built
    run_command sh -c "cd '$FLATPAK_BUILD_DIR/contrib/contr' && make $*"
    ;;
  run)
    check_built
    run_command sh -c "$FLATPAK_BUILD_DIR/build/vlc"
    ;;
  help|"")
    print_help
    ;;
  *)
    echo "Unknown subcommand: $subcommand"
    print_help
    exit 1
    ;;
esac

