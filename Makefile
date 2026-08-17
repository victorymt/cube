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
TEST_BUILD_DIR := $(BUILD_DIR)/tests
TARGET := $(BUILD_DIR)/voxelcraft
TEST_TARGET := $(TEST_BUILD_DIR)/test_world_environment
PLANET_SURFACE_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_surface
PLANET_MATERIAL_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_material
PLANET_CLIMATE_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_climate
PLANET_OBSERVATION_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_observation
SPACE_PHYSICS_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_physics
SPACE_BARYCENTER_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_barycenter
SPACE_ORBIT_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_orbit
SPACE_REMNANT_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_remnant
SPACE_ILLUMINATION_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_illumination
SPACE_SATELLITE_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_satellite
SPACE_UNITS_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_units
SPACE_COORDINATES_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_coordinates
SURFACE_TOPOLOGY_TEST_TARGET := $(TEST_BUILD_DIR)/test_surface_topology
SURFACE_SAVE_TEST_TARGET := $(TEST_BUILD_DIR)/test_surface_save
SPACE_PROPERTIES_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_properties
SPACE_SYSTEM_TEST_TARGET := $(TEST_BUILD_DIR)/test_space_system
ECOLOGY_SYSTEM_TEST_TARGET := $(TEST_BUILD_DIR)/test_ecology_system
ECOLOGY_PROPERTIES_TEST_TARGET := $(TEST_BUILD_DIR)/test_ecology_properties
STELLAR_TEST_TARGET := $(TEST_BUILD_DIR)/test_stellar
ECOLOGY_MODEL_TEST_TARGET := $(TEST_BUILD_DIR)/test_ecology_model
FAUNA_MOTION_TEST_TARGET := $(TEST_BUILD_DIR)/test_fauna_motion
FAUNA_BEHAVIOR_TEST_TARGET := $(TEST_BUILD_DIR)/test_fauna_behavior
EVOLUTION_TEST_TARGET := $(TEST_BUILD_DIR)/test_evolution
EVOLUTION_CATALOG_TEST_TARGET := $(TEST_BUILD_DIR)/test_evolution_catalog
FLUID_TEST_TARGET := $(TEST_BUILD_DIR)/test_fluid
WEATHER_MODEL_TEST_TARGET := $(TEST_BUILD_DIR)/test_weather_model
WEATHER_RUNTIME_TEST_TARGET := $(TEST_BUILD_DIR)/test_weather_runtime
WEATHER_VISUAL_TEST_TARGET := $(TEST_BUILD_DIR)/test_weather_visual
PLAYER_COLLISION_TEST_TARGET := $(TEST_BUILD_DIR)/test_player_collision
SHIP_STATE_TEST_TARGET := $(TEST_BUILD_DIR)/test_ship_state
SHIP_FLIGHT_CONTROLLER_TEST_TARGET := $(TEST_BUILD_DIR)/test_ship_flight_controller
SHIP_EXHAUST_TEST_TARGET := $(TEST_BUILD_DIR)/test_ship_exhaust
SHIP_LOCATOR_TEST_TARGET := $(TEST_BUILD_DIR)/test_ship_locator
BLOCK_ATLAS_TEST_TARGET := $(TEST_BUILD_DIR)/test_block_atlas
INVENTORY_TEST_TARGET := $(TEST_BUILD_DIR)/test_inventory
ALBUM_TEST_TARGET := $(TEST_BUILD_DIR)/test_album
CHUNK_ATLAS_TEST_TARGET := $(TEST_BUILD_DIR)/test_chunk_atlas
CHUNK_STREAMING_TEST_TARGET := $(TEST_BUILD_DIR)/test_chunk_streaming
TERRAIN_SCALE_TEST_TARGET := $(TEST_BUILD_DIR)/test_terrain_scale
SUBSURFACE_TEST_TARGET := $(TEST_BUILD_DIR)/test_subsurface
CHUNK_BENCHMARK_TARGET := $(TEST_BUILD_DIR)/benchmark_chunks
PERF_TEST_TARGET := $(TEST_BUILD_DIR)/test_perf
RENDER_SORT_TEST_TARGET := $(TEST_BUILD_DIR)/test_render_sort
RENDER_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_render_resources
RENDER_UI_TEST_TARGET := $(TEST_BUILD_DIR)/test_render_ui
WORLD_RENDERER_TEST_TARGET := $(TEST_BUILD_DIR)/test_world_renderer
WORLD_LIGHTING_TEST_TARGET := $(TEST_BUILD_DIR)/test_world_lighting
SAVE_IO_TEST_TARGET := $(TEST_BUILD_DIR)/test_save_io
SAVE_FORMAT_TEST_TARGET := $(TEST_BUILD_DIR)/test_save_format
GAME_SETTINGS_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_settings
SCREENSHOT_TEST_TARGET := $(TEST_BUILD_DIR)/test_screenshot
DEBUG_CONTROL_TEST_TARGET := $(TEST_BUILD_DIR)/test_debug_control
GAME_DEBUG_TRACE_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_debug_trace
GAME_STREAM_AUDIT_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_stream_audit
GAME_EFFECTS_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_effects
GAME_NOTICE_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_notice
ENVIRONMENT_PRESENTATION_TEST_TARGET := $(TEST_BUILD_DIR)/test_environment_presentation
ENVIRONMENT_RUNTIME_TEST_TARGET := $(TEST_BUILD_DIR)/test_environment_runtime
AUDIO_ENVIRONMENT_TEST_TARGET := $(TEST_BUILD_DIR)/test_audio_environment
ENTITY_REPLAY_TEST_TARGET := $(TEST_BUILD_DIR)/test_entity_replay
ENTITY_ECOLOGY_TEST_TARGET := $(TEST_BUILD_DIR)/test_entity_ecology
INTERACTION_RAYCAST_TEST_TARGET := $(TEST_BUILD_DIR)/test_interaction_raycast
PLANET_RENDERER_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_renderer_resources
PLANET_TEXTURE_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_texture_resources
HOMEWORLD_MAP_MODEL_TEST_TARGET := $(TEST_BUILD_DIR)/test_homeworld_map_model
MAP_MARKERS_TEST_TARGET := $(TEST_BUILD_DIR)/test_map_markers

include mk/modules.mk

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

TEST_TARGETS := $(TEST_TARGET) $(PLANET_SURFACE_TEST_TARGET) $(PLANET_MATERIAL_TEST_TARGET) $(PLANET_CLIMATE_TEST_TARGET) $(PLANET_OBSERVATION_TEST_TARGET) $(SPACE_PHYSICS_TEST_TARGET) $(SPACE_BARYCENTER_TEST_TARGET) $(SPACE_ORBIT_TEST_TARGET) $(SPACE_REMNANT_TEST_TARGET) $(SPACE_ILLUMINATION_TEST_TARGET) $(SPACE_SATELLITE_TEST_TARGET) $(SPACE_UNITS_TEST_TARGET) $(SPACE_PROPERTIES_TEST_TARGET) $(SPACE_SYSTEM_TEST_TARGET) $(ECOLOGY_SYSTEM_TEST_TARGET) $(ECOLOGY_PROPERTIES_TEST_TARGET) $(STELLAR_TEST_TARGET) $(ECOLOGY_MODEL_TEST_TARGET) $(FAUNA_MOTION_TEST_TARGET) $(FAUNA_BEHAVIOR_TEST_TARGET) $(EVOLUTION_TEST_TARGET) $(EVOLUTION_CATALOG_TEST_TARGET) $(FLUID_TEST_TARGET) $(WEATHER_MODEL_TEST_TARGET) $(WEATHER_RUNTIME_TEST_TARGET) $(WEATHER_VISUAL_TEST_TARGET) $(PLAYER_COLLISION_TEST_TARGET) $(SHIP_STATE_TEST_TARGET) $(SHIP_FLIGHT_CONTROLLER_TEST_TARGET) $(SHIP_EXHAUST_TEST_TARGET) $(SHIP_LOCATOR_TEST_TARGET) $(BLOCK_ATLAS_TEST_TARGET) $(INVENTORY_TEST_TARGET) $(CHUNK_ATLAS_TEST_TARGET) $(CHUNK_STREAMING_TEST_TARGET) $(TERRAIN_SCALE_TEST_TARGET) $(SUBSURFACE_TEST_TARGET) $(PERF_TEST_TARGET) $(RENDER_SORT_TEST_TARGET) $(RENDER_RESOURCES_TEST_TARGET) $(RENDER_UI_TEST_TARGET) $(WORLD_RENDERER_TEST_TARGET) $(WORLD_LIGHTING_TEST_TARGET) $(SAVE_IO_TEST_TARGET) $(GAME_SETTINGS_TEST_TARGET) $(SCREENSHOT_TEST_TARGET) $(DEBUG_CONTROL_TEST_TARGET) $(ENVIRONMENT_PRESENTATION_TEST_TARGET) $(ENVIRONMENT_RUNTIME_TEST_TARGET) $(AUDIO_ENVIRONMENT_TEST_TARGET) $(ENTITY_REPLAY_TEST_TARGET) $(ENTITY_ECOLOGY_TEST_TARGET) $(INTERACTION_RAYCAST_TEST_TARGET) $(PLANET_RENDERER_RESOURCES_TEST_TARGET) $(PLANET_TEXTURE_RESOURCES_TEST_TARGET) $(HOMEWORLD_MAP_MODEL_TEST_TARGET)
TEST_TARGETS += $(SPACE_COORDINATES_TEST_TARGET)
TEST_TARGETS += $(SURFACE_TOPOLOGY_TEST_TARGET)
TEST_TARGETS += $(SURFACE_SAVE_TEST_TARGET)
TEST_TARGETS += $(MAP_MARKERS_TEST_TARGET)
TEST_TARGETS += $(SAVE_FORMAT_TEST_TARGET)
TEST_TARGETS += $(GAME_DEBUG_TRACE_TEST_TARGET)
TEST_TARGETS += $(GAME_STREAM_AUDIT_TEST_TARGET)
TEST_TARGETS += $(GAME_EFFECTS_TEST_TARGET)
TEST_TARGETS += $(GAME_NOTICE_TEST_TARGET)
TEST_TARGETS += $(ALBUM_TEST_TARGET)
TEST_HEADERS := $(sort $(wildcard src/*/*.h) $(wildcard tests/*.h))
TEST_TIMEOUT_SECONDS ?= 120
SANITIZER_LEAKS ?= 1
SANITIZE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer
COVERAGE_CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g -pthread --coverage
CI_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -pthread

.PHONY: all voxelcraft run test test-headers test-modules test-architecture test-ci test-sanitize test-coverage test-e2e test-long-run benchmark-chunks release-linux release-check clean

all: $(TARGET)

voxelcraft: $(TARGET)

$(BUILD_DIR) $(TEST_BUILD_DIR) $(MODULE_BUILD_DIR):
	mkdir -p $@

$(TARGET): $(OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(RAYLIB_LIBS) -lm -pthread

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) $(RAYLIB_CFLAGS) -MMD -MP -c -o $@ $<

-include $(DEP)

$(MODULE_BUILD_DIR)/core.a: $(CORE_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/world.a: $(WORLD_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/space.a: $(SPACE_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/ecology.a: $(ECOLOGY_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/gameplay.a: $(GAMEPLAY_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/presentation.a: $(PRESENTATION_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

$(MODULE_BUILD_DIR)/app.a: $(APP_OBJ) | $(MODULE_BUILD_DIR)
	$(AR) rcs $@ $^

# Test executables currently compile focused source closures in one command.
# Depend on every project header so a transitive header change cannot reuse a
# stale binary while those closures are migrated to per-source test objects.
$(TEST_TARGETS) $(CHUNK_BENCHMARK_TARGET): $(TEST_HEADERS) | $(TEST_BUILD_DIR)

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

$(PLANET_RENDERER_RESOURCES_TEST_TARGET): tests/test_planet_renderer_resources.c src/presentation/planet_renderer.c src/presentation/planet_renderer.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_planet_renderer_resources.c -lm

$(PLANET_TEXTURE_RESOURCES_TEST_TARGET): tests/test_planet_texture_resources.c src/presentation/render_planets.c src/presentation/render_sky.c src/presentation/render.h src/space/planet_material.c src/space/planet_material.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_planet_texture_resources.c src/presentation/render_sky.c src/space/planet_material.c src/space/space_units.c $(RAYLIB_LIBS) -lm -pthread

$(INTERACTION_RAYCAST_TEST_TARGET): tests/test_interaction_raycast.c src/gameplay/interaction.c src/gameplay/interaction.h src/world/world_types.h src/world/world.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -Isrc -o $@ tests/test_interaction_raycast.c src/gameplay/interaction.c -lm

$(TEST_TARGET): tests/test_world_environment.c src/world/world_environment.c src/world/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_environment.c src/world/world_environment.c -lm

$(PLANET_SURFACE_TEST_TARGET): tests/test_planet_surface.c src/space/planet_surface.c src/space/planet_surface.h src/space/space.h src/space/planet_profile.c src/space/planet_profile.h src/space/planet_climate.c src/space/planet_climate.h src/space/space_illumination.c src/space/space_illumination.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_surface.c src/space/planet_surface.c src/space/planet_profile.c src/space/planet_climate.c src/space/space_illumination.c src/space/space_units.c -lm

$(SURFACE_TOPOLOGY_TEST_TARGET): tests/test_surface_topology.c src/world/surface_topology.c src/world/surface_topology.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_surface_topology.c src/world/surface_topology.c -lm

$(SURFACE_SAVE_TEST_TARGET): tests/test_surface_save.c src/world/surface_save.c src/world/surface_save.h src/world/surface_topology.c src/world/surface_topology.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_surface_save.c src/world/surface_save.c src/world/surface_topology.c -lm

$(PLANET_MATERIAL_TEST_TARGET): tests/test_planet_material.c src/space/planet_material.c src/space/planet_material.h src/space/planet_surface.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_material.c src/space/planet_material.c -lm

$(PLANET_CLIMATE_TEST_TARGET): tests/test_planet_climate.c src/space/planet_climate.c src/space/planet_climate.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_planet_climate.c src/space/planet_climate.c -lm

$(PLANET_OBSERVATION_TEST_TARGET): tests/test_planet_observation.c src/space/planet_observation.c src/space/planet_observation.h src/space/space.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_observation.c src/space/planet_observation.c -lm

$(SPACE_PHYSICS_TEST_TARGET): tests/test_space_physics.c src/space/space_physics.c src/space/space_physics.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_physics.c src/space/space_physics.c -lm

$(SPACE_BARYCENTER_TEST_TARGET): tests/test_space_barycenter.c src/space/space_barycenter.c src/space/space_barycenter.h src/space/space_orbit.c src/space/space_orbit.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_barycenter.c src/space/space_barycenter.c src/space/space_orbit.c src/space/space_units.c -lm

$(SPACE_ORBIT_TEST_TARGET): tests/test_space_orbit.c src/space/space_orbit.c src/space/space_orbit.h src/space/space_coordinates.c src/space/space_coordinates.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_orbit.c src/space/space_orbit.c src/space/space_coordinates.c src/space/space_units.c -lm

$(SPACE_REMNANT_TEST_TARGET): tests/test_space_remnant.c src/space/space_remnant.c src/space/space_remnant.h src/space/stellar.c src/space/stellar.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_remnant.c src/space/space_remnant.c src/space/stellar.c src/space/space_units.c -lm

$(SPACE_ILLUMINATION_TEST_TARGET): tests/test_space_illumination.c src/space/space_illumination.c src/space/space_illumination.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_illumination.c src/space/space_illumination.c src/space/space_units.c -lm

$(SPACE_SATELLITE_TEST_TARGET): tests/test_space_satellite.c src/space/space_satellite.c src/space/space_satellite.h src/space/space_illumination.c src/space/space_illumination.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_satellite.c src/space/space_satellite.c src/space/space_illumination.c src/space/space_units.c -lm

$(SPACE_UNITS_TEST_TARGET): tests/test_space_units.c src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_units.c src/space/space_units.c -lm

$(SPACE_COORDINATES_TEST_TARGET): tests/test_space_coordinates.c src/space/space_coordinates.c src/space/space_coordinates.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_coordinates.c src/space/space_coordinates.c src/space/space_units.c -lm

$(SPACE_PROPERTIES_TEST_TARGET): tests/test_space_properties.c $(SPACE_FEATURE_SRC) src/space/space.h src/space/planet_profile.c src/space/planet_profile.h src/space/space_system_physics.c src/space/space_system_physics.h src/space/space_system.c src/space/space_system.h src/space/space_barycenter.c src/space/space_barycenter.h src/space/space_orbit.c src/space/space_orbit.h src/space/space_remnant.c src/space/space_remnant.h src/space/space_illumination.c src/space/space_illumination.h src/space/planet_climate.c src/space/planet_climate.h src/space/planet_surface.c src/space/planet_surface.h src/space/space_physics.c src/space/space_physics.h src/space/space_satellite.c src/space/space_satellite.h src/space/space_units.c src/space/space_units.h src/space/stellar.c src/space/stellar.h src/world/terrain.c src/world/terrain.h src/world/subsurface.c src/world/subsurface.h $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/world/weather_model.c src/world/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_space_properties.c $(SPACE_FEATURE_SRC) src/space/solar_catalog.c src/space/space_query_cache.c src/space/planet_profile.c src/space/space_system_physics.c src/space/space_system.c src/space/space_barycenter.c src/space/space_orbit.c src/space/space_remnant.c src/space/space_illumination.c src/space/planet_climate.c src/space/planet_surface.c src/space/space_physics.c src/space/space_satellite.c src/space/space_units.c src/space/stellar.c $(TERRAIN_FEATURE_SRC) $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/world/weather_model.c $(GAME_EFFECTS_SRC) -lm -pthread
$(SPACE_PROPERTIES_TEST_TARGET): src/space/space_query_cache.c src/space/space_query_cache.h

$(SPACE_SYSTEM_TEST_TARGET): tests/test_space_system.c src/space/space_system.c src/space/space_system.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_system.c src/space/space_system.c src/space/space_units.c -lm

$(ECOLOGY_SYSTEM_TEST_TARGET): tests/test_ecology_system.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h $(SPACE_FEATURE_SRC) src/space/space.h src/space/planet_profile.c src/space/planet_profile.h src/space/space_system_physics.c src/space/space_system_physics.h src/space/space_system.c src/space/space_system.h src/space/space_barycenter.c src/space/space_barycenter.h src/space/space_orbit.c src/space/space_orbit.h src/space/space_remnant.c src/space/space_remnant.h src/space/space_illumination.c src/space/space_illumination.h src/space/planet_climate.c src/space/planet_climate.h src/space/planet_surface.c src/space/planet_surface.h src/space/space_physics.c src/space/space_physics.h src/space/space_satellite.c src/space/space_satellite.h src/space/space_units.c src/space/space_units.h src/space/stellar.c src/space/stellar.h src/world/terrain.c src/world/terrain.h $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/ecology/ecology.c src/ecology/ecology.h src/ecology/ecology_internal.h src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/ecology/ecology_model.h src/world/weather.c src/world/weather.h src/world/weather_model.c src/world/weather_model.h
	$(CC) $(CFLAGS) -DCHUNKS_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_ecology_system.c tests/ecology_test_fixture.c $(SPACE_FEATURE_SRC) src/space/solar_catalog.c src/space/space_query_cache.c src/space/planet_profile.c src/space/space_system_physics.c src/space/space_system.c src/space/space_barycenter.c src/space/space_orbit.c src/space/space_remnant.c src/space/space_illumination.c src/space/planet_climate.c src/space/planet_surface.c src/space/space_physics.c src/space/space_satellite.c src/space/space_units.c src/space/stellar.c $(TERRAIN_FEATURE_SRC) $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/ecology/ecology.c src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/ecology/evolution.c src/world/weather.c src/world/weather_model.c $(GAME_EFFECTS_SRC) -lm -pthread
$(ECOLOGY_SYSTEM_TEST_TARGET): src/space/space_query_cache.c src/space/space_query_cache.h
$(ECOLOGY_SYSTEM_TEST_TARGET): src/ecology/evolution.c src/ecology/evolution.h

$(ECOLOGY_PROPERTIES_TEST_TARGET): tests/test_ecology_properties.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h $(SPACE_FEATURE_SRC) src/space/space.h src/space/planet_profile.c src/space/planet_profile.h src/space/space_system_physics.c src/space/space_system_physics.h src/space/space_system.c src/space/space_system.h src/space/space_barycenter.c src/space/space_barycenter.h src/space/space_orbit.c src/space/space_orbit.h src/space/space_remnant.c src/space/space_remnant.h src/space/space_illumination.c src/space/space_illumination.h src/space/planet_climate.c src/space/planet_climate.h src/space/planet_surface.c src/space/planet_surface.h src/space/space_physics.c src/space/space_physics.h src/space/space_satellite.c src/space/space_satellite.h src/space/space_units.c src/space/space_units.h src/space/stellar.c src/space/stellar.h src/world/terrain.c src/world/terrain.h $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/ecology/ecology.c src/ecology/ecology.h src/ecology/ecology_internal.h src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/ecology/ecology_model.h src/world/weather.c src/world/weather.h src/world/weather_model.c src/world/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_ecology_properties.c tests/ecology_test_fixture.c $(SPACE_FEATURE_SRC) src/space/solar_catalog.c src/space/space_query_cache.c src/space/planet_profile.c src/space/space_system_physics.c src/space/space_system.c src/space/space_barycenter.c src/space/space_orbit.c src/space/space_remnant.c src/space/space_illumination.c src/space/planet_climate.c src/space/planet_surface.c src/space/space_physics.c src/space/space_satellite.c src/space/space_units.c src/space/stellar.c $(TERRAIN_FEATURE_SRC) $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/ecology/ecology.c src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/ecology/evolution.c src/world/weather.c src/world/weather_model.c $(GAME_EFFECTS_SRC) -lm -pthread
$(ECOLOGY_PROPERTIES_TEST_TARGET): src/space/space_query_cache.c src/space/space_query_cache.h
$(ECOLOGY_PROPERTIES_TEST_TARGET): src/ecology/evolution.c src/ecology/evolution.h

$(STELLAR_TEST_TARGET): tests/test_stellar.c src/space/stellar.c src/space/stellar.h src/space/space_units.c src/space/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_stellar.c src/space/stellar.c src/space/space_units.c -lm

$(ECOLOGY_MODEL_TEST_TARGET): tests/test_ecology_model.c src/ecology/ecology_model.c src/ecology/ecology_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_ecology_model.c src/ecology/ecology_model.c -lm

$(FAUNA_MOTION_TEST_TARGET): tests/test_fauna_motion.c src/ecology/fauna_motion.c src/ecology/fauna_motion.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_fauna_motion.c src/ecology/fauna_motion.c -lm

$(FAUNA_BEHAVIOR_TEST_TARGET): tests/test_fauna_behavior.c src/ecology/fauna_behavior.c src/ecology/fauna_behavior.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_fauna_behavior.c src/ecology/fauna_behavior.c -lm

$(EVOLUTION_TEST_TARGET): tests/test_evolution.c src/ecology/evolution.c src/ecology/evolution.h src/ecology/creature_visual.c src/ecology/creature_visual.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_evolution.c src/ecology/evolution.c src/ecology/creature_visual.c -lm

$(EVOLUTION_CATALOG_TEST_TARGET): tests/test_evolution_catalog.c src/ecology/evolution_catalog.c src/ecology/evolution_catalog.h src/ecology/evolution.c src/ecology/evolution.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_evolution_catalog.c src/ecology/evolution_catalog.c src/ecology/evolution.c -lm

$(FLUID_TEST_TARGET): tests/test_fluid.c src/world/fluid.c src/world/fluid.h src/world/world_types.h src/world/chunks.h src/world/world.h src/world/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_fluid.c src/world/fluid.c -lm

$(WEATHER_MODEL_TEST_TARGET): tests/test_weather_model.c src/world/weather_model.c src/world/weather_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_weather_model.c src/world/weather_model.c -lm

$(WEATHER_RUNTIME_TEST_TARGET): tests/test_weather_runtime.c src/world/weather.c src/world/weather.h src/world/weather_model.c src/world/weather_model.h src/presentation/weather_visual.c src/presentation/weather_visual.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_weather_runtime.c src/world/weather.c src/world/weather_model.c src/presentation/weather_visual.c $(GAME_EFFECTS_SRC) -lm

$(WEATHER_VISUAL_TEST_TARGET): tests/test_weather_visual.c src/presentation/weather_visual.c src/presentation/weather_visual.h src/world/weather_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_weather_visual.c src/presentation/weather_visual.c -lm

$(HOMEWORLD_MAP_MODEL_TEST_TARGET): tests/test_homeworld_map_model.c src/presentation/homeworld_map_model.c src/presentation/homeworld_map_model.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_homeworld_map_model.c src/presentation/homeworld_map_model.c -lm

$(MAP_MARKERS_TEST_TARGET): tests/test_map_markers.c src/gameplay/map_markers.c src/gameplay/map_markers.h src/world/surface_topology.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_map_markers.c src/gameplay/map_markers.c -lm

$(PLAYER_COLLISION_TEST_TARGET): tests/test_player_collision.c src/gameplay/player.c src/gameplay/player.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_player_collision.c src/gameplay/player.c $(GAME_EFFECTS_SRC) -lm

$(SHIP_STATE_TEST_TARGET): tests/test_ship_state.c src/gameplay/ship.c src/gameplay/ship.h src/gameplay/ship_internal.h src/gameplay/ship_runtime.c src/gameplay/ship_visual_internal.h src/gameplay/ship_navigation.c src/gameplay/ship_navigation.h src/gameplay/ship_flight_controller.c src/gameplay/ship_flight_controller.h src/gameplay/ship_locator.c src/gameplay/ship_locator.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_ship_state.c src/gameplay/ship.c src/gameplay/ship_runtime.c src/gameplay/ship_navigation.c src/gameplay/ship_flight_controller.c src/gameplay/ship_locator.c $(RAYLIB_LIBS) -lm

$(SHIP_FLIGHT_CONTROLLER_TEST_TARGET): tests/test_ship_flight_controller.c src/gameplay/ship_flight_controller.c src/gameplay/ship_flight_controller.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_ship_flight_controller.c src/gameplay/ship_flight_controller.c $(RAYLIB_LIBS) -lm

$(SHIP_EXHAUST_TEST_TARGET): tests/test_ship_exhaust.c src/gameplay/ship_exhaust.c src/gameplay/ship_exhaust.h src/gameplay/ship.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_ship_exhaust.c src/gameplay/ship_exhaust.c -lm

$(SHIP_LOCATOR_TEST_TARGET): tests/test_ship_locator.c src/gameplay/ship_locator.c src/gameplay/ship_locator.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_ship_locator.c src/gameplay/ship_locator.c -lm

$(BLOCK_ATLAS_TEST_TARGET): tests/test_block_atlas.c $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h src/world/world_types.h src/world/world.c src/world/world.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_block_atlas.c $(BLOCK_ATLAS_FEATURE_SRC) src/world/world.c $(RAYLIB_LIBS) -lm

$(INVENTORY_TEST_TARGET): tests/test_inventory.c src/gameplay/inventory.c src/gameplay/inventory.h src/world/world_types.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_inventory.c src/gameplay/inventory.c

$(ALBUM_TEST_TARGET): tests/test_album.c src/gameplay/album.c src/gameplay/album.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_album.c src/gameplay/album.c $(RAYLIB_LIBS)

$(CHUNK_ATLAS_TEST_TARGET): tests/test_chunk_atlas.c $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/world/world_types.h src/world/world.c src/world/world.h
	$(CC) $(CFLAGS) -DCHUNKS_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_chunk_atlas.c $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/world/world.c $(RAYLIB_LIBS) -lm -pthread

$(CHUNK_STREAMING_TEST_TARGET): tests/test_chunk_streaming.c $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/world/world_types.h src/world/world.c src/world/world.h
	$(CC) $(CFLAGS) -DCHUNKS_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_chunk_streaming.c $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/world/world.c $(RAYLIB_LIBS) -lm -pthread

$(TERRAIN_SCALE_TEST_TARGET): tests/test_terrain_scale.c src/world/terrain.c src/world/terrain.h src/world/subsurface.c src/world/subsurface.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/world/world_types.h
	$(CC) $(CFLAGS) -DTERRAIN_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_terrain_scale.c $(TERRAIN_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) $(RAYLIB_LIBS) -lm -pthread

$(SUBSURFACE_TEST_TARGET): tests/test_subsurface.c src/world/subsurface.c src/world/subsurface.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_subsurface.c src/world/subsurface.c -lm

$(CHUNK_BENCHMARK_TARGET): tests/benchmark_chunks.c $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/world/terrain.c src/world/terrain.h src/world/subsurface.c src/world/subsurface.h src/world/world.c src/world/world.h src/ecology/evolution_catalog.c src/ecology/evolution_catalog.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/benchmark_chunks.c $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) $(TERRAIN_FEATURE_SRC) src/world/world.c src/ecology/evolution_catalog.c $(RAYLIB_LIBS) -lm -pthread

benchmark-chunks: $(CHUNK_BENCHMARK_TARGET)
	./$(CHUNK_BENCHMARK_TARGET)

$(PERF_TEST_TARGET): tests/test_perf.c src/core/perf.c src/core/perf.h src/core/perf_metrics.h
	$(CC) $(CFLAGS) -DPERF_TESTING $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_perf.c src/core/perf.c $(RAYLIB_LIBS) -lm

$(RENDER_SORT_TEST_TARGET): tests/test_render_sort.c src/presentation/render_sort.c src/presentation/render_sort.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_render_sort.c src/presentation/render_sort.c

$(RENDER_RESOURCES_TEST_TARGET): tests/test_render_resources.c src/presentation/render_resources.c src/presentation/render_resources.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_render_resources.c src/presentation/render_resources.c

$(RENDER_UI_TEST_TARGET): tests/test_render_ui.c src/presentation/render_ui.c src/presentation/render_ui.h src/core/game_notice.c src/core/game_notice.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_render_ui.c src/presentation/render_ui.c src/core/game_notice.c $(RAYLIB_LIBS) -lm

$(WORLD_RENDERER_TEST_TARGET): tests/test_world_renderer.c src/presentation/world_renderer.c src/presentation/world_renderer.h src/presentation/render_quality.c src/presentation/render_quality.h
	$(CC) $(CFLAGS) -DWORLD_RENDERER_TESTING $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_renderer.c src/presentation/world_renderer.c src/presentation/render_quality.c $(RAYLIB_LIBS) -lm

$(WORLD_LIGHTING_TEST_TARGET): tests/test_world_lighting.c src/world/world_lighting.c src/world/world_lighting.h src/presentation/world_renderer.c src/presentation/world_renderer.h src/presentation/environment_presentation.c src/presentation/environment_presentation.h src/presentation/render_quality.c src/presentation/render_quality.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_lighting.c src/world/world_lighting.c src/presentation/world_renderer.c src/presentation/environment_presentation.c src/presentation/render_quality.c $(RAYLIB_LIBS) -lm

$(SAVE_IO_TEST_TARGET): tests/test_save_io.c src/core/save_io.c src/core/save_io.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_save_io.c src/core/save_io.c

$(SAVE_FORMAT_TEST_TARGET): tests/test_save_format.c src/world/save_format.c src/world/save_format.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_save_format.c src/world/save_format.c

$(GAME_SETTINGS_TEST_TARGET): tests/test_game_settings.c src/app/game_settings.c src/app/game_settings.h src/core/save_io.c src/core/save_io.h src/presentation/render_quality.c src/presentation/render_quality.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_game_settings.c src/app/game_settings.c src/core/save_io.c src/presentation/render_quality.c -lm

$(SCREENSHOT_TEST_TARGET): tests/test_screenshot.c src/app/screenshot.c src/app/screenshot.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_screenshot.c src/app/screenshot.c $(RAYLIB_LIBS)

$(DEBUG_CONTROL_TEST_TARGET): tests/test_debug_control.c src/core/debug_control.c src/core/debug_control.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_debug_control.c src/core/debug_control.c

$(GAME_EFFECTS_TEST_TARGET): tests/test_game_effects.c $(GAME_EFFECTS_SRC) src/core/game_effects.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_game_effects.c $(GAME_EFFECTS_SRC)

$(GAME_NOTICE_TEST_TARGET): tests/test_game_notice.c src/core/game_notice.c src/core/game_notice.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_game_notice.c src/core/game_notice.c

$(GAME_DEBUG_TRACE_TEST_TARGET): tests/test_game_debug_trace.c src/app/game_debug_trace.c src/app/game_debug_trace.h
	$(CC) $(CFLAGS) -DGAME_DEBUG_TRACE_TESTING $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_game_debug_trace.c src/app/game_debug_trace.c $(RAYLIB_LIBS) -lm

$(GAME_STREAM_AUDIT_TEST_TARGET): tests/test_game_stream_audit.c src/app/game_stream_audit.c src/app/game_stream_audit.h
	$(CC) $(CFLAGS) -DGAME_STREAM_AUDIT_TESTING $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_game_stream_audit.c src/app/game_stream_audit.c $(RAYLIB_LIBS) -lm

$(ENVIRONMENT_PRESENTATION_TEST_TARGET): tests/test_environment_presentation.c src/presentation/environment_presentation.c src/presentation/environment_presentation.h src/presentation/render_quality.c src/presentation/render_quality.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_environment_presentation.c src/presentation/environment_presentation.c src/presentation/render_quality.c -lm

$(ENVIRONMENT_RUNTIME_TEST_TARGET): tests/test_environment_runtime.c src/presentation/environment_runtime.c src/presentation/environment_runtime.h src/presentation/environment_presentation.c src/presentation/environment_presentation.h src/presentation/render_quality.c src/presentation/render_quality.h src/world/world_environment.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_environment_runtime.c src/presentation/environment_runtime.c src/presentation/environment_presentation.c src/presentation/render_quality.c -lm

$(AUDIO_ENVIRONMENT_TEST_TARGET): tests/test_audio_environment.c src/presentation/audio.c src/presentation/audio.h src/presentation/environment_presentation.h src/presentation/render_quality.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_audio_environment.c src/presentation/audio.c $(RAYLIB_LIBS) -lm

$(ENTITY_REPLAY_TEST_TARGET): tests/test_entity_replay.c $(ENTITY_FEATURE_SRC) src/ecology/entity.h src/presentation/creature_renderer.c src/presentation/creature_renderer.h src/ecology/creature_visual.c src/ecology/creature_visual.h src/ecology/fauna_motion.c src/ecology/fauna_motion.h src/ecology/fauna_behavior.c src/ecology/fauna_behavior.h src/ecology/evolution.c src/ecology/evolution.h src/ecology/ecology_model.c src/ecology/ecology_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_entity_replay.c $(ENTITY_FEATURE_SRC) src/presentation/creature_renderer.c src/ecology/creature_visual.c src/ecology/fauna_motion.c src/ecology/fauna_behavior.c src/ecology/evolution.c src/ecology/ecology_model.c $(GAME_EFFECTS_SRC) -lm

$(ENTITY_ECOLOGY_TEST_TARGET): tests/test_entity_ecology.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h $(ENTITY_FEATURE_SRC) src/ecology/entity.h src/presentation/creature_renderer.c src/presentation/creature_renderer.h src/ecology/creature_visual.c src/ecology/creature_visual.h src/ecology/fauna_motion.c src/ecology/fauna_motion.h src/ecology/fauna_behavior.c src/ecology/fauna_behavior.h src/world/world_environment.h $(SPACE_FEATURE_SRC) src/space/space.h src/space/planet_profile.c src/space/planet_profile.h src/space/space_system_physics.c src/space/space_system_physics.h src/space/space_system.c src/space/space_system.h src/space/space_barycenter.c src/space/space_barycenter.h src/space/space_orbit.c src/space/space_orbit.h src/space/space_remnant.c src/space/space_remnant.h src/space/space_illumination.c src/space/space_illumination.h src/space/planet_climate.c src/space/planet_climate.h src/space/planet_surface.c src/space/planet_surface.h src/space/space_physics.c src/space/space_physics.h src/space/space_satellite.c src/space/space_satellite.h src/space/space_units.c src/space/space_units.h src/space/stellar.c src/space/stellar.h src/world/terrain.c src/world/terrain.h $(BLOCK_ATLAS_FEATURE_SRC) src/world/block_atlas.h src/world/block_atlas_artwork_internal.h $(CHUNKS_FEATURE_SRC) src/world/chunks.h src/ecology/ecology.c src/ecology/ecology.h src/ecology/ecology_internal.h src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/ecology/ecology_model.h src/world/weather.c src/world/weather.h src/world/weather_model.c src/world/weather_model.h
	$(CC) $(CFLAGS) -DENTITY_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_entity_ecology.c tests/ecology_test_fixture.c $(ENTITY_FEATURE_SRC) src/presentation/creature_renderer.c src/ecology/creature_visual.c src/ecology/fauna_motion.c src/ecology/fauna_behavior.c src/ecology/evolution.c $(SPACE_FEATURE_SRC) src/space/solar_catalog.c src/space/space_query_cache.c src/space/planet_profile.c src/space/space_system_physics.c src/space/space_system.c src/space/space_barycenter.c src/space/space_orbit.c src/space/space_remnant.c src/space/space_illumination.c src/space/planet_climate.c src/space/planet_surface.c src/space/space_physics.c src/space/space_satellite.c src/space/space_units.c src/space/stellar.c $(TERRAIN_FEATURE_SRC) $(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/ecology/ecology.c src/ecology/ecology_profile.c src/ecology/ecology_population.c src/ecology/ecology_flora.c src/ecology/ecology_model.c src/world/weather.c src/world/weather_model.c $(GAME_EFFECTS_SRC) -lm -pthread
$(ENTITY_ECOLOGY_TEST_TARGET): src/ecology/evolution.c src/ecology/evolution.h
$(ENTITY_ECOLOGY_TEST_TARGET): src/space/space_query_cache.c src/space/space_query_cache.h

$(BLOCK_ATLAS_TEST_TARGET) \
$(CHUNK_ATLAS_TEST_TARGET) \
$(CHUNK_STREAMING_TEST_TARGET) \
$(CHUNK_BENCHMARK_TARGET): src/world/block_catalog.h

$(SPACE_PROPERTIES_TEST_TARGET) $(ECOLOGY_SYSTEM_TEST_TARGET) $(ECOLOGY_PROPERTIES_TEST_TARGET) $(ENTITY_ECOLOGY_TEST_TARGET): src/space/solar_catalog.c src/space/solar_catalog.h

$(SPACE_PROPERTIES_TEST_TARGET) \
$(ECOLOGY_SYSTEM_TEST_TARGET) \
$(ECOLOGY_PROPERTIES_TEST_TARGET) \
$(WEATHER_RUNTIME_TEST_TARGET) \
$(PLAYER_COLLISION_TEST_TARGET) \
$(ENTITY_REPLAY_TEST_TARGET) \
$(ENTITY_ECOLOGY_TEST_TARGET): $(GAME_EFFECTS_SRC)

$(SPACE_PROPERTIES_TEST_TARGET) \
$(ECOLOGY_SYSTEM_TEST_TARGET) \
$(ECOLOGY_PROPERTIES_TEST_TARGET) \
$(TERRAIN_SCALE_TEST_TARGET) \
$(CHUNK_BENCHMARK_TARGET) \
$(ENTITY_ECOLOGY_TEST_TARGET): \
	$(TERRAIN_FEATURE_SRC) \
	src/world/terrain.h \
	src/world/terrain_geology_internal.h \
	src/world/terrain_home_materials_internal.h

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
	rm -rf $(BUILD_ROOT) dist
