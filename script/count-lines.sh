#!/bin/sh

set -eu

project_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

printf 'extension\tall lines\tnon-empty lines\n'
printf '%s\t%s\t%s\n' '---------' '---------' '---------------'

find "$project_root" \
    -type d \( \
        -name .git -o \
        -name build -o \
        -name assets -o \
        -name screenshots -o \
        -name docs \
    \) -prune -o \
    -type f \( \
        -name '*.c' -o -name '*.h' -o \
        -name '*.cc' -o -name '*.cpp' -o -name '*.hh' -o -name '*.hpp' -o \
        -name '*.m' -o -name '*.mm' -o \
        -name '*.sh' -o -name '*.py' -o -name '*.js' -o -name '*.qml' \
    \) -print0 |
    xargs -0 -r awk '
        {
            name = FILENAME
            sub(/^.*\//, "", name)
            extension = name
            sub(/^.*\./, "", extension)

            lines[extension]++
            if ($0 !~ /^[[:space:]]*$/) {
                nonempty[extension]++
            }
            total++
            if ($0 !~ /^[[:space:]]*$/) {
                total_nonempty++
            }
        }

        END {
            for (extension in lines) {
                printf "%s\t%d\t%d\n", extension, lines[extension], nonempty[extension]
            }
            printf "total\t%d\t%d\n", total, total_nonempty
        }
    ' |
    sort -k1,1
