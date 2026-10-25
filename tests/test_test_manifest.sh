#!/bin/sh
#
# Every test manifest entry must supply all nine fields.
#
# `mk/tests.mk` declares each test as a positional `$(call define_test, ...)`
# with nine arguments: variable, binary, sources, cppflags, cflags, ldflags,
# ldlibs, raylib cflags, raylib libs. Empty fields are written as consecutive
# commas, which is what makes an *omitted* field invisible: make fills the
# slots from the left, so dropping one silently shifts every later field.
#
# That happened twice (test_cube_raycast and cube_save_reload_tool). Both kept
# compiling because the shifted flags happened to be accepted in their new
# positions -- `-l...` is legal in LDFLAGS, `-I` is legal in LDLIBS -- while
# the last field, the vendored raylib archive, was dropped entirely. The day
# either target calls a raylib function the failure would be an undefined
# reference, pointing at the test source instead of at the missing comma.
#
# Scope: arity only. It cannot tell whether a flag is in the *right* field,
# only that no field is missing. Judging placement is a review question.
#
# Not covered: entries that pass nine fields but put them in the wrong slots.
# The fix for that class would be named fields, which is a larger change than
# this defect warrants.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

manifest=mk/tests.mk
[ -f "$manifest" ] || {
    echo "missing $manifest" >&2
    exit 2
}

# Count top-level commas by tracking bracket depth, so that a $(call ...) or
# $(sort ...) inside a field does not split it.
report=$(awk '
/^\$\(eval \$\(call define_(test|benchmark),/ {
    line = $0
    sub(/^\$\(eval \$\(call define_(test|benchmark),/, "", line)
    sub(/\)\)[[:space:]]*$/, "", line)
    depth = 0
    fields = 1
    for (i = 1; i <= length(line); i++) {
        c = substr(line, i, 1)
        if (c == "(") depth++
        else if (c == ")") depth--
        else if (c == "," && depth == 0) fields++
    }
    if (fields != 9) {
        printf "%s:%d: %d field(s), expected 9: %s\n", FILENAME, NR, fields, substr($0, 1, 100)
    }
}' "$manifest")

if [ -n "$report" ]; then
    printf '%s\n' "$report" >&2
    echo "" >&2
    echo "Fields for define_test/define_benchmark, in order:" >&2
    echo "  variable, binary, sources, cppflags, cflags, ldflags, ldlibs," >&2
    echo "  raylib cflags, raylib libs" >&2
    echo "An empty field is an empty string between two commas; a *missing*" >&2
    echo "field shifts everything after it left by one." >&2
    exit 1
fi

entries=$(awk '/^\$\(eval \$\(call define_(test|benchmark),/ { n++ } END { print n + 0 }' "$manifest")
echo "test manifest arity passed: $entries entr(y|ies) with 9 fields"
