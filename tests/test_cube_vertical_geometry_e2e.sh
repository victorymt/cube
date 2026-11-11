#!/bin/sh
#
# G1 visual replay: the same flat vertical wall is captured through legacy and
# cube-grid. The unit test owns the exact per-vertex transform invariant; this
# scene keeps the real Hyprland render path, cube activation, block edits and
# mesh upload deterministic and reviewable. It does not claim polar coverage.
set -eu

repo=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
binary=${1:-"$repo/build/normal/voxelcraft"}
run_dir=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-vertical.XXXXXX")
trap 'rm -rf "$run_dir"' EXIT

cd "$repo"
scripts/run-scene.sh \
    --binary "$binary" \
    --run-dir "$run_dir" \
    cube_vertical_geometry_replay

reports=$(find "$run_dir/screenshots" -name '*.txt' | wc -l | tr -d ' ')
images=$(find "$run_dir/screenshots" -name '*.png' | wc -l | tr -d ' ')
[ "$reports" -eq 4 ] || {
    printf 'expected 4 capture reports, got %s\n' "$reports" >&2
    exit 1
}
[ "$images" -eq 4 ] || {
    printf 'expected 4 screenshots, got %s\n' "$images" >&2
    exit 1
}
grep -q '^camera.position=0.000000,10.550000,0.000000$' \
    "$run_dir"/screenshots/*.txt || {
    echo 'cube/legacy captures did not share the expected camera pose' >&2
    exit 1
}
grep -q 'surface debug ok world=cube' "$run_dir/game.log" || {
    echo 'cube activation was not observed in the replay log' >&2
    exit 1
}

python3 - "$run_dir/trace.jsonl" <<'PY'
import json
import sys
ready = {}
for line in open(sys.argv[1], encoding="utf-8"):
    event = json.loads(line)
    reason = event.get("reason")
    if event.get("type") == "event" and reason in (
            "mouse_left_set_block", "mouse_left_mesh_ready"):
        ready[reason] = event
if set(ready) != {"mouse_left_set_block", "mouse_left_mesh_ready"}:
    raise SystemExit("missing left-click mesh lifecycle event")
if ready["mouse_left_mesh_ready"]["frame"] <= ready["mouse_left_set_block"]["frame"]:
    raise SystemExit("mesh_ready must follow the cube mesh upload frame")
PY

set -- $(find "$run_dir/screenshots" -name '*.txt' | sort)
before_report=$3
after_report=$4
before_triangles=$(sed -n 's/^render.coverage.main_solid_triangles=//p' "$before_report")
after_triangles=$(sed -n 's/^render.coverage.main_solid_triangles=//p' "$after_report")
[ -n "$before_triangles" ] && [ -n "$after_triangles" ] || {
    echo 'missing cube render triangle evidence' >&2
    exit 1
}
[ "$before_triangles" != "$after_triangles" ] || {
    echo 'block destruction did not change rendered cube triangles' >&2
    exit 1
}

printf 'cube vertical geometry replay passed; artifacts=%s\n' "$run_dir"
trap - EXIT
