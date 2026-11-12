#!/bin/sh
# Hyprland replay for cube block collision. The unit suite enumerates all
# zero-height block families and body-corner overlap; this route proves real
# Flower/Tall Grass edits are passable, Stone remains solid, and a grounded
# player can sprint across the flat curved surface without sticking to it.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-decor-collision.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_decorative_collision

grep -q 'cell inspect ok .*block="Flower"' "$run_dir/game.log" || {
    echo 'flower collision fixture was not installed' >&2
    exit 1
}
grep -q 'cell inspect ok .*block="Tall Grass"' "$run_dir/game.log" || {
    echo 'tall-grass collision fixture was not installed' >&2
    exit 1
}
grep -q 'cell inspect ok .*block="Stone"' "$run_dir/game.log" || {
    echo 'stone collision control was not installed' >&2
    exit 1
}

printf 'cube decorative collision replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
