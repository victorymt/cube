#!/bin/sh
#
# G2 cross-process replay: phase 1 saves a cube Homeworld through the debug gate.
# Phase 2 starts with the gate closed and restores cube topology, canonical pose
# and the complete cell edit from the V30 header instead of enabling debug cube.
# It does not test V29 migration; legacy saves must be rejected.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
save_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-v30-cube-save.XXXXXX")
load_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-v30-cube-load.XXXXXX")
trap 'rm -rf "$save_dir" "$load_dir"' EXIT

cd "$repo"
scripts/run-scene.sh --binary "$binary" --run-dir "$save_dir" \
    v30_cube_reload_save
save_file="$save_dir/voxelcraft_save.txt"
[ -f "$save_file" ] || { echo 'V30 save was not created' >&2; exit 1; }
python3 - "$save_file" <<'PY'
from pathlib import Path
import sys
data = Path(sys.argv[1]).read_bytes()
assert data[:19] == b'VOXELCRAFT_SAVE_V30'
assert data[19:27] == b'TOPOL002'
assert b'CUBADDR2' in data
PY

cp "$save_file" "$load_dir/voxelcraft_save.txt"
scripts/run-scene.sh --binary "$binary" --run-dir "$load_dir" --force \
    v30_cube_reload_load

grep -q 'load result=Loaded voxelcraft_save.txt (1 edits)' \
    "$load_dir/game.log"
grep -q 'block set ok .*previous_name="Glass"' "$load_dir/game.log"
printf 'V30 fresh-process cube reload passed; save=%s load=%s\n' \
    "$save_dir" "$load_dir"
trap - EXIT
