#!/usr/bin/env bash
set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cloud-altitude.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/cloud_altitude_replay.dsl" "$runtime/check.dsl"
set +e
(cd "$runtime" && timeout 120 "$binary" --debug-resolution 960x640 \
    --debug-script "$runtime/check.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'cloud altitude e2e artifacts: %s\n' "$runtime"
if ((status != 0)); then
    tail -50 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
[[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 4 ]]
for report in "$runtime"/screenshots/*.txt; do
    rg -q '^weather.cloud_reference_y=80\.000000$' "$report"
    rg -q '^weather.cloud_layer_0_base_height=19\.600000$' "$report"
done
printf '%s\n' 'cloud altitude e2e passed'
