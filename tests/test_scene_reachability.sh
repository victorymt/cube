#!/bin/sh
#
# Every scene in tests/scenes must be reachable from something.
#
# A `.dsl` scene is not a test until a script runs it. Scenes accumulate: they
# are written for a bug investigation, the investigation ends, and the file stays
# behind with nothing invoking it. Three had drifted that way -- landform_replay,
# landform_terrace and volcano_history_face_boundary -- and two of them turned
# out to be the only coverage of river and terrace landform assertions.
#
# This test fails when no `tests/*.sh` driver and no make target runs a scene.
# The fix is either to wire it up or to delete it, and both are decisions someone
# has to make. Documentation does not count -- see the note below.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

missing=
total=0
for scene in tests/scenes/*.dsl; do
    total=$((total + 1))
    name=$(basename "$scene" .dsl)
    # Only a driver counts. A prose mention is not execution: `landform_replay`
    # and `landform_terrace` were both named in a design document while no script
    # ran either one, which is exactly the state this test exists to catch.
    if grep -rqF "$name" tests/*.sh Makefile mk/*.mk scripts/*.sh 2>/dev/null; then
        continue
    fi
    missing="$missing $name"
done

if [ -n "$missing" ]; then
    printf 'scenes nothing invokes:' >&2
    for name in $missing; do printf '\n  tests/scenes/%s.dsl' "$name" >&2; done
    printf '\n\nRun the scene from a tests/*.sh driver or a make target, or delete it.\n' >&2
    printf 'A document citing the scene is not enough: it has to execute.\n' >&2
    exit 1
fi

printf 'scene reachability passed: %s scene(s), all referenced\n' "$total"
