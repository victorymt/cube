#!/usr/bin/env bash
set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-atmosphere.XXXXXX")
ln -s "$repo/assets" "$runtime/assets"
cat > "$runtime/check.dsl" <<'DSL'
terrain flat
start
pause on
teleport 0 50 0 0 0
assert atmosphere.valid && atmosphere.active
assert atmosphere.layer == "Troposphere"
assert atmosphere.altitude_m == 38
assert atmosphere.pressure_atm > 0.99
let surface_pressure = atmosphere.pressure_atm
teleport 0 283 0 0 0
assert atmosphere.layer == "Troposphere"
assert atmosphere.pressure_atm < surface_pressure
let lower_temperature = atmosphere.temperature_k
teleport 0 295 0 0 0
assert atmosphere.layer == "Stratosphere"
assert atmosphere.pressure_atm < 0.25
assert atmosphere.ozone > 0.2
let stratosphere_pressure = atmosphere.pressure_atm
teleport 0 307 0 0 0
assert atmosphere.layer == "Mesosphere"
assert atmosphere.pressure_atm < stratosphere_pressure
let mesosphere_temperature = atmosphere.temperature_k
teleport 0 322 0 0 0
assert atmosphere.layer == "Thermosphere"
assert atmosphere.temperature_k > mesosphere_temperature
assert atmosphere.ionization > 0.5
assert atmosphere.density_ratio < 0.001
teleport 0 338 0 0 0
assert atmosphere.layer == "Exosphere"
assert atmosphere.density_kg_m3 < 0.00001
teleport 0 345 0 0 0
assert atmosphere.layer == "Vacuum"
assert !atmosphere.active && atmosphere.pressure_atm == 0
teleport 0 50 0 0 0
assert atmosphere.layer == "Troposphere"
assert atmosphere.pressure_atm == surface_pressure
pause off
teleport 0 80 0 0 -0.1
screenshot
status
teleport 0 295 0 0 -0.1
screenshot
status
teleport 0 322 0 0 -0.1
screenshot
status
exit 0
DSL
set +e
(cd "$runtime" && timeout 120 "$binary" --debug-resolution 960x640 \
    --debug-script "$runtime/check.dsl" --debug-trace "$runtime/trace.jsonl") \
    > "$runtime/game.log" 2>&1
status=$?
set -e
printf 'atmosphere e2e artifacts: %s\n' "$runtime"
if ((status != 0)); then
    tail -50 "$runtime/game.log"
    exit "$status"
fi
rg 'DEBUG_CONTROL capture ok|DEBUG_SCRIPT complete|DEBUG_SCRIPT exit' "$runtime/game.log"
[[ $(rg -c '^DEBUG_CONTROL capture ok ' "$runtime/game.log") == 3 ]]
printf '%s\n' 'atmosphere e2e passed'
