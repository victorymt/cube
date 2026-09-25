#!/usr/bin/env bash

set -euo pipefail

game_binary=${1:-build/normal/voxelcraft}
command_timeout=${E2E_GRAPHICS_COMMAND_TIMEOUT:-120}
debug_resolution=${E2E_GRAPHICS_RESOLUTION:-1280x720}

if [[ ! -x "$game_binary" ]]; then
    echo "game binary is not executable: $game_binary" >&2
    exit 2
fi
game_binary=$(realpath "$game_binary")

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo "an active Hyprland/Wayland session is required" >&2
    exit 2
fi

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-graphics-e2e-runtime.XXXXXX")
ln -s "$(pwd)/assets" "$runtime_dir/assets"
trace_path="$runtime_dir/trace.jsonl"

coproc GAME_PROCESS {
    cd "$runtime_dir"
    env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-stdin \
        --debug-trace "$trace_path" \
        --debug-resolution="$debug_resolution" 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"

cleanup() {
    local status=$?
    if kill -0 "$game_pid" >/dev/null 2>&1; then
        printf 'exit 1\n' >&"$game_input" || true
        kill "$game_pid" >/dev/null 2>&1 || true
    fi
    if ((status == 0)) && [[ "${E2E_KEEP_ARTIFACTS:-0}" != 1 ]]; then
        rm -rf "$runtime_dir"
    else
        printf 'graphics debug e2e artifacts (exit=%s): runtime=%s trace=%s replies=%s\n' \
            "$status" "$runtime_dir" "$trace_path" "$debug_protocol_log" >&2
    fi
}
trap cleanup EXIT

debug_protocol_log="$runtime_dir/replies.log"
source tests/debug_stdin_protocol.sh

wait_for_reply '^DEBUG_CONTROL ready '
send_command 'start'
wait_for_reply '^DEBUG_CONTROL start ok seed=1448040515$' 120
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Regional state is prepared by the simulation, and map/atlas reads must not
# grow it while paused. Keep a checkpoint through the public DSL/save path.
send_command 'wait until !ecology.catchup_pending && ecology.stored_regions > 0 timeout 600'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Session and presentation state must be reversible without restarting the
# window. These commands exercise the same pause, map, camera, and look paths
# used by interactive play.
send_command 'pause on'
wait_for_reply '^DEBUG_CONTROL pause enabled=1$'
send_command 'assert game.paused'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'let ecology_saved_count = ecology.stored_regions'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'let ecology_saved_fauna = evolution.herbivore_density'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'evolution region'
wait_for_reply '^DEBUG_CONTROL evolution region ok '
send_command 'assert ecology.stored_regions == ecology_saved_count && evolution.herbivore_density == ecology_saved_fauna && ecology.failed_admissions == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'save'
wait_for_reply '^DEBUG_CONTROL save result=Saved map '
send_command 'load'
wait_for_reply '^DEBUG_CONTROL load result=Loaded voxelcraft_save' 120
send_command 'assert ecology.stored_regions == ecology_saved_count && evolution.herbivore_density == ecology_saved_fauna'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'pause off'
wait_for_reply '^DEBUG_CONTROL pause enabled=0$'
send_command 'assert !game.paused'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'hud debug on'
wait_for_reply '^DEBUG_CONTROL hud debug enabled=1$'
send_command 'hud debug off'
wait_for_reply '^DEBUG_CONTROL hud debug enabled=0$'

send_command 'view third'
wait_for_reply '^DEBUG_CONTROL view ok mode=third$'
send_command 'view first'
wait_for_reply '^DEBUG_CONTROL view ok mode=first$'
send_command 'look 1.250000 -0.500000'
wait_for_reply '^DEBUG_CONTROL look ok yaw=1\.250000 pitch=-0\.500000$'
send_command 'look delta -0.250000 0.100000'
wait_for_reply '^DEBUG_CONTROL look ok yaw=1\.000000 pitch=-0\.400000$'

# Open and close the map, including its subsurface-liquid layer. The map
# command is intentionally toggled back before continuing so subsequent
# interaction commands run through the ordinary playing HUD.
send_command 'map'
wait_for_reply '^DEBUG_CONTROL map open$'
send_command 'map layer liquids on'
wait_for_reply '^DEBUG_CONTROL map layer liquids enabled=1$'
send_command 'map layer liquids off'
wait_for_reply '^DEBUG_CONTROL map layer liquids enabled=0$'
send_command 'map'
wait_for_reply '^DEBUG_CONTROL map closed$'

# Exercise a deterministic surface transition and wait for its local stream
# window to settle before injecting interaction input.
send_command 'surface debug home'
wait_for_reply '^DEBUG_CONTROL surface debug ok world=home seed=1448040515$' 120
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 120
send_command 'wait until hydrology.active timeout 300'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 120
send_command 'assert hydrology.step_count > 0 && hydrology.total_water_m3 >= 0 && hydrology.total_sediment_kg >= 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Hydrology follows the same canonical map partition as radiation and weather.
# Compare against the region implied by the actual player pose because floating
# point pose canonicalization may leave a boundary coordinate just below 1024.
send_command 'teleport 1024 95 256 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=1024.000000,95.000000,256.000000$'
send_command 'wait until hydrology.active && hydrology.region_x == hydrology.player_region_x && hydrology.region_z == hydrology.player_region_z timeout 600'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 120
send_command 'teleport 0 95 0 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=0.000000,95.000000,0.000000$'
send_command 'wait until hydrology.active && hydrology.region_x == hydrology.player_region_x && hydrology.region_z == hydrology.player_region_z timeout 600'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 120

send_command 'mouse right'
wait_for_reply '^DEBUG_CONTROL mouse right queued$'
send_command 'mouse left'
wait_for_reply '^DEBUG_CONTROL mouse left queued frame=[0-9]+$'
send_command 'wait until interaction.mouse_left_handle_frame > 0 timeout 120'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$' 120

# Debug flight is self-contained and leaves the process in driving mode only
# until the test exits. This covers ship begin/input/exhaust/dust callbacks and
# the status snapshot used by the DSL resolver.
send_command 'ship begin'
wait_for_reply '^DEBUG_CONTROL ship begin ok position='
send_command 'assert ship.driving && ship.mode == "MANEUVER"'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'ship input 0.250 0.000 0.000 5'
wait_for_reply '^DEBUG_CONTROL ship input ok forward=0\.250 strafe=0\.000 vertical=0\.000 frames=5$'
send_command 'ship exhaust 0.500'
wait_for_reply '^DEBUG_CONTROL ship exhaust ok demand=0\.500$'
send_command 'ship dust'
wait_for_reply '^DEBUG_CONTROL ship dust ok$'
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'ship_driving=1'* ]]
[[ "$matched_line" == *'third_person=0'* ]]

send_command 'exit 0'
wait_for_reply '^DEBUG_SCRIPT exit code=0$' 30
exec {game_input}>&-
wait "$game_pid"
printf '%s\n' 'graphics debug e2e passed: pause=map=surface=view=look=mouse=ship'
