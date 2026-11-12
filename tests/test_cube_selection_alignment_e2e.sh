#!/bin/sh
#
# Hyprland face-edge selection probe. The unit test owns exact corner and
# cross-face equality; this route proves the real debug/render path accepts the
# edge address and emits reviewable white and green curved wireframes.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-selection.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_selection_alignment

reports=$(find "$run_dir/screenshots" -name '*.txt' | wc -l | tr -d ' ')
images=$(find "$run_dir/screenshots" -name '*.png' | wc -l | tr -d ' ')
[ "$reports" -eq 1 ] && [ "$images" -eq 1 ] || {
    echo 'expected one selection-alignment capture' >&2
    exit 1
}
grep -q 'surface address ok face=0 u=4095 v=2048' "$run_dir/game.log" || {
    echo 'face-edge address was not reached' >&2
    exit 1
}
grep -q 'render pick ok .* hit=1 ' "$run_dir/game.log" || {
    echo 'selection probe did not hit a block' >&2
    exit 1
}
draws=$(sed -n 's/^render.coverage.main_solid_draws=//p' \
    "$run_dir"/screenshots/*.txt)
[ -n "$draws" ] && [ "$draws" -gt 0 ] || {
    echo 'selection scene did not render solid terrain' >&2
    exit 1
}

printf 'cube selection alignment replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
