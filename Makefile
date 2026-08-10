CC ?= cc
CFLAGS ?= -std=c99 -Wall -Wextra -O2 -pthread
PKG_CONFIG ?= pkg-config
RAYLIB_CFLAGS := $(shell $(PKG_CONFIG) --cflags raylib)
RAYLIB_LIBS := $(shell $(PKG_CONFIG) --libs raylib)

TARGET := voxelcraft
TEST_TARGET := tests/test_world_environment
PLANET_SURFACE_TEST_TARGET := tests/test_planet_surface
PLANET_MATERIAL_TEST_TARGET := tests/test_planet_material
PLANET_CLIMATE_TEST_TARGET := tests/test_planet_climate
SPACE_PHYSICS_TEST_TARGET := tests/test_space_physics
SPACE_BARYCENTER_TEST_TARGET := tests/test_space_barycenter
SPACE_SATELLITE_TEST_TARGET := tests/test_space_satellite
SPACE_UNITS_TEST_TARGET := tests/test_space_units
SPACE_PROPERTIES_TEST_TARGET := tests/test_space_properties
STELLAR_TEST_TARGET := tests/test_stellar
ECOLOGY_MODEL_TEST_TARGET := tests/test_ecology_model
CHUNK_ATLAS_TEST_TARGET := tests/test_chunk_atlas
SRC := src/main.c src/album.c src/inventory.c src/space.c src/space_barycenter.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/world_environment.c src/ship.c src/nether.c src/entity.c src/ecology_model.c src/ecology.c src/terrain.c src/planet_climate.c src/planet_surface.c src/planet_material.c src/planet_renderer.c src/discovery.c src/chunks.c src/world.c src/player.c src/interaction.c src/render.c src/particles.c src/audio.c src/weather.c src/starmap.c

.PHONY: all run test clean

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $@ $(SRC) $(RAYLIB_LIBS) -lm -pthread

run: $(TARGET)
	./$(TARGET)

test: $(TEST_TARGET) $(PLANET_SURFACE_TEST_TARGET) $(PLANET_MATERIAL_TEST_TARGET) $(PLANET_CLIMATE_TEST_TARGET) $(SPACE_PHYSICS_TEST_TARGET) $(SPACE_BARYCENTER_TEST_TARGET) $(SPACE_SATELLITE_TEST_TARGET) $(SPACE_UNITS_TEST_TARGET) $(SPACE_PROPERTIES_TEST_TARGET) $(STELLAR_TEST_TARGET) $(ECOLOGY_MODEL_TEST_TARGET) $(CHUNK_ATLAS_TEST_TARGET)
	./$(TEST_TARGET)
	./$(PLANET_SURFACE_TEST_TARGET)
	./$(PLANET_MATERIAL_TEST_TARGET)
	./$(PLANET_CLIMATE_TEST_TARGET)
	./$(SPACE_PHYSICS_TEST_TARGET)
	./$(SPACE_BARYCENTER_TEST_TARGET)
	./$(SPACE_SATELLITE_TEST_TARGET)
	./$(SPACE_UNITS_TEST_TARGET)
	./$(SPACE_PROPERTIES_TEST_TARGET)
	./$(STELLAR_TEST_TARGET)
	./$(ECOLOGY_MODEL_TEST_TARGET)
	./$(CHUNK_ATLAS_TEST_TARGET)

$(TEST_TARGET): tests/test_world_environment.c src/world_environment.c src/world_environment.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_world_environment.c src/world_environment.c -lm

$(PLANET_SURFACE_TEST_TARGET): tests/test_planet_surface.c src/planet_surface.c src/planet_surface.h src/space.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_surface.c src/planet_surface.c -lm

$(PLANET_MATERIAL_TEST_TARGET): tests/test_planet_material.c src/planet_material.c src/planet_material.h src/planet_surface.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_planet_material.c src/planet_material.c -lm

$(PLANET_CLIMATE_TEST_TARGET): tests/test_planet_climate.c src/planet_climate.c src/planet_climate.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_planet_climate.c src/planet_climate.c -lm

$(SPACE_PHYSICS_TEST_TARGET): tests/test_space_physics.c src/space_physics.c src/space_physics.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_physics.c src/space_physics.c -lm

$(SPACE_BARYCENTER_TEST_TARGET): tests/test_space_barycenter.c src/space_barycenter.c src/space_barycenter.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_space_barycenter.c src/space_barycenter.c src/space_units.c -lm

$(SPACE_SATELLITE_TEST_TARGET): tests/test_space_satellite.c src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_satellite.c src/space_satellite.c src/space_units.c -lm

$(SPACE_UNITS_TEST_TARGET): tests/test_space_units.c src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_space_units.c src/space_units.c -lm

$(SPACE_PROPERTIES_TEST_TARGET): tests/test_space_properties.c src/space.c src/space.h src/space_barycenter.c src/space_barycenter.h src/planet_climate.c src/planet_climate.h src/space_physics.c src/space_physics.h src/space_satellite.c src/space_satellite.h src/space_units.c src/space_units.h src/stellar.c src/stellar.h src/terrain.c src/terrain.h src/chunks.c src/chunks.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_space_properties.c src/space.c src/space_barycenter.c src/planet_climate.c src/space_physics.c src/space_satellite.c src/space_units.c src/stellar.c src/terrain.c src/chunks.c -lm -pthread

$(STELLAR_TEST_TARGET): tests/test_stellar.c src/stellar.c src/stellar.h src/space_units.c src/space_units.h
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -Isrc -o $@ tests/test_stellar.c src/stellar.c src/space_units.c -lm

$(ECOLOGY_MODEL_TEST_TARGET): tests/test_ecology_model.c src/ecology_model.c src/ecology_model.h
	$(CC) $(CFLAGS) -Isrc -o $@ tests/test_ecology_model.c src/ecology_model.c -lm

$(CHUNK_ATLAS_TEST_TARGET): tests/test_chunk_atlas.c src/chunks.c src/chunks.h src/types.h src/world.c src/world.h
	$(CC) $(CFLAGS) -ffunction-sections -fdata-sections $(RAYLIB_CFLAGS) -Isrc -Wl,--gc-sections -o $@ tests/test_chunk_atlas.c src/chunks.c src/world.c $(RAYLIB_LIBS) -lm -pthread

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(PLANET_SURFACE_TEST_TARGET) $(PLANET_MATERIAL_TEST_TARGET) $(PLANET_CLIMATE_TEST_TARGET) $(SPACE_PHYSICS_TEST_TARGET) $(SPACE_BARYCENTER_TEST_TARGET) $(SPACE_SATELLITE_TEST_TARGET) $(SPACE_UNITS_TEST_TARGET) $(SPACE_PROPERTIES_TEST_TARGET) $(STELLAR_TEST_TARGET) $(ECOLOGY_MODEL_TEST_TARGET) $(CHUNK_ATLAS_TEST_TARGET)
