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

for requirement in hyprctl; do
    if ! command -v "$requirement" >/dev/null 2>&1; then
        echo "$requirement is required for the graphics debug e2e test" >&2
        exit 2
    fi
done

if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo "an active Hyprland/Wayland session is required" >&2
    exit 2
fi
hyprctl monitors -j >/dev/null

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-graphics-e2e-runtime.XXXXXX")
ln -s "$(pwd)/assets" "$runtime_dir/assets"

coproc GAME_PROCESS {
    cd "$runtime_dir"
    env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-stdin \
        --debug-resolution="$debug_resolution" 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"

cleanup() {
    if kill -0 "$game_pid" >/dev/null 2>&1; then
        printf 'exit 1\n' >&"$game_input" || true
        kill "$game_pid" >/dev/null 2>&1 || true
    fi
    rm -rf "$runtime_dir"
}
trap cleanup EXIT

matched_line=""

wait_for_reply() {
    local pattern=$1
    local timeout_seconds=${2:-$command_timeout}
    local deadline=$((SECONDS + timeout_seconds))
    local line

    matched_line=""
    while ((SECONDS < deadline)); do
        if IFS= read -r -t 1 line <&"$game_output"; then
            printf '%s\n' "$line"
            if [[ "$line" =~ $pattern ]]; then
                matched_line=$line
                return 0
            fi
        elif ! kill -0 "$game_pid" >/dev/null 2>&1; then
            echo "game exited while waiting for: $pattern" >&2
            return 1
        fi
    done

    echo "timed out waiting for: $pattern" >&2
    return 1
}

send_command() {
    printf '%s\n' "$1" >&"$game_input"
}

wait_for_reply '^DEBUG_CONTROL ready '
send_command 'start'
wait_for_reply '^DEBUG_CONTROL start ok seed=1448040515$' 120
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Session and presentation state must be reversible without restarting the
# window. These commands exercise the same pause, map, camera, and look paths
# used by interactive play.
send_command 'pause on'
wait_for_reply '^DEBUG_CONTROL pause enabled=1$'
send_command 'assert game.paused'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'pause off'
wait_for_reply '^DEBUG_CONTROL pause enabled=0$'
send_command 'assert !game.paused'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

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
