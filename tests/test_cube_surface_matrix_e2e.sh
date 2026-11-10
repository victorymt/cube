#!/usr/bin/env bash
set -euo pipefail

game_binary=$(realpath "${1:-build/normal/voxelcraft}")
cases_file=${CUBE_MATRIX_CASES:-tests/cube_surface_matrix.cases}
if [[ -z "${HYPRLAND_INSTANCE_SIGNATURE:-}" || -z "${WAYLAND_DISPLAY:-}" ||
      -z "${XDG_RUNTIME_DIR:-}" || ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Hyprland/Wayland session is required' >&2
    exit 2
fi
if [[ ! -f "$cases_file" ]]; then
    echo "missing cube surface matrix cases: $cases_file" >&2
    exit 2
fi

mapfile -t cases < <(awk 'NF && $1 !~ /^#/ { print }' "$cases_file")
case_count=${#cases[@]}
if [[ "$case_count" -ne 31 ]]; then
    echo "cube surface matrix requires 31 cases, found $case_count" >&2
    exit 2
fi
awk '
    BEGIN { faces = edges = corners = poles = high = distances = 0 }
    $2 == "face" { faces++ }
    $2 == "edge" { edges++ }
    $2 == "corner" { corners++ }
    $2 == "pole" { poles++ }
    $2 == "high" { high++ }
    $2 == "distance" { distances++ }
    END {
        if (faces != 6 || edges != 12 || corners != 8 || poles != 2 ||
            high != 1 || distances != 2) {
            printf "invalid matrix category counts: faces=%d edges=%d corners=%d poles=%d high=%d distances=%d\n",
                faces, edges, corners, poles, high, distances > "/dev/stderr"
            exit 1
        }
    }
' "$cases_file"
awk 'NF && $1 !~ /^#/ {
        if ($3 < 0 || $3 > 5 || $4 < 0 || $4 > 4095 ||
            $5 < 0 || $5 > 4095 || $8 > 1.5707963268 ||
            $8 < -1.5707963268 || $9 < 1 || $9 > 12) {
            printf "invalid matrix row at line %d: %s\n", NR, $0 > "/dev/stderr"
            exit 1
        }
    }' "$cases_file"

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-matrix.XXXXXX")
case_dir="$runtime_dir/cases"
mkdir -p "$case_dir"
ln -s "$(pwd)/assets" "$runtime_dir/assets"
dsl="$runtime_dir/cube_surface_matrix.dsl"
cat > "$dsl" <<'EOF'
# Generated from tests/cube_surface_matrix.cases by
# tests/test_cube_surface_matrix_e2e.sh. The case order and evidence mapping
# are validated by the shell wrapper after the run.
assert game.screen == "start"
terrain varied
world seed 1448040515
start
wait until game.screen == "playing" timeout 7200
hud debug off
fly on
view first
world time 0.5
weather force clear 0.0 36000
weather damage off
EOF

for row in "${cases[@]}"; do
    read -r case_id category face u v altitude yaw pitch distance <<<"$row"
    cat >> "$dsl" <<EOF
# case=$case_id category=$category face=$face u=$u v=$v altitude=$altitude distance=$distance
view distance $distance
surface address $face $u $v $altitude $yaw $pitch
wait until stream.surface_ready timeout 1800
wait until stream.cube.available timeout 300
wait until render.coverage.main_visible_sections > 0 && render.coverage.main_drawn_sections > 0 && render.coverage.main_target_lod_missing_sections == 0 timeout 1200
let settle_$case_id = debug.frame + 30
wait until debug.frame > settle_$case_id timeout 60
wait until render.coverage.main_visible_sections > 0 && render.coverage.main_drawn_sections > 0 && render.coverage.main_target_lod_missing_sections == 0 timeout 1200
assert stream.cube.active == true && stream.cube.local_terrain_ready == true
assert stream.cube.admitted > 0
assert stream.cube.observation_sequence == render.coverage.observation_sequence
assert render.coverage.admitted == stream.cube.admitted
assert render.coverage.main_candidate_sections >= render.coverage.main_visible_sections
assert render.coverage.main_visible_sections >= render.coverage.main_drawn_sections
assert render.coverage.main_solid_draws > 0 && render.coverage.main_solid_triangles > 0
screenshot
EOF
done
printf '%s\n' 'exit 0' >> "$dsl"

game_log="$runtime_dir/game.log"
timeout_seconds=${CUBE_MATRIX_E2E_TIMEOUT:-900}
if ! (cd "$runtime_dir" && timeout "$timeout_seconds" "$game_binary" \
        --debug-resolution 1280x720 --debug-script "$dsl" >"$game_log" 2>&1); then
    echo "cube surface matrix failed; artifacts: $runtime_dir" >&2
    tail -120 "$game_log" >&2 || true
    exit 1
fi
if ! grep -qx 'DEBUG_SCRIPT exit code=0' "$game_log"; then
    echo "cube surface matrix did not exit cleanly; artifacts: $runtime_dir" >&2
    tail -120 "$game_log" >&2 || true
    exit 1
fi

mapfile -t captures < <(grep '^DEBUG_CONTROL capture ok ' "$game_log" || true)
if [[ "${#captures[@]}" -ne "$case_count" ]]; then
    echo "expected $case_count captures, found ${#captures[@]}; artifacts: $runtime_dir" >&2
    tail -120 "$game_log" >&2 || true
    exit 1
fi

summary="$runtime_dir/matrix-summary.tsv"
printf 'case_id\tcategory\tface\tu\tv\taltitude\tdistance\trequested\tadmitted\tready\tsaturated\tvisible\tdrawn\tsolid_draws\tsolid_triangles\tgeneration_queue_peak\tmesh_queue_peak\tpng\treport\n' > "$summary"
report_value() {
    awk -F= -v key="$1" '$1 == key { print $2; exit }' "$2"
}
for index in "${!cases[@]}"; do
    row=${cases[$index]}
    read -r case_id category face u v altitude yaw pitch distance <<<"$row"
    capture=${captures[$index]}
    png=${capture#*png=}
    png=${png%% report=*}
    report=${capture##*report=}
    if [[ ! -f "$runtime_dir/$png" || ! -f "$runtime_dir/$report" ]]; then
        echo "missing capture artifact for $case_id: png=$png report=$report" >&2
        exit 1
    fi
    mv "$runtime_dir/$png" "$case_dir/$case_id.png"
    mv "$runtime_dir/$report" "$case_dir/$case_id.txt"
    report="$case_dir/$case_id.txt"
    current_png="$case_dir/$case_id.png"
    for expected in \
        'coverage.cube.active=true' \
        'coverage.cube.available=true' \
        'coverage.cube.local_terrain_ready=true' \
        'render.coverage.main_target_lod_missing_sections=0'; do
        if ! grep -qx "$expected" "$report"; then
            echo "$case_id failed report assertion: $expected" >&2
            exit 1
        fi
    done
    visible=$(report_value render.coverage.main_visible_sections "$report")
    drawn=$(report_value render.coverage.main_drawn_sections "$report")
    solid_draws=$(report_value render.coverage.main_solid_draws "$report")
    solid_triangles=$(report_value render.coverage.main_solid_triangles "$report")
    requested=$(report_value coverage.cube.requested "$report")
    admitted=$(report_value coverage.cube.admitted "$report")
    saturated=$(report_value coverage.cube.saturated "$report")
    if [[ "$saturated" == true && "$requested" -le "$admitted" ]]; then
        echo "$case_id reports saturation without requested > admitted" >&2
        exit 1
    fi
    if [[ "$saturated" == false && "$requested" -ne "$admitted" ]]; then
        echo "$case_id is unsaturated but requested=$requested admitted=$admitted" >&2
        exit 1
    fi
    if (( visible <= 0 || drawn <= 0 || solid_draws <= 0 || solid_triangles <= 0 )); then
        echo "$case_id has no rendered surface: visible=$visible drawn=$drawn solid_draws=$solid_draws triangles=$solid_triangles" >&2
        exit 1
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$case_id" "$category" "$face" "$u" "$v" "$altitude" "$distance" \
        "$requested" "$admitted" \
        "$(report_value coverage.cube.ready "$report")" \
        "$saturated" \
        "$visible" "$drawn" "$solid_draws" "$solid_triangles" \
        "$(report_value streaming.generation_queue_peak "$report")" \
        "$(report_value streaming.mesh_queue_peak "$report")" \
        "$current_png" "$report" >> "$summary"
done

echo "cube surface matrix passed: $case_count cases"
echo "matrix artifacts: $runtime_dir"
echo "matrix summary: $summary"
