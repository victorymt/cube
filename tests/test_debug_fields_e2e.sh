#!/usr/bin/env bash
#
# Every runtime name `docs/debug-dsl.md` advertises must still resolve.
#
# `make docs-probe` already answers this, but it needs a live frame loop and a
# readable display, so it only runs on the Hyprland runner. This test is the
# subset that needs no GPU: it drives `--debug-stdin` against the real parser and
# checks that each documented field name is accepted as an expression. It does
# not check values -- that is what docs-probe is for -- only that the name the
# documentation promises is a name the engine knows.
#
# A sample of one field per namespace runs by default so `make test` stays fast.
# Set DEBUG_FIELD_SAMPLE=all to check every documented field.

set -euo pipefail

binary=$(realpath "${1:-build/normal/voxelcraft}")
repo=$(cd "$(dirname "$0")/.." && pwd)
sample_size=${DEBUG_FIELD_SAMPLE:-24}

if [[ ! -x "$binary" ]]; then
    echo "game binary is not executable: $binary" >&2
    exit 2
fi
if [[ -z "${WAYLAND_DISPLAY:-}" || -z "${XDG_RUNTIME_DIR:-}" ]]; then
    echo 'an active Wayland session is required' >&2
    exit 2
fi

runtime=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-fields.XXXXXX")
trap 'rm -rf "$runtime"' EXIT
ln -s "$repo/assets" "$runtime/assets"

# Field names come out of the namespace tables, as in scripts/docs-probe.mjs, so
# the documentation stays the single source of what is advertised.
fields=$(cd "$repo" && python3 - "$sample_size" <<'PY'
import re, sys
limit = sys.argv[1]
source = open('docs/debug-dsl.md', encoding='utf8').read()
names, seen = [], set()
in_namespaces = False
for line in source.split('\n'):
    if line.startswith('## '):
        in_namespaces = bool(re.search(r'runtime (namespace|namespaces|field)', line, re.I))
    if not in_namespaces or not line.startswith('|'):
        continue
    first_cell = line[1:].split('|')[0]
    for span in re.findall(r'`([^`]+)`', first_cell):
        for candidate in span.split(','):
            name = candidate.strip()
            if not re.fullmatch(r'[a-z][a-z0-9_]*(\.[a-z0-9_]+)*', name):
                continue
            if name in seen:
                continue
            seen.add(name)
            names.append(name)

# One field per top-level namespace first, so a small sample still covers every
# namespace the document defines rather than 24 fields from whichever table
# happens to be first.
by_namespace = {}
for name in names:
    by_namespace.setdefault(name.split('.')[0], []).append(name)
picked = [group[0] for group in by_namespace.values()]
if limit != 'all':
    limit = int(limit)
    for name in names:
        if len(picked) >= limit:
            break
        if name not in picked:
            picked.append(name)
for name in picked:
    print(name)
PY
)

if [[ -z "$fields" ]]; then
    echo "no runtime fields found in docs/debug-dsl.md" >&2
    exit 1
fi

count=$(printf '%s\n' "$fields" | wc -l)
printf 'checking %s documented runtime field(s)\n' "$count"

# One `let` per field, then `exit`. A name the engine does not know produces
# `DEBUG_SCRIPT error ... message=undefined variable '<name>'` and aborts the
# block, so a single failure names the field that broke.
{
    printf 'terrain flat\nstart\nwait until game.screen == "playing" timeout 7200\n'
    while IFS= read -r name; do
        variable="probe_$(printf '%s' "$name" | tr '.' '_')"
        printf 'let %s = %s\n' "$variable" "$name"
    done <<<"$fields"
    printf 'exit 0\n'
} > "$runtime/probe.dsl"

set +e
(cd "$runtime" && timeout 300 "$binary" --debug-resolution 640x480 \
    --debug-script "$runtime/probe.dsl") >"$runtime/game.log" 2>&1
status=$?
set -e

if ((status != 0)); then
    undefined=$(grep -oE "undefined variable '[^']+'" "$runtime/game.log" | head -n 5 || true)
    if [[ -n "$undefined" ]]; then
        printf 'documented runtime names do not resolve:\n%s\n' "$undefined" >&2
        printf '\nFix docs/debug-dsl.md or the dispatch that stopped accepting them.\n' >&2
    else
        tail -40 "$runtime/game.log" >&2
    fi
    exit 1
fi

grep -q '^DEBUG_SCRIPT exit code=0$' "$runtime/game.log"
printf 'documented runtime fields resolved: %s name(s)\n' "$count"
