#!/usr/bin/env bash

set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cga-house.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cat > "$runtime/house.dsl" <<'DSL'
terrain varied
start
wait until game.screen == "playing" timeout 7200
wait until stream.surface_ready timeout 1800
teleport -1280 110 -2075 0 -0.35
stream wait 600
teleport -1280 110 -2075 0 -0.35
screenshot
status
exit 0
DSL

set +e
(cd "$runtime" && timeout 180 "$binary" --debug-resolution 1280x720 \
    --debug-script "$runtime/house.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'cga house e2e artifacts: %s\n' "$runtime"
if ((status != 0)); then
    tail -50 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
[[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 1 ]]
printf '%s\n' 'cga house e2e passed'
