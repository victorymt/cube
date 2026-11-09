#!/bin/sh
#
# G1 visual replay: the same flat vertical wall is captured through legacy and
# cube-grid. The unit test owns the exact per-vertex transform invariant; this
# scene keeps the real Hyprland render path, cube activation, block edits and
# mesh upload deterministic and reviewable. It does not claim polar coverage.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-vertical.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_vertical_geometry_replay

reports=$(find "$run_dir/screenshots" -name '*.txt' | wc -l | tr -d ' ')
images=$(find "$run_dir/screenshots" -name '*.png' | wc -l | tr -d ' ')
[ "$reports" -eq 2 ] || {
    printf 'expected 2 capture reports, got %s\n' "$reports" >&2
    exit 1
}
[ "$images" -eq 2 ] || {
    printf 'expected 2 screenshots, got %s\n' "$images" >&2
    exit 1
}
grep -q '^camera.position=0.000000,10.550000,0.000000$' \
    "$run_dir"/screenshots/*.txt || {
    echo 'cube/legacy captures did not share the expected camera pose' >&2
    exit 1
}
grep -q 'surface debug ok world=cube' "$run_dir/game.log" || {
    echo 'cube activation was not observed in the replay log' >&2
    exit 1
}

printf 'cube vertical geometry replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
