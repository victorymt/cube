#!/bin/sh
#
# M4.2 cross-face fluid replay: a finite source cell is placed one block
# inside a cube face edge. The scene steps fluid and inspects the adjacent map
# coordinate, which resolves to the neighboring face. It does not claim a
# visual water render or a save/load round trip; those remain separate gates.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-fluid-edge.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_fluid_face_edge

grep -q 'surface debug ok world=cube' "$run_dir/game.log" || {
    echo 'cube activation was not observed in the replay log' >&2
    exit 1
}

printf 'cube fluid face-edge replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
