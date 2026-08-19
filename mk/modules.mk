CORE_SRC := $(sort $(wildcard src/core/*.c))
WORLD_SRC := $(sort $(wildcard src/world/*.c))
SPACE_SRC := $(sort $(wildcard src/space/*.c))
ECOLOGY_SRC := $(sort $(wildcard src/ecology/*.c))
GAMEPLAY_SRC := $(sort $(wildcard src/gameplay/*.c))
PRESENTATION_SRC := $(sort $(wildcard src/presentation/*.c))
APP_SRC := $(sort $(wildcard src/app/*.c))

SPACE_FEATURE_SRC := \
	src/space/space.c \
	src/space/space_state.c \
	src/space/space_runtime.c \
	src/space/space_query.c \
	src/space/space_chunks.c \
	src/space/planet_world.c \
	src/space/planet_lighting.c

CHUNKS_FEATURE_SRC := \
	src/world/chunks.c \
	src/world/chunks_test_api.c \
	src/world/chunks_storage.c \
	src/world/chunks_streaming.c \
	src/world/chunks_ecology.c \
	src/world/chunks_mesh.c \
	src/world/chunks_water.c \
	src/world/chunks_flora.c \
	src/world/chunks_runtime.c \
	src/world/surface_topology.c

ENTITY_FEATURE_SRC := \
	src/ecology/entity.c \
	src/ecology/entity_simulation.c \
	src/ecology/entity_spawn.c \
	src/world/tornado_model.c

BLOCK_ATLAS_FEATURE_SRC := \
	src/world/block_atlas.c \
	src/world/block_atlas_expansion.c \
	src/world/block_atlas_ecology.c \
	src/world/block_atlas_geology.c \
	src/world/block_atlas_items.c \
	src/world/block_atlas_space.c

GAME_EFFECTS_SRC := src/core/game_effects.c

TERRAIN_FEATURE_SRC := \
	src/world/terrain.c \
	src/world/terrain_geology.c \
	src/world/terrain_home_materials.c \
	src/world/terrain_structures.c \
	src/world/subsurface.c

MODULE_SRC := \
	$(CORE_SRC) \
	$(WORLD_SRC) \
	$(SPACE_SRC) \
	$(ECOLOGY_SRC) \
	$(GAMEPLAY_SRC) \
	$(PRESENTATION_SRC) \
	$(APP_SRC)
