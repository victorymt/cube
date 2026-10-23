#!/bin/sh

set -eu

# Installs the one raylib version this tree builds against. RAYLIB_VERSION is
# set once in .github/workflows/ci.yml and defaults to the same value as
# RAYLIB_VERSION_REQUIRED in the Makefile; keep the three in sync.
#
# RAYLIB_WAYLAND=1 builds the GLFW Wayland backend instead of X11 only. The
# graphics-E2E job needs it because the runner is a Hyprland session with no
# reachable X11 display; unit and sanitizer jobs do not render and stay with
# the faster default.
raylib_version=${RAYLIB_VERSION:-6.0}
raylib_wayland=${RAYLIB_WAYLAND:-0}

source_dir=$(mktemp -d /tmp/raylib-build.XXXXXX)
archive_path="$source_dir/raylib.tar.gz"

curl --fail --location --retry 3 \
    "https://github.com/raysan5/raylib/archive/refs/tags/${raylib_version}.tar.gz" \
    --output "$archive_path"
mkdir -p "$source_dir/source"
tar -xzf "$archive_path" -C "$source_dir/source" --strip-components=1

raylib_make_args="PLATFORM=PLATFORM_DESKTOP"
if [ "$raylib_wayland" = "1" ]; then
    raylib_make_args="$raylib_make_args GLFW_LINUX_ENABLE_WAYLAND=TRUE GLFW_LINUX_ENABLE_X11=TRUE"
fi

# Intentional word splitting: raylib_make_args is a list of make assignments.
# shellcheck disable=SC2086
make -C "$source_dir/source/src" -j2 $raylib_make_args
# shellcheck disable=SC2086
sudo make -C "$source_dir/source/src" install $raylib_make_args
sudo ldconfig

installed=$(pkg-config --modversion raylib)
printf 'raylib installed: %s (wayland=%s)\n' "$installed" "$raylib_wayland"

case "$installed" in
    "$raylib_version"|"$raylib_version".*) ;;
    *)
        printf 'raylib version mismatch: installed %s, expected %s\n' \
            "$installed" "$raylib_version" >&2
        printf 'a second raylib is shadowing this one; check /usr/local/lib/pkgconfig/raylib.pc\n' >&2
        exit 1
        ;;
esac
