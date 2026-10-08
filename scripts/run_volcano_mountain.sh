#!/usr/bin/env bash
# Build and open a playable isolated volcano demonstration. Keep the world,
# settings, screenshots and log together; never load a user's normal save.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
make -C "$repo" -j"${JOBS:-8}" all
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-volcano-play.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
printf 'Volcano demo world and log: %s\n' "$runtime"
printf 'The game stays open after generation. Use the normal movement controls to explore.\n'
cd "$runtime"
exec "$repo/build/normal/voxelcraft" --debug-resolution "${VOLCANO_TEST_RESOLUTION:-1280x900}" \
    --debug-script "$repo/tests/scenes/volcano_mountain.dsl" \
    --debug-trace "$runtime/trace.jsonl" > "$runtime/game.log" 2>&1
