#!/bin/sh
#
# Compile every production translation unit the way CI does.
#
# `make test` builds with the default CFLAGS, which do not include -Werror. CI
# supplies its own CFLAGS with -Werror, so a warning in production code passed
# locally and failed on CI. This script closes that gap by compiling each file
# with the CI flags and failing on the first warning.
#
# Only src/ is swept. Tests are excluded on purpose: mk/tests.mk gives each test
# its own -D*_TESTING macro set, so compiling a test file without them reports
# implicit declarations that are not defects.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

: "${CC:=cc}"
CFLAGS=${WERROR_CFLAGS:--std=c99 -Wall -Wextra -Werror -O2 -pthread}

raylib_include=
if [ -d build/raylib ]; then
    raylib_include=$(find build/raylib -maxdepth 3 -type d -name src -print |
        head -n 1)
fi
[ -z "$raylib_include" ] || raylib_include="-I$raylib_include"

work=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-werror.XXXXXX")
trap 'rm -rf -- "$work"' EXIT

# One compiler invocation per file, eight at a time, each writing its own log so
# the eight never interleave into one file. A serial sweep costs about 42s; this
# costs about 6s, cheap enough to run on every `make test`.
# shellcheck disable=SC2086
ls -1 src/*/*.c |
    xargs -P8 -I{} sh -c '
        out=$2/$(printf "%s" "$1" | tr / _).log
        if "'"$CC"'" -Isrc '"$raylib_include"' '"$CFLAGS"' -c -o /dev/null "$1" \
            >"$out" 2>&1; then
            rm -f -- "$out"
        fi
    ' _ {} "$work" ||
    true

failed=$(grep -l . "$work"/*.log 2>/dev/null | wc -l)
if [ "$failed" -ne 0 ]; then
    for log in $(grep -l . "$work"/*.log 2>/dev/null | head -n 5); do
        grep -E 'error:|warning:' "$log" | head -n 3 >&2
    done
    printf '\n-werror sweep failed: %s of %s production file(s) do not compile under CI flags (%s)\n' \
        "$failed" "$(ls -1 src/*/*.c | wc -l)" "$CFLAGS" >&2
    exit 1
fi

printf 'werror sweep passed: %s source file(s) under CI flags\n' \
    "$(ls -1 src/*/*.c | wc -l)"
