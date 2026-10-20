#!/usr/bin/env bash
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
binary=${VOXELCRAFT_BINARY:-$repo/build/normal/voxelcraft}
scene=${1:?usage: tests/run_scene.sh SCENE [WIDTHxHEIGHT]}
resolution=${2:-1280x720}
timeout_seconds=${VOXELCRAFT_SCENE_TIMEOUT:-300}

if [[ "$scene" == */* ]]; then
    scene_path=$scene
else
    scene_path=$repo/tests/scenes/$scene
fi

preflight() {
    local failed=0
    if [[ ! -x "$binary" ]]; then
        printf 'error: game binary not executable: %s\n' "$binary" >&2
        printf 'build it with: make\n' >&2
        failed=1
    fi
    if [[ ! -f "$scene_path" ]]; then
        printf 'error: scene not found: %s\n' "$scene_path" >&2
        failed=1
    fi
    if [[ ! -d "$repo/assets" ]]; then
        printf 'error: assets directory missing: %s\n' "$repo/assets" >&2
        failed=1
    fi
    if [[ -z "${XDG_RUNTIME_DIR:-}" || ! -d "${XDG_RUNTIME_DIR:-/nonexistent}" ]]; then
        printf 'error: XDG_RUNTIME_DIR is unset or not a directory\n' >&2
        failed=1
    fi
    if [[ -z "${DISPLAY:-}" && -z "${WAYLAND_DISPLAY:-}" ]]; then
        printf 'error: neither DISPLAY nor WAYLAND_DISPLAY is set\n' >&2
        failed=1
    elif [[ -z "${DISPLAY:-}" ]]; then
        printf 'warning: DISPLAY is unset; this raylib/GLFW build normally needs X11\n' >&2
    fi
    if [[ ! -d /dev/dri ]]; then
        printf 'warning: /dev/dri is unavailable; rendering may fail\n' >&2
    fi
    return "$failed"
}

preflight

runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-scene.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cp "$scene_path" "$runtime/preview.dsl"
printf '\nexit 0\n' >> "$runtime/preview.dsl"
printf 'run: scene=%s runtime=%s\n' "$scene_path" "$runtime"

set +e
(cd "$runtime" && timeout "$timeout_seconds" "$binary" --debug-resolution "$resolution" \
    --debug-script "$runtime/preview.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e

printf 'artifacts: %s\n' "$runtime"
if ((status != 0)); then
    printf 'scene failed with status %d; last log lines:\n' "$status" >&2
    tail -50 "$runtime/game.log" >&2
    exit "$status"
fi
if ! rg -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"; then
    printf 'scene did not report a clean DSL exit; last log lines:\n' >&2
    tail -50 "$runtime/game.log" >&2
    exit 1
fi
captures=$(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log" 2>/dev/null || true)
printf 'captures: %s\n' "${captures:-0}"
printf 'screenshots: %s\n' "$runtime/screenshots"
