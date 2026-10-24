#!/bin/sh
# Install the repository's git hooks.
#
# The hooks are real files under tools/git-hooks/ so they are reviewable and
# versioned. This script only links them into .git/hooks/, and refuses to
# overwrite a hook it did not install.
set -eu

repo=$(git rev-parse --show-toplevel)
cd "$repo"

hooks_dir=$(git rev-parse --git-path hooks)
mkdir -p "$hooks_dir"

installed=0
for hook in tools/git-hooks/*; do
    [ -f "$hook" ] || continue
    name=$(basename "$hook")
    target=$hooks_dir/$name

    if [ -e "$target" ] && ! grep -q "Voxelcraft documentation gate" "$target" 2>/dev/null; then
        echo "install-hooks: refusing to replace an existing $name hook" >&2
        echo "install-hooks: move $target aside and re-run 'make hooks'" >&2
        exit 1
    fi

    chmod +x "$hook"
    ln -sf "$(realpath --relative-to="$hooks_dir" "$hook")" "$target"
    echo "install-hooks: $name -> $hook"
    installed=$((installed + 1))
done

[ "$installed" -gt 0 ] || {
    echo "install-hooks: no hooks found under tools/git-hooks" >&2
    exit 1
}

echo "install-hooks: $installed hook(s) installed; skip the runtime probe with SKIP_DOCS_PROBE=1"
