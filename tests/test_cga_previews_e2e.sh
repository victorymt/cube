#!/usr/bin/env bash

set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)

scenes=(
    cga_house.dsl
    cga_classical_complex.dsl
    cga_mineshaft.dsl
    cga_dungeon.dsl
    cga_desert_temple.dsl
)

for scene in "${scenes[@]}"; do
    runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-${scene%.dsl}.XXXXXX")
    ln -s "$repo/assets" "$runtime/assets"
    cp "$repo/tests/scenes/$scene" "$runtime/preview.dsl"
    printf '\nexit 0\n' >> "$runtime/preview.dsl"
    set +e
    (cd "$runtime" && timeout 180 "$binary" --debug-resolution 1280x720 \
        --debug-script "$runtime/preview.dsl" \
        --debug-trace "$runtime/trace.jsonl") \
        > "$runtime/game.log" 2>&1
    status=$?
    set -e
    printf 'cga preview %s artifacts: %s\n' "$scene" "$runtime"
    if ((status != 0)); then
        tail -50 "$runtime/game.log"
        exit "$status"
    fi
    rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
    [[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 1 ]]
    rg -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"
done

review_scene=cga_classical_complex_review.dsl
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-${review_scene%.dsl}.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/$review_scene" "$runtime/preview.dsl"
printf '\nexit 0\n' >> "$runtime/preview.dsl"
set +e
(cd "$runtime" && timeout 180 "$binary" --debug-resolution 1280x720 \
    --debug-script "$runtime/preview.dsl" \
    --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'cga review %s artifacts: %s\n' "$review_scene" "$runtime"
if ((status != 0)); then
    tail -50 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL cga inspect|DEBUG_CONTROL cga visible|DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' \
    "$runtime/game.log"

[[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 4 ]]
[[ $(rg -c '^DEBUG_CONTROL cga visible .*on_screen=1 ' "$runtime/game.log") == 4 ]]
rg -q '^DEBUG_CONTROL cga inspect count=' "$runtime/game.log"
rg -q '^DEBUG_CGA feature id=.* grammar=7 ' "$runtime/game.log"
rg -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"

printf '%s\n' 'cga structure previews passed'
