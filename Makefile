CC ?= cc
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
SHIP_LOCATOR_TEST_TARGET := $(TEST_BUILD_DIR)/test_ship_locator
BLOCK_ATLAS_TEST_TARGET := $(TEST_BUILD_DIR)/test_block_atlas
CHUNK_ATLAS_TEST_TARGET := $(TEST_BUILD_DIR)/test_chunk_atlas
CHUNK_STREAMING_TEST_TARGET := $(TEST_BUILD_DIR)/test_chunk_streaming
TERRAIN_SCALE_TEST_TARGET := $(TEST_BUILD_DIR)/test_terrain_scale
SUBSURFACE_TEST_TARGET := $(TEST_BUILD_DIR)/test_subsurface
CHUNK_BENCHMARK_TARGET := $(TEST_BUILD_DIR)/benchmark_chunks
PERF_TEST_TARGET := $(TEST_BUILD_DIR)/test_perf
RENDER_SORT_TEST_TARGET := $(TEST_BUILD_DIR)/test_render_sort
RENDER_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_render_resources
WORLD_RENDERER_TEST_TARGET := $(TEST_BUILD_DIR)/test_world_renderer
WORLD_LIGHTING_TEST_TARGET := $(TEST_BUILD_DIR)/test_world_lighting
SAVE_IO_TEST_TARGET := $(TEST_BUILD_DIR)/test_save_io
GAME_SETTINGS_TEST_TARGET := $(TEST_BUILD_DIR)/test_game_settings
SCREENSHOT_TEST_TARGET := $(TEST_BUILD_DIR)/test_screenshot
DEBUG_CONTROL_TEST_TARGET := $(TEST_BUILD_DIR)/test_debug_control
ENVIRONMENT_PRESENTATION_TEST_TARGET := $(TEST_BUILD_DIR)/test_environment_presentation
ENVIRONMENT_RUNTIME_TEST_TARGET := $(TEST_BUILD_DIR)/test_environment_runtime
AUDIO_ENVIRONMENT_TEST_TARGET := $(TEST_BUILD_DIR)/test_audio_environment
ENTITY_REPLAY_TEST_TARGET := $(TEST_BUILD_DIR)/test_entity_replay
ENTITY_ECOLOGY_TEST_TARGET := $(TEST_BUILD_DIR)/test_entity_ecology
INTERACTION_RAYCAST_TEST_TARGET := $(TEST_BUILD_DIR)/test_interaction_raycast
PLANET_RENDERER_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_renderer_resources
PLANET_TEXTURE_RESOURCES_TEST_TARGET := $(TEST_BUILD_DIR)/test_planet_texture_resources
SRC := src/main.c src/game.c src/game_debug.c src/game_interaction.c src/game_runtime.c src/album.c src/inventory.c src/space.c src/space_query_cache.c src/planet_profile.c src/space_system_physics.c src/space_system.c src/space_barycenter.c src/space_orbit.c src/space_remnant.c src/space_illumination.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/world_environment.c src/ship.c src/ship_locator.c src/nether.c src/entity.c src/fauna_motion.c src/fauna_behavior.c src/evolution.c src/evolution_catalog.c src/fluid.c src/ecology_model.c src/ecology.c src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/terrain.c src/subsurface.c src/planet_climate.c src/planet_observation.c src/planet_surface.c src/planet_material.c src/planet_renderer.c src/discovery.c src/block_atlas.c src/chunks.c src/world.c src/save_io.c src/game_settings.c src/screenshot.c src/debug_control.c src/environment_presentation.c src/environment_runtime.c src/world_lighting.c src/player.c src/interaction.c src/render.c src/world_renderer.c src/render_sort.c src/render_resources.c src/perf.c src/particles.c src/audio.c src/weather_model.c src/weather_visual.c src/weather.c src/starmap.c

TEST_TARGETS := $(TEST_TARGET) $(PLANET_SURFACE_TEST_TARGET) $(PLANET_MATERIAL_TEST_TARGET) $(PLANET_CLIMATE_TEST_TARGET) $(PLANET_OBSERVATION_TEST_TARGET) $(SPACE_PHYSICS_TEST_TARGET) $(SPACE_BARYCENTER_TEST_TARGET) $(SPACE_ORBIT_TEST_TARGET) $(SPACE_REMNANT_TEST_TARGET) $(SPACE_ILLUMINATION_TEST_TARGET) $(SPACE_SATELLITE_TEST_TARGET) $(SPACE_UNITS_TEST_TARGET) $(SPACE_PROPERTIES_TEST_TARGET) $(SPACE_SYSTEM_TEST_TARGET) $(ECOLOGY_SYSTEM_TEST_TARGET) $(ECOLOGY_PROPERTIES_TEST_TARGET) $(STELLAR_TEST_TARGET) $(ECOLOGY_MODEL_TEST_TARGET) $(FAUNA_MOTION_TEST_TARGET) $(FAUNA_BEHAVIOR_TEST_TARGET) $(EVOLUTION_TEST_TARGET) $(EVOLUTION_CATALOG_TEST_TARGET) $(FLUID_TEST_TARGET) $(WEATHER_MODEL_TEST_TARGET) $(WEATHER_RUNTIME_TEST_TARGET) $(WEATHER_VISUAL_TEST_TARGET) $(PLAYER_COLLISION_TEST_TARGET) $(SHIP_STATE_TEST_TARGET) $(SHIP_LOCATOR_TEST_TARGET) $(BLOCK_ATLAS_TEST_TARGET) $(CHUNK_ATLAS_TEST_TARGET) $(CHUNK_STREAMING_TEST_TARGET) $(TERRAIN_SCALE_TEST_TARGET) $(SUBSURFACE_TEST_TARGET) $(PERF_TEST_TARGET) $(RENDER_SORT_TEST_TARGET) $(RENDER_RESOURCES_TEST_TARGET) $(WORLD_RENDERER_TEST_TARGET) $(WORLD_LIGHTING_TEST_TARGET) $(SAVE_IO_TEST_TARGET) $(GAME_SETTINGS_TEST_TARGET) $(SCREENSHOT_TEST_TARGET) $(DEBUG_CONTROL_TEST_TARGET) $(ENVIRONMENT_PRESENTATION_TEST_TARGET) $(ENVIRONMENT_RUNTIME_TEST_TARGET) $(AUDIO_ENVIRONMENT_TEST_TARGET) $(ENTITY_REPLAY_TEST_TARGET) $(ENTITY_ECOLOGY_TEST_TARGET) $(INTERACTION_RAYCAST_TEST_TARGET) $(PLANET_RENDERER_RESOURCES_TEST_TARGET) $(PLANET_TEXTURE_RESOURCES_TEST_TARGET)
TEST_TIMEOUT_SECONDS ?= 120
SANITIZER_LEAKS ?= 1
SANITIZE_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O1 -g -pthread -fsanitize=address,undefined -fno-omit-frame-pointer
COVERAGE_CFLAGS ?= -std=c99 -Wall -Wextra -O0 -g -pthread --coverage
CI_CFLAGS ?= -std=c99 -Wall -Wextra -Werror -O2 -pthread

.PHONY: all voxelcraft run test test-architecture test-ci test-sanitize test-coverage test-e2e test-long-run benchmark-chunks release-linux release-check clean

all: $(TARGET)

voxelcraft: $(TARGET)

$(BUILD_DIR) $(TEST_BUILD_DIR):
	mkdir -p $@

$(TARGET): $(SRC) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(SRC) $(RAYLIB_LIBS) -lm -pthread

$(TEST_TARGETS) $(CHUNK_BENCHMARK_TARGET): | $(TEST_BUILD_DIR)

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGETS)
	@TEST_TIMEOUT_SECONDS=$(TEST_TIMEOUT_SECONDS) sh scripts/run-tests.sh $(TEST_TARGETS)
	@sh tests/test_architecture.sh

test-architecture:
	@sh tests/test_architecture.sh

test-ci:
	$(MAKE) BUILD_VARIANT=normal CFLAGS='$(CI_CFLAGS)' test
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

$(PLANET_RENDERER_RESOURCES_TEST_TARGET): tests/test_planet_renderer_resources.c src/planet_renderer.c src/planet_renderer.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_planet_renderer_resources.c -lm

$(PLANET_TEXTURE_RESOURCES_TEST_TARGET): tests/test_planet_texture_resources.c src/render.c src/render.h src/planet_material.c src/planet_material.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_planet_texture_resources.c src/planet_material.c src/space_units.c $(RAYLIB_LIBS) -lm -pthread

$(INTERACTION_RAYCAST_TEST_TARGET): tests/test_interaction_raycast.c src/interaction.c src/interaction.h src/types.h src/world.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Wl,--gc-sections -Isrc -o $@ tests/test_interaction_raycast.c src/interaction.c -lm

$(TEST_TARGET): tests/test_world_environment.c src/world_environment.c src/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_environment.c src/world_environment.c -lm

$(PLANET_SURFACE_TEST_TARGET): tests/test_planet_surface.c src/planet_surface.c src/planet_surface.h src/space.h src/planet_profile.c src/planet_profile.h src/planet_climate.c src/planet_climate.h src/space_illumination.c src/space_illumination.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_surface.c src/planet_surface.c src/planet_profile.c src/planet_climate.c src/space_illumination.c src/space_units.c -lm

$(PLANET_MATERIAL_TEST_TARGET): tests/test_planet_material.c src/planet_material.c src/planet_material.h src/planet_surface.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_material.c src/planet_material.c -lm

$(PLANET_CLIMATE_TEST_TARGET): tests/test_planet_climate.c src/planet_climate.c src/planet_climate.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_planet_climate.c src/planet_climate.c -lm

$(PLANET_OBSERVATION_TEST_TARGET): tests/test_planet_observation.c src/planet_observation.c src/planet_observation.h src/space.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_observation.c src/planet_observation.c -lm

$(SPACE_PHYSICS_TEST_TARGET): tests/test_space_physics.c src/space_physics.c src/space_physics.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_physics.c src/space_physics.c -lm

$(SPACE_BARYCENTER_TEST_TARGET): tests/test_space_barycenter.c src/space_barycenter.c src/space_barycenter.h src/space_orbit.c src/space_orbit.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_barycenter.c src/space_barycenter.c src/space_orbit.c src/space_units.c -lm

$(SPACE_ORBIT_TEST_TARGET): tests/test_space_orbit.c src/space_orbit.c src/space_orbit.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_orbit.c src/space_orbit.c src/space_units.c -lm

$(SPACE_REMNANT_TEST_TARGET): tests/test_space_remnant.c src/space_remnant.c src/space_remnant.h src/stellar.c src/stellar.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_remnant.c src/space_remnant.c src/stellar.c src/space_units.c -lm

$(SPACE_ILLUMINATION_TEST_TARGET): tests/test_space_illumination.c src/space_illumination.c src/space_illumination.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_illumination.c src/space_illumination.c src/space_units.c -lm

$(SPACE_SATELLITE_TEST_TARGET): tests/test_space_satellite.c src/space_satellite.c src/space_satellite.h src/space_illumination.c src/space_illumination.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_satellite.c src/space_satellite.c src/space_illumination.c src/space_units.c -lm

$(SPACE_UNITS_TEST_TARGET): tests/test_space_units.c src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_units.c src/space_units.c -lm

$(SPACE_PROPERTIES_TEST_TARGET): tests/test_space_properties.c src/space.c src/space.h src/planet_profile.c src/planet_profile.h src/space_system_physics.c src/space_system_physics.h src/space_system.c src/space_system.h src/space_barycenter.c src/space_barycenter.h src/space_orbit.c src/space_orbit.h src/space_remnant.c src/space_remnant.h src/space_illumination.c src/space_illumination.h src/planet_climate.c src/planet_climate.h src/planet_surface.c src/planet_surface.h src/space_physics.c src/space_physics.h src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h src/stellar.c src/stellar.h src/terrain.c src/terrain.h src/subsurface.c src/subsurface.h src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/weather_model.c src/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_space_properties.c src/space.c src/space_query_cache.c src/planet_profile.c src/space_system_physics.c src/space_system.c src/space_barycenter.c src/space_orbit.c src/space_remnant.c src/space_illumination.c src/planet_climate.c src/planet_surface.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/terrain.c src/subsurface.c src/block_atlas.c src/chunks.c src/weather_model.c -lm -pthread
$(SPACE_PROPERTIES_TEST_TARGET): src/space_query_cache.c src/space_query_cache.h

$(SPACE_SYSTEM_TEST_TARGET): tests/test_space_system.c src/space_system.c src/space_system.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_system.c src/space_system.c src/space_units.c -lm

$(ECOLOGY_SYSTEM_TEST_TARGET): tests/test_ecology_system.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h src/space.c src/space.h src/planet_profile.c src/planet_profile.h src/space_system_physics.c src/space_system_physics.h src/space_system.c src/space_system.h src/space_barycenter.c src/space_barycenter.h src/space_orbit.c src/space_orbit.h src/space_remnant.c src/space_remnant.h src/space_illumination.c src/space_illumination.h src/planet_climate.c src/planet_climate.h src/planet_surface.c src/planet_surface.h src/space_physics.c src/space_physics.h src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h src/stellar.c src/stellar.h src/terrain.c src/terrain.h src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/ecology.c src/ecology.h src/ecology_internal.h src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/ecology_model.h src/weather.c src/weather.h src/weather_model.c src/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_ecology_system.c tests/ecology_test_fixture.c src/space.c src/space_query_cache.c src/planet_profile.c src/space_system_physics.c src/space_system.c src/space_barycenter.c src/space_orbit.c src/space_remnant.c src/space_illumination.c src/planet_climate.c src/planet_surface.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/terrain.c src/subsurface.c src/block_atlas.c src/chunks.c src/ecology.c src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/evolution.c src/weather.c src/weather_model.c -lm -pthread
$(ECOLOGY_SYSTEM_TEST_TARGET): src/space_query_cache.c src/space_query_cache.h
$(ECOLOGY_SYSTEM_TEST_TARGET): src/evolution.c src/evolution.h

$(ECOLOGY_PROPERTIES_TEST_TARGET): tests/test_ecology_properties.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h src/space.c src/space.h src/planet_profile.c src/planet_profile.h src/space_system_physics.c src/space_system_physics.h src/space_system.c src/space_system.h src/space_barycenter.c src/space_barycenter.h src/space_orbit.c src/space_orbit.h src/space_remnant.c src/space_remnant.h src/space_illumination.c src/space_illumination.h src/planet_climate.c src/planet_climate.h src/planet_surface.c src/planet_surface.h src/space_physics.c src/space_physics.h src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h src/stellar.c src/stellar.h src/terrain.c src/terrain.h src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/ecology.c src/ecology.h src/ecology_internal.h src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/ecology_model.h src/weather.c src/weather.h src/weather_model.c src/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_ecology_properties.c tests/ecology_test_fixture.c src/space.c src/space_query_cache.c src/planet_profile.c src/space_system_physics.c src/space_system.c src/space_barycenter.c src/space_orbit.c src/space_remnant.c src/space_illumination.c src/planet_climate.c src/planet_surface.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/terrain.c src/subsurface.c src/block_atlas.c src/chunks.c src/ecology.c src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/evolution.c src/weather.c src/weather_model.c -lm -pthread
$(ECOLOGY_PROPERTIES_TEST_TARGET): src/space_query_cache.c src/space_query_cache.h
$(ECOLOGY_PROPERTIES_TEST_TARGET): src/evolution.c src/evolution.h

$(STELLAR_TEST_TARGET): tests/test_stellar.c src/stellar.c src/stellar.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_stellar.c src/stellar.c src/space_units.c -lm

$(ECOLOGY_MODEL_TEST_TARGET): tests/test_ecology_model.c src/ecology_model.c src/ecology_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_ecology_model.c src/ecology_model.c -lm

$(FAUNA_MOTION_TEST_TARGET): tests/test_fauna_motion.c src/fauna_motion.c src/fauna_motion.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_fauna_motion.c src/fauna_motion.c -lm

$(FAUNA_BEHAVIOR_TEST_TARGET): tests/test_fauna_behavior.c src/fauna_behavior.c src/fauna_behavior.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_fauna_behavior.c src/fauna_behavior.c -lm

$(EVOLUTION_TEST_TARGET): tests/test_evolution.c src/evolution.c src/evolution.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_evolution.c src/evolution.c -lm

$(EVOLUTION_CATALOG_TEST_TARGET): tests/test_evolution_catalog.c src/evolution_catalog.c src/evolution_catalog.h src/evolution.c src/evolution.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_evolution_catalog.c src/evolution_catalog.c src/evolution.c -lm

$(FLUID_TEST_TARGET): tests/test_fluid.c src/fluid.c src/fluid.h src/types.h src/chunks.h src/world.h src/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_fluid.c src/fluid.c -lm

$(WEATHER_MODEL_TEST_TARGET): tests/test_weather_model.c src/weather_model.c src/weather_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_weather_model.c src/weather_model.c -lm

$(WEATHER_RUNTIME_TEST_TARGET): tests/test_weather_runtime.c src/weather.c src/weather.h src/weather_model.c src/weather_model.h src/weather_visual.c src/weather_visual.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_weather_runtime.c src/weather.c src/weather_model.c src/weather_visual.c -lm

$(WEATHER_VISUAL_TEST_TARGET): tests/test_weather_visual.c src/weather_visual.c src/weather_visual.h src/weather_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_weather_visual.c src/weather_visual.c -lm

$(PLAYER_COLLISION_TEST_TARGET): tests/test_player_collision.c src/player.c src/player.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_player_collision.c src/player.c -lm

$(SHIP_STATE_TEST_TARGET): tests/test_ship_state.c src/ship.c src/ship.h src/ship_locator.c src/ship_locator.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -ffunction-sections -fdata-sections -Isrc -Wl,--gc-sections -o $@ tests/test_ship_state.c src/ship.c src/ship_locator.c -lm

$(SHIP_LOCATOR_TEST_TARGET): tests/test_ship_locator.c src/ship_locator.c src/ship_locator.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_ship_locator.c src/ship_locator.c -lm

$(BLOCK_ATLAS_TEST_TARGET): tests/test_block_atlas.c src/block_atlas.c src/block_atlas.h src/types.h src/world.c src/world.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_block_atlas.c src/block_atlas.c src/world.c $(RAYLIB_LIBS) -lm

$(CHUNK_ATLAS_TEST_TARGET): tests/test_chunk_atlas.c src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/types.h src/world.c src/world.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_chunk_atlas.c src/block_atlas.c src/chunks.c src/world.c $(RAYLIB_LIBS) -lm -pthread

$(CHUNK_STREAMING_TEST_TARGET): tests/test_chunk_streaming.c src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/types.h src/world.c src/world.h
	$(CC) $(CFLAGS) -DCHUNKS_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_chunk_streaming.c src/block_atlas.c src/chunks.c src/world.c $(RAYLIB_LIBS) -lm -pthread

$(TERRAIN_SCALE_TEST_TARGET): tests/test_terrain_scale.c src/terrain.c src/terrain.h src/subsurface.c src/subsurface.h src/chunks.c src/chunks.h src/types.h
	$(CC) $(CFLAGS) -DTERRAIN_TESTING -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_terrain_scale.c src/terrain.c src/subsurface.c src/chunks.c $(RAYLIB_LIBS) -lm -pthread

$(SUBSURFACE_TEST_TARGET): tests/test_subsurface.c src/subsurface.c src/subsurface.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_subsurface.c src/subsurface.c -lm

$(CHUNK_BENCHMARK_TARGET): tests/benchmark_chunks.c src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/terrain.c src/terrain.h src/subsurface.c src/subsurface.h src/world.c src/world.h src/evolution_catalog.c src/evolution_catalog.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/benchmark_chunks.c src/block_atlas.c src/chunks.c src/terrain.c src/subsurface.c src/world.c src/evolution_catalog.c $(RAYLIB_LIBS) -lm -pthread

benchmark-chunks: $(CHUNK_BENCHMARK_TARGET)
	./$(CHUNK_BENCHMARK_TARGET)

$(PERF_TEST_TARGET): tests/test_perf.c src/perf.c src/perf.h src/chunks.h src/render_resources.c src/render_resources.h
	$(CC) $(CFLAGS) -DPERF_TESTING $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_perf.c src/perf.c src/render_resources.c $(RAYLIB_LIBS) -lm

$(RENDER_SORT_TEST_TARGET): tests/test_render_sort.c src/render_sort.c src/render_sort.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_render_sort.c src/render_sort.c

$(RENDER_RESOURCES_TEST_TARGET): tests/test_render_resources.c src/render_resources.c src/render_resources.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_render_resources.c src/render_resources.c

$(WORLD_RENDERER_TEST_TARGET): tests/test_world_renderer.c src/world_renderer.c src/world_renderer.h src/game_settings.c src/game_settings.h src/save_io.c src/save_io.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_renderer.c src/world_renderer.c src/game_settings.c src/save_io.c $(RAYLIB_LIBS) -lm

$(WORLD_LIGHTING_TEST_TARGET): tests/test_world_lighting.c src/world_lighting.c src/world_lighting.h src/world_renderer.c src/world_renderer.h src/environment_presentation.c src/environment_presentation.h src/game_settings.c src/game_settings.h src/save_io.c src/save_io.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_lighting.c src/world_lighting.c src/world_renderer.c src/environment_presentation.c src/game_settings.c src/save_io.c $(RAYLIB_LIBS) -lm

$(SAVE_IO_TEST_TARGET): tests/test_save_io.c src/save_io.c src/save_io.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_save_io.c src/save_io.c

$(GAME_SETTINGS_TEST_TARGET): tests/test_game_settings.c src/game_settings.c src/game_settings.h src/save_io.c src/save_io.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_game_settings.c src/game_settings.c src/save_io.c -lm

$(SCREENSHOT_TEST_TARGET): tests/test_screenshot.c src/screenshot.c src/screenshot.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_screenshot.c src/screenshot.c $(RAYLIB_LIBS)

$(DEBUG_CONTROL_TEST_TARGET): tests/test_debug_control.c src/debug_control.c src/debug_control.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_debug_control.c src/debug_control.c

$(ENVIRONMENT_PRESENTATION_TEST_TARGET): tests/test_environment_presentation.c src/environment_presentation.c src/environment_presentation.h src/game_settings.c src/game_settings.h src/save_io.c src/save_io.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_environment_presentation.c src/environment_presentation.c src/game_settings.c src/save_io.c -lm

$(ENVIRONMENT_RUNTIME_TEST_TARGET): tests/test_environment_runtime.c src/environment_runtime.c src/environment_runtime.h src/environment_presentation.c src/environment_presentation.h src/game_settings.c src/game_settings.h src/save_io.c src/save_io.h src/world_environment.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_environment_runtime.c src/environment_runtime.c src/environment_presentation.c src/game_settings.c src/save_io.c -lm

$(AUDIO_ENVIRONMENT_TEST_TARGET): tests/test_audio_environment.c src/audio.c src/audio.h src/environment_presentation.h src/game_settings.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_audio_environment.c src/audio.c $(RAYLIB_LIBS) -lm

$(ENTITY_REPLAY_TEST_TARGET): tests/test_entity_replay.c src/entity.c src/entity.h src/fauna_motion.c src/fauna_motion.h src/fauna_behavior.c src/fauna_behavior.h src/evolution.c src/evolution.h src/ecology_model.c src/ecology_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_entity_replay.c src/entity.c src/fauna_motion.c src/fauna_behavior.c src/evolution.c src/ecology_model.c -lm

$(ENTITY_ECOLOGY_TEST_TARGET): tests/test_entity_ecology.c tests/ecology_test_fixture.c tests/ecology_test_fixture.h src/entity.c src/entity.h src/fauna_motion.c src/fauna_motion.h src/fauna_behavior.c src/fauna_behavior.h src/particles.h src/world_environment.h src/space.c src/space.h src/planet_profile.c src/planet_profile.h src/space_system_physics.c src/space_system_physics.h src/space_system.c src/space_system.h src/space_barycenter.c src/space_barycenter.h src/space_orbit.c src/space_orbit.h src/space_remnant.c src/space_remnant.h src/space_illumination.c src/space_illumination.h src/planet_climate.c src/planet_climate.h src/planet_surface.c src/planet_surface.h src/space_physics.c src/space_physics.h src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h src/stellar.c src/stellar.h src/terrain.c src/terrain.h src/block_atlas.c src/block_atlas.h src/chunks.c src/chunks.h src/ecology.c src/ecology.h src/ecology_internal.h src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/ecology_model.h src/weather.c src/weather.h src/weather_model.c src/weather_model.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Itests -Wl,--gc-sections -o $@ tests/test_entity_ecology.c tests/ecology_test_fixture.c src/entity.c src/fauna_motion.c src/fauna_behavior.c src/evolution.c src/space.c src/space_query_cache.c src/planet_profile.c src/space_system_physics.c src/space_system.c src/space_barycenter.c src/space_orbit.c src/space_remnant.c src/space_illumination.c src/planet_climate.c src/planet_surface.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/terrain.c src/subsurface.c src/block_atlas.c src/chunks.c src/ecology.c src/ecology_profile.c src/ecology_population.c src/ecology_flora.c src/ecology_model.c src/weather.c src/weather_model.c -lm -pthread
$(ENTITY_ECOLOGY_TEST_TARGET): src/evolution.c src/evolution.h
$(ENTITY_ECOLOGY_TEST_TARGET): src/space_query_cache.c src/space_query_cache.h

release-linux: all
	@set -eu; version=$$(git describe --tags --always --dirty 2>/dev/null || echo dev); out="dist/voxelcraft-linux-$${version}"; rm -rf "$${out}" "$${out}.tar.gz"; mkdir -p "$${out}"; cp voxelcraft README.md "$${out}/"; cp -R assets "$${out}/"; printf '%s\n' "Voxelcraft Linux build $${version}" > "$${out}/BUILD.txt"; tar -czf "$${out}.tar.gz" -C dist "$$(basename "$${out}")"; sha256sum "$${out}.tar.gz" > "$${out}.tar.gz.sha256"; printf 'release=%s\narchive=%s.tar.gz\n' "$${version}" "$${out}";

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
