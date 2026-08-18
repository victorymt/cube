#ifndef VOXELCRAFT_WORLD_TYPES_H
#define VOXELCRAFT_WORLD_TYPES_H

#include "core/config.h"
#include "world/surface_topology.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum BlockType {
    BLOCK_AIR = 0,
    BLOCK_GRASS,
    BLOCK_DIRT,
    BLOCK_STONE,
    BLOCK_WOOD,
    BLOCK_SAND,
    BLOCK_LEAVES,
    BLOCK_RED,
    BLOCK_ORANGE,
    BLOCK_YELLOW,
    BLOCK_BLUE,
    BLOCK_PURPLE,
    BLOCK_GREEN,
    BLOCK_CYAN,
    BLOCK_PINK,
    BLOCK_WHITE,
    BLOCK_GRAY,
    BLOCK_BLACK,
    BLOCK_PLANK,
    BLOCK_BRICK,
    BLOCK_GLASS,
    BLOCK_WATER,
    BLOCK_SNOW,
    BLOCK_ICE,
    BLOCK_CACTUS,
    BLOCK_BEDROCK,
    BLOCK_COAL_ORE,
    BLOCK_IRON_ORE,
    BLOCK_GOLD_ORE,
    BLOCK_DIAMOND_ORE,
    BLOCK_TORCH,
    BLOCK_ALBUM,
    BLOCK_SLAB,
    BLOCK_DOOR,
    BLOCK_DOOR_OPEN,
    BLOCK_MOON_ROCK,
    BLOCK_METEORITE,
    BLOCK_MOON_SAND,
    BLOCK_STAR_MATTER,
    BLOCK_SPACESHIP,
    BLOCK_STONE_STAIRS,
    BLOCK_WOOD_STAIRS,
    BLOCK_FENCE,
    BLOCK_FENCE_GATE,
    BLOCK_FENCE_GATE_OPEN,
    BLOCK_GLASS_PANE,
    BLOCK_LAVA,
    BLOCK_FLOWER,
    BLOCK_MUSHROOM,
    BLOCK_BOOKSHELF,
    BLOCK_HAY_BALE,
    BLOCK_PUMPKIN,
    BLOCK_NETHERRACK,
    BLOCK_SOUL_SAND,
    BLOCK_GLOWSTONE,
    BLOCK_STONE_BRICKS,
    BLOCK_SANDSTONE,
    BLOCK_OBSIDIAN,
    BLOCK_NETHER_PORTAL,
    BLOCK_SPACESHIP_CORE_NORTH,
    BLOCK_SPACESHIP_CORE_EAST,
    BLOCK_SPACESHIP_CORE_SOUTH,
    BLOCK_SPACESHIP_CORE_WEST,
    BLOCK_SPACESHIP_OCCUPIED,
    BLOCK_GRAVEL,
    BLOCK_CLAY,
    BLOCK_MUD,
    BLOCK_MOSSY_STONE,
    BLOCK_RED_SAND,
    BLOCK_BASALT,
    BLOCK_COPPER_ORE,
    BLOCK_CRYSTAL,
    BLOCK_GRANITE,
    BLOCK_LIMESTONE,
    BLOCK_SHALE,
    BLOCK_MARBLE,
    BLOCK_PEAT,
    BLOCK_PERMAFROST,
    BLOCK_ROCK_SALT,
    BLOCK_VOLCANIC_ASH,
    BLOCK_PUMICE,
    BLOCK_SULFUR_ORE,
    BLOCK_PACKED_ICE,
    BLOCK_QUARTZ_ORE,
    BLOCK_LOAM,
    BLOCK_PODZOL,
    BLOCK_SILT,
    BLOCK_CHALK,
    BLOCK_GNEISS,
    BLOCK_LATERITE,
    BLOCK_SCORIA,
    BLOCK_REGOLITH,
    BLOCK_SALT_CRUST,
    BLOCK_TIN_ORE,
    BLOCK_SILVER_ORE,
    BLOCK_NICKEL_ORE,
    BLOCK_TALL_GRASS,
    BLOCK_FERN,
    BLOCK_REED,
    BLOCK_MOSS_CARPET,
    BLOCK_LICHEN,
    BLOCK_MICROBIAL_MAT,
    BLOCK_MYCELIUM,
    BLOCK_LIVING_STEM,
    BLOCK_CANOPY_FROND,
    BLOCK_LUMINOUS_POD,
    BLOCK_FUNGAL_STEM,
    BLOCK_SPORE_CAP,
    BLOCK_CRYSTAL_BLOOM,
    BLOCK_VENT_CHIMNEY,
    BLOCK_CHEMO_MAT,
    BLOCK_NATURAL_START = BLOCK_GRAVEL,
    BLOCK_NATURAL_END = BLOCK_CHEMO_MAT,
    BLOCK_COLOR_START = 256,
    BLOCK_COLOR_END = BLOCK_COLOR_START + COLOR_BLOCK_COUNT - 1
} BlockType;

typedef enum BlockTexture {
    TEX_GRASS_TOP = 0,
    TEX_GRASS_SIDE,
    TEX_DIRT,
    TEX_STONE,
    TEX_WOOD_SIDE,
    TEX_WOOD_TOP,
    TEX_SAND,
    TEX_LEAVES,
    TEX_RED,
    TEX_ORANGE,
    TEX_YELLOW,
    TEX_BLUE,
    TEX_PURPLE,
    TEX_GREEN,
    TEX_CYAN,
    TEX_PINK,
    TEX_WHITE,
    TEX_GRAY,
    TEX_BLACK,
    TEX_PLANK,
    TEX_BRICK,
    TEX_GLASS,
    TEX_WATER,
    TEX_SNOW,
    TEX_ICE,
    TEX_CACTUS,
    TEX_BEDROCK,
    TEX_COAL_ORE,
    TEX_IRON_ORE,
    TEX_GOLD_ORE,
    TEX_DIAMOND_ORE,
    TEX_TORCH,
    TEX_ALBUM,
    TEX_DOOR,
    TEX_MOON_ROCK,
    TEX_METEORITE,
    TEX_MOON_SAND,
    TEX_STAR_MATTER,
    TEX_SPACESHIP,
    TEX_FENCE,
    TEX_LAVA,
    TEX_FLOWER,
    TEX_MUSHROOM,
    TEX_BOOKSHELF,
    TEX_HAY,
    TEX_PUMPKIN,
    TEX_NETHERRACK,
    TEX_SOUL_SAND,
    TEX_GLOWSTONE,
    TEX_STONE_BRICKS,
    TEX_SANDSTONE,
    TEX_OBSIDIAN,
    TEX_NETHER_PORTAL,
    TEX_COLOR_START,
    TEX_COLOR_END = TEX_COLOR_START + COLOR_BLOCK_COUNT - 1,
    TEX_GRAVEL = TEX_COLOR_END + 1,
    TEX_CLAY,
    TEX_MUD,
    TEX_MOSSY_STONE,
    TEX_RED_SAND,
    TEX_BASALT,
    TEX_COPPER_ORE,
    TEX_CRYSTAL,
    TEX_GRANITE,
    TEX_LIMESTONE,
    TEX_SHALE,
    TEX_MARBLE,
    TEX_PEAT,
    TEX_PERMAFROST,
    TEX_ROCK_SALT,
    TEX_VOLCANIC_ASH,
    TEX_PUMICE,
    TEX_SULFUR_ORE,
    TEX_PACKED_ICE,
    TEX_QUARTZ_ORE,
    TEX_LOAM,
    TEX_PODZOL,
    TEX_SILT,
    TEX_CHALK,
    TEX_GNEISS,
    TEX_LATERITE,
    TEX_SCORIA,
    TEX_REGOLITH,
    TEX_SALT_CRUST,
    TEX_TIN_ORE,
    TEX_SILVER_ORE,
    TEX_NICKEL_ORE,
    TEX_TALL_GRASS,
    TEX_FERN,
    TEX_REED,
    TEX_MOSS_CARPET,
    TEX_LICHEN,
    TEX_MICROBIAL_MAT,
    TEX_MYCELIUM,
    TEX_LIVING_STEM,
    TEX_CANOPY_FROND,
    TEX_LUMINOUS_POD,
    TEX_FUNGAL_STEM,
    TEX_SPORE_CAP,
    TEX_CRYSTAL_BLOOM,
    TEX_VENT_CHIMNEY,
    TEX_CHEMO_MAT,
    TEX_COUNT
} BlockTexture;

typedef enum BlockRenderShape {
    BLOCK_RENDER_CUBE = 0,
    BLOCK_RENDER_CROSS,
    BLOCK_RENDER_CARPET
} BlockRenderShape;

typedef enum TerrainMode {
    TERRAIN_VARIED = 0,
    TERRAIN_FLAT
} TerrainMode;

typedef enum ChunkGenScope {
    CHUNK_GEN_SCOPE_COLUMN = 0,
    CHUNK_GEN_SCOPE_SECTION
} ChunkGenScope;

typedef enum Biome {
    BIOME_PLAINS = 0,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_SNOW,
    BIOME_MOUNTAIN
} Biome;

typedef struct ChunkGenJob {
    bool inUse;
    bool running;
    bool done;
    bool succeeded;
    bool hasSectionBlocks;
    ChunkGenScope scope;
    int cx;
    int cz;
    int sectionY;
    int slotIndex;
    uint32_t chunkGeneration;
    bool spherical;
    SurfaceAddress surfaceAddress;
    TerrainMode terrainMode;
    uint64_t queueSequence;
    double submittedAtMs;
    double startedAtMs;
    double completedAtMs;
    // Section jobs generate into staging storage because their target chunk
    // is already visible to the main/render thread. Completion copies this
    // snapshot only if the chunk incarnation still matches.
    unsigned short sectionBlocks
        [CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
} ChunkGenJob;

typedef struct FloraVisualInstance {
    int firstVertex;
    int vertexCount;
    Vector3 anchor;
    float height;
    float windResponse;
} FloraVisualInstance;

typedef enum FloraStructureKind {
    FLORA_STRUCTURE_ALIEN_CANOPY = 0,
    FLORA_STRUCTURE_CRYSTAL,
    FLORA_STRUCTURE_SPORE,
    FLORA_STRUCTURE_THERMAL_VENT
} FloraStructureKind;

typedef struct FloraStructureInstance {
    FloraStructureKind kind;
    uint32_t shapeHash;
    int rootX;
    int groundY;
    int rootZ;
    int minX;
    int minY;
    int minZ;
    int maxX;
    int maxY;
    int maxZ;
    BlockType primaryBlock;
    BlockType accentBlock;
    float windResponse;
} FloraStructureInstance;

typedef struct ChunkSection {
    bool dirty;
    // Monotonic content revision bumped by every MarkChunkDirty* call.
    // Mesh jobs record it at snapshot time so stale uploads do not clear
    // dirty flags that were re-set after the snapshot (lost-edit race).
    uint32_t dirtyStamp;
    double dirtySinceMs;
    bool hasModel;
    bool hasWaterModel;
    bool hasFloraModel;
    int sectionY;
    Model model;
    Model waterModel;
    Model floraModel;
    float floraVisualScale;
    float *floraTargetScales;
    float *floraTargetWind;
    float *floraTargetWindAngle;
    float *floraTargetPresence;
    float *floraBaseVertices;
    unsigned char *floraBaseColors;
    FloraVisualInstance *floraVisualInstances;
    int floraTargetScaleCount;
    unsigned char *waterVolumes;
    unsigned char *fluidQueuedBits;
    unsigned char *fluidDeferredBits;
    signed char *fluidFlow;
    bool fluidDirty;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
} ChunkSection;

typedef struct Chunk {
    bool loaded;
    bool generating;
    int cx;
    int cz;
    bool spherical;
    SurfaceAddress surfaceAddress;
    // Incarnation counter bumped on every slot reuse (EnsureChunk).
    // In-flight mesh jobs from a previous incarnation are discarded on
    // upload so stale terrain never overwrites a freshly generated chunk.
    uint32_t generation;
    float floraActivity;
    float floraCapacity;
    float floraSampleTimer;
    float floraWindAngle;
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    // Materialized vertical sections, sorted by signed sectionY. The logical
    // world height must not determine the size of every resident XZ column.
    ChunkSection **sections;
    int sectionCount;
    int sectionCapacity;
    // Sorted Section Y values whose procedural baseline has been evaluated.
    // This records all-air results without allocating a full ChunkSection.
    int *resolvedTerrainSectionYs;
    int resolvedTerrainSectionCount;
    int resolvedTerrainSectionCapacity;
} Chunk;

typedef struct BlockEdit {
    int x;
    int y;
    int z;
    BlockType type;
} BlockEdit;

typedef struct BlockEditIndex {
    int x;
    int y;
    int z;
    uint32_t dimension;
    int editIndex;
    bool used;
} BlockEditIndex;

#endif
