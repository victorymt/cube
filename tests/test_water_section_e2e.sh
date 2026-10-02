#!/usr/bin/env bash
set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-water-section.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
printf 'water section e2e artifacts: %s\n' "$runtime"

set +e
(cd "$runtime" && timeout 120 "$binary" \
    --chunk-workers "${WATER_SECTION_WORKERS:-auto}" \
    --debug-resolution 1546x930 \
    --debug-script "$repo/tests/scenes/water_section_replay.dsl" \
    --debug-trace "$runtime/trace.jsonl") > "$runtime/game.log" 2>&1
status=$?
set -e
if ((status != 0)); then
    tail -50 "$runtime/game.log"
    exit "$status"
fi

# The lower section must stay untouched both before and after the 30 fluid ticks.
[[ $(rg -c '^DEBUG_CONTROL fluid inspect ok position=226,63,276 volume=0 .* edits=0 ' \
    "$runtime/game.log") == 2 ]]

shopt -s nullglob
reports=("$runtime"/screenshots/*.txt)
[[ ${#reports[@]} == 2 ]]
for report in "${reports[@]}"; do
    # Fully flooded ocean interiors have no water/air faces at eye level.
    for expected in \
        'environment.eyes_submerged=true' \
        'fluid.local_volume=255' \
        'streaming.surface_ready=true' \
        'streaming.water_section_triangle_count=0'; do
        if ! rg -Fxq "$expected" "$report"; then
            printf 'missing %s in %s\n' "$expected" "$report" >&2
            exit 1
        fi
    done
done
rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
printf '%s\n' 'water section e2e passed (descent and save/load)'
