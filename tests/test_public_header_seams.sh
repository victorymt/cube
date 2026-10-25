#!/bin/sh
#
# Public headers must not grow new test-only surfaces without someone deciding
# to allow them.
#
# A `#ifdef *_TESTING` block inside a public header is how this repo exposes
# internals to a test without making them part of the production API. That is a
# deliberate trade, not an accident, and each one costs a conditional in the
# contract a consumer reads. Fifteen headers carry one today. Nothing stopped a
# sixteenth from appearing silently, which is what this test does: it pins the
# current set, so adding a seam requires editing this list and saying why.
#
# Test seams were also recently moved out of `terrain.h` into `terrain_test.h`
# for exactly this reason -- the production header had been carrying eleven
# declarations that only test builds compiled. Keeping the list visible makes the
# next one a decision instead of a side effect.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

# header:macro -- one line per accepted seam. Sorted.
expected='app/game_debug_trace.h:GAME_DEBUG_TRACE_TESTING
app/game_save.h:GAME_SAVE_TESTING
app/game_stream_audit.h:GAME_STREAM_AUDIT_TESTING
ecology/ecology.h:CHUNKS_TESTING
ecology/ecology.h:ECOLOGY_TESTING
ecology/entity.h:ENTITY_TESTING
gameplay/map_markers.h:MAP_MARKERS_TESTING
presentation/planet_renderer.h:PLANET_RENDERER_TESTING
presentation/render.h:RENDER_PLANETS_TESTING
presentation/world_renderer.h:WORLD_RENDERER_TESTING
world/cga_voxel.h:TERRAIN_TESTING
world/chunks.h:CHUNKS_TESTING
world/terrain_test.h:TERRAIN_TESTING
world/volcano_deposition_model.h:VOLCANO_DEPOSITION_TESTING
world/world_cube.h:CHUNKS_TESTING'

# Internal headers are excluded: a seam there costs a public consumer nothing.
# `chunks_dependencies.h` qualifies without the `_internal` suffix -- only
# `chunks_internal.h` includes it -- so it is named explicitly.
#
# The match is anchored to a whole line that is exactly `#ifdef NAME`, so prose
# mentioning `#ifdef TERRAIN_TESTING` in a comment does not register.
actual=$(grep -rnE '^[[:space:]]*#ifdef[[:space:]]+[A-Z_]+_TESTING[[:space:]]*$' src/*/*.h 2>/dev/null |
    grep -vE '_internal\.h:|chunks_dependencies\.h:' |
    sed 's|^src/||' |
    sed -E 's|^([^:]+):[0-9]+:[[:space:]]*#ifdef[[:space:]]+([A-Z_]+_TESTING)[[:space:]]*$|\1:\2|' |
    sort -u)

if [ "$actual" != "$expected" ]; then
    added=$(printf '%s\n%s\n' "$expected" "$actual" | sort | uniq -u)
    printf 'test seams in public headers changed:\n' >&2
    printf '%s\n' "$added" | sed 's/^/  /' >&2
    printf '\nIf a seam was added on purpose, add it to the list in tests/%s\n' \
        'test_public_header_seams.sh and note why in the commit message.' >&2
    printf 'If a header should stay clean, move the seam into an _internal.h header,\n' >&2
    printf 'as terrain.h did when its test declarations moved to terrain_test.h.\n' >&2
    exit 1
fi

printf 'public header test seams unchanged: %s known seam(s)\n' \
    "$(printf '%s\n' "$expected" | wc -l)"
