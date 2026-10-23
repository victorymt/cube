# Wayland background for the graphical build.
#
# Why this exists: the system raylib from pkg-config is frequently compiled for
# X11 only. On a Wayland-only session (Hyprland without an XWayland display
# reachable through DISPLAY) the game exits with `X11: Failed to open display`
# and no screenshot route can run. That is a build-backend problem, not a
# missing capability of the machine, so the fix belongs in the build rather
# than in a local wrapper script.
#
# `make wayland` rebuilds raylib with the GLFW Wayland backend and links that
# copy into a separate variant directory. The normal `make` output is left
# untouched, so the two builds cannot silently overwrite each other.

WAYLAND_VARIANT := wayland
WAYLAND_BUILD_DIR := $(BUILD_ROOT)/$(WAYLAND_VARIANT)
WAYLAND_TARGET := $(WAYLAND_BUILD_DIR)/voxelcraft

# Generated from the raylib checkout. Never committed: the source archive is
# fetched on demand and the object files are build output.
WAYLAND_RAYLIB_SRC := $(WAYLAND_RAYLIB_DIR)/src
WAYLAND_RAYLIB_LIB := $(WAYLAND_RAYLIB_SRC)/libraylib.a
WAYLAND_RAYLIB_TARBALL := $(BUILD_ROOT)/wayland-raylib/raylib-$(WAYLAND_RAYLIB_VERSION).tar.gz
WAYLAND_RAYLIB_URL := https://github.com/raysan5/raylib/archive/refs/tags/$(WAYLAND_RAYLIB_VERSION).tar.gz

# raylib always needs a graphics stack; a Wayland raylib still links libGL and
# also needs the X11 libraries for GLFW's remaining X11 paths.
WAYLAND_PKG_CONFIG_PACKAGES := wayland-client wayland-cursor wayland-egl xkbcommon

.PHONY: wayland wayland-raylib wayland-check

wayland: wayland-check $(WAYLAND_TARGET)
	@printf 'wayland build: %s\n' '$(WAYLAND_TARGET)'

# Fail with an actionable message instead of a wall of linker errors.
wayland-check:
	@set -eu; \
	missing=''; \
	for package in $(WAYLAND_PKG_CONFIG_PACKAGES); do \
		$(PKG_CONFIG) --exists "$$package" || missing="$$missing $$package"; \
	done; \
	command -v wayland-scanner >/dev/null 2>&1 || missing="$$missing wayland-scanner"; \
	if test -n "$$missing"; then \
		printf 'wayland build: missing development packages:%s\n' "$$missing" >&2; \
		printf 'wayland build: install the Wayland/EGL/xkbcommon development headers and wayland-scanner, or use plain `make` for the X11 build.\n' >&2; \
		exit 2; \
	fi

$(WAYLAND_RAYLIB_TARBALL): | $(BUILD_ROOT_MARKER)
	@mkdir -p $(dir $@)
	@if test -f "$@"; then :; else \
		printf 'wayland build: fetching raylib $(WAYLAND_RAYLIB_VERSION)\n'; \
		curl -fsSL -o "$@.tmp" '$(WAYLAND_RAYLIB_URL)' || { \
			rm -f "$@.tmp"; \
			printf 'wayland build: could not download %s\n' '$(WAYLAND_RAYLIB_URL)' >&2; \
			printf 'wayland build: place the tarball at %s and re-run.\n' '$@' >&2; \
			exit 2; \
		}; \
		mv -f "$@.tmp" "$@"; \
	fi

# raylib's Makefile generates the Wayland protocol headers it needs, so the
# build must run with the checkout as the working directory. Its target is
# `raylib` (not the output filename): the recipe ar's the objects into
# $(RAYLIB_RELEASE_PATH)/libraylib.a, which defaults to the src directory.
$(WAYLAND_RAYLIB_LIB): $(WAYLAND_RAYLIB_TARBALL)
	@mkdir -p $(dir $(WAYLAND_RAYLIB_DIR))
	@if test -f '$(WAYLAND_RAYLIB_LIB)'; then :; else \
		if test -d '$(WAYLAND_RAYLIB_DIR)'; then :; else \
			printf 'wayland build: unpacking raylib into %s\n' '$(WAYLAND_RAYLIB_DIR)'; \
			tar -xzf '$<' -C '$(dir $(WAYLAND_RAYLIB_DIR))'; \
		fi; \
		printf 'wayland build: compiling raylib with the GLFW Wayland backend\n'; \
		$(MAKE) --no-print-directory -C '$(WAYLAND_RAYLIB_SRC)' \
			PLATFORM=PLATFORM_DESKTOP \
			GLFW_LINUX_ENABLE_WAYLAND=TRUE \
			GLFW_LINUX_ENABLE_X11=TRUE \
			RAYLIB_LIBTYPE=STATIC \
			RAYLIB_BUILD_MODE=RELEASE \
			raylib; \
	fi

# The variant build shares nothing with `build/normal` except source: objects,
# archives and the .build-config stamp all live under the variant directory, so
# switching backends recompiles cleanly instead of keeping stale link results.
$(WAYLAND_TARGET): wayland-check $(WAYLAND_RAYLIB_LIB)
	@$(MAKE) --no-print-directory -C '$(CURDIR)' \
		BUILD_VARIANT='$(WAYLAND_VARIANT)' \
		RAYLIB_CFLAGS='-I$(WAYLAND_RAYLIB_SRC)' \
		RAYLIB_LIBS='$(WAYLAND_RAYLIB_LIB) $$(shell $(PKG_CONFIG) --libs $(WAYLAND_PKG_CONFIG_PACKAGES)) $$(shell $(PKG_CONFIG) --libs x11) -lGL -lm -lpthread -ldl -lrt' \
		$(WAYLAND_TARGET)
