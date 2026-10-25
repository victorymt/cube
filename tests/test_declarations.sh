#!/bin/sh
#
# Every test file must say what it verifies before it includes anything.
#
# `.dsh/skills/voxelcraft-tests` asks for a 3-8 line declaration covering
# intent, scope, what is deliberately not covered, and the requirement or bug it
# came from. That rule was unenforced prose, so 6 of 126 files followed it.
#
# This script makes the mechanical part enforceable: a test file must carry a
# substantive comment before its first `#include`. It does NOT require the four
# field labels to appear literally, because several of the best existing
# declarations state the same content in their own words -- "it does not touch
# the production runtime" is a scope boundary without the word "scope". Demanding
# vocabulary would fail good writing and teach people to game the check.
#
# Field coverage is reported as a note. `--strict` promotes it to an error, which
# is how the backlog gets counted without blocking work today.
#
# The rule targets new and edited tests, not the existing corpus. Files that
# predate it are listed in tests/test_declarations_allowlist.txt; each entry is
# removable debt, and the list is checked for staleness so it cannot drift.
#
# What this cannot do: judge whether a declaration is true. A passing check means
# the file explains itself, not that the explanation matches the assertions.
# That stays a review question, which is why the skill's section 4 exists.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

allowlist=tests/test_declarations_allowlist.txt
strict=0
case "${1:-}" in
    --strict) strict=1 ;;
    "") ;;
    *)
        echo "usage: $0 [--strict]" >&2
        exit 2
        ;;
esac

if [ ! -f "$allowlist" ]; then
    echo "missing $allowlist" >&2
    exit 2
fi

# A declaration shorter than this is a label, not an explanation. Eight words is
# the floor set by the shortest declaration that is actually good in the tree --
# "Staged cube activation: ordering, coverage wait, rollback and the gate." --
# which names four covered aspects and is exactly what the rule wants.
minimum_words=8

missing_declaration=
too_thin=
thin_fields=
grandfathered=

# Everything before the first #include, which is where a file-level comment goes.
header_of() {
    awk '/^[[:space:]]*#include/ { exit } { print }' "$1"
}

is_grandfathered() {
    grep -qxF "$1" "$allowlist"
}

# Prose of the leading comment block, as a word count.
declaration_words() {
    header_of "$1" |
        sed -n '/\/\*/,/\*\//p' |
        sed 's|/\*||; s|\*/||; s|^[[:space:]]*\*||' |
        sed 's|^[[:space:]]*//||' |
        tr -s ' \t\n' ' ' |
        awk '{ print NF }'
}

for file in tests/test_*.c; do
    header=$(header_of "$file")
    has_prose=no
    printf '%s\n' "$header" | grep -qE '/\*|//' && has_prose=yes

    if [ "$has_prose" = "no" ]; then
        if is_grandfathered "$file"; then
            grandfathered="$grandfathered $file"
        else
            missing_declaration="$missing_declaration $file"
        fi
        continue
    fi

    words=$(declaration_words "$file")
    if [ "$words" -lt "$minimum_words" ]; then
        if is_grandfathered "$file"; then
            grandfathered="$grandfathered $file"
        else
            too_thin="$too_thin ${file}:${words}"
        fi
        continue
    fi

    # Soft check: which of the four concepts are discernible in the wording.
    fields=0
    printf '%s\n' "$header" | grep -qiE 'intent|verif|must |ensures?|guards?|probe|foundation' && fields=$((fields + 1))
    printf '%s\n' "$header" | grep -qiE 'scope|covers?|inclu|across|every |all |samples?|cases?' && fields=$((fields + 1))
    printf '%s\n' "$header" | grep -qiE 'not cover|does not|doesn.t|only |pure |no .*link|out of scope|unrelated' && fields=$((fields + 1))
    printf '%s\n' "$header" | grep -qiE 'source|from |issue|bug|record [0-9]|docs/|todo/|[0-9]{4}-[0-9]{2}-[0-9]{2}' && fields=$((fields + 1))
    if [ "$fields" -lt 4 ]; then
        thin_fields="$thin_fields $file($fields/4)"
    fi
done

status=0

if [ -n "$missing_declaration" ]; then
    printf 'test files with no declaration:\n' >&2
    for file in $missing_declaration; do printf '  %s\n' "$file" >&2; done
    printf '\nAdd a comment before the first #include stating what the test\n' >&2
    printf 'verifies and what it deliberately does not cover, plus where the\n' >&2
    printf 'requirement or bug came from.\n' >&2
    printf 'See .dsh/skills/voxelcraft-tests section 3, and the positive\n' >&2
    printf 'examples in tests/test_world_block_landing.c.\n' >&2
    status=1
fi

if [ -n "$too_thin" ]; then
    printf 'declarations too short to say anything (minimum %s words):\n' \
        "$minimum_words" >&2
    for entry in $too_thin; do
        printf '  %s (%s words)\n' "${entry%%:*}" "${entry##*:}" >&2
    done
    printf '\nName the behaviour and at least one boundary. A restatement of\n' >&2
    printf 'the function name is not a declaration.\n' >&2
    status=1
fi

# An allowlist entry that gained a declaration, or whose file is gone, is stale
# debt. Fail so the list stays equal to the real backlog.
stale=
while IFS= read -r entry; do
    case "$entry" in ''|'#'*) continue ;; esac
    if [ ! -f "$entry" ]; then
        stale="$stale $entry(gone)"
        continue
    fi
    if printf '%s\n' "$(header_of "$entry")" | grep -qE '/\*|//' &&
        [ "$(declaration_words "$entry")" -ge "$minimum_words" ]; then
        stale="$stale ${entry}:documented"
    fi
done < "$allowlist"

if [ -n "$stale" ]; then
    printf '\nstale allowlist entries:\n' >&2
    for entry in $stale; do
        printf '  %s (%s)\n' "${entry%%:*}" "${entry##*:}" >&2
    done
    printf '\nDelete these lines from %s. The list must equal the real backlog,\n' \
        "$allowlist" >&2
    printf 'or it stops measuring anything.\n' >&2
    status=1
fi

count=$(ls -1 tests/test_*.c | wc -l)
backlog=$(printf '%s' "$grandfathered" | wc -w)

if [ -n "$thin_fields" ]; then
    if [ "$strict" -eq 1 ]; then
        printf 'declarations not covering all four fields:\n' >&2
        for entry in $thin_fields; do printf '  %s\n' "$entry" >&2; done
        printf '\nState intent, scope, what is not covered, and the source.\n' >&2
        status=1
    else
        printf 'note: %s declaration(s) do not mention all four fields of the\n' \
            "$(printf '%s' "$thin_fields" | wc -w)" >&2
        printf 'skill; run with --strict to list them and fail on them.\n' >&2
    fi
fi

if [ "$status" -eq 0 ]; then
    printf 'test declarations passed: %s file(s), %s grandfathered\n' \
        "$count" "$backlog"
fi

if [ "$strict" -eq 1 ] && [ -n "$grandfathered" ]; then
    printf '\n--strict backlog (%s of %s files lack a declaration):\n' \
        "$backlog" "$count" >&2
    for file in $grandfathered; do printf '  %s\n' "$file" >&2; done
    printf '\nDelete each line from %s as you document the file.\n' \
        "$allowlist" >&2
fi

exit "$status"
