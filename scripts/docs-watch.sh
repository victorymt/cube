#!/bin/sh
# Live documentation drift watcher for a working session.
#
# `make check-docs` already fails on drift, but only when someone runs it. This
# watcher runs the cheap half on every save so the drift shows up in the terminal
# where the edit happened, instead of at commit time.
#
# Deliberately not part of CI: it watches a working tree, which CI does not have.
# Use it while editing:
#
#     make docs-watch          # or: scripts/docs-watch.sh
#
set -eu

repo=$(git rev-parse --show-toplevel)
cd "$repo"

command -v inotifywait >/dev/null 2>&1 ||
    { echo "docs-watch: inotifywait is required (inotify-tools)" >&2; exit 1; }

echo "docs-watch: watching src/, mk/, Makefile, docs/, todo/, .dsh/skills/"
echo "docs-watch: the static half runs on each change; the runtime probe does not"
echo "docs-watch: press Ctrl+C to stop"

# Coalesce editor write bursts (a save can emit several events) without a
# long delay on the last one.
run() {
    sleep 0.3
    if ! out=$(node scripts/check-docs.mjs --root . 2>&1 && node scripts/check-docs-truth.mjs --root . 2>&1); then
        printf '\n--- %s\n' "$(date +%H:%M:%S)"
        printf '%s\n' "$out" | grep -E '^(error|warn)' || printf '%s\n' "$out"
    else
        printf '%s docs ok\n' "$(date +%H:%M:%S)"
    fi
}

inotifywait -m -q -r \
    -e modify -e create -e delete -e move \
    --include '\.(md|json|c|h|mk|sh|mjs)$' \
    src mk docs todo .dsh/skills Makefile 2>/dev/null |
while read -r _directory _events file; do
    run
done
