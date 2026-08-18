#!/bin/sh

set -eu

build_root=${1-}
marker=.voxelcraft-build-root

fail()
{
    echo "clean refused: $1" >&2
    exit 2
}

case "$build_root" in
    ''|/|.|..|../*|*/..|*/../*)
        fail "unsafe BUILD_ROOT '$build_root'"
        ;;
esac

if [ -e "$build_root" ] || [ -L "$build_root" ]; then
    if [ "$build_root" != build ] && [ ! -f "$build_root/$marker" ]; then
        fail "'$build_root' is not a managed voxelcraft build directory"
    fi
    rm -rf -- "$build_root"
fi
rm -rf -- dist
