#!/usr/bin/env bash

set -euo pipefail

game_binary=${1:-build/normal/voxelcraft}
settle_timeout=${E2E_SETTLE_TIMEOUT:-60}
command_timeout=${E2E_COMMAND_TIMEOUT:-60}

if [[ ! -x "$game_binary" ]]; then
    echo "game binary is not executable: $game_binary" >&2
    exit 2
fi

for requirement in python3; do
    if ! command -v "$requirement" >/dev/null 2>&1; then
        echo "$requirement is required for the game end-to-end test" >&2
        exit 2
    fi
done

game_runner=()
if command -v xvfb-run >/dev/null 2>&1; then
    game_runner=(xvfb-run -a)
elif [[ -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    echo "xvfb-run not found; using the existing display session"
else
    echo "xvfb-run or an existing display session is required" >&2
    exit 2
fi

coproc GAME_PROCESS {
    env LIBGL_ALWAYS_SOFTWARE=1 "${game_runner[@]}" \
        "$game_binary" --debug-stdin 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"

cleanup() {
    if kill -0 "$game_pid" >/dev/null 2>&1; then
        printf 'quit\n' >&"$game_input" || true
        kill "$game_pid" >/dev/null 2>&1 || true
    fi
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

send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'screen=playing seed=1448040515 dimension=home'* ]]
[[ "$matched_line" == *'water=0,0,0'* ]]

send_command 'evolution inspect'
wait_for_reply '^DEBUG_CONTROL evolution inspect none radius=24.000$'
send_command 'evolution advance 1'
wait_for_reply '^DEBUG_CONTROL evolution advance ok days=1.000$'

send_command 'teleport 0.5 76.002960 0.5 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok '

water_ready=false
settle_deadline=$((SECONDS + settle_timeout))
while ((SECONDS < settle_deadline)); do
    sleep 1
    send_command 'teleport 0.5 76.002960 0.5 3.141593 -0.25'
    wait_for_reply '^DEBUG_CONTROL teleport ok '
    send_command 'status'
    wait_for_reply '^DEBUG_CONTROL status '
    if [[ "$matched_line" == *'water=1,1,1'* &&
          "$matched_line" == *'surface=81.000000'* ]]; then
        water_ready=true
        break
    fi
done

if [[ "$water_ready" != true ]]; then
    echo "underwater chunk did not become ready within ${settle_timeout}s" >&2
    exit 1
fi

send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

png_path=${matched_line#*png=}
png_path=${png_path%% report=*}
report_path=${matched_line##*report=}

[[ -s "$png_path" ]]
[[ -s "$report_path" ]]
grep -Fxq 'format.version=3' "$report_path"
grep -Fxq 'world.seed=1448040515' "$report_path"
grep -Fxq 'world.dimension=home' "$report_path"
grep -Fxq 'environment.water_surface_y=81.000000' "$report_path"
grep -Fxq 'environment.underwater=true' "$report_path"
grep -Fxq 'environment.feet_submerged=true' "$report_path"
grep -Fxq 'environment.body_submerged=true' "$report_path"
grep -Fxq 'environment.eyes_submerged=true' "$report_path"
grep -Fxq 'evolution.entity_selected=false' "$report_path"
grep -Fxq 'evolution.region_available=false' "$report_path"

underwater_depth=$(awk -F= '$1 == "environment.underwater_depth" { print $2 }' "$report_path")
awk -v depth="$underwater_depth" 'BEGIN { exit !(depth >= 3.0 && depth <= 4.5) }'
python3 tests/validate_png.py "$png_path" 1280 720

send_command 'quit'
wait_for_reply '^DEBUG_CONTROL quit accepted$'
exec {game_input}>&-
wait "$game_pid"
trap - EXIT

printf 'game end-to-end test passed: %s\n' "$png_path"
