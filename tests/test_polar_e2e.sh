#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
project_root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
wall_timeout=${POLAR_E2E_TIMEOUT:-90}
if [[ ! "$wall_timeout" =~ ^[1-9][0-9]*$ || ! -x "$game_binary" ]]; then
    echo 'polar E2E requires an executable game and a positive POLAR_E2E_TIMEOUT' >&2
    exit 2
fi
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-polar-e2e.XXXXXX")
ln -s "$project_root/assets" "$runtime_dir/assets"
keep_artifacts=${POLAR_E2E_KEEP_ARTIFACTS:-0}
cleanup() {
    local result=$?
    if ((result != 0)) || [[ "$keep_artifacts" == 1 ]]; then
        printf 'polar E2E artifacts: %s\n' "$runtime_dir" >&2
    else
        rm -rf "$runtime_dir"
    fi
}
trap cleanup EXIT

started=$SECONDS
if (
    cd "$runtime_dir"
    timeout --kill-after=5s "${wall_timeout}s" env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-resolution=1920x1080 \
        --debug-script "$project_root/tests/scenes/polar_replay.dsl" \
        --debug-trace "$runtime_dir/trace.jsonl"
) >"$runtime_dir/game.log" 2>&1; then
    result=0
else
    result=$?
fi
cat "$runtime_dir/game.log"
if ((result != 0)); then
    echo "polar E2E failed: exit=$result elapsed=$((SECONDS - started))s budget=${wall_timeout}s" >&2
    exit "$result"
fi
grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime_dir/game.log"
grep -q '^DEBUG_CONTROL capture ok png=' "$runtime_dir/game.log"
progress=$(grep -c '^DEBUG_CONTROL input ok .*frames=100$' "$runtime_dir/game.log")
[[ "$progress" == 6 ]]
printf 'polar E2E passed: input_frames=600 elapsed=%ss budget=%ss\n' \
    "$((SECONDS - started))" "$wall_timeout"
