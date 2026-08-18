CC ?= cc
AR ?= ar
CPPFLAGS ?= -Isrc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config
RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)

BUILD_ROOT ?= build
BUILD_VARIANT ?= normal
BUILD_DIR := $(BUILD_ROOT)/$(BUILD_VARIANT)
BUILD_ROOT_MARKER := $(BUILD_ROOT)/.voxelcraft-build-root
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TARGET := $(BUILD_DIR)/voxelcraft
BUILD_CONFIG := $(BUILD_DIR)/.build-config
BUILD_CONFIG_INPUTS := Makefile mk/modules.mk mk/tests.mk

include mk/modules.mk
include mk/tests.mk

OBJ_DIR := $(BUILD_DIR)/obj
OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(MODULE_SRC))
DEP := $(OBJ:.o=.d)
MODULE_BUILD_DIR := $(BUILD_DIR)/modules
CORE_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(CORE_SRC))
WORLD_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(WORLD_SRC))
SPACE_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SPACE_SRC))
ECOLOGY_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(ECOLOGY_SRC))
GAMEPLAY_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(GAMEPLAY_SRC))
PRESENTATION_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(PRESENTATION_SRC))
APP_OBJ := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(APP_SRC))
MODULE_ARCHIVES := \
	$(MODULE_BUILD_DIR)/core.a \
	$(MODULE_BUILD_DIR)/world.a \
	$(MODULE_BUILD_DIR)/space.a \
	$(MODULE_BUILD_DIR)/ecology.a \
	$(MODULE_BUILD_DIR)/gameplay.a \
	$(MODULE_BUILD_DIR)/presentation.a \
	$(MODULE_BUILD_DIR)/app.a
PUBLIC_HEADERS := $(filter-out %_internal.h,$(sort $(wildcard src/*/*.h)))

TEST_HEADERS := $(sort $(wildcard src/*/*.h) $(wildcard tests/*.h))
TEST_TIMEOUT_SECONDS ?= 120
SANITIZER_LEAKS ?= 1
SANITIZE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer
COVERAGE_CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g -pthread --coverage
CI_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -pthread

.PHONY: all voxelcraft run test test-headers test-modules test-architecture test-ci test-sanitize test-coverage test-e2e test-long-run benchmark-chunks release-linux release-check clean FORCE

all: $(TARGET)

voxelcraft: $(TARGET)

$(BUILD_ROOT_MARKER):
	mkdir -p $(BUILD_ROOT)
	@: > $@

$(BUILD_DIR) $(TEST_BUILD_DIR) $(MODULE_BUILD_DIR): | $(BUILD_ROOT_MARKER)
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

$(TARGET): $(OBJ) $(BUILD_CONFIG) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ) $(RAYLIB_LIBS) -lm -pthread $(LDLIBS)

$(OBJ_DIR)/%.o: src/%.c $(BUILD_CONFIG)
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

$(MODULE_BUILD_DIR)/core.a: $(CORE_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/world.a: $(WORLD_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/space.a: $(SPACE_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/ecology.a: $(ECOLOGY_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/gameplay.a: $(GAMEPLAY_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/presentation.a: $(PRESENTATION_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

$(MODULE_BUILD_DIR)/app.a: $(APP_OBJ) $(BUILD_CONFIG) | $(MODULE_BUILD_DIR)
	@rm -f $@
	$(AR) rcs $@ $(filter %.o,$^)

.SECONDEXPANSION:
$(TEST_BUILD_DIR)/%: tests/%.c $$(TEST_SOURCES_$$*) $(TEST_HEADERS) $(BUILD_CONFIG) mk/tests.mk | $(TEST_BUILD_DIR)
	$(CC) $(CPPFLAGS) $(TEST_CPPFLAGS_$*) $(CFLAGS) $(TEST_CFLAGS_$*) $(TEST_RAYLIB_CFLAGS_$*) $(LDFLAGS) $(TEST_LDFLAGS_$*) -o $@ $< $(TEST_SOURCES_$*) $(TEST_RAYLIB_LIBS_$*) $(TEST_LDLIBS_$*) $(LDLIBS)

run: $(TARGET)
	./$(TARGET)

test: test-headers test-modules $(TEST_TARGETS)
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) sh scripts/run-tests.sh $(TEST_TARGETS)
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

test-architecture:
	@sh tests/test_architecture.sh

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

test-e2e: $(TARGET)
	bash tests/test_game_e2e.sh $(TARGET)

# The weather runtime test includes a deterministic multi-thousand-frame
# simulation loop in addition to its focused boundary cases.
test-long-run: $(WEATHER_RUNTIME_TEST_TARGET)
	./$(WEATHER_RUNTIME_TEST_TARGET)

benchmark-chunks: $(CHUNK_BENCHMARK_TARGET)
	./$(CHUNK_BENCHMARK_TARGET)

release-linux: all
	@set -eu; version=$$(git describe --tags --always --dirty 2>/dev/null || echo dev); out="dist/voxelcraft-linux-$${version}"; rm -rf "$${out}" "$${out}.tar.gz"; mkdir -p "$${out}"; cp $(TARGET) README.md "$${out}/"; cp -R assets "$${out}/"; printf '%s\n' "Voxelcraft Linux build $${version}" > "$${out}/BUILD.txt"; tar -czf "$${out}.tar.gz" -C dist "$$(basename "$${out}")"; sha256sum "$${out}.tar.gz" > "$${out}.tar.gz.sha256"; printf 'release=%s\narchive=%s.tar.gz\n' "$${version}" "$${out}";

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
