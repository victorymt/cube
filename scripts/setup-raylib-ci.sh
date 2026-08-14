#!/bin/sh

set -eu

raylib_version=${RAYLIB_VERSION:-5.5}
source_dir=$(mktemp -d /tmp/raylib-build.XXXXXX)
archive_path="$source_dir/raylib.tar.gz"

curl --fail --location --retry 3 \
    "https://github.com/raysan5/raylib/archive/refs/tags/${raylib_version}.tar.gz" \
    --output "$archive_path"
mkdir -p "$source_dir/source"
tar -xzf "$archive_path" -C "$source_dir/source" --strip-components=1

make -C "$source_dir/source/src" -j2 PLATFORM=PLATFORM_DESKTOP
sudo make -C "$source_dir/source/src" install PLATFORM=PLATFORM_DESKTOP
sudo ldconfig

pkg-config --modversion raylib
