CC ?= cc
AR ?= ar
CPPFLAGS ?= -Isrc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config

# raylib is vendored, not detected. The project builds its own pinned copy and
# links that, so a raylib installed elsewhere on the machine -- a different
# major version, a hand-built copy under /usr/local, or none at all -- cannot
# change what the game compiles and links against. See mk/raylib.mk.
RAYLIB_VERSION := 6.0
# raylib's tags are plain `6.0`, `6.2`, never padded to three components.

.PHONY: raylib

.DEFAULT_GOAL := all

BUILD_ROOT ?= build
BUILD_VARIANT ?= normal
BUILD_DIR := $(BUILD_ROOT)/$(BUILD_VARIANT)
BUILD_ROOT_MARKER := $(BUILD_ROOT)/.voxelcraft-build-root
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TARGET := $(BUILD_DIR)/voxelcraft
BUILD_CONFIG := $(BUILD_DIR)/.build-config
BUILD_CONFIG_INPUTS := Makefile mk/modules.mk mk/components.mk mk/tests.mk mk/plugins.mk mk/raylib.mk plugins/manifest.mk

include mk/plugins.mk

# Defines RAYLIB_CFLAGS/RAYLIB_LIBS and the vendored raylib build. It must be
# included before mk/tests.mk, which expands both variables into per-test link
# lines at include time -- expanding them earlier would capture empty values and
# leave tests linking no raylib at all.
include mk/raylib.mk

include mk/modules.mk
include mk/tests.mk

OBJ_DIR := $(BUILD_DIR)/obj
OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(MODULE_SRC))
DEP := $(OBJ:.o=.d)
MODULE_BUILD_DIR := $(BUILD_DIR)/modules
include mk/components.mk
PUBLIC_HEADERS := $(filter-out %_internal.h,$(sort $(wildcard src/*/*.h)))

TEST_HEADERS := $(sort $(wildcard src/*/*.h) $(wildcard tests/*.h))
TEST_TIMEOUT_SECONDS ?= 120
# Seasonal entity coverage now explicitly advances the full regional workset.
TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS ?= 900
SANITIZER_LEAKS ?= 1
SANITIZE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer
COVERAGE_CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g -pthread --coverage
CI_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -pthread

.PHONY: all voxelcraft run modules components component-core component-world component-space component-ecology component-gameplay component-presentation component-app plugins test test-headers test-modules test-plugins test-werror test-benchmark-invariants test-public-header-seams test-scene-reachability test-architecture test-simulation-replay test-ci test-sanitize test-coverage test-e2e test-debug-graphics-e2e test-polar-e2e test-debug-stdin-protocol test-debug-cli test-atmosphere-e2e test-landform-planets-e2e test-water-section-e2e test-flat-baseline-e2e test-debug-fields-e2e test-long-run benchmark-chunks release-linux release-check clean FORCE

# The vendored raylib is a prerequisite rather than a checked precondition: the
# build either produces it or fails while producing it. There is no state in
# which the game compiles against an unverified raylib.
all: $(RAYLIB_LIB) $(TARGET)

voxelcraft: $(RAYLIB_LIB) $(TARGET)

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

# Object compilation needs raylib's headers, so the vendored library is an
# order-only-ish prerequisite: without it the compiler would silently fall back
# to a raylib header found elsewhere on the machine.
$(OBJ_DIR)/%.o: src/%.c $(BUILD_CONFIG) $(RAYLIB_HEADER_STAMP)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

.SECONDEXPANSION:
$(TEST_BUILD_DIR)/%: tests/%.c $$(TEST_SOURCES_$$*) $(TEST_HEADERS) $(BUILD_CONFIG) mk/tests.mk $(RAYLIB_HEADER_STAMP) | $(TEST_BUILD_DIR)
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

test: test-headers test-modules test-plugins $(TEST_TARGETS) test-debug-cli test-debug-stdin-protocol test-werror test-benchmark-invariants test-public-header-seams test-scene-reachability
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS=$(TEST_ENTITY_ECOLOGY_TIMEOUT_SECONDS) sh scripts/run-tests.sh $(TEST_TARGETS)
	@sh tests/test_architecture.sh

# The default CFLAGS carry no -Werror, but CI passes its own flags with it, so a
# warning in production code passed locally and failed on CI. Sweeping src here
# makes the local run agree with CI.
test-werror:
	@CC='$(CC)' sh tests/test_werror.sh

# The four numbers documented as invariants must be measured, not printed. This
# runs the benchmark and checks them; it also rejects a return to a literal sync
# counter, which is how the invariant became a constant in the first place.
test-benchmark-invariants: $(CHUNK_BENCHMARK_TARGET)
	@sh tests/test_benchmark_invariants.sh

# A `#ifdef *_TESTING` block in a public header widens the contract. Fifteen
# exist; this test pins them so a sixteenth is a decision, not a side effect.
test-public-header-seams:
	@sh tests/test_public_header_seams.sh

# A `.dsl` scene is not a test until a driver runs it. This fails when a scene in
# tests/scenes is executed by nothing.
test-scene-reachability:
	@sh tests/test_scene_reachability.sh

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
	bash tests/test_atmosphere_e2e.sh $(TARGET)
	bash tests/test_landform_planets_e2e.sh $(TARGET)
	bash tests/test_water_section_e2e.sh $(TARGET)
	bash tests/test_flat_baseline_e2e.sh $(TARGET)
	bash tests/test_debug_fields_e2e.sh $(TARGET)

# These scripts existed and passed but nothing invoked them: no make target, no
# CI entry, nobody sourcing them. They are the only coverage of stratified
# atmosphere assertions, planet landform replay across body switches, water
# section geometry, and the flat rotation baseline behind
# docs/performance/flat-rotation-baseline.md, so they run here rather than being
# deleted. They need a Wayland session like the rest of the e2e layer.
.PHONY: test-atmosphere-e2e test-landform-planets-e2e test-water-section-e2e test-flat-baseline-e2e test-debug-fields-e2e
test-atmosphere-e2e: $(TARGET)
	bash tests/test_atmosphere_e2e.sh $(TARGET)

test-landform-planets-e2e: $(TARGET)
	bash tests/test_landform_planets_e2e.sh $(TARGET)

test-water-section-e2e: $(TARGET)
	bash tests/test_water_section_e2e.sh $(TARGET)

test-flat-baseline-e2e: $(TARGET)
	bash tests/test_flat_baseline_e2e.sh $(TARGET)

# Every runtime name docs/debug-dsl.md advertises must resolve. This is the part
# of `make docs-probe` that needs no GPU: it checks names, not values.
test-debug-fields-e2e: $(TARGET)
	bash tests/test_debug_fields_e2e.sh $(TARGET)
	bash tests/test_debug_fields_e2e.sh $(TARGET)

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

.PHONY: check-docs check-docs-truth docs-probe docs-record
check-docs:
	node scripts/check-docs.mjs --root .
	node scripts/check-docs-truth.mjs --root .

check-docs-truth:
	node scripts/check-docs-truth.mjs --root .

# Runtime half of the documentation gate: it starts the real binary and proves
# that the names docs/debug-dsl.md advertises still resolve.
docs-probe: $(TARGET)
	node scripts/docs-probe.mjs --root . --binary $(TARGET)

# Maintenance: re-stamp verifiedAt after running a document's recipes.
docs-record:
	node scripts/check-docs-truth.mjs --root . --record

# Install the repository hooks (pre-commit runs the gate).
.PHONY: hooks
hooks:
	sh scripts/install-hooks.sh

# Watch a working tree and re-run the static gate on each save. For editing
# sessions only; CI has no working tree to watch.
.PHONY: docs-watch
docs-watch:
	sh scripts/docs-watch.sh
