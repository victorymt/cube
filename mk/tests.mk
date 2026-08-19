# Each test keeps its focused source closure, but the build metadata lives in
# one declarative manifest consumed by the shared Makefile recipe.

TEST_TARGETS :=
TEST_BINARY_NAMES :=

comma := ,
GC_SECTIONS_LDFLAG := -Wl$(comma)--gc-sections

define define_test
$1 := $$(TEST_BUILD_DIR)/$2
TEST_TARGETS += $$($1)
TEST_BINARY_NAMES += $2
TEST_SOURCES_$2 := $3
TEST_CPPFLAGS_$2 := $4
TEST_CFLAGS_$2 := $5
TEST_LDFLAGS_$2 := $6
TEST_LDLIBS_$2 := $7
TEST_RAYLIB_CFLAGS_$2 := $8
TEST_RAYLIB_LIBS_$2 := $9
endef

define define_benchmark
$1 := $$(TEST_BUILD_DIR)/$2
TEST_BINARY_NAMES += $2
TEST_SOURCES_$2 := $3
TEST_CPPFLAGS_$2 := $4
TEST_CFLAGS_$2 := $5
TEST_LDFLAGS_$2 := $6
TEST_LDLIBS_$2 := $7
TEST_RAYLIB_CFLAGS_$2 := $8
TEST_RAYLIB_LIBS_$2 := $9
endef

$(eval $(call define_test,TEST_TARGET,test_world_environment,src/world/world_environment.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,PLANET_SURFACE_TEST_TARGET,test_planet_surface,src/space/planet_surface.c src/space/planet_profile.c src/space/planet_climate.c src/space/space_illumination.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,PLANET_MATERIAL_TEST_TARGET,test_planet_material,src/space/planet_material.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,PLANET_CLIMATE_TEST_TARGET,test_planet_climate,src/space/planet_climate.c,,,, -lm,,))
$(eval $(call define_test,PLANET_OBSERVATION_TEST_TARGET,test_planet_observation,src/space/planet_observation.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SPACE_PHYSICS_TEST_TARGET,test_space_physics,src/space/space_physics.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SPACE_BARYCENTER_TEST_TARGET,test_space_barycenter,src/space/space_barycenter.c src/space/space_orbit.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SPACE_ORBIT_TEST_TARGET,test_space_orbit,src/space/space_orbit.c src/space/space_coordinates.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SPACE_REMNANT_TEST_TARGET,test_space_remnant,src/space/space_remnant.c src/space/stellar.c src/space/space_units.c,,,, -lm,,))
$(eval $(call define_test,SPACE_ILLUMINATION_TEST_TARGET,test_space_illumination,src/space/space_illumination.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SPACE_SATELLITE_TEST_TARGET,test_space_satellite,src/space/space_satellite.c src/space/space_illumination.c src/space/space_units.c,,,, -lm,,))
$(eval $(call define_test,SPACE_UNITS_TEST_TARGET,test_space_units,src/space/space_units.c,,,, -lm,,))
$(eval $(call define_test,SPACE_COORDINATES_TEST_TARGET,test_space_coordinates,src/space/space_coordinates.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SURFACE_TOPOLOGY_TEST_TARGET,test_surface_topology,src/world/surface_topology.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SURFACE_SAVE_TEST_TARGET,test_surface_save,src/world/surface_save.c src/world/surface_topology.c,,,, -lm,$(RAYLIB_CFLAGS),))

SPACE_PROPERTIES_SOURCES := \
	$(SPACE_FEATURE_SRC) \
	src/space/solar_catalog.c \
	src/space/space_query_cache.c \
	src/space/planet_profile.c \
	src/space/space_system_physics.c \
	src/space/space_system.c \
	src/space/space_barycenter.c \
	src/space/space_orbit.c \
	src/space/space_remnant.c \
	src/space/space_illumination.c \
	src/space/planet_climate.c \
	src/space/planet_surface.c \
	src/space/space_physics.c \
	src/space/space_satellite.c \
	src/space/space_units.c \
	src/space/stellar.c \
	$(TERRAIN_FEATURE_SRC) \
	$(BLOCK_ATLAS_FEATURE_SRC) \
	$(CHUNKS_FEATURE_SRC) \
	src/world/weather_model.c \
	$(GAME_EFFECTS_SRC)
$(eval $(call define_test,SPACE_PROPERTIES_TEST_TARGET,test_space_properties,$(SPACE_PROPERTIES_SOURCES),,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),))

$(eval $(call define_test,SPACE_SYSTEM_TEST_TARGET,test_space_system,src/space/space_system.c src/space/space_units.c,,,, -lm,,))

ECOLOGY_SYSTEM_SOURCES := \
	tests/ecology_test_fixture.c \
	$(SPACE_FEATURE_SRC) \
	src/space/solar_catalog.c \
	src/space/space_query_cache.c \
	src/space/planet_profile.c \
	src/space/space_system_physics.c \
	src/space/space_system.c \
	src/space/space_barycenter.c \
	src/space/space_orbit.c \
	src/space/space_remnant.c \
	src/space/space_illumination.c \
	src/space/planet_climate.c \
	src/space/planet_surface.c \
	src/space/space_physics.c \
	src/space/space_satellite.c \
	src/space/space_units.c \
	src/space/stellar.c \
	$(TERRAIN_FEATURE_SRC) \
	$(BLOCK_ATLAS_FEATURE_SRC) \
	$(CHUNKS_FEATURE_SRC) \
	src/ecology/ecology.c \
	src/ecology/ecology_profile.c \
	src/ecology/ecology_population.c \
	src/ecology/ecology_flora.c \
	src/ecology/ecology_model.c \
	src/ecology/evolution.c \
	src/world/weather.c \
	src/world/weather_model.c \
	src/world/local_climate.c \
	$(GAME_EFFECTS_SRC)
$(eval $(call define_test,ECOLOGY_SYSTEM_TEST_TARGET,test_ecology_system,$(ECOLOGY_SYSTEM_SOURCES),-Itests,-DCHUNKS_TESTING -DECOLOGY_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,ECOLOGY_PROPERTIES_TEST_TARGET,test_ecology_properties,$(ECOLOGY_SYSTEM_SOURCES),-Itests,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),))

$(eval $(call define_test,STELLAR_TEST_TARGET,test_stellar,src/space/stellar.c src/space/space_units.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,ECOLOGY_MODEL_TEST_TARGET,test_ecology_model,src/ecology/ecology_model.c,,,, -lm,,))
$(eval $(call define_test,FAUNA_MOTION_TEST_TARGET,test_fauna_motion,src/ecology/fauna_motion.c,,,, -lm,,))
$(eval $(call define_test,FAUNA_BEHAVIOR_TEST_TARGET,test_fauna_behavior,src/ecology/fauna_behavior.c,,,, -lm,,))
$(eval $(call define_test,EVOLUTION_TEST_TARGET,test_evolution,src/ecology/evolution.c src/ecology/creature_visual.c,,,, -lm,,))
$(eval $(call define_test,EVOLUTION_CATALOG_TEST_TARGET,test_evolution_catalog,src/ecology/evolution_catalog.c src/ecology/evolution.c,,,, -lm,,))
$(eval $(call define_test,FLUID_TEST_TARGET,test_fluid,src/world/fluid.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,BLOCK_MATERIAL_TEST_TARGET,test_block_material,,,,,,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,LOCAL_CLIMATE_TEST_TARGET,test_local_climate,src/world/local_climate.c,,,, -lm,,))
$(eval $(call define_test,WEATHER_MODEL_TEST_TARGET,test_weather_model,src/world/weather_model.c,,,, -lm,,))
$(eval $(call define_test,WEATHER_IMPACT_TEST_TARGET,test_weather_impact,src/world/weather_impact.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,WEATHER_RUNTIME_TEST_TARGET,test_weather_runtime,src/world/weather.c src/world/weather_model.c src/world/local_climate.c src/presentation/weather_visual.c $(GAME_EFFECTS_SRC),,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,WEATHER_VISUAL_TEST_TARGET,test_weather_visual,src/presentation/weather_visual.c,,,, -lm,,))
$(eval $(call define_test,TORNADO_MODEL_TEST_TARGET,test_tornado_model,src/world/tornado_model.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,TORNADO_TEST_TARGET,test_tornado,src/world/tornado.c src/world/tornado_model.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,HOMEWORLD_MAP_MODEL_TEST_TARGET,test_homeworld_map_model,src/presentation/homeworld_map_model.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,MAP_MARKERS_TEST_TARGET,test_map_markers,src/gameplay/map_markers.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,PLAYER_COLLISION_TEST_TARGET,test_player_collision,src/gameplay/player.c $(GAME_EFFECTS_SRC),,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SHIP_STATE_TEST_TARGET,test_ship_state,src/gameplay/ship.c src/gameplay/ship_runtime.c src/gameplay/ship_navigation.c src/gameplay/ship_flight_controller.c src/gameplay/ship_locator.c,,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,SHIP_FLIGHT_CONTROLLER_TEST_TARGET,test_ship_flight_controller,src/gameplay/ship_flight_controller.c,,,, -lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,SHIP_EXHAUST_TEST_TARGET,test_ship_exhaust,src/gameplay/ship_exhaust.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,SHIP_LOCATOR_TEST_TARGET,test_ship_locator,src/gameplay/ship_locator.c,,,, -lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,BLOCK_ATLAS_TEST_TARGET,test_block_atlas,$(BLOCK_ATLAS_FEATURE_SRC) src/world/world.c,,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,INVENTORY_TEST_TARGET,test_inventory,src/gameplay/inventory.c,,,,,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,ALBUM_TEST_TARGET,test_album,src/gameplay/album.c,,,,,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,CHUNK_ATLAS_TEST_TARGET,test_chunk_atlas,$(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/world/world.c,,-DCHUNKS_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,CHUNK_STREAMING_TEST_TARGET,test_chunk_streaming,$(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) src/world/world.c,,-DCHUNKS_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,TERRAIN_SCALE_TEST_TARGET,test_terrain_scale,$(TERRAIN_FEATURE_SRC) $(CHUNKS_FEATURE_SRC),,-DTERRAIN_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,SUBSURFACE_TEST_TARGET,test_subsurface,src/world/subsurface.c,,,, -lm,,))
$(eval $(call define_benchmark,CHUNK_BENCHMARK_TARGET,benchmark_chunks,$(BLOCK_ATLAS_FEATURE_SRC) $(CHUNKS_FEATURE_SRC) $(TERRAIN_FEATURE_SRC) src/world/world.c src/ecology/evolution_catalog.c,,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,PERF_TEST_TARGET,test_perf,src/core/perf.c,,-DPERF_TESTING,,-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,RENDER_SORT_TEST_TARGET,test_render_sort,src/presentation/render_sort.c,,,,,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,RENDER_RESOURCES_TEST_TARGET,test_render_resources,src/presentation/render_resources.c,,,,,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,RENDER_UI_TEST_TARGET,test_render_ui,src/presentation/render_ui.c src/core/game_notice.c,,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,WORLD_RENDERER_TEST_TARGET,test_world_renderer,src/presentation/world_renderer.c src/presentation/render_quality.c,,-DWORLD_RENDERER_TESTING,,-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,WORLD_LIGHTING_TEST_TARGET,test_world_lighting,src/world/world_lighting.c src/presentation/world_renderer.c src/presentation/environment_presentation.c src/presentation/render_quality.c,,,, -lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,SAVE_IO_TEST_TARGET,test_save_io,src/core/save_io.c,,,,,,))
$(eval $(call define_test,SAVE_FORMAT_TEST_TARGET,test_save_format,src/world/save_format.c,,,,,,))
$(eval $(call define_test,GAME_SETTINGS_TEST_TARGET,test_game_settings,src/app/game_settings.c src/core/save_io.c src/presentation/render_quality.c,,,, -lm,,))
$(eval $(call define_test,SCREENSHOT_TEST_TARGET,test_screenshot,src/app/screenshot.c,,,,,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,DEBUG_CONTROL_TEST_TARGET,test_debug_control,src/core/debug_control.c,,,,,,))
$(eval $(call define_test,DEBUG_DSL_TEST_TARGET,test_debug_dsl,src/core/debug_dsl.c src/core/debug_dsl_error.c,-DDEBUG_DSL_MAX_EXECUTION_STEPS=64u,,, -lm,,))
$(eval $(call define_test,GAME_EFFECTS_TEST_TARGET,test_game_effects,$(GAME_EFFECTS_SRC),,,,, $(RAYLIB_CFLAGS),))
$(eval $(call define_test,GAME_NOTICE_TEST_TARGET,test_game_notice,src/core/game_notice.c,,,,,,))
$(eval $(call define_test,GAME_DEBUG_TRACE_TEST_TARGET,test_game_debug_trace,src/app/game_debug_trace.c,,-DGAME_DEBUG_TRACE_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,GAME_STREAM_AUDIT_TEST_TARGET,test_game_stream_audit,src/app/game_stream_audit.c,,-DGAME_STREAM_AUDIT_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,ENVIRONMENT_PRESENTATION_TEST_TARGET,test_environment_presentation,src/presentation/environment_presentation.c src/presentation/render_quality.c,,,, -lm,,))
$(eval $(call define_test,ENVIRONMENT_RUNTIME_TEST_TARGET,test_environment_runtime,src/presentation/environment_runtime.c src/presentation/environment_presentation.c src/presentation/render_quality.c,,,, -lm,,))
$(eval $(call define_test,AUDIO_ENVIRONMENT_TEST_TARGET,test_audio_environment,src/presentation/audio.c,,,, -lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,ENTITY_REPLAY_TEST_TARGET,test_entity_replay,$(ENTITY_FEATURE_SRC) src/presentation/creature_renderer.c src/ecology/creature_visual.c src/ecology/fauna_motion.c src/ecology/fauna_behavior.c src/ecology/evolution.c src/ecology/ecology_model.c $(GAME_EFFECTS_SRC),,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),))

ENTITY_ECOLOGY_SOURCES := \
	tests/ecology_test_fixture.c \
	$(ENTITY_FEATURE_SRC) \
	src/presentation/creature_renderer.c \
	src/ecology/creature_visual.c \
	src/ecology/fauna_motion.c \
	src/ecology/fauna_behavior.c \
	src/ecology/evolution.c \
	$(SPACE_FEATURE_SRC) \
	src/space/solar_catalog.c \
	src/space/space_query_cache.c \
	src/space/planet_profile.c \
	src/space/space_system_physics.c \
	src/space/space_system.c \
	src/space/space_barycenter.c \
	src/space/space_orbit.c \
	src/space/space_remnant.c \
	src/space/space_illumination.c \
	src/space/planet_climate.c \
	src/space/planet_surface.c \
	src/space/space_physics.c \
	src/space/space_satellite.c \
	src/space/space_units.c \
	src/space/stellar.c \
	$(TERRAIN_FEATURE_SRC) \
	$(BLOCK_ATLAS_FEATURE_SRC) \
	$(CHUNKS_FEATURE_SRC) \
	src/ecology/ecology.c \
	src/ecology/ecology_profile.c \
	src/ecology/ecology_population.c \
	src/ecology/ecology_flora.c \
	src/ecology/ecology_model.c \
	src/world/weather.c \
	src/world/weather_model.c \
	src/world/local_climate.c \
	$(GAME_EFFECTS_SRC)
$(eval $(call define_test,ENTITY_ECOLOGY_TEST_TARGET,test_entity_ecology,$(ENTITY_ECOLOGY_SOURCES),-Itests,-DENTITY_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),))

$(eval $(call define_test,INTERACTION_RAYCAST_TEST_TARGET,test_interaction_raycast,src/gameplay/interaction.c,,-ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),))
$(eval $(call define_test,PLANET_RENDERER_RESOURCES_TEST_TARGET,test_planet_renderer_resources,src/presentation/planet_renderer.c,,-DPLANET_RENDERER_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
$(eval $(call define_test,PLANET_TEXTURE_RESOURCES_TEST_TARGET,test_planet_texture_resources,src/presentation/render_planets.c src/presentation/render_sky.c src/space/planet_material.c src/space/space_units.c,,-DRENDER_PLANETS_TESTING -ffunction-sections -fdata-sections,$(GC_SECTIONS_LDFLAG),-lm -pthread,$(RAYLIB_CFLAGS),$(RAYLIB_LIBS)))
