#!/usr/bin/env bash
# Cross-process cube persistence: the reader must be a fresh process, so this
# cannot be a single-process unit test.
set -euo pipefail
binary=${1:-build/normal/tests/cube_save_reload_tool}
work=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-cube-save.XXXXXX")
trap 'rm -rf "$work"' EXIT
"$binary" write "$work/edits.bin"
"$binary" read "$work/edits.bin"
# A truncated file must be rejected by a fresh process, not accepted.
head -c -40 "$work/edits.bin" > "$work/truncated.bin"
if "$binary" read "$work/truncated.bin" >/dev/null 2>&1; then
    echo 'truncated payload was accepted' >&2
    exit 1
fi
echo 'cross-process cube persistence passed'
