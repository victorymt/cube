#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-diagnostics-e2e.XXXXXX")
ln -s "$(pwd)/assets" "$runtime_dir/assets"
cleanup() {
    local status=$?
    if ((status == 0)) && [[ "${E2E_KEEP_ARTIFACTS:-0}" != 1 ]]; then
        rm -rf "$runtime_dir"
    else
        printf 'chunk diagnostics e2e artifacts (exit=%s): %s\n' "$status" "$runtime_dir" >&2
        tail -40 "$runtime_dir/game.log" >&2 || true
    fi
}
trap cleanup EXIT

cat > "$runtime_dir/check.dsl" <<'DSL'
start
wait until game.screen == "playing" timeout 7200
pause on
assert stream.chunks.status == "valid" && stream.chunks.lod_consistency == "not_captured"
assert stream.chunks.capture_id == stream.chunks.capture_id
assert stream.lod_capture_id == stream.lod_capture_id
assert stream.lod_frame_id == stream.chunks.frame_id && stream.lod_world_generation == stream.chunks.world_generation
assert stream.chunks.capture_id < stream.lod_capture_id && stream.chunks.capture_id < stream.lod_capture_id && stream.chunks.lod_consistency == "not_captured"
let base = stream.chunks.capture_id
let detail = stream.lod_capture_id
let frame = stream.chunks.frame_id
look 0 -0.3
assert stream.chunks.capture_id > base && stream.lod_capture_id > detail
wait until stream.chunks.frame_id > frame timeout 120
assert stream.lod_frame_id == stream.chunks.frame_id
let home_generation = stream.chunks.world_generation
surface debug planet temperate 424242
assert world.dimension == "planet"
assert stream.chunks.world_generation > home_generation && stream.lod_world_generation == stream.chunks.world_generation
let planet_generation = stream.chunks.world_generation
surface debug home
assert world.dimension == "home"
assert stream.chunks.world_generation > planet_generation && stream.lod_world_generation == stream.chunks.world_generation
assert stream.chunks.last_event_sequence == stream.chunks.blocked_reason_sequence
assert stream.chunks.completed_generation_jobs >= 0 && stream.chunks.completed_mesh_jobs >= 0
exit 0
DSL

(
    cd "$runtime_dir"
    timeout "${E2E_DIAGNOSTICS_TIMEOUT:-180}" env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-script "$runtime_dir/check.dsl" \
        --debug-resolution=640x480 > game.log 2>&1
)
rg -q '^DEBUG_SCRIPT exit code=0$' "$runtime_dir/game.log"
printf '%s\n' 'chunk diagnostics e2e passed: base/lod cache, command/frame invalidation, planet/home identity'
