#!/usr/bin/env bash
set -euo pipefail
binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi
for scene in volcano_replay volcano_cooling_domains volcano_overlay_raycast; do
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-${scene}.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/$scene.dsl" "$runtime/check.dsl"
set +e
(cd "$runtime" && timeout 120 "$binary" --debug-resolution 960x640 \
    --debug-script "$runtime/check.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'volcano e2e artifacts: %s\n' "$runtime"
if ((status != 0)); then
    tail -60 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL geology|DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
if [[ "$scene" == volcano_replay ]]; then
    [[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 3 ]]
fi
done
printf '%s\n' 'volcano e2e passed'
