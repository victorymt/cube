#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
command_timeout=120
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" ||
      ! -S "${XDG_RUNTIME_DIR:-}/${WAYLAND_DISPLAY:-}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
runtime_dir=$(mktemp -d /tmp/voxelcraft-animal-poses.XXXXXX)
ln -s "$(pwd)/assets" "$runtime_dir/assets"
coproc GAME_PROCESS {
    cd "$runtime_dir"
    env -u LIBGL_ALWAYS_SOFTWARE "$game_binary" --debug-stdin \
        --debug-resolution 1600x1200 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"
debug_protocol_log="$runtime_dir/replies.log"
source tests/debug_stdin_protocol.sh
cleanup() {
    local status=$?
    if kill -0 "$game_pid" 2>/dev/null; then
        printf 'exit 1\n' >&"$game_input" || true
        kill "$game_pid" 2>/dev/null || true
    fi
    printf 'animal pose artifacts (exit=%d): %s\n' "$status" "$runtime_dir"
}
trap cleanup EXIT

wait_for_reply '^DEBUG_CONTROL ready '
send_command 'start'
wait_for_reply '^DEBUG_CONTROL start ok ' 120
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

capture_pose() {
    local action=$1 speed=$2 phase=$3 turn=$4 label=$5
    send_command "evolution pose $action $speed $phase $turn"
    wait_for_reply '^DEBUG_CONTROL evolution pose ok '
    send_command "assert evolution.preview_action == \"$action\" && evolution.preview_frozen"
    wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
    send_command 'screenshot'
    wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
    wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30
    local image_path=${matched_line#*png=}
    image_path=${image_path%% *}
    if [[ "$image_path" != /* ]]; then image_path="$runtime_dir/$image_path"; fi
    cp "$image_path" "$runtime_dir/$label.png"
}

captures=0
for plan in ${E2E_POSE_PLANS:-ground flight aquatic}; do
    send_command "evolution preview $plan 123 100 100 100 0"
    wait_for_reply '^DEBUG_CONTROL evolution preview ok$'
    for action in idle alert forage drink rest nest mate hunt scavenge dead; do
        capture_pose "$action" 0 0.48 0 "$plan-$action"
    done
    capture_pose wander 1 0.1 0 "$plan-walk-contact"
    capture_pose wander 1 0.82 0 "$plan-walk-swing"
    capture_pose flee 2 0.35 0.7 "$plan-flee-turn"
    captures=$((captures + 13))
done
send_command 'evolution pose live'
wait_for_reply '^DEBUG_CONTROL evolution pose ok '
send_command 'assert !evolution.preview_frozen'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'exit 0'
wait_for_reply '^DEBUG_SCRIPT exit code=0$'
exec {game_input}>&-
wait "$game_pid"
printf 'animal pose e2e passed: %d captures, fixed phases and live playback\n' "$captures"
