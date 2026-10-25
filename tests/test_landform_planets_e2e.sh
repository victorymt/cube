#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
scene=$(realpath tests/scenes/landform_planets.dsl)
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-landform-planets.XXXXXX")
ln -s "$(pwd)/assets" "$runtime_dir/assets"
printf 'landform planet replay artifacts: %s\n' "$runtime_dir"
(
    cd "$runtime_dir"
    timeout "${LANDFORM_E2E_TIMEOUT:-240}" env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-script "$scene" --debug-resolution=640x480 \
        > game.log 2>&1
) || { tail -40 "$runtime_dir/game.log" >&2; exit 1; }
grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime_dir/game.log"
printf '%s\n' 'landform planet replay passed: strategies, diagnostics, save/load, body switching'
