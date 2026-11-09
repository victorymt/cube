#!/bin/sh
#
# G2 complete-cell replay: save a flower sharing a cell with water, exercise
# volume-aware undo/redo, then load in a fresh process and compare the exact
# volume recorded by the save run. This does not migrate V29 and does not run
# cube fluid, which remains suspended until the consumer migration.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
save_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-v30-cell-save.XXXXXX")
load_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-v30-cell-load.XXXXXX")
trap 'rm -rf "$save_dir" "$load_dir"' EXIT

cd "$repo"
scripts/run-scene.sh --binary "$binary" --run-dir "$save_dir" \
    v30_cell_reload_save
save_file="$save_dir/voxelcraft_save.txt"
[ -f "$save_file" ] || { echo 'V30 cell save was not created' >&2; exit 1; }
expected=$(sed -n 's/.*cell inspect ok .*water_volume=\([0-9][0-9]*\).*/\1/p' \
    "$save_dir/game.log" | tail -1)
[ -n "$expected" ] && [ "$expected" -gt 0 ] || {
    echo 'save run did not report a positive cell volume' >&2
    exit 1
}
python3 - "$save_file" <<'PY'
from pathlib import Path
import struct, sys
data = Path(sys.argv[1]).read_bytes()
assert data[:19] == b'VOXELCRAFT_SAVE_V30'
assert data[19:27] == b'TOPOL002'
assert b'CUBADDR2' in data
PY

load_scene="$load_dir/cell_reload.dsl"
cat > "$load_scene" <<DSL
assert game.screen == "start"
terrain flat
start
wait until game.screen == "playing" timeout 7200
load
pause on
stream wait 1200
cell inspect 4 12 8
assert cell.known && cell.block == "Flower"
assert cell.water_volume == $expected
exit 0
DSL
cp "$save_file" "$load_dir/voxelcraft_save.txt"
scripts/run-scene.sh --binary "$binary" --run-dir "$load_dir" --force \
    --scene-file "$load_scene"
grep -q "cell inspect ok .*block=\"Flower\" water_volume=$expected" \
    "$load_dir/game.log"

printf 'V30 complete-cell reload passed; volume=%s save=%s load=%s\n' \
    "$expected" "$save_dir" "$load_dir"
trap - EXIT
