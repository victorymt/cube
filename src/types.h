#ifndef VOXELCRAFT_TYPES_H
#define VOXELCRAFT_TYPES_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define CHUNK_SIZE 16
#define SURFACE_WORLD_HEIGHT 256
#define SURFACE_SECTION_HEIGHT 16
#define SURFACE_SECTION_COUNT (SURFACE_WORLD_HEIGHT / SURFACE_SECTION_HEIGHT)
// WORLD_HEIGHT remains the public logical surface height for older call sites.
#define WORLD_HEIGHT SURFACE_WORLD_HEIGHT
#define MIN_RENDER_DISTANCE_CHUNKS 2
#define DEFAULT_RENDER_DISTANCE_CHUNKS 10
#define MAX_RENDER_DISTANCE_CHUNKS 12
#define MAX_ACTIVE_CHUNKS ((MAX_RENDER_DISTANCE_CHUNKS * 2 + 1) * (MAX_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define CHUNK_GEN_SUBMISSIONS_PER_FRAME 32
#define MAX_CHUNK_GEN_JOBS 64
#define MAX_CHUNK_FLORA_STRUCTURES 32
#define MAX_MESH_REBUILDS_PER_FRAME 10
#define INITIAL_BLOCK_EDIT_CAPACITY 1024
#define BLOCK_SIZE 1.0f
#define HOTBAR_SIZE 10
#define ATLAS_TILE_SIZE 16
#define ATLAS_TILE_PADDING 8
#define ATLAS_CELL_SIZE (ATLAS_TILE_SIZE + ATLAS_TILE_PADDING * 2)
#define ATLAS_COLUMNS 32
#define ATLAS_ROWS ((TEX_COUNT + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS)
#define COLOR_BLOCK_COUNT 256
#define IMPORT_MIN_BLOCKS 16
#define IMPORT_DEFAULT_BLOCKS 64
#define IMPORT_MAX_BLOCKS 256
#define IMPORT_MAX_FILE_BYTES (64 * 1024 * 1024)
#define IMPORT_MAX_SOURCE_DIMENSION 8192
#define IMPORT_MAX_SOURCE_PIXELS 33554432u
#define IMPORT_MAX_TARGET_PIXELS 65536u
#define IMPORT_MAX_BLOCK_OPERATIONS \
    (64u * 64u * (unsigned int)SURFACE_WORLD_HEIGHT * 2u)
#define IMPORT_PRECISION_STEP 16
#define IMPORT_PRECISION_BIG_STEP 64
#define SAVE_FILE "voxelcraft_save.txt"
#define AUTO_SAVE_INTERVAL_SECONDS 60.0f
#define WATER_VOLUME_CAPACITY 255u
#define DAY_LENGTH_SECONDS 240.0f
#define DEFAULT_WORLD_SEED 0x564f5843u
#define SUN_DISTANCE 420.0f
#define PLAYER_HEIGHT 1.75f
#define PLAYER_RADIUS 0.28f
#define EYE_HEIGHT 1.55f
#define GRAVITY 20.0f
#define JUMP_SPEED 7.0f
#define WALK_SPEED 5.0f
#define SPRINT_SPEED 8.0f
#define FLOAT_VERTICAL_SPEED 5.5f
#define MOUSE_SENSITIVITY 0.0022f
#define REACH_DISTANCE 6.0f
#define CAMERA_MIN_FOV 70.0f
#define CAMERA_MAX_FOV 90.0f
#define CAMERA_FOV_MIN_HEIGHT 4.0f
#define CAMERA_FOV_MAX_HEIGHT 28.0f
#define CAMERA_FOV_SMOOTHING 8.0f
#define CAMERA_MAX_EXTRA_DISTANCE_CHUNKS 2
#define CAMERA_NEAR_CULL_DISTANCE 0.1f

#define SPACE_LAYER_Y (SURFACE_WORLD_HEIGHT + 64)
#define SPACE_LAYER_HEIGHT 128
#define SPACE_LAYER_TOP (SPACE_LAYER_Y + SPACE_LAYER_HEIGHT)
#define SPACE_ENTER_Y ((float)SPACE_LAYER_Y + 24.0f)
#define SPACE_EXIT_Y ((float)SURFACE_WORLD_HEIGHT + 48.0f)
#define SPACE_RENDER_DISTANCE_CHUNKS 4

#define NETHER_LAYER_Y (-64)
#define NETHER_LAYER_TOP (-32)
#define NETHER_HEIGHT (NETHER_LAYER_TOP - NETHER_LAYER_Y)
#define NETHER_RENDER_DISTANCE_CHUNKS 4

#define MAX_TORCH_LIGHTS 512
#define TORCH_LIGHT_RADIUS 6.0f
#define TORCH_LIGHT_STRENGTH 0.85f

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
    TEX_COUNT = TEX_COLOR_START + COLOR_BLOCK_COUNT
} BlockTexture;

typedef enum GameScreen {
    SCREEN_START = 0,
    SCREEN_PLAYING
} GameScreen;

typedef enum TerrainMode {
    TERRAIN_VARIED = 0,
    TERRAIN_FLAT
} TerrainMode;

typedef enum Biome {
    BIOME_PLAINS = 0,
    BIOME_FOREST,
    BIOME_DESERT,
    BIOME_SNOW,
    BIOME_MOUNTAIN
} Biome;

typedef struct ChunkGenJob {
    bool inUse;
    bool done;
    int cx;
    int cz;
    int slotIndex;
    TerrainMode terrainMode;
} ChunkGenJob;

typedef struct Player {
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float pitch;
    bool onGround;
    bool floating;
    bool wasInWater;
    float stepTimer;
} Player;

typedef struct HitResult {
    bool hit;
    int x;
    int y;
    int z;
    int nx;
    int ny;
    int nz;
} HitResult;

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
    ChunkSection *sections[SURFACE_SECTION_COUNT];
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

typedef struct ImportDialog {
    bool open;
    bool relief;
    int maxBlocks;
    char path[1024];
} ImportDialog;

#endif
