#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" || -z "${WAYLAND_DISPLAY:-}" ||
      -z "${XDG_RUNTIME_DIR:-}" || ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-topology-v29.XXXXXX")
ln -s "$(pwd)/assets" "$runtime_dir/assets"
printf 'topology V29 replay artifacts: %s\n' "$runtime_dir"
command_timeout=${TOPOLOGY_E2E_TIMEOUT:-120}
printf 'assert settings.autosave == false\n' > "$runtime_dir/start.dsl"
coproc GAME_PROCESS {
    cd "$runtime_dir"
    env -u LIBGL_ALWAYS_SOFTWARE "$game_binary" --debug-stdin \
        --debug-script "$runtime_dir/start.dsl" --debug-resolution=640x480 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"
cleanup() {
    if kill -0 "$game_pid" 2>/dev/null; then
        printf 'exit 1\n' >&"$game_input" || true
        kill "$game_pid" 2>/dev/null || true
    fi
}
trap cleanup EXIT
debug_protocol_log="$runtime_dir/replies.log"
source tests/debug_stdin_protocol.sh
wait_for_reply '^DEBUG_CONTROL ready '
wait_for_reply '^DEBUG_SCRIPT complete source='
run_command() {
    send_command "$1"
    wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
}
save_world() {
    send_command save
    wait_for_reply '^DEBUG_CONTROL save result=Saved map '
    wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
}
load_world() {
    send_command load
    wait_for_reply '^DEBUG_CONTROL load result=Loaded '
    wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
}
reject_load() {
    send_command load
    wait_for_reply "^DEBUG_CONTROL load result=Load failed: $1"
    wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
}
run_command 'start'
run_command 'pause on'
run_command 'assert world.surface_topology == "legacy-map" && world.surface_topology_version == 1'
run_command 'teleport 4 200 8 0 0'
run_command 'stream wait 1200'
run_command 'block set 4 200 8 Glass'
run_command 'let home_pose = player.position'
save_world
cp "$runtime_dir/voxelcraft_save.txt" "$runtime_dir/home.save"
run_command 'teleport 20 200 20 0 0'
load_world
run_command 'assert player.position == home_pose && world.dimension == "home"'

run_command 'surface debug planet temperate 991'
run_command 'assert world.surface_topology == "legacy-map" && world.surface_topology_version == 1'
run_command 'teleport 40 200 50 0 0'
run_command 'stream wait 1200'
run_command 'block set 40 200 55 Brick'
run_command 'let planet_pose = player.position'
run_command 'let planet_body = world.surface_body'
save_world
run_command 'surface debug home'
load_world
run_command 'assert world.dimension == "planet" && world.surface_body == planet_body && player.position == planet_pose'
# Produce byte fixtures without asking the game to generate unsupported saves.
save_world
cp "$runtime_dir/voxelcraft_save.txt" "$runtime_dir/planet.save"
python3 - "$runtime_dir" <<'PY'
from pathlib import Path
import struct, sys
p = Path(sys.argv[1])
data = (p / 'planet.save').read_bytes()
assert data[:19] == b'VOXELCRAFT_SAVE_V29'
assert data[19:27] == b'TOPOL001'
assert struct.unpack_from('<4I', data, 27) == (1, 1, 1, 1)
for version in range(18, 29):
    (p / f'v{version}.save').write_bytes(f'VOXELCRAFT_SAVE_V{version}'.encode() + data[43:])
for name, offset, value in [('cube', 35, 2), ('unknown', 35, 99), ('version', 39, 2)]:
    buf = bytearray(data)
    struct.pack_into('<I', buf, offset, value)
    (p / f'{name}.save').write_bytes(buf)
(p / 'truncated.save').write_bytes(data[:35])
(p / 'late.save').write_bytes((p / 'home.save').read_bytes()[:-1])
PY
for version in {18..28}; do
    cp "$runtime_dir/v${version}.save" "$runtime_dir/voxelcraft_save.txt"
    reject_load 'unsupported save format\.'
    cmp "$runtime_dir/v${version}.save" "$runtime_dir/voxelcraft_save.txt"
done
for case in cube unknown version truncated; do
    cp "$runtime_dir/$case.save" "$runtime_dir/voxelcraft_save.txt"
    reject_load '(cube-grid topology|unknown surface topology|topology metadata)'
    cmp "$runtime_dir/$case.save" "$runtime_dir/voxelcraft_save.txt"
done
run_command 'assert world.dimension == "planet" && world.surface_body == planet_body && player.position == planet_pose'
run_command 'assert world.surface_topology == "legacy-map" && world.surface_topology_version == 1'
save_world
cmp "$runtime_dir/planet.save" "$runtime_dir/voxelcraft_save.txt"
# This file passes preflight, restores Home subsystems, then fails at the trailer.
# The transaction must restore the live planet and both bodies' edit records.
cp "$runtime_dir/late.save" "$runtime_dir/voxelcraft_save.txt"
reject_load 'spherical save state is corrupted\.'
run_command 'assert world.dimension == "planet" && world.surface_body == planet_body && player.position == planet_pose'
run_command 'assert world.surface_topology == "legacy-map" && world.surface_topology_version == 1'
save_world
cmp "$runtime_dir/planet.save" "$runtime_dir/voxelcraft_save.txt"
run_command 'stream wait 1200'
send_command 'block set 40 200 55 Brick'
wait_for_reply '^DEBUG_CONTROL block set ok .*previous_name="Brick"'
run_command 'surface debug home'
run_command 'teleport 4 200 8 0 0'
run_command 'stream wait 1200'
send_command 'block set 4 200 8 Glass'
wait_for_reply '^DEBUG_CONTROL block set ok .*previous_name="Glass"'
# Normal gameplay ascent provides a non-surface save without a topology switch.
run_command 'teleport 0 1000 0 0 0'
run_command 'pause off'
run_command 'wait until world.dimension == "space" timeout 600'
run_command 'pause on'
run_command 'assert world.surface_topology == "none" && world.surface_topology_version == 0'
save_world
load_world
run_command 'assert world.dimension == "space" && world.surface_topology == "none" && world.surface_topology_version == 0'
run_command 'surface debug home'
run_command 'assert world.surface_topology == "legacy-map" && world.surface_topology_version == 1'
send_command 'exit 0'
wait_for_reply '^DEBUG_SCRIPT exit code=0$'
wait "$game_pid"
printf '%s\n' 'topology V29 replay passed: round trips, retired/unsupported headers, late rollback, non-surface save'
