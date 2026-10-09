#!/usr/bin/env bash
set -euo pipefail
binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
resolution=${VOLCANO_TEST_RESOLUTION:-960x640}
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi
scenes=("${@:2}")
if ((${#scenes[@]} == 0)); then
    scenes=(volcano_deposition volcano_history volcano_history_world volcano_history_initial volcano_history_final volcano_history_edit volcano_terrain volcano_live volcano_replay volcano_cooling_domains volcano_overlay_raycast volcano_water_contact volcano_ash_layers volcano_rock_products volcano_collection volcano_rejected_deposit volcano_enabled volcano_disabled_force)
fi
for scene in "${scenes[@]}"; do
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-${scene}.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$repo/tests/scenes/$scene.dsl" "$runtime/check.dsl"
if [[ "$scene" == volcano_mountain || "$scene" == volcano_live ]]; then
    printf '\nexit 0\n' >> "$runtime/check.dsl"
fi
set +e
scene_timeout=120
[[ "$scene" == volcano_live ]] && scene_timeout=300
(cd "$runtime" && timeout "$scene_timeout" "$binary" --debug-resolution "$resolution" \
    --debug-script "$runtime/check.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'volcano e2e artifacts: %s\n' "$runtime"
if [[ "$scene" == volcano_rejected_deposit || "$scene" == volcano_disabled_force ]]; then
    [[ "$status" == 3 ]]
    rg 'code=callback message=debug command failed: geology_unavailable_or_invalid_state' "$runtime/game.log"
    continue
fi
if ((status != 0)); then
    tail -60 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL geology|DEBUG_CONTROL deposition |DEBUG_CONTROL capture ok|DEBUG_SCRIPT exit' "$runtime/game.log"
if [[ "$scene" == volcano_replay ]]; then
    [[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 3 ]]
elif [[ "$scene" == volcano_water_contact ]]; then
    [[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 2 ]]
elif [[ "$scene" == volcano_enabled ]]; then
    (cd "$runtime" && timeout 120 "$binary" --debug-resolution "$resolution" \
        --debug-script "$repo/tests/scenes/volcano_enabled_restart.dsl") \
        > "$runtime/restart.log" 2>&1
    rg 'DEBUG_CONTROL geology enabled=1 saved=1|DEBUG_SCRIPT exit' "$runtime/restart.log"
elif [[ "$scene" == volcano_history_world ]]; then
    (cd "$runtime" && timeout 120 "$binary" --debug-resolution "$resolution" \
        --debug-script "$repo/tests/scenes/volcano_history_world_restart.dsl") \
        > "$runtime/restart.log" 2>&1
    rg 'DEBUG_CONTROL geology history_world|DEBUG_SCRIPT exit' "$runtime/restart.log"
fi
done
printf '%s\n' 'volcano e2e passed'
