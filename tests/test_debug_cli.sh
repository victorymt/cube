#!/usr/bin/env bash

set -euo pipefail

game_binary=${1:-build/normal/voxelcraft}
if [[ ! -x "$game_binary" ]]; then
    echo "game binary is not executable: $game_binary" >&2
    exit 2
fi
game_binary=$(realpath "$game_binary")

runtime_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cli.XXXXXX")
trap 'rm -rf "$runtime_dir"' EXIT

run_case() {
    local expected=$1
    local label=$2
    shift 2
    local output status
    set +e
    output=$(cd "$runtime_dir" && "$game_binary" "$@" 2>&1)
    status=$?
    set -e
    if [[ "$status" -ne "$expected" ]]; then
        printf 'FAIL %s: expected exit %s, got %s\n%s\n' \
            "$label" "$expected" "$status" "$output" >&2
        exit 1
    fi
    printf '[PASS] %s (exit %s)\n' "$label" "$status"
}

assert_output_contains() {
    local label=$1
    local needle=$2
    shift 2
    local output
    output=$(cd "$runtime_dir" && "$game_binary" "$@" 2>&1 || true)
    if [[ "$output" != *"$needle"* ]]; then
        printf 'FAIL %s: missing %q in output:\n%s\n' \
            "$label" "$needle" "$output" >&2
        exit 1
    fi
}

run_case 2 "resolution below minimum" --debug-resolution 319x240
assert_output_contains "resolution error message" \
    "Invalid or repeated --debug-resolution option" \
    --debug-resolution 319x240
run_case 2 "resolution above maximum" --debug-resolution=7681x240
run_case 2 "repeated resolution" \
    --debug-resolution 640x480 --debug-resolution 800x600

run_case 2 "worker count below minimum" --chunk-workers 0
run_case 2 "worker count above maximum" --chunk-workers 9
run_case 2 "repeated worker count" \
    --chunk-workers 2 --chunk-workers 4

run_case 2 "missing debug script path" --debug-script
run_case 2 "empty debug script path" --debug-script=
run_case 2 "repeated debug script path" \
    --debug-script first.dsl --debug-script second.dsl

run_case 2 "empty debug trace path" --debug-trace=
run_case 2 "overlong debug trace path" \
    --debug-trace="$(printf '%*s' 512 '' | tr ' ' x)"

run_case 2 "missing plugin directory" --plugin-dir
run_case 2 "empty plugin directory" --plugin-dir=
run_case 2 "repeated plugin directory" \
    --plugin-dir first --plugin-dir second
run_case 2 "overlong plugin directory" \
    --plugin-dir="$(printf '%*s' 512 '' | tr ' ' x)"

bad_script="$runtime_dir/bad.dsl"
printf '%s\n' 'assert (' 'exit 0' >"$bad_script"
run_case 3 "batch parse failure" --debug-script "$bad_script"

large_script="$runtime_dir/large.dsl"
perl -e 'print "# filler\n" x 131073' >"$large_script"
run_case 2 "script size limit" --debug-script "$large_script"

printf '%s\n' 'debug CLI contract tests passed'
