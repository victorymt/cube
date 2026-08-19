#!/usr/bin/env bash

set -euo pipefail

game_binary=${1:-build/normal/voxelcraft}
settle_timeout=${E2E_SETTLE_TIMEOUT:-60}
command_timeout=${E2E_COMMAND_TIMEOUT:-60}

if [[ ! -x "$game_binary" ]]; then
    echo "game binary is not executable: $game_binary" >&2
    exit 2
fi

persistent_state_fingerprint() {
    local path
    for path in voxelcraft_save.txt voxelcraft_save.bak \
                voxelcraft_settings.cfg voxelcraft_settings.cfg.bak; do
        if [[ -f "$path" ]]; then
            cksum "$path"
        else
            printf 'missing %s\n' "$path"
        fi
    done
}

persistent_state_before=$(persistent_state_fingerprint)
trace_path=$(mktemp "${TMPDIR:-/tmp}/voxelcraft-e2e-trace.XXXXXX.jsonl")
startup_script=$(mktemp "${TMPDIR:-/tmp}/voxelcraft-e2e-startup.XXXXXX.dsl")
printf '%s\n' \
    'let startup_screen = game.screen' \
    'assert startup_screen == "start"' \
    'assert settings.autosave == false' >"$startup_script"

for requirement in python3 hyprctl; do
    if ! command -v "$requirement" >/dev/null 2>&1; then
        echo "$requirement is required for the game end-to-end test" >&2
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

set +e
resolution_error=$(
    "$game_binary" --debug-resolution 319x240 2>&1
)
resolution_status=$?
set -e
printf '%s\n' "$resolution_error"
[[ "$resolution_status" -eq 2 ]]
grep -Fq 'Invalid or repeated --debug-resolution option' \
    <<<"$resolution_error"

coproc GAME_PROCESS {
    env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-script "$startup_script" --debug-stdin \
        --debug-trace "$trace_path" --debug-resolution=1280x720 2>&1
}
game_pid=$GAME_PROCESS_PID
exec {game_output}<&"${GAME_PROCESS[0]}"
exec {game_input}>&"${GAME_PROCESS[1]}"

cleanup() {
    if kill -0 "$game_pid" >/dev/null 2>&1; then
        printf 'exit 1\n' >&"$game_input" || true
        kill "$game_pid" >/dev/null 2>&1 || true
    fi
    rm -f "$trace_path" "$startup_script"
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

wait_for_reply '^DEBUG_SCRIPT loaded .* batch=0$'
wait_for_reply '^DEBUG_CONTROL ready '
wait_for_reply '^DEBUG_SCRIPT complete source='
send_command 'assert startup_screen == "start"'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert perf.enabled == false && perf.route_complete == false && perf.report_written == false'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'start'
wait_for_reply '^DEBUG_CONTROL start ok seed=1448040515$' 120
send_command 'assert game.screen == "playing" && world.seed == 1448040515 && world.dimension == "home"'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'screen=playing seed=1448040515 dimension=home'* ]]
[[ "$matched_line" == *'water=0,0,0'* ]]
[[ "$matched_line" == *'autosave=0'* ]]

send_command 'water debug on'
wait_for_reply '^DEBUG_CONTROL water debug enabled=1$'
send_command 'assert render.water_debug == true'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'water_debug=1'* ]]
send_command 'water debug off'
wait_for_reply '^DEBUG_CONTROL water debug enabled=0$'
send_command 'repeat 2 {'
send_command 'water debug on'
send_command 'water debug off'
send_command '}'
wait_for_reply '^DEBUG_CONTROL water debug enabled=1$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=0$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=1$'
wait_for_reply '^DEBUG_CONTROL water debug enabled=0$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# This chunk-boundary coordinate previously exposed unloaded surface chunks as
# air. While streaming catches up, walking collision must hold the player in
# place; after the audit settles, the same coordinate must be fully usable.
send_command 'teleport 15 110 -252 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok position=15.000000,110.000000,-252.000000$'
send_command 'input 1 0 0 0 120'
wait_for_reply '^DEBUG_CONTROL input ok '
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
if [[ "$matched_line" == *'surface_ready=0'* ]]; then
    [[ "$matched_line" == *'velocity=0.000000,0.000000,0.000000'* ]]
fi

send_command 'stream wait 300'
wait_for_reply '^DEBUG_CONTROL stream wait started timeout_frames=300$'
wait_for_reply '^DEBUG_CONTROL stream wait result='
[[ "$matched_line" == *'result=settled'* ]]
[[ "$matched_line" == *'pending_local_sections=0'* ]]

ravine_ready=false
settle_deadline=$((SECONDS + settle_timeout))
while ((SECONDS < settle_deadline)); do
    send_command 'stream audit at 15 110 -252 1'
    wait_for_reply '^DEBUG_CONTROL stream audit started focus=0,6,-16 radius=1$'
    wait_for_reply '^DEBUG_CONTROL stream audit result='
    if [[ "$matched_line" == *'result=complete'* &&
          "$matched_line" == *'chunks_missing=0'* &&
          "$matched_line" == *'issues_total=0 '* ]]; then
        ravine_ready=true
        break
    fi
    sleep 0.5
done

if [[ "$ravine_ready" != true ]]; then
    echo "ravine regression coordinate did not settle within ${settle_timeout}s" >&2
    exit 1
fi
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'surface_ready=1 player_missing_surface_chunks=0'* ]]
ravine_y=$(sed -n 's/.*position=[^,]*,\([^,]*\),.*/\1/p' <<<"$matched_line")
awk -v y="$ravine_y" 'BEGIN { exit !(y > 100.0) }'
send_command 'assert stream.surface_ready && stream.missing_surface_chunks == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'weather force cloudy 0.8 36000'
wait_for_reply '^DEBUG_CONTROL weather force ok phenomenon=Cloudy intensity=0.800000 frames=36000$'
send_command 'weather cloud cumulonimbus 0.85 36000'
wait_for_reply '^DEBUG_CONTROL weather cloud ok genus=Cumulonimbus coverage=0.850000 frames=36000$'
send_command 'assert weather.cloud_genus == "Cumulonimbus" && weather.cloud_layers == 1 && weather.cloud_forced_frames > 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather inspect'
wait_for_reply '^DEBUG_CONTROL weather inspect ok '
[[ "$matched_line" == *'cloud_genus=Cumulonimbus'* ]]
[[ "$matched_line" == *'cloud_layers=1'* ]]

send_command 'evolution inspect'
wait_for_reply '^DEBUG_CONTROL evolution inspect none radius=24.000$'
send_command 'evolution advance 1'
wait_for_reply '^DEBUG_CONTROL evolution advance ok days=1.000$'
send_command 'evolution region'
wait_for_reply '^DEBUG_CONTROL evolution region ok lineages=3 '
send_command 'evolution bootstrap status'
wait_for_reply '^DEBUG_CONTROL evolution region ok lineages=3 '
send_command 'evolution catalog'
wait_for_reply '^DEBUG_CONTROL evolution catalog ok species=0 individuals=0 surface=[0-9]+$'
send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas open species=0$'
send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas closed species=0$'

send_command 'teleport -2895.5 76.002960 16.5 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok '

water_ready=false
settle_deadline=$((SECONDS + settle_timeout))
while ((SECONDS < settle_deadline)); do
    sleep 1
    send_command 'teleport -2895.5 76.002960 16.5 3.141593 -0.25'
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
[[ "$matched_line" == *'fluid_overflows=0'* ]]
send_command 'assert water.feet_submerged && water.body_submerged && water.eyes_submerged'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Build a one-cell reservoir directly above the settled ocean surface. The
# full water below acts as a floor, so the injected unit must spread sideways.
# Conservation is checked in test_fluid against an isolated loaded world;
# loaded-volume snapshots here can change as generation jobs finish.
send_command 'fluid set -2894 81 20 0'
wait_for_reply '^DEBUG_CONTROL fluid set ok position=-2894,81,20 volume=0$'
send_command 'fluid set -2895 81 20 255'
wait_for_reply '^DEBUG_CONTROL fluid set ok position=-2895,81,20 volume=255$'
send_command 'fluid inspect -2895 81 20'
wait_for_reply '^DEBUG_CONTROL fluid inspect ok '
send_command 'fluid step 64'
wait_for_reply '^DEBUG_CONTROL fluid step ok ticks=64 '
send_command 'fluid inspect -2894 81 20'
wait_for_reply '^DEBUG_CONTROL fluid inspect ok position=-2894,81,20 '
[[ "$matched_line" =~ volume=([1-9][0-9]*) ]]

send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

png_path=${matched_line#*png=}
png_path=${png_path%% report=*}
report_path=${matched_line##*report=}

[[ -s "$png_path" ]]
[[ -s "$report_path" ]]
grep -Fxq 'format.version=8' "$report_path"
grep -Fxq 'world.seed=1448040515' "$report_path"
grep -Fxq 'world.dimension=home' "$report_path"
grep -Fxq 'weather.cloud_genus=Cumulonimbus' "$report_path"
grep -Fxq 'weather.cloud_layer_count=1' "$report_path"
grep -Fxq 'weather.cloud_layer_0_name=Cumulonimbus' "$report_path"
grep -Eq '^weather.forced_cloud_frames=[1-9][0-9]*$' "$report_path"
grep -Fxq 'environment.seabed_y=-4299' "$report_path"
grep -Fxq 'environment.water_column_depth=4379' "$report_path"
grep -Fxq 'environment.bathymetry_zone=abyssal_plain' "$report_path"
grep -Fxq 'environment.seabed_material=sediment' "$report_path"
grep -Fxq 'environment.underwater=true' "$report_path"
grep -Fxq 'environment.feet_submerged=true' "$report_path"
grep -Fxq 'environment.body_submerged=true' "$report_path"
grep -Fxq 'environment.eyes_submerged=true' "$report_path"
grep -Fxq 'streaming.surface_ready=true' "$report_path"
grep -Fxq 'streaming.player_missing_surface_chunks=0' "$report_path"
grep -Fxq 'streaming.water_debug_enabled=false' "$report_path"
grep -Fxq 'streaming.water_debug_through=false' "$report_path"
grep -Eq '^streaming.water_visible_section_count=[1-9][0-9]*$' "$report_path"
grep -Fxq 'streaming.water_has_nearest=true' "$report_path"
grep -Eq '^streaming.water_nearest_chunk=-?[0-9]+,-?[0-9]+$' "$report_path"
grep -Eq '^streaming.water_nearest_section_y=-?[0-9]+$' "$report_path"
grep -Eq '^fluid.local_volume=([1-9][0-9]*)$' "$report_path"
grep -Eq '^fluid.loaded_volume=([1-9][0-9]*)$' "$report_path"
grep -Eq '^fluid.edit_count=([1-9][0-9]*)$' "$report_path"
grep -Fxq 'evolution.entity_selected=false' "$report_path"
grep -Fxq 'evolution.scan_locked=false' "$report_path"
grep -Fxq 'evolution.atlas_open=false' "$report_path"
grep -Fxq 'evolution.catalog_species_count=0' "$report_path"
grep -Fxq 'evolution.region_available=true' "$report_path"

underwater_depth=$(awk -F= '$1 == "environment.underwater_depth" { print $2 }' "$report_path")
water_surface_y=$(awk -F= '$1 == "environment.water_surface_y" { print $2 }' "$report_path")
awk -v depth="$underwater_depth" 'BEGIN { exit !(depth >= 3.0 && depth <= 4.5) }'
awk -v surface="$water_surface_y" 'BEGIN { exit !(surface >= 81.0 && surface <= 82.0) }'
python3 tests/validate_png.py "$png_path" 1280 720 --underwater-scene

send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas open species=0$'
send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

atlas_png_path=${matched_line#*png=}
atlas_png_path=${atlas_png_path%% report=*}
atlas_report_path=${matched_line##*report=}
[[ -s "$atlas_png_path" ]]
[[ -s "$atlas_report_path" ]]
grep -Fxq 'format.version=8' "$atlas_report_path"
grep -Fxq 'evolution.atlas_open=true' "$atlas_report_path"
grep -Fxq 'evolution.catalog_species_count=0' "$atlas_report_path"
python3 tests/validate_png.py "$atlas_png_path" 1280 720 --allow-dark-ui
send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas closed species=0$'

send_command 'evolution focus'
wait_for_reply '^DEBUG_CONTROL evolution focus (none radius=24\.000|ok organism=[0-9]+ species=[0-9]+)$'

send_command 'exit 0'
wait_for_reply '^DEBUG_SCRIPT exit code=0$'
exec {game_input}>&-
wait "$game_pid"

batch_script=$(mktemp "${TMPDIR:-/tmp}/voxelcraft-e2e-batch.XXXXXX.dsl")
batch_output=$(mktemp "${TMPDIR:-/tmp}/voxelcraft-e2e-batch.XXXXXX.log")
printf '%s\n' 'exit 7' >"$batch_script"
set +e
env -u LIBGL_ALWAYS_SOFTWARE \
    "$game_binary" --debug-script "$batch_script" >"$batch_output" 2>&1
batch_status=$?
set -e
cat "$batch_output"
[[ "$batch_status" -eq 7 ]]
grep -Fxq 'DEBUG_SCRIPT exit code=7' "$batch_output"

printf '%s\n' 'assert false' 'exit' >"$batch_script"
set +e
env -u LIBGL_ALWAYS_SOFTWARE \
    "$game_binary" --debug-script "$batch_script" >"$batch_output" 2>&1
batch_status=$?
set -e
cat "$batch_output"
[[ "$batch_status" -eq 3 ]]
grep -Eq '^DEBUG_SCRIPT error .*code=assertion ' "$batch_output"

printf '%s\n' \
    'start' \
    'wait until game.screen == "playing" timeout 10' \
    'marker target 999999' \
    'exit 0' >"$batch_script"
set +e
env -u LIBGL_ALWAYS_SOFTWARE \
    "$game_binary" --debug-script "$batch_script" >"$batch_output" 2>&1
batch_status=$?
set -e
cat "$batch_output"
[[ "$batch_status" -eq 3 ]]
grep -Eq '^DEBUG_SCRIPT error .*code=callback .*marker_target_not_found' "$batch_output"

printf '%s\n' 'assert (' 'exit;' >"$batch_script"
set +e
env -u LIBGL_ALWAYS_SOFTWARE \
    "$game_binary" --debug-script "$batch_script" >"$batch_output" 2>&1
batch_status=$?
set -e
cat "$batch_output"
[[ "$batch_status" -eq 3 ]]
grep -Eq '^DEBUG_SCRIPT error .*code=syntax ' "$batch_output"
rm -f "$batch_script" "$batch_output"

python3 - "$trace_path" <<'PY'
import json
import sys

types = set()
with open(sys.argv[1], encoding="utf-8") as trace:
    for line_number, line in enumerate(trace, 1):
        record = json.loads(line)
        assert record["version"] == 1, (line_number, record)
        assert record["timestamp_unix_ms"] > 0, (line_number, record)
        assert record["elapsed_real_ms"] >= 0, (line_number, record)
        if record["type"] == "sample":
            pipeline = record["pipeline"]
            assert pipeline["focus_stage"], (line_number, record)
            assert pipeline["focus_stage_age_ms"] >= 0
        types.add(record["type"])
assert {"start", "sample", "event", "stop"} <= types, types
PY
rm -f "$trace_path" "$startup_script"
trap - EXIT

[[ "$(persistent_state_fingerprint)" == "$persistent_state_before" ]]

printf 'game end-to-end test passed: world=%s atlas=%s\n' \
    "$png_path" "$atlas_png_path"
