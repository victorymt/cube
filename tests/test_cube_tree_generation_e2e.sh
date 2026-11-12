#!/bin/sh
# Cube-native tree replay: generation, cube-address diagnostics, structure
# ownership and the real flora render path must agree under Hyprland.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-trees.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_tree_generation

reports=$(find "$run_dir/screenshots" -name '*.txt' | wc -l | tr -d ' ')
images=$(find "$run_dir/screenshots" -name '*.png' | wc -l | tr -d ' ')
[ "$reports" -eq 1 ] && [ "$images" -eq 1 ] || {
    echo 'expected one cube-tree capture' >&2
    exit 1
}
grep -q 'surface address ok face=0 u=1884 v=1762' "$run_dir/game.log" || {
    echo 'the deterministic tree address was not reached' >&2
    exit 1
}
grep -Eq 'flora near ok .* count=[1-9][0-9]* ' "$run_dir/game.log" || {
    echo 'no cube-native flora roots were reported' >&2
    exit 1
}
grep -q 'flora near item .* root_address=0,1884,1762,85 ' \
    "$run_dir/game.log" || {
    echo 'the expected Scots Pine root was not reported' >&2
    exit 1
}
flora_draws=$(sed -n 's/^render.coverage.main_flora_draws=//p' \
    "$run_dir"/screenshots/*.txt)
flora_triangles=$(sed -n 's/^render.coverage.main_flora_triangles=//p' \
    "$run_dir"/screenshots/*.txt)
[ -n "$flora_draws" ] && [ "$flora_draws" -gt 0 ] &&
    [ -n "$flora_triangles" ] && [ "$flora_triangles" -gt 0 ] || {
    echo 'the cube-tree capture did not render a flora mesh' >&2
    exit 1
}

printf 'cube native tree generation passed; draws=%s triangles=%s artifacts=%s\n' \
    "$flora_draws" "$flora_triangles" "$run_dir"
trap - EXIT
