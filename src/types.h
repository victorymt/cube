#ifndef VOXELCRAFT_TYPES_H
#define VOXELCRAFT_TYPES_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define WORLD_HEIGHT 32
#define CHUNK_SIZE 16
#define MIN_RENDER_DISTANCE_CHUNKS 2
#define DEFAULT_RENDER_DISTANCE_CHUNKS 10
#define MAX_RENDER_DISTANCE_CHUNKS 12
#define MAX_ACTIVE_CHUNKS ((MAX_RENDER_DISTANCE_CHUNKS * 2 + 1) * (MAX_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define CHUNK_GEN_SUBMISSIONS_PER_FRAME 32
#define MAX_CHUNK_GEN_JOBS 64
#define MAX_MESH_REBUILDS_PER_FRAME 10
#define INITIAL_BLOCK_EDIT_CAPACITY 1024
#define BLOCK_SIZE 1.0f
#define HOTBAR_SIZE 10
#define ATLAS_TILE_SIZE 16
#define ATLAS_COLUMNS 32
#define ATLAS_ROWS ((TEX_COUNT + ATLAS_COLUMNS - 1) / ATLAS_COLUMNS)
#define COLOR_BLOCK_COUNT 256
#define IMPORT_MIN_BLOCKS 16
#define IMPORT_DEFAULT_BLOCKS 64
#define IMPORT_PRECISION_STEP 16
#define IMPORT_PRECISION_BIG_STEP 64
#define SAVE_FILE "voxelcraft_save.txt"
#define AUTO_SAVE_INTERVAL_SECONDS 60.0f
#define DAY_LENGTH_SECONDS 240.0f
#define DEFAULT_WORLD_SEED 0x564f5843u
#define SUN_DISTANCE 420.0f
#define CLOUD_COUNT 8
#define CLOUD_SPAN 500.0f
#define CLOUD_DRIFT 0.8f
#define CLOUD_BASE_HEIGHT 40.0f

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

#define SPACE_LAYER_Y 100
#define SPACE_LAYER_HEIGHT 128
#define SPACE_LAYER_TOP (SPACE_LAYER_Y + SPACE_LAYER_HEIGHT)
#define SPACE_ENTER_Y 120.0f
#define SPACE_EXIT_Y 80.0f
#define SPACE_RENDER_DISTANCE_CHUNKS 4

#define NETHER_LAYER_Y (-64)
#define NETHER_LAYER_TOP (-32)
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

typedef struct Chunk {
    bool loaded;
    bool dirty;
    bool generating;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
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
