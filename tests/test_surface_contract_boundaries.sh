#!/bin/sh
#
# The neutral surface contract is only useful if ordinary consumers stop naming
# legacy map chunks or cube-grid storage. This gate scans every ecology/gameplay/
# presentation file and the application layer, then permits only exact existing
# boundary files from the allowlist. It does not prove semantic correctness; it
# prevents a second backend leak from entering while M2-M4 remove the listed debt.
set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

allowlist=tests/surface_contract_boundary_allowlist.txt
[ -f "$allowlist" ] || {
    echo "missing $allowlist" >&2
    exit 2
}

tokens='SURFACE_TOPOLOGY_CUBE_GRID
SurfaceCubeChunkKey
WorldCube
WorldToChunkLocal
FindChunk
SurfaceChunkKey
world/surface_cube.h
world/chunks_cube.h
world/terrain_cube.h'

violations=
while IFS= read -r token; do
    [ -n "$token" ] || continue
    matches=$(
        grep -R -l -F "$token" \
            src/ecology src/gameplay src/presentation src/app \
            --include='*.c' --include='*.h' || true
    )
    for file in $matches; do
        if ! grep -qxF "$file|$token" "$allowlist"; then
            violations="$violations\n  $file: $token"
        fi
    done
done <<EOF_TOKENS
$tokens
EOF_TOKENS

if [ -n "$violations" ]; then
    printf 'surface contract boundary violations:%b\n' "$violations" >&2
    echo "Use SurfaceContract/SurfaceAddress instead of backend storage APIs." >&2
    exit 1
fi

stale=
while IFS= read -r entry; do
    case "$entry" in ''|'#'*) continue ;; esac
    file=${entry%%|*}
    token=${entry#*|}
    if [ ! -f "$file" ] || ! grep -q -F "$token" "$file"; then
        stale="$stale\n  $entry"
    fi
done < "$allowlist"
if [ -n "$stale" ]; then
    printf 'stale surface contract boundary allowlist entries:%b\n' "$stale" >&2
    echo "Delete each entry after its file stops using that backend token." >&2
    exit 1
fi

echo "surface contract boundary check passed"
