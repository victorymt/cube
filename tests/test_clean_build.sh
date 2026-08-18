#!/bin/sh

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
sandbox=$(mktemp -d /tmp/voxelcraft-clean-build.XXXXXX)
trap 'rm -rf -- "$sandbox"' EXIT

foreign_root="$sandbox/foreign"
mkdir -p "$foreign_root"
touch "$foreign_root/keep"
if (cd "$sandbox" &&
    sh "$repository/scripts/clean-build.sh" "$foreign_root" 2>/dev/null); then
    echo "clean guard accepted an unmanaged build root" >&2
    exit 1
fi
[ -f "$foreign_root/keep" ] || {
    echo "clean guard modified an unmanaged build root" >&2
    exit 1
}

touch "$foreign_root/.voxelcraft-build-root"
mkdir -p "$sandbox/dist"
touch "$sandbox/dist/archive"
(cd "$sandbox" && sh "$repository/scripts/clean-build.sh" "$foreign_root")
[ ! -e "$foreign_root" ] || {
    echo "clean guard did not remove a managed build root" >&2
    exit 1
}
[ ! -e "$sandbox/dist" ] || {
    echo "clean guard did not remove dist" >&2
    exit 1
}

mkdir -p "$sandbox/build"
touch "$sandbox/build/legacy-artifact"
(cd "$sandbox" && sh "$repository/scripts/clean-build.sh" build)
[ ! -e "$sandbox/build" ] || {
    echo "clean guard did not preserve default build compatibility" >&2
    exit 1
}

echo "clean build guard tests passed"
