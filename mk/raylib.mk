# Build the one raylib this tree uses.
#
# raylib is vendored rather than taken from the system. Everything else was
# tried and failed: pkg-config reported versions that did not match the headers
# actually compiled, a hand-built copy under /usr/local silently shadowed the
# package manager's, and gcc ignores -I/usr/include because it is a system
# directory, so none of it could be corrected from the build. Vendoring makes
# the answer to "which raylib" structural instead of a runtime check that can
# only tell you it is wrong after the fact.
#
# One library serves both display backends. GLFW on Linux can be compiled with
# X11 and Wayland support together and picks one at startup, so there is a
# single build for X11 sessions and Hyprland alike -- no second variant to keep
# in sync.
#
# It is also one library for every BUILD_VARIANT. The archive lives under
# $(BUILD_ROOT)/raylib rather than $(BUILD_DIR), because raylib's flags come
# from its own Makefile and have nothing to do with the sanitizer or coverage
# flags the outer variant sets. `make clean` removes it, which is the only way
# to recompile it.
#
# That separation only holds if the outer make stops *passing its variables
# down*. A command-line `CFLAGS=...` reaches a recursive make through both the
# environment and MAKEFLAGS, and raylib assigns `CFLAGS =` without `override`,
# so it loses its own `-D_GLFW_X11 -D_GLFW_WAYLAND -DPLATFORM_DESKTOP_GLFW`
# and the build fails on -Werror or on "Cannot disable Wayland and X11 at the
# same time". The recipe below unsets the three variables that carry that
# state (`MAKEOVERRIDES=` and `MAKEFLAGS=` do not work: CFLAGS is exported as
# a plain environment variable too). Consequence: the raylib build does not
# use the outer jobserver, so it is serial. That is ~30s, once, per build root.

RAYLIB_SRC_DIR := $(BUILD_ROOT)/raylib/raylib-$(RAYLIB_VERSION)
RAYLIB_LIB := $(RAYLIB_SRC_DIR)/src/libraylib.a
RAYLIB_TARBALL := $(BUILD_ROOT)/raylib/raylib-$(RAYLIB_VERSION).tar.gz
# codeload is the host the github.com archive URL redirects to; going there
# directly avoids a slow redirect hop that has timed out on this machine.
RAYLIB_URL := https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/$(RAYLIB_VERSION)

# Link-time dependencies of a raylib built with both backends. These are
# ordinary distribution packages; the header/version problem above was the
# reason for vendoring, not these. The module list comes from mk/deps.mk, which
# is the one place system dependencies are declared.
RAYLIB_SYSTEM_LIBS := \
	$(shell $(PKG_CONFIG) --libs wayland-client wayland-cursor wayland-egl xkbcommon 2>/dev/null) \
	$(shell $(PKG_CONFIG) --libs x11 2>/dev/null || echo -lX11) \
	-lGL -lm -lpthread -ldl -lrt

# A stamp that exists only once the raylib headers are on disk. Object rules
# depend on it so that no translation unit can compile before the include path
# it points at has anything in it.
RAYLIB_HEADER_STAMP := $(RAYLIB_SRC_DIR)/.headers-ready

RAYLIB_CFLAGS := -I$(RAYLIB_SRC_DIR)/src
RAYLIB_LIBS := $(RAYLIB_LIB) $(RAYLIB_SYSTEM_LIBS)

.PHONY: raylib raylib-check prerequisites

raylib: $(RAYLIB_LIB) raylib-check
	@:

# Report missing *system* packages by name. The vendored raylib itself needs no
# version check: its path is fixed by RAYLIB_VERSION.
#
# Packages are derived from RAYLIB_PKG_CONFIG_DEPS / RAYLIB_REQUIRED_PROGRAMS in
# mk/deps.mk and the install lines from that file's package lists, so the probe,
# the advice it prints, README and CI cannot drift apart again.
raylib-check:
	@set -eu; \
	missing=''; \
	for package in $(RAYLIB_PKG_CONFIG_DEPS); do \
		$(PKG_CONFIG) --exists "$$package" || missing="$$missing $$package"; \
	done; \
	for program in $(RAYLIB_REQUIRED_PROGRAMS); do \
		command -v "$$program" >/dev/null 2>&1 || missing="$$missing $$program"; \
	done; \
	if test -n "$$missing"; then \
		printf 'missing system packages needed to build raylib:%s\n' "$$missing" >&2; \
		printf 'On Arch:   pacman -S $(RAYLIB_ARCH_PACKAGES)\n' >&2; \
		printf 'On Debian: apt install $(RAYLIB_DEBIAN_PACKAGES)\n' >&2; \
		printf 'Run "make prerequisites" for the full list, including dev tools.\n' >&2; \
		exit 1; \
	fi

# The single place a new contributor is told what to install. prints what this
# machine is missing rather than only what is needed, because "which of these do
# I already have" is the actual question when a build fails.
prerequisites:
	@printf 'Build dependencies (needed by every target):\n'
	@printf '  Arch:   pacman -S $(RAYLIB_ARCH_PACKAGES)\n'
	@printf '  Debian: apt install $(RAYLIB_DEBIAN_PACKAGES)\n'
	@printf '\nDevelopment tools (needed by some targets only):\n'
	@printf '  node >= 20    check-docs, check-docs-truth, docs-probe, docs-record\n'
	@printf '  uv            test-animal-scenarios-*, two e2e post-processing scripts\n'
	@printf '                (uv run --offline also needs a writable UV_CACHE_DIR)\n'
	@printf '  gcovr         test-coverage\n'
	@printf '  inotify-tools docs-watch\n'
	@printf '\nAlso required: make, a C compiler, and a Wayland session for e2e targets\n'
	@printf '(WAYLAND_DISPLAY must name a socket under XDG_RUNTIME_DIR).\n'
	@printf '\nCurrently missing on this machine:\n'
	@set -eu; \
	absent=''; \
	for package in $(RAYLIB_PKG_CONFIG_DEPS); do \
		$(PKG_CONFIG) --exists "$$package" || absent="$$absent $$package"; \
	done; \
	for program in $(RAYLIB_REQUIRED_PROGRAMS) $(DEV_TOOL_DEPS); do \
		command -v "$$program" >/dev/null 2>&1 || absent="$$absent $$program"; \
	done; \
	if test -n "$$absent"; then printf '  missing:%s\n' "$$absent"; \
	else printf '  nothing\n'; fi

# The tarball is the only external input in this build, so it gets verified
# before anything unpacks it. A tag is not a pin -- it can be moved upstream --
# and a tarball placed by hand is not evidence either, so both paths go through
# the same check.
#
# This is a stamp rather than a line inside the download recipe so that a
# hand-placed archive is checked too, and so the check re-runs when
# RAYLIB_SHA256 changes (BUILD_CONFIG fingerprints Makefile and mk/raylib.mk).
#
# `make RAYLIB_SHA256= ...` skips the check for one deliberate local build and
# prints that it did. The message on a mismatch gives the measured value, so
# re-pinning is possible without guessing which side is wrong.
RAYLIB_TARBALL_STAMP := $(RAYLIB_SRC_DIR)/.tarball-verified

$(RAYLIB_TARBALL_STAMP): $(RAYLIB_TARBALL) $(BUILD_CONFIG)
	@if test -z '$(RAYLIB_SHA256)'; then \
		printf 'raylib: RAYLIB_SHA256 is empty, tarball checksum NOT verified\n' >&2; \
	else \
		if actual=$$(sha256sum '$(RAYLIB_TARBALL)' | cut -d' ' -f1) && \
			test "$$actual" = '$(RAYLIB_SHA256)'; then :; else \
			printf 'raylib: checksum mismatch for %s\n' '$(RAYLIB_TARBALL)' >&2; \
			printf 'raylib:   expected %s\n' '$(RAYLIB_SHA256)' >&2; \
			printf 'raylib:   measured %s\n' "$${actual:-<unreadable>}" >&2; \
			printf 'raylib: the tag may have been repacked upstream. Check the archive\n' >&2; \
			printf 'raylib: yourself, then update RAYLIB_SHA256 in the Makefile. To build\n' >&2; \
			printf 'raylib: once without the check: make RAYLIB_SHA256= ...\n' >&2; \
			exit 1; \
		fi; \
	fi
	@mkdir -p $(dir $@)
	@printf '%s\n' '$(RAYLIB_SHA256)' > $@

$(RAYLIB_TARBALL): | $(BUILD_ROOT_MARKER)
	@mkdir -p $(dir $@)
	@if test -f '$@'; then :; else \
		printf 'raylib: fetching %s\n' '$(RAYLIB_VERSION)'; \
		curl -fsSL --retry 3 --connect-timeout 30 -o '$@.tmp' '$(RAYLIB_URL)' || { \
			rm -f '$@.tmp'; \
			printf 'raylib: could not download %s\n' '$(RAYLIB_URL)' >&2; \
			printf 'raylib: place the tarball at %s and re-run to build offline.\n' '$@' >&2; \
			exit 1; \
		}; \
		mv -f '$@.tmp' '$@'; \
	fi

# The unpack step tests for a file the archive provides, not for the directory.
# Tarball verification is a stamp living inside RAYLIB_SRC_DIR, so the directory
# exists before anything is unpacked, and `test -d` would skip the extraction and
# hand make a -C path with nothing in it.
$(RAYLIB_LIB): $(RAYLIB_TARBALL_STAMP)
	@if test -f '$@'; then :; else \
		if test -f '$(RAYLIB_SRC_DIR)/src/raylib.h'; then :; else \
			printf 'raylib: unpacking into %s\n' '$(RAYLIB_SRC_DIR)'; \
			mkdir -p $(dir $(RAYLIB_SRC_DIR)); \
			tar -xzf '$(RAYLIB_TARBALL)' -C $(dir $(RAYLIB_SRC_DIR)); \
		fi; \
		printf 'raylib: compiling %s with X11 and Wayland backends\n' '$(RAYLIB_VERSION)'; \
		env -u MAKEFLAGS -u MFLAGS -u MAKELEVEL \
			$(MAKE) --no-print-directory -C '$(RAYLIB_SRC_DIR)/src' \
			PLATFORM=PLATFORM_DESKTOP \
			GLFW_LINUX_ENABLE_X11=TRUE \
			GLFW_LINUX_ENABLE_WAYLAND=TRUE \
			RAYLIB_LIBTYPE=STATIC \
			RAYLIB_BUILD_MODE=RELEASE \
			raylib; \
	fi

$(RAYLIB_HEADER_STAMP): $(RAYLIB_LIB)
	@mkdir -p $(dir $@)
	@: > $@
