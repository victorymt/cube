#!/bin/sh
# Hyprland replay for first-person strafe. Unit coverage pins the camera-frame
# vector; this route proves the scripted game input and both movement paths
# consume the same screen-right direction.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-strafe.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    player_strafe_direction

grep -q 'DEBUG_SCRIPT exit code=0' "$run_dir/game.log" || {
    echo 'strafe replay did not complete' >&2
    exit 1
}

printf 'player strafe direction replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
