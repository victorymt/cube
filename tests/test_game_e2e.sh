#!/usr/bin/env bash

set -euo pipefail

game_binary=${1:-build/normal/voxelcraft}
settle_timeout=${E2E_SETTLE_TIMEOUT:-60}
command_timeout=${E2E_COMMAND_TIMEOUT:-120}
debug_resolution=${E2E_RESOLUTION:-1920x1080}

if [[ ! "$debug_resolution" =~ ^([0-9]+)x([0-9]+)$ ]]; then
    echo "E2E_RESOLUTION must use WIDTHxHEIGHT" >&2
    exit 2
fi
debug_width=${BASH_REMATCH[1]}
debug_height=${BASH_REMATCH[2]}

if [[ ! -x "$game_binary" ]]; then
    echo "game binary is not executable: $game_binary" >&2
    exit 2
fi
game_binary=$(realpath "$game_binary")
project_root=$(pwd)
runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-e2e-runtime.XXXXXX")
ln -s "$project_root/assets" "$runtime_dir/assets"

runtime_artifact_path() {
    case $1 in
        /*) printf '%s\n' "$1" ;;
        *) printf '%s/%s\n' "$runtime_dir" "$1" ;;
    esac
}

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
    cd "$runtime_dir"
    env -u LIBGL_ALWAYS_SOFTWARE \
        "$game_binary" --debug-script "$startup_script" --debug-stdin \
        --debug-trace "$trace_path" \
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
    rm -f "$trace_path" "$startup_script"
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

# Verify catalog resolution and the stdin error contract before placing a
# gallery. A rejected command aborts only its current block; the live process
# must accept the next block and retain an untouched gallery state.
send_command 'stream wait 1800'
wait_for_reply '^DEBUG_CONTROL stream wait started timeout_frames=1800$'
wait_for_reply '^DEBUG_CONTROL stream wait result=settled '
send_command 'assert block.catalog_count == 412 && block.natural_count == 92 && block.stage05_count == 26 && !block.gallery_active && block.gallery_placed == 0 && block.gallery_rows == 3 && flora.catalog_count == 13 && !flora.gallery_active && flora.gallery_placed == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block inspect Andesite'
wait_for_reply '^DEBUG_CONTROL block inspect ok id=111 name="Andesite" .*stage05=true$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block inspect coral_limestone'
wait_for_reply '^DEBUG_CONTROL block inspect ok id=132 name="Coral Limestone" .*stage05=true$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block inspect 136'
wait_for_reply '^DEBUG_CONTROL block inspect ok id=136 name="Fire Ash" .*flammability=0\.000 .*stage05=true$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block gallery 900000 96 900000'
wait_for_reply '^DEBUG_CONTROL block gallery error reason=region_unloaded position='
wait_for_reply '^DEBUG_SCRIPT error source=stdin .*code=callback message=debug command failed: gallery_region_unloaded$'
send_command 'assert !block.gallery_active && block.gallery_placed == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Inspect the real-world Homeworld taxa and sample a deterministic habitat
# before mutating anything. The failed flora gallery must leave both its
# typed state and the world untouched, just like the Stage 05 gallery.
send_command 'flora inspect Quercus_robur'
wait_for_reply '^DEBUG_CONTROL flora inspect ok id=0 common="Pedunculate Oak" scientific="Quercus robur" family=Fagaceae .*primary=Pedunculate Oak Log accent=Pedunculate Oak Leaves$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'flora sample 16 -16'
wait_for_reply '^DEBUG_CONTROL flora sample ok position=16,-16 biome='
send_command 'assert flora.sample_ground != "none" && flora.sample_biome != "unavailable" && flora.sample_habitat.x > 0 && flora.sample_habitat.y >= 0 && flora.sample_habitat.z >= 0 && flora.sample_burn_stage != "unavailable"'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'flora gallery 999997 96 999997'
wait_for_reply '^DEBUG_CONTROL flora gallery error reason=invalid_region$'
wait_for_reply '^DEBUG_SCRIPT error source=stdin .*code=callback message=debug command failed: invalid_flora_gallery_region$'
send_command 'assert !flora.gallery_active && flora.gallery_placed == 0 && flora.gallery_trees == 0 && flora.gallery_ground == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

# Place all appended identities through the ordinary edit path, then prove
# that the full game save/load transaction accepts and restores those edits.
send_command 'teleport 0 95 -15 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=0.000000,95.000000,-15.000000$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block gallery -4 96 -16'
wait_for_reply '^DEBUG_CONTROL block gallery ok origin=-4,96,-16 placed=26 rows=3 geology=14 biogenic=9 fire_residue=3$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert block.gallery_active && block.gallery_origin == vec3(-4,96,-16) && block.gallery_placed == 26 && block.gallery_rows == 3'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'save'
wait_for_reply '^DEBUG_CONTROL save result=Saved map to voxelcraft_save\.txt \([0-9]+ edits\)\.$'
[[ "$matched_line" =~ \(([0-9]+)\ edits\)\. ]]
gallery_save_edits=${BASH_REMATCH[1]}
((gallery_save_edits >= 26))
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'load'
wait_for_reply '^DEBUG_CONTROL load result=Loaded voxelcraft_save\.txt \([0-9]+ edits\)\.$' 120
[[ "$matched_line" =~ \(([0-9]+)\ edits\)\. ]]
[[ "${BASH_REMATCH[1]}" -eq "$gallery_save_edits" ]]
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert game.screen == "playing" && world.seed == 1448040515 && world.dimension == "home" && block.gallery_active && block.gallery_origin == vec3(-4,96,-16) && block.gallery_placed == 26'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'teleport -4 95 -10 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=-4.000000,95.000000,-10.000000$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'teleport 36 95 -10 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=36.000000,95.000000,-10.000000$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'flora gallery -4 96 -16'
wait_for_reply '^DEBUG_CONTROL flora gallery ok origin=-4,96,-16 placed=573 trees=6 ground=7 taxa=13$'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert flora.gallery_active && flora.gallery_origin == vec3(-4,96,-16) && flora.gallery_placed == 573 && flora.gallery_trees == 6 && flora.gallery_ground == 7'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'input 0 0 0 0 600'
wait_for_reply '^DEBUG_CONTROL input ok '
send_command 'wait until player.input_frames == 0 timeout 700'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'teleport 20 113 -7 0 -0.95'
wait_for_reply '^DEBUG_CONTROL teleport ok position=20.000000,113.000000,-7.000000$'
send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

gallery_png_path=${matched_line#*png=}
gallery_png_path=${gallery_png_path%% report=*}
gallery_png_path=$(runtime_artifact_path "$gallery_png_path")
gallery_report_path=${matched_line##*report=}
gallery_report_path=$(runtime_artifact_path "$gallery_report_path")
[[ -s "$gallery_png_path" ]]
[[ -s "$gallery_report_path" ]]
grep -Fxq 'format.version=12' "$gallery_report_path"
grep -Fxq 'block.catalog_count=412' "$gallery_report_path"
grep -Fxq 'block.natural_count=92' "$gallery_report_path"
grep -Fxq 'block.stage05_count=26' "$gallery_report_path"
grep -Fxq 'block.gallery_active=true' "$gallery_report_path"
grep -Fxq 'block.gallery_origin=-4.000000,96.000000,-16.000000' "$gallery_report_path"
grep -Fxq 'block.gallery_placed=26' "$gallery_report_path"
grep -Fxq 'block.gallery_rows=3' "$gallery_report_path"
grep -Fxq 'block.gallery_width=14' "$gallery_report_path"
grep -Fxq 'flora.catalog_count=13' "$gallery_report_path"
grep -Fxq 'flora.gallery_active=true' "$gallery_report_path"
grep -Fxq 'flora.gallery_origin=-4.000000,96.000000,-16.000000' "$gallery_report_path"
grep -Fxq 'flora.gallery_placed=573' "$gallery_report_path"
grep -Fxq 'flora.gallery_trees=6' "$gallery_report_path"
grep -Fxq 'flora.gallery_ground=7' "$gallery_report_path"
grep -Fxq 'ui.help_visible=false' "$gallery_report_path"
python3 tests/validate_png.py "$gallery_png_path" \
    "$debug_width" "$debug_height"

# Exercise the complete wildfire debug path at a deterministic flammable
# Homeworld cell, capture the same-frame renderer inputs, then clear only fire.
# Place the fuel explicitly so procedural topology changes cannot invalidate
# this lifecycle fixture by changing the generated surface material.
send_command 'block set 9 82 -10 Pedunculate Oak Log'
wait_for_reply '^DEBUG_CONTROL block set ok raw=9,82,-10 canonical=9,82,-10 '
send_command 'weather force strong-wind 1 36000'
wait_for_reply '^DEBUG_CONTROL weather force ok phenomenon=Strong wind intensity=1.000000 frames=36000$'
send_command 'weather fire ignite 9 82 -10 1'
wait_for_reply '^DEBUG_CONTROL weather fire ignite ok position=9,82,-10 '
send_command 'weather fire ignite 9 82 -10 1'
wait_for_reply '^DEBUG_CONTROL weather fire ignite ok position=9,82,-10 phase=(igniting|flaming) intensity=1.000000 '
send_command 'assert weather.active_fires == 1 && weather.fire_phase != "inactive" && weather.fire_position == vec3(9,82,-10) && weather.fire_distance >= 0 && weather.fire_intensity > 0 && weather.fire_fuel > 0 && weather.fire_moisture < 0.72 && weather.fire_ignitions >= 1'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather inspect'
wait_for_reply '^DEBUG_CONTROL weather inspect ok '
[[ "$matched_line" == *'fires=1'* ]]
[[ "$matched_line" == *'fire_position=9,82,-10'* ]]
[[ "$matched_line" == *'fire_ignitions='* ]]
send_command 'teleport 9 85 -4 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok position=9.000000,85.000000,-4.000000$'
send_command 'stream wait 300'
wait_for_reply '^DEBUG_CONTROL stream wait started timeout_frames=300$'
wait_for_reply '^DEBUG_CONTROL stream wait result=settled '
send_command 'weather fire ignite 9 82 -10 1'
wait_for_reply '^DEBUG_CONTROL weather fire ignite ok position=9,82,-10 '
send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

wildfire_png_path=${matched_line#*png=}
wildfire_png_path=${wildfire_png_path%% report=*}
wildfire_png_path=$(runtime_artifact_path "$wildfire_png_path")
wildfire_report_path=${matched_line##*report=}
wildfire_report_path=$(runtime_artifact_path "$wildfire_report_path")
[[ -s "$wildfire_png_path" ]]
[[ -s "$wildfire_report_path" ]]
grep -Fxq 'format.version=12' "$wildfire_report_path"
grep -Fxq 'weather.fire_present=true' "$wildfire_report_path"
grep -Eq '^weather.fire_phase=(igniting|flaming|smoldering)$' "$wildfire_report_path"
grep -Fxq 'weather.fire_position=9.000000,82.000000,-10.000000' "$wildfire_report_path"
grep -Eq '^weather.fire_snapshot_count=[1-9][0-9]*$' "$wildfire_report_path"
grep -Fxq 'weather.fire_render_max_fires=24' "$wildfire_report_path"
grep -Fxq 'weather.fire_render_flame_tongues=3' "$wildfire_report_path"
grep -Fxq 'weather.fire_render_smoke_puffs=5' "$wildfire_report_path"
grep -Eq '^weather.fire_smoke_output=0\.[0-9]*[1-9][0-9]*$' "$wildfire_report_path"
grep -Eq '^weather.fire_plume_wind_drift=0\.[0-9]*[1-9][0-9]*$' "$wildfire_report_path"
python3 tests/validate_png.py "$wildfire_png_path" \
    "$debug_width" "$debug_height"
send_command 'weather fire suppress 9 82 -10 0 0.5'
wait_for_reply '^DEBUG_CONTROL weather fire suppress ok position=9,82,-10 radius=0.000 amount=0.500000 affected=1$'
send_command 'assert weather.fire_suppressions >= 1'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather fire clear'
wait_for_reply '^DEBUG_CONTROL weather fire clear ok cleared=1$'
send_command 'assert weather.active_fires == 0 && weather.fire_phase == "inactive" && weather.fire_distance == -1 && weather.forced_frames > 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather clear'
wait_for_reply '^DEBUG_CONTROL weather clear ok$'

# This chunk-boundary coordinate previously exposed unloaded surface chunks as
# air. While streaming catches up, walking collision must hold the player in
# place; after the audit settles, the same coordinate must be fully usable.
send_command 'teleport 1024 140 256 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok position=1024.000000,140.000000,256.000000$'
send_command 'input 1 0 0 0 120'
wait_for_reply '^DEBUG_CONTROL input ok '
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
if [[ "$matched_line" == *'surface_ready=0'* ]]; then
    [[ "$matched_line" == *'velocity=0.000000,0.000000,0.000000'* ]]
fi

send_command 'stream wait 1800'
wait_for_reply '^DEBUG_CONTROL stream wait started timeout_frames=1800$'
wait_for_reply '^DEBUG_CONTROL stream wait result='
[[ "$matched_line" == *'result=settled'* ]]
[[ "$matched_line" == *'pending_local_sections=0'* ]]

boundary_ready=false
settle_deadline=$((SECONDS + settle_timeout))
while ((SECONDS < settle_deadline)); do
    send_command 'stream audit at 1024 140 256 1'
    wait_for_reply '^DEBUG_CONTROL stream audit started focus=64,8,16 radius=1$'
    wait_for_reply '^DEBUG_CONTROL stream audit result='
    if [[ "$matched_line" == *'result=complete'* &&
          "$matched_line" == *'chunks_missing=0'* &&
          "$matched_line" == *'issues_total=0 '* ]]; then
        boundary_ready=true
        break
    fi
    sleep 0.5
done

if [[ "$boundary_ready" != true ]]; then
    echo "chunk-boundary regression coordinate did not settle within ${settle_timeout}s" >&2
    exit 1
fi
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'surface_ready=1 player_missing_surface_chunks=0'* ]]
boundary_y=$(sed -n 's/.*position=[^,]*,\([^,]*\),.*/\1/p' <<<"$matched_line")
awk -v y="$boundary_y" 'BEGIN { exit !(y > 100.0) }'
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

send_command 'weather tornado force 0.9 36000 48'
wait_for_reply '^DEBUG_CONTROL weather tornado ok intensity=0.900000 frames=36000 distance=48.000$'
send_command 'assert weather.tornado_active && weather.tornado_forced && weather.tornado_phase == "forming" && weather.tornado_forced_frames > 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather tornado clear'
wait_for_reply '^DEBUG_CONTROL weather tornado clear ok$'
send_command 'assert !weather.tornado_active && weather.cloud_genus == "Cumulonimbus" && weather.forced_frames > 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'weather tornado force 0.9 36000 48'
wait_for_reply '^DEBUG_CONTROL weather tornado ok intensity=0.900000 frames=36000 distance=48.000$'
send_command 'weather inspect'
wait_for_reply '^DEBUG_CONTROL weather inspect ok '
[[ "$matched_line" == *'tornado_active=1'* ]]
[[ "$matched_line" == *'tornado_forced=1'* ]]

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

send_command 'teleport -128.5 76.002960 0.5 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok '

water_ready=false
settle_deadline=$((SECONDS + settle_timeout))
while ((SECONDS < settle_deadline)); do
    sleep 1
    send_command 'teleport -128.5 76.002960 0.5 3.141593 -0.25'
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
send_command 'stream wait 1800'
wait_for_reply '^DEBUG_CONTROL stream wait started timeout_frames=1800$'
wait_for_reply '^DEBUG_CONTROL stream wait result=settled '
send_command 'teleport -128.5 76.002960 0.5 3.141593 -0.25'
wait_for_reply '^DEBUG_CONTROL teleport ok '
send_command 'status'
wait_for_reply '^DEBUG_CONTROL status '
[[ "$matched_line" == *'water=1,1,1'* ]]
[[ "$matched_line" == *'surface=81.000000'* ]]

# Build a one-cell reservoir directly above the settled ocean surface. The
# full water below acts as a floor, so the injected unit must spread sideways.
# Conservation is checked in test_fluid against an isolated loaded world;
# loaded-volume snapshots here can change as generation jobs finish.
send_command 'fluid set -127 81 4 0'
wait_for_reply '^DEBUG_CONTROL fluid set ok position=-127,81,4 volume=0$'
send_command 'fluid set -128 81 4 255'
wait_for_reply '^DEBUG_CONTROL fluid set ok position=-128,81,4 volume=255$'
send_command 'fluid inspect -128 81 4'
wait_for_reply '^DEBUG_CONTROL fluid inspect ok '
send_command 'fluid step 64'
wait_for_reply '^DEBUG_CONTROL fluid step ok ticks=64 '
send_command 'fluid inspect -127 81 4'
wait_for_reply '^DEBUG_CONTROL fluid inspect ok position=-127,81,4 '
[[ "$matched_line" =~ volume=([1-9][0-9]*) ]]

send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

png_path=${matched_line#*png=}
png_path=${png_path%% report=*}
png_path=$(runtime_artifact_path "$png_path")
report_path=${matched_line##*report=}
report_path=$(runtime_artifact_path "$report_path")

[[ -s "$png_path" ]]
[[ -s "$report_path" ]]
grep -Fxq 'format.version=12' "$report_path"
grep -Fxq 'world.seed=1448040515' "$report_path"
grep -Fxq 'world.dimension=home' "$report_path"
grep -Fxq 'weather.cloud_genus=Cumulonimbus' "$report_path"
grep -Fxq 'weather.cloud_layer_count=1' "$report_path"
grep -Fxq 'weather.cloud_layer_0_name=Cumulonimbus' "$report_path"
grep -Eq '^weather.forced_cloud_frames=[1-9][0-9]*$' "$report_path"
grep -Fxq 'weather.tornado_active=true' "$report_path"
grep -Fxq 'weather.tornado_forced=true' "$report_path"
grep -Eq '^weather.tornado_phase=(forming|intensifying|mature|dissipating)$' "$report_path"
grep -Eq '^weather.tornado_forced_frames=[1-9][0-9]*$' "$report_path"
grep -Fxq 'environment.seabed_y=-3807' "$report_path"
grep -Fxq 'environment.water_column_depth=3887' "$report_path"
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
python3 tests/validate_png.py "$png_path" \
    "$debug_width" "$debug_height" --underwater-scene

send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas open species=0$'
send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

atlas_png_path=${matched_line#*png=}
atlas_png_path=${atlas_png_path%% report=*}
atlas_png_path=$(runtime_artifact_path "$atlas_png_path")
atlas_report_path=${matched_line##*report=}
atlas_report_path=$(runtime_artifact_path "$atlas_report_path")
[[ -s "$atlas_png_path" ]]
[[ -s "$atlas_report_path" ]]
grep -Fxq 'format.version=12' "$atlas_report_path"
grep -Fxq 'evolution.atlas_open=true' "$atlas_report_path"
grep -Fxq 'evolution.catalog_species_count=0' "$atlas_report_path"
python3 tests/validate_png.py "$atlas_png_path" \
    "$debug_width" "$debug_height" --allow-dark-ui
send_command 'evolution atlas'
wait_for_reply '^DEBUG_CONTROL evolution atlas closed species=0$'

send_command 'evolution focus'
wait_for_reply '^DEBUG_CONTROL evolution focus (none radius=24\.000|ok organism=[0-9]+ species=[0-9]+)$'

# Traverse every global chart boundary through the live DSL. A longitude alias
# edit must retain one persistent identity, and the same canonical marker and
# edit must survive the full game save/load transaction.
send_command 'world topology'
wait_for_reply '^DEBUG_CONTROL world topology ok '
[[ "$matched_line" == *'aliases=longitude:1,north:1,south:1'* ]]
[[ "$matched_line" == *'duplicate_canonical_chunks=0'* ]]
send_command 'assert world.longitude_alias && world.north_pole_alias && world.south_pole_alias && world.duplicate_canonical_chunks == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'

send_command 'teleport 8193 250 12 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=8193.000000,250.000000,12.000000$'
send_command 'wait until world.last_rebase && world.last_rebase_to.x == -8191 && world.last_rebase_to.z == 12 timeout 120'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert world.canonical_position.x == -8191 && world.canonical_position.z == 12 && world.last_rebase_north_sign == 1 && world.duplicate_canonical_chunks == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert weather.canonical_cell == ecology.canonical_cell && weather.canonical_cell.x == -8191 && weather.canonical_cell.z == 12 && ecology.region_x == -128 && ecology.region_z == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'world topology'
wait_for_reply '^DEBUG_CONTROL world topology ok '
[[ "$matched_line" == *'canonical=-8191.000000,12.000000'* ]]
[[ "$matched_line" == *'duplicate_canonical_chunks=0'* ]]

send_command 'block set 8193 250 12 Glass'
wait_for_reply '^DEBUG_CONTROL block set ok raw=8193,250,12 canonical=-8191,250,12 .*name="Glass"$'
send_command 'save'
wait_for_reply '^DEBUG_CONTROL save result=Saved map to voxelcraft_save\.txt \([0-9]+ edits\)\.$'
[[ "$matched_line" =~ \(([0-9]+)\ edits\)\. ]]
topology_alias_edits=${BASH_REMATCH[1]}
send_command 'block set -8191 250 12 Brick'
wait_for_reply '^DEBUG_CONTROL block set ok raw=-8191,250,12 canonical=-8191,250,12 .*previous_name="Glass" .*name="Brick"$'
send_command 'save'
wait_for_reply '^DEBUG_CONTROL save result=Saved map to voxelcraft_save\.txt \([0-9]+ edits\)\.$'
[[ "$matched_line" =~ \(([0-9]+)\ edits\)\. ]]
[[ "${BASH_REMATCH[1]}" -eq "$topology_alias_edits" ]]

send_command 'marker add 8193 12 cyan topology-seam'
wait_for_reply '^DEBUG_CONTROL marker add ok id=[0-9]+ dimension=home surface=0 x=8193.000 z=12.000 color=cyan name=topology-seam$'
[[ "$matched_line" =~ id=([0-9]+) ]]
topology_marker_id=${BASH_REMATCH[1]}
send_command 'marker list'
wait_for_reply "^DEBUG_CONTROL marker list ok dimension=home surface=0 count=[0-9]+ target=0$"
wait_for_reply "^DEBUG_CONTROL marker item id=${topology_marker_id} x=-8191.000 z=12.000 color=cyan target=0 name=topology-seam$"
send_command "marker target $topology_marker_id"
wait_for_reply "^DEBUG_CONTROL marker target ok id=${topology_marker_id}$"
send_command 'save'
wait_for_reply '^DEBUG_CONTROL save result=Saved map to voxelcraft_save\.txt \([0-9]+ edits\)\.$'
send_command 'load'
wait_for_reply '^DEBUG_CONTROL load result=Loaded voxelcraft_save\.txt \([0-9]+ edits\)\.$' 120
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'block set 8193 250 12 Brick'
wait_for_reply '^DEBUG_CONTROL block set ok raw=8193,250,12 canonical=-8191,250,12 .*previous_name="Brick" .*name="Brick"$'
send_command 'marker list'
wait_for_reply "^DEBUG_CONTROL marker list ok dimension=home surface=0 count=[0-9]+ target=${topology_marker_id}$"
wait_for_reply "^DEBUG_CONTROL marker item id=${topology_marker_id} x=-8191.000 z=12.000 color=cyan target=1 name=topology-seam$"

send_command 'teleport 123.5 130 4096.5 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=123.500000,130.000000,4096.500000$'
send_command 'wait until world.last_rebase && world.last_rebase_to.x > -8069 && world.last_rebase_to.x < -8068 && world.last_rebase_to.z == 4095.5 && world.last_rebase_north_sign == -1 timeout 120'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert world.canonical_position.x > -8069 && world.canonical_position.x < -8068 && world.canonical_position.z == 4095.5 && world.last_rebase_north_sign == -1 && world.latitude > 1.56 && world.north_pole_alias && world.duplicate_canonical_chunks == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert weather.canonical_cell == ecology.canonical_cell && weather.canonical_cell.x == -8069 && weather.canonical_cell.z == 4095 && ecology.region_x == -127 && ecology.region_z == 63'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'world topology'
wait_for_reply '^DEBUG_CONTROL world topology ok '
[[ "$matched_line" == *'canonical=-8068.'* ]]
[[ "$matched_line" == *',4095.500000 longitude='* ]]
[[ "$matched_line" == *'duplicate_canonical_chunks=0'* ]]

send_command 'teleport -321.5 130 -4096.5 0 -0.35'
wait_for_reply '^DEBUG_CONTROL teleport ok position=-321.500000,130.000000,-4096.500000$'
send_command 'wait until world.last_rebase_to.x > 7870 && world.last_rebase_to.x < 7871 && world.last_rebase_to.z == -4095.5 && world.last_rebase_north_sign == -1 timeout 120'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'wait until stream.surface_ready timeout 1800'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'input 0 0 0 0 600'
wait_for_reply '^DEBUG_CONTROL input ok '
send_command 'wait until player.input_frames == 0 timeout 700'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert world.canonical_position.x > 7870 && world.canonical_position.x < 7871 && world.canonical_position.z == -4095.5 && world.last_rebase_north_sign == -1 && world.latitude < -1.56 && world.south_pole_alias && world.duplicate_canonical_chunks == 0'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'assert weather.canonical_cell == ecology.canonical_cell && weather.canonical_cell.x == 7870 && weather.canonical_cell.z == -4096 && ecology.region_x == 122 && ecology.region_z == -64'
wait_for_reply '^DEBUG_SCRIPT complete source=stdin$'
send_command 'world topology'
wait_for_reply '^DEBUG_CONTROL world topology ok '
[[ "$matched_line" == *'canonical=7870.'* ]]
[[ "$matched_line" == *',-4095.500000 longitude='* ]]
[[ "$matched_line" == *'duplicate_canonical_chunks=0'* ]]
send_command 'screenshot'
wait_for_reply '^DEBUG_CONTROL screenshot scheduled$'
wait_for_reply '^DEBUG_CONTROL capture ok png=.* report=.*$' 30

topology_png_path=${matched_line#*png=}
topology_png_path=${topology_png_path%% report=*}
topology_png_path=$(runtime_artifact_path "$topology_png_path")
topology_report_path=${matched_line##*report=}
topology_report_path=$(runtime_artifact_path "$topology_report_path")
[[ -s "$topology_png_path" ]]
[[ -s "$topology_report_path" ]]
grep -Fxq 'format.version=12' "$topology_report_path"
grep -Fxq 'world.dimension=home' "$topology_report_path"
grep -Fxq 'streaming.surface_ready=true' "$topology_report_path"
grep -Fxq 'streaming.player_missing_surface_chunks=0' "$topology_report_path"
python3 tests/validate_png.py "$topology_png_path" \
    "$debug_width" "$debug_height"

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
gallery_artifact=validated
topology_artifact=validated
if [[ -n "${E2E_ARTIFACT_DIR:-}" ]]; then
    mkdir -p "$E2E_ARTIFACT_DIR"
    cp "$gallery_png_path" "$E2E_ARTIFACT_DIR/stage06-flora-gallery.png"
    cp "$gallery_report_path" "$E2E_ARTIFACT_DIR/stage06-flora-gallery.txt"
    cp "$topology_png_path" "$E2E_ARTIFACT_DIR/stage07-south-pole.png"
    cp "$topology_report_path" "$E2E_ARTIFACT_DIR/stage07-south-pole.txt"
    gallery_artifact="$E2E_ARTIFACT_DIR/stage06-flora-gallery.png"
    topology_artifact="$E2E_ARTIFACT_DIR/stage07-south-pole.png"
fi
rm -f "$trace_path" "$startup_script"
rm -rf "$runtime_dir"
trap - EXIT

[[ "$(persistent_state_fingerprint)" == "$persistent_state_before" ]]

printf 'game end-to-end test passed: gallery=%s topology=%s captures=5\n' \
    "$gallery_artifact" "$topology_artifact"
