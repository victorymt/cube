#!/bin/sh
#
# The system dependency list has one source: mk/deps.mk.
#
# It used to exist in three places that disagreed. `raylib-check` probed five
# pkg-config names and named four distro packages, README printed a different
# install command, and CI installed a superset containing two packages this
# project does not use. The consequence was concrete: README's command omitted
# every X11 extension header GLFW needs, so following the README produced a
# raylib build that died inside third-party code; only CI happened to install
# enough.
#
# Scope: this checks that the *text* of the lists agrees. It does not check
# that the packages are correct -- only that correcting them happens in one
# file and everywhere else follows. That is the part a machine can enforce; the
# judgement stays with the reviewer.
#
# What it cannot catch: a package that is listed everywhere but is not actually
# needed, or a needed header that no list mentions. Both were true before this
# gate existed (libasound2-dev, libglu1-mesa-dev were unnecessary; libxext-dev
# was missing) and both were found by tracing what GLFW includes, not by this
# script.
#
# It deliberately does not compare against the distro: pkg-config names are
# distro-neutral and a Debian package list cannot be validated on Arch. The
# invariant is agreement between this repository's own files.

set -eu

repository=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
cd "$repository"

fail() {
    printf '%s\n' "$1" >&2
    exit 1
}

[ -f mk/deps.mk ] || fail "mk/deps.mk is missing; dependency lists need one home"
grep -q '^RAYLIB_DEBIAN_PACKAGES' mk/deps.mk ||
    fail "mk/deps.mk must declare RAYLIB_DEBIAN_PACKAGES"
grep -q '^RAYLIB_PKG_CONFIG_DEPS' mk/deps.mk ||
    fail "mk/deps.mk must declare RAYLIB_PKG_CONFIG_DEPS"
grep -q '^DEV_TOOL_DEPS' mk/deps.mk ||
    fail "mk/deps.mk must declare DEV_TOOL_DEPS"

# The Debian package list, expanded the way make expands it.
debian=$(make -s --no-print-directory -f - print-debian <<'EOF'
include mk/deps.mk
print-debian:
	@printf '%s\n' '$(RAYLIB_DEBIAN_PACKAGES)'
EOF
)
[ -n "$debian" ] || fail "RAYLIB_DEBIAN_PACKAGES expanded to nothing"

# CI installs exactly these. Every job's apt line must be satisfied by the
# declared list, and no job may install a package the list does not mention:
# the first direction catches a list that outgrew CI, the second catches a CI
# line that drifted.
work=$(mktemp -d "${TMPDIR:-/tmp}/voxelcraft-deps.XXXXXX")
trap 'rm -rf -- "$work"' EXIT
work_debian=$work/debian
work_ci=$work/ci

ci_packages=$(sed -n '/apt-get install --yes/,/[^\\]$/p' .github/workflows/ci.yml |
    sed 's/^ *sudo apt-get install --yes //' |
    sed 's/^ *//' |
    sed 's/ \\$//' |
    tr ' ' '\n' |
    grep -vE '^(build-essential|clang|curl|pkg-config|gcovr)$' |
    grep -v '^$' |
    sort -u)

[ -n "$ci_packages" ] || fail "no apt packages found in .github/workflows/ci.yml"

# build-essential, curl and pkg-config are in the deps.mk list so the install
# command it prints is self-sufficient, but CI installs them on one line above
# the X11 packages. Compare only the package set both sides are asserting.
printf '%s\n' $debian |
    grep -vE '^(build-essential|curl|pkg-config)$' |
    sort -u > "$work_debian"
printf '%s\n' "$ci_packages" | sort -u > "$work_ci"
missing_from_ci=$(comm -23 "$work_debian" "$work_ci")
extra_in_ci=$(comm -13 "$work_debian" "$work_ci")

if [ -n "$missing_from_ci" ]; then
    printf 'packages declared in mk/deps.mk but absent from CI:\n%s\n' \
        "$(printf '%s\n' "$missing_from_ci" | sed 's/^/  /')" >&2
    fail "CI would build against a narrower dependency set than mk/deps.mk declares"
fi

if [ -n "$extra_in_ci" ]; then
    printf 'packages installed by CI but not declared in mk/deps.mk:\n%s\n' \
        "$(printf '%s\n' "$extra_in_ci" | sed 's/^/  /')" >&2
    fail "CI installs packages this project does not declare; add or remove them in mk/deps.mk"
fi

# README must not carry its own install command any more. It points at
# `make prerequisites`, which prints the lists above.
if grep -nE 'apt install lib(wayland|xkbcommon|x11|xrandr|xi|xinerama|xcursor|xext|gl1)' README.md; then
    fail "README repeats the install command; point it at 'make prerequisites' instead"
fi
grep -q 'make prerequisites' README.md ||
    fail "README must point at 'make prerequisites' for the dependency list"

# raylib-check must read the shared variables, not its own literal list.
if grep -nE 'for package in [a-z]' mk/raylib.mk; then
    fail "mk/raylib.mk hardcodes a package list; use RAYLIB_PKG_CONFIG_DEPS"
fi
grep -q 'RAYLIB_PKG_CONFIG_DEPS' mk/raylib.mk ||
    fail "mk/raylib.mk must probe RAYLIB_PKG_CONFIG_DEPS"

printf 'build prerequisites: %s declared, CI matches, README defers\n' \
    "$(printf '%s\n' $debian | sort -u | wc -l)"
