#!/usr/bin/env bash
#
# Performance baseline routes, now executed instead of only documented.
#
# `tests/scenes/flat_static_benchmark.dsl` and `flat_rotation_benchmark.dsl` are
# referenced from docs/performance/flat-rotation-baseline.md, but nothing ran
# them: the document gave the commands and the scenes sat in the tree. A scene
# that only a document invokes is a scene that can rot unnoticed, which is what
# both of these did until tests/test_scene_reachability.sh was added.
#
# This driver asserts the scenes still execute and still hold their own
# invariants (the static route keeps the player anchored; the rotation route
# turns without moving the player). It deliberately does not compare frame times:
# those are host-dependent, which is why the document calls them a dated sample.

set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)

if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ||
      ! -S "${XDG_RUNTIME_DIR}/${WAYLAND_DISPLAY}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi

for scene in flat_static_benchmark flat_rotation_benchmark; do
    runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-${scene}.XXXXXX")
    ln -s "$repo/assets" "$runtime/assets"
    cp "$repo/tests/scenes/$scene.dsl" "$runtime/check.dsl"
    set +e
    (cd "$runtime" && timeout 300 "$binary" --debug-resolution 1280x720 \
        --debug-script "$runtime/check.dsl" \
        --debug-trace "$runtime/trace.jsonl" --debug-trace-frames) \
        >"$runtime/game.log" 2>&1
    status=$?
    set -e
    printf '%s artifacts: %s\n' "$scene" "$runtime"
    if ((status != 0)); then
        tail -50 "$runtime/game.log"
        exit "$status"
    fi
    [[ -s "$runtime/trace.jsonl" ]] || {
        echo "$scene produced no trace frames" >&2
        exit 1
    }
    grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"
done

printf '%s\n' 'flat baseline routes passed (static anchored, rotation turned)'
