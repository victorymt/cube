#!/bin/sh
#
# The performance documents quote four numbers as if they were invariants:
#
#   mesh_snapshot_bytes=8192
#   solid_vertices_per_chunk=2187.8
#   solid_vertex_reduction_percent=68.5
#   sync_rebuilds=0
#
# Three of them are computed from the mesh the benchmark builds. The fourth used
# to be printed as a literal, so the "invariant" survived any change to the
# streaming path. This test pins the property that made that bug possible: the
# benchmark's four values must match what the benchmark itself measured, and the
# sync counter must come from the streaming stats, not from a constant.
#
# It runs the benchmark and compares against the recorded semantic invariants.
# Timing numbers are deliberately not asserted -- they are host-dependent, which
# is why docs/terrain-performance.md calls them a dated baseline.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

benchmark=build/normal/tests/benchmark_chunks
[ -x "$benchmark" ] || {
    echo "benchmark binary is missing; run 'make benchmark-chunks' first" >&2
    exit 2
}

output=$("$benchmark")

value_of() {
    printf '%s\n' "$output" | sed -n "s/^$1=//p" | head -n 1
}

fail=0
expect() {
    actual=$(value_of "$1")
    if [ "$actual" != "$2" ]; then
        printf 'FAIL %s: expected %s, measured %s\n' "$1" "$2" "$actual" >&2
        fail=1
    fi
}

expect mesh_snapshot_bytes 8192
expect solid_vertices_per_chunk 2187.8
expect solid_vertex_reduction_percent 68.5
expect sync_rebuilds 0

# The counter must be present at all. A stray edit that drops the line would
# otherwise make the check above read an empty string against "0" and pass only
# because sed found nothing.
if ! printf '%s\n' "$output" | grep -q '^sync_rebuilds='; then
    echo "FAIL sync_rebuilds is missing from the benchmark output" >&2
    fail=1
fi

# The benchmark must not report a sync rebuild it did not measure. This guards
# the specific regression that shipped: a literal in place of the counter.
if grep -q 'printf("sync_rebuilds=0' tests/benchmark_chunks.c; then
    echo "FAIL tests/benchmark_chunks.c prints sync_rebuilds as a literal" >&2
    fail=1
fi

[ "$fail" -eq 0 ] || exit 1
echo "benchmark invariants passed: mesh 8192 B, 2187.8 vertices, 68.5% reduction, 0 sync rebuilds"
