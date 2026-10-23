CC ?= cc
AR ?= ar
CPPFLAGS ?= -Isrc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config

# One raylib version, everywhere: the system package, the Wayland rebuild and
# CI all use this value. Two different raylib majors in one tree produced real
# drift (skinning layout, DrawCircleGradient signature, rlGetProcAddress), so
# the version is pinned rather than detected and adapted to.
RAYLIB_VERSION_REQUIRED := 6.0
# raylib tags are plain `6.0`, `6.2` — never padded to three components.
WAYLAND_RAYLIB_VERSION ?= $(RAYLIB_VERSION_REQUIRED)

RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)
RAYLIB_VERSION_FOUND := $(shell $(PKG_CONFIG) --modversion raylib 2>/dev/null)

# Recursive assignment: BUILD_ROOT is declared further down and this must not
# capture an empty value at parse time.
WAYLAND_RAYLIB_DIR = $(BUILD_ROOT)/wayland-raylib/raylib-$(WAYLAND_RAYLIB_VERSION)

.DEFAULT_GOAL := all

BUILD_ROOT ?= build
BUILD_VARIANT ?= normal
BUILD_DIR := $(BUILD_ROOT)/$(BUILD_VARIANT)
BUILD_ROOT_MARKER := $(BUILD_ROOT)/.voxelcraft-build-root
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TARGET := $(BUILD_DIR)/voxelcraft
BUILD_CONFIG := $(BUILD_DIR)/.build-config
BUILD_CONFIG_INPUTS := Makefile mk/modules.mk mk/components.mk mk/tests.mk mk/plugins.mk mk/wayland.mk plugins/manifest.mk

include mk/plugins.mk

include mk/modules.mk
include mk/tests.mk

OBJ_DIR := $(BUILD_DIR)/obj
OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(MODULE_SRC))
DEP := $(OBJ:.o=.d)
MODULE_BUILD_DIR := $(BUILD_DIR)/modules
include mk/components.mk
# Defines WAYLAND_RAYLIB_DIR-dependent targets and `make wayland`. Included
# after BUILD_ROOT/BUILD_DIR so its recursive variables resolve correctly.
include mk/wayland.mk
PUBLIC_HEADERS := $(filter-out %_internal.h,$(sort $(wildcard src/*/*.h)))

TEST_HEADERS := $(sort $(wildcard src/*/*.h) $(wildcard tests/*.h))
TEST_TIMEOUT_SECONDS ?= 120
# Seasonal entity coverage now explicitly advances the full regional workset.
TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS ?= 900
SANITIZER_LEAKS ?= 1
SANITIZE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer
COVERAGE_CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g -pthread --coverage
CI_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -pthread

.PHONY: all voxelcraft run modules components component-core component-world component-space component-ecology component-gameplay component-presentation component-app plugins test test-headers test-modules test-plugins test-architecture test-simulation-replay test-ci test-sanitize test-coverage test-e2e test-debug-graphics-e2e test-polar-e2e test-debug-stdin-protocol test-debug-cli test-long-run benchmark-chunks release-linux release-check clean raylib-version-check FORCE

# A second raylib under /usr/local (hand-built, owned by no package) silently
# shadows the distro one and links a different major version into the game.
# That produced builds that compiled here and failed elsewhere, so fail loudly
# and name the culprit instead of adapting to whatever pkg-config returns.
#
# A version string alone is not enough: a hand-built raylib under /usr/local
# shadows the package-manager one through gcc's default include path, and gcc
# silently ignores `-I/usr/include` because it is a system directory, so
# pkg-config can report the right version while the compiler reads different
# headers. The probe therefore compiles against the headers the build will
# actually use and checks the field layout the sources depend on.
raylib-version-check:
	@printf '%s\n' \
		'#include "raylib.h"' \
		'#if !defined(RAYLIB_VERSION_MAJOR) || RAYLIB_VERSION_MAJOR != 6' \
		'#error RAYLIB_VERSION_MAJOR must be 6' \
		'#endif' \
		'/* The sources use the 6.x skinning layout. */' \
		'int main(void) { Mesh m = { 0 }; Model d = { 0 };' \
		'  (void)m.boneIndices; (void)d.boneMatrices;' \
		'  (void)d.skeleton.boneCount; return 0; }' \
		| $(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) -Werror -x c -fsyntax-only - >/dev/null 2>&1 \
		|| { \
			printf 'raylib %s is required, but the headers this build would use are not it.\n' '$(RAYLIB_VERSION_REQUIRED)' >&2; \
			printf 'pkg-config reports %s from %s\n' '$(RAYLIB_VERSION_FOUND)' "$$($(PKG_CONFIG) --variable=pcfiledir raylib 2>/dev/null || echo unknown)" >&2; \
			printf 'Headers actually resolved: %s\n' "$$(printf '#include \"raylib.h\"\nRAYLIB_VERSION\n' | $(CC) $(RAYLIB_CFLAGS) -E - 2>/dev/null | tail -1)" >&2; \
			printf 'If those disagree, a hand-built raylib under /usr/local is shadowing the package one; remove /usr/local/include/raylib.h and /usr/local/lib/libraylib.so* or install raylib %s there instead.\n' '$(RAYLIB_VERSION_REQUIRED)' >&2; \
			exit 1; \
		}

all: raylib-version-check $(TARGET)

voxelcraft: raylib-version-check $(TARGET)

$(BUILD_ROOT_MARKER):
	mkdir -p $(BUILD_ROOT)
	@: > $@

$(BUILD_DIR) $(TEST_BUILD_DIR) $(MODULE_BUILD_DIR) $(PLUGIN_BUILD_DIR): | $(BUILD_ROOT_MARKER)
	mkdir -p $@

FORCE:

# Re-run this recipe to observe command-line overrides without making every
# artifact stale when the effective build configuration has not changed.
$(BUILD_CONFIG): FORCE $(BUILD_CONFIG_INPUTS) | $(BUILD_DIR)
	@{ \
		printf '%s\n' \
			'AR=$(AR)' \
			'CC=$(CC)' \
			'CPPFLAGS=$(CPPFLAGS)' \
			'CFLAGS=$(CFLAGS)' \
			'LDFLAGS=$(LDFLAGS)' \
			'LDLIBS=$(LDLIBS)' \
			'PKG_CONFIG=$(PKG_CONFIG)' \
			'RAYLIB_CFLAGS=$(RAYLIB_CFLAGS)' \
			'RAYLIB_LIBS=$(RAYLIB_LIBS)'; \
		cksum $(BUILD_CONFIG_INPUTS); \
	} > $@.tmp
	@if test -r "$@" && cmp -s "$@.tmp" "$@"; then \
		rm -f "$@.tmp"; \
	else \
		mv -f "$@.tmp" "$@"; \
	fi

$(TARGET): $(MODULE_ARCHIVES) $(BUILD_CONFIG) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ -Wl,--start-group $(MODULE_ARCHIVES) -Wl,--end-group $(RAYLIB_LIBS) -lm -pthread $(PLUGIN_HOST_LIBS) $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c $(BUILD_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

.SECONDEXPANSION:
$(TEST_BUILD_DIR)/%: tests/%.c $$(TEST_SOURCES_$$*) $(TEST_HEADERS) $(BUILD_CONFIG) mk/tests.mk | $(TEST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(TEST_CPPFLAGS_$*) $(CFLAGS) $(TEST_CFLAGS_$*) $(TEST_RAYLIB_CFLAGS_$*) $(LDFLAGS) $(TEST_LDFLAGS_$*) -o $@ $< $(TEST_SOURCES_$*) $(TEST_RAYLIB_LIBS_$*) $(TEST_LDLIBS_$*) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

plugins: $(PLUGIN_TARGETS)

# Build each built-in module archive independently.  Together with the
# optional shared-library `plugins` target this gives contributors explicit
# parallel seams (`make -j modules plugins`).
modules: $(MODULE_ARCHIVES)

# Build all ownership domains without touching unrelated component archives.
components: $(MODULE_ARCHIVES)

test: test-headers test-modules test-plugins $(TEST_TARGETS) test-debug-cli test-debug-stdin-protocol
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS=$(TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS) sh scripts/run-tests.sh $(TEST_TARGETS)
	@sh tests/test_architecture.sh

test-headers:
	@set -eu; \
	for header in $(PUBLIC_HEADERS); do \
		include=$${header#src/}; \
		printf '#include "%s"\n' "$${include}" | \
			$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) \
			-Werror -x c -fsyntax-only -; \
	done; \
	printf 'public headers passed: %s\n' '$(words $(PUBLIC_HEADERS))'

test-modules: $(MODULE_ARCHIVES)
	@printf 'module archives passed: %s\n' '$(words $(MODULE_ARCHIVES))'

test-plugins: $(PLUGIN_TARGETS) $(PLUGIN_TEST_TARGET)
	@printf 'plugin targets passed: %s\n' '$(words $(PLUGIN_TARGETS))'
	@if test -n "$(PLUGIN_TARGETS)"; then \
		$(PLUGIN_TEST_TARGET) "$(PLUGIN_BUILD_DIR)"; \
	fi

test-architecture:
	@sh tests/test_architecture.sh

test-simulation-replay: $(SIMULATION_REPLAY_TEST_TARGET) $(SIMULATION_INTEGRATION_TEST_TARGET)
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) sh scripts/run-tests.sh $^

test-ci:
	$(MAKE) BUILD_VARIANT=ci CFLAGS='$(CI_CFLAGS)' test
	$(MAKE) test-sanitize
	@test -z "$$(git status --porcelain --untracked-files=normal)" || { git status --short; exit 1; }

test-sanitize:
	ASAN_OPTIONS='detect_leaks=$(SANITIZER_LEAKS):halt_on_error=1:strict_string_checks=1' \
	UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1' \
	$(MAKE) BUILD_VARIANT=sanitize CFLAGS='$(SANITIZE_CFLAGS)' test

test-coverage:
	$(MAKE) BUILD_VARIANT=coverage CFLAGS='$(COVERAGE_CFLAGS)' test
	@command -v gcovr >/dev/null 2>&1 || { echo 'gcovr is required for test-coverage'; exit 1; }
	gcovr --root . --filter '^src/' --exclude 'tests/' --xml-pretty --xml build/coverage/coverage.xml --html-details build/coverage/coverage.html

test-e2e: $(TARGET) $(BUILD_DIR)/diagnostics-plugins/diagnostics.so
	bash tests/test_game_e2e.sh $(TARGET)
	bash tests/test_debug_graphics_e2e.sh $(TARGET)
	bash tests/test_chunk_diagnostics_e2e.sh $(TARGET) $(BUILD_DIR)/diagnostics-plugins

test-debug-graphics-e2e: $(TARGET)
	bash tests/test_debug_graphics_e2e.sh $(TARGET)

.PHONY: test-cga-house-e2e test-cga-previews-e2e test-cloud-altitude-e2e
test-cga-house-e2e: $(TARGET)
	bash tests/test_cga_house_e2e.sh $(TARGET)

test-cga-previews-e2e: $(TARGET)
	bash tests/test_cga_previews_e2e.sh $(TARGET)

.PHONY: test-volcano-e2e
test-volcano-e2e: $(TARGET)
	bash tests/test_volcano_e2e.sh $(TARGET)

test-cloud-altitude-e2e: $(TARGET)
	bash tests/test_cloud_altitude_e2e.sh $(TARGET)

.PHONY: test-chunk-diagnostics-e2e
$(BUILD_DIR)/diagnostics-plugins/diagnostics.so: tests/diagnostics_plugin.c src/core/plugin.h $(BUILD_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -fPIC -shared -o $@ $<

test-chunk-diagnostics-e2e: $(TARGET) $(BUILD_DIR)/diagnostics-plugins/diagnostics.so
	bash tests/test_chunk_diagnostics_e2e.sh $(TARGET) $(BUILD_DIR)/diagnostics-plugins

.PHONY: test-animal-pose-e2e
test-animal-pose-e2e: $(TARGET)
	bash tests/test_animal_pose_e2e.sh $(TARGET)

.PHONY: animal-scenarios test-animal-scenarios-e2e test-animal-scenarios-script
animal-scenarios: $(TARGET)
	uv run --no-project --offline python scripts/animal_scenarios.py --no-build --binary $(TARGET)

test-animal-scenarios-script:
	uv run --no-project --offline python -m unittest discover -s tests -p test_animal_scenarios.py

test-animal-scenarios-e2e: $(TARGET) test-animal-scenarios-script
	uv run --no-project --offline python scripts/animal_scenarios.py --no-build --binary $(TARGET) --check

test-polar-e2e: $(TARGET)
	bash tests/test_polar_e2e.sh $(TARGET)

test-cube-save-reload: $(CUBE_SAVE_RELOAD_TEST_TARGET)
	@bash tests/test_cube_save_reload.sh $(CUBE_SAVE_RELOAD_TEST_TARGET)

test-debug-stdin-protocol:
	@bash tests/test_debug_stdin_protocol.sh

test-debug-cli: $(TARGET)
	@bash tests/test_debug_cli.sh $(TARGET)

# The weather runtime test includes a deterministic multi-thousand-frame
# simulation loop in addition to its focused boundary cases.
test-long-run: $(WEATHER_RUNTIME_TEST_TARGET)
	./$(WEATHER_RUNTIME_TEST_TARGET)

benchmark-chunks: $(CHUNK_BENCHMARK_TARGET)
	./$(CHUNK_BENCHMARK_TARGET)

release-linux: all plugins
	@set -eu; version=$$(git describe --tags --always --dirty 2>/dev/null || echo dev); out="dist/voxelcraft-linux-$${version}"; rm -rf "$${out}" "$${out}.tar.gz"; mkdir -p "$${out}"; cp $(TARGET) README.md "$${out}/"; cp -R assets "$${out}/"; if test -n "$(PLUGIN_TARGETS)"; then mkdir -p "$${out}/plugins"; cp $(PLUGIN_TARGETS) "$${out}/plugins/"; fi; printf '%s\n' "Voxelcraft Linux build $${version}" > "$${out}/BUILD.txt"; tar -czf "$${out}.tar.gz" -C dist "$$(basename "$${out}")"; sha256sum "$${out}.tar.gz" > "$${out}.tar.gz.sha256"; printf 'release=%s\narchive=%s.tar.gz\n' "$${version}" "$${out}";

release-check:
	$(MAKE) clean
	$(MAKE) test
	$(MAKE) test-sanitize
	$(MAKE) clean
	$(MAKE) release-linux
	$(MAKE) $(SAVE_IO_TEST_TARGET)
	./$(SAVE_IO_TEST_TARGET)
	@set -eu; archive=$$(find dist -maxdepth 1 -name 'voxelcraft-linux-*.tar.gz' | sort | tail -n 1); test -n "$${archive}"; tar -tzf "$${archive}" | grep -q '/voxelcraft$$'; tar -tzf "$${archive}" | grep -q '/README.md$$'; tar -tzf "$${archive}" | grep -q '/assets/fonts/FSEX302-alt.ttf$$'; tar -tzf "$${archive}" | grep -q '/assets/LICENSES.md$$'; tar -tzf "$${archive}" | grep -q '/assets/audio/rain.ogg$$'; tar -tzf "$${archive}" | grep -q '/assets/audio/water.ogg$$'; tar -tzf "$${archive}" | grep -q '/assets/audio/cave.ogg$$'; sha256sum -c "$${archive}.sha256"; printf '%s\n' 'release check passed'

clean:
	@sh scripts/clean-build.sh '$(BUILD_ROOT)'

.PHONY: test-landform-quality test-landform-quality-e2e
test-landform-quality: $(PLANET_LANDFORM_QUALITY_TEST_TARGET)
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) sh scripts/run-tests.sh $^

test-landform-quality-e2e: $(TARGET)
	bash tests/test_landform_quality_e2e.sh $(TARGET)

.PHONY: test-surface-cube
test-surface-cube: $(SURFACE_CUBE_TEST_TARGET) $(SURFACE_TOPOLOGY_TEST_TARGET) $(SURFACE_POSE_TEST_TARGET) $(TERRAIN_CUBE_TEST_TARGET) $(SURFACE_GEOMETRY_TEST_TARGET) $(CUBE_STORAGE_TEST_TARGET) $(SURFACE_COVERAGE_TEST_TARGET) $(CUBE_GENERATION_TEST_TARGET)
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) sh scripts/run-tests.sh $^

.PHONY: test-surface-topology-e2e
test-surface-topology-e2e: $(TARGET)
	bash tests/test_surface_topology_e2e.sh $(TARGET)
