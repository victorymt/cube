#define _POSIX_C_SOURCE 200809L
#include "world.h"

#include "raymath.h"
#include "chunks.h"
#include "player.h"
#include "space.h"
#include "nether.h"
#include "album.h"
#include "inventory.h"
#include "ship.h"
#include "entity.h"
#include "ecology.h"
#include "evolution_catalog.h"
#include "terrain.h"
#include "world_environment.h"
#include "world_extension.h"
#include "save_io.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_FILE_BAK "voxelcraft_save.bak"
#define SAVE_MAGIC_V15 "VOXELCRAFT_SAVE_V15"
#define SAVE_MAGIC_V15_LEN (sizeof(SAVE_MAGIC_V15) - 1)
#define SAVE_MAGIC_V14 "VOXELCRAFT_SAVE_V14"
#define SAVE_MAGIC_V14_LEN (sizeof(SAVE_MAGIC_V14) - 1)
#define SAVE_MAGIC_V13 "VOXELCRAFT_SAVE_V13"
#define SAVE_MAGIC_V13_LEN (sizeof(SAVE_MAGIC_V13) - 1)
#define TERRAIN_GENERATION_VERSION 3u
#define MIN_SUPPORTED_TERRAIN_GENERATION_VERSION 2u
#define LEGACY_SPACE_LAYER_Y 100
#define SAVE_MAGIC_V12 "VOXELCRAFT_SAVE_V12"
#define SAVE_MAGIC_V12_LEN (sizeof(SAVE_MAGIC_V12) - 1)
#define SAVE_MAGIC_V11 "VOXELCRAFT_SAVE_V11"
#define SAVE_MAGIC_V11_LEN (sizeof(SAVE_MAGIC_V11) - 1)
#define SAVE_MAGIC_V10 "VOXELCRAFT_SAVE_V10"
#define SAVE_MAGIC_V10_LEN (sizeof(SAVE_MAGIC_V10) - 1)
#define SAVE_MAGIC_V9 "VOXELCRAFT_SAVE_V9"
#define SAVE_MAGIC_V9_LEN (sizeof(SAVE_MAGIC_V9) - 1)
#define SAVE_MAGIC_V8 "VOXELCRAFT_SAVE_V8"
#define SAVE_MAGIC_V8_LEN (sizeof(SAVE_MAGIC_V8) - 1)
#define SAVE_MAGIC_V7 "VOXELCRAFT_SAVE_V7"
#define SAVE_MAGIC_V7_LEN (sizeof(SAVE_MAGIC_V7) - 1)
#define SAVE_MAGIC_V6 "VOXELCRAFT_SAVE_V6"
#define SAVE_MAGIC_V6_LEN (sizeof(SAVE_MAGIC_V6) - 1)
#define SAVE_MAGIC_V5 "VOXELCRAFT_SAVE_V5"
#define SAVE_MAGIC_V5_LEN (sizeof(SAVE_MAGIC_V5) - 1)
#define SAVE_MAGIC_V4 "VOXELCRAFT_SAVE_V4"
#define SAVE_MAGIC_V4_LEN (sizeof(SAVE_MAGIC_V4) - 1)
#define SAVE_MAGIC_V3 "VOXELCRAFT_SAVE_V3"
#define SAVE_MAGIC_V3_LEN (sizeof(SAVE_MAGIC_V3) - 1)
#define SAVE_MAGIC_V2_PREFIX "VOXELCRAFT_SAVE_V2"
#define SAVE_MAGIC_V2_PREFIX_LEN 17
#define LEGACY_PLANET_REGION_RADIUS 1024
#define SAVE_MAX_FILE_BYTES (256u * 1024u * 1024u)
// Upper bound for edit counts read from save files. Keeps transient
// allocations (edits + dimensions + the edit-index hash) bounded for
// crafted/corrupt files; normal saves are far below this.
#define MAX_LOAD_EDIT_COUNT 1000000u

#include "raymath.h"
#include "chunks.h"
#include "player.h"
#include "space.h"
#include "nether.h"
#include "album.h"
BlockEdit *blockEdits = NULL;
uint32_t *blockEditDimensions = NULL;
BlockEditIndex *blockEditIndex = NULL;
int blockEditCount = 0;
int blockEditCapacity = 0;
static uint64_t blockEditRevision = 1u;
static uint32_t worldSeed = DEFAULT_WORLD_SEED;
static TerrainMode worldTerrainMode = TERRAIN_VARIED;
static WorldExtensionHooks worldExtensionHooks = { 0 };

void WorldInstallExtensionHooks(const WorldExtensionHooks *hooks)
{
    worldExtensionHooks = hooks ? *hooks : (WorldExtensionHooks){ 0 };
}

static void WorldExtensionReset(void)
{
    if (worldExtensionHooks.reset) worldExtensionHooks.reset();
}

static void WorldExtensionCleanup(void)
{
    if (worldExtensionHooks.cleanup) worldExtensionHooks.cleanup();
}

static bool WorldExtensionSaveState(FILE *file)
{
    return worldExtensionHooks.saveState &&
           worldExtensionHooks.saveState(file);
}

static bool WorldExtensionLoadState(FILE *file)
{
    return worldExtensionHooks.loadState &&
           worldExtensionHooks.loadState(file);
}

static bool WorldExtensionTryDisplaceBlock(
    int x, int y, int z, FluidBlockDisplacement *outDisplacement)
{
    return !worldExtensionHooks.tryDisplaceBlock ||
           worldExtensionHooks.tryDisplaceBlock(
               x, y, z, outDisplacement);
}

static bool WorldExtensionReplayBlockDisplacement(
    const FluidBlockDisplacement *displacement, bool after)
{
    if (!displacement || displacement->count == 0u) return true;
    return worldExtensionHooks.replayBlockDisplacement &&
           worldExtensionHooks.replayBlockDisplacement(displacement, after);
}

static void WorldExtensionOnBlockChanged(
    int x, int y, int z, BlockType previous, BlockType next)
{
    if (worldExtensionHooks.onBlockChanged) {
        worldExtensionHooks.onBlockChanged(x, y, z, previous, next);
    }
}

void WorldNotifyChunkLoaded(Chunk *chunk)
{
    if (worldExtensionHooks.onChunkLoaded) {
        worldExtensionHooks.onChunkLoaded(chunk);
    }
}

static void BumpBlockEditRevision(void)
{
    blockEditRevision++;
    if (blockEditRevision == 0u) blockEditRevision = 1u;
}

uint32_t WorldGetSeed(void)
{
    return worldSeed;
}

void WorldSetSeed(uint32_t seed)
{
    worldSeed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
}

TerrainMode WorldTerrainMode(void)
{
    return worldTerrainMode;
}

void WorldSetTerrainMode(TerrainMode mode)
{
    worldTerrainMode = mode;
}

static uint32_t WorldCurrentEditDimension(void)
{
    return WorldCurrentSurfaceId();
}
int blockEditIndexCapacity = 0;
char importMessage[160] = "Flat mode: press I to import an image path.";
float importMessageTimer = 8.0f;
const char *BlockName(BlockType type)
{
    if (type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END) return TextFormat("Color %03d", (int)type - BLOCK_COLOR_START);

    switch (type) {
    case BLOCK_GRASS: return "Grass";
    case BLOCK_DIRT: return "Dirt";
    case BLOCK_STONE: return "Stone";
    case BLOCK_WOOD: return "Wood";
    case BLOCK_SAND: return "Sand";
    case BLOCK_LEAVES: return "Leaves";
    case BLOCK_RED: return "Red";
    case BLOCK_ORANGE: return "Orange";
    case BLOCK_YELLOW: return "Yellow";
    case BLOCK_BLUE: return "Blue";
    case BLOCK_PURPLE: return "Purple";
    case BLOCK_GREEN: return "Green";
    case BLOCK_CYAN: return "Cyan";
    case BLOCK_PINK: return "Pink";
    case BLOCK_WHITE: return "White";
    case BLOCK_GRAY: return "Gray";
    case BLOCK_BLACK: return "Black";
    case BLOCK_PLANK: return "Plank";
    case BLOCK_BRICK: return "Brick";
    case BLOCK_GLASS: return "Glass";
    case BLOCK_WATER: return "Water";
    case BLOCK_SNOW: return "Snow";
    case BLOCK_ICE: return "Ice";
    case BLOCK_CACTUS: return "Cactus";
    case BLOCK_BEDROCK: return "Bedrock";
    case BLOCK_COAL_ORE: return "Coal Ore";
    case BLOCK_IRON_ORE: return "Iron Ore";
    case BLOCK_GOLD_ORE: return "Gold Ore";
    case BLOCK_DIAMOND_ORE: return "Diamond Ore";
    case BLOCK_TORCH: return "Torch";
    case BLOCK_ALBUM: return "Album";
    case BLOCK_SLAB: return "Stone Slab";
    case BLOCK_DOOR: return "Door";
    case BLOCK_DOOR_OPEN: return "Open Door";
    case BLOCK_MOON_ROCK: return "Moon Rock";
    case BLOCK_METEORITE: return "Meteorite";
    case BLOCK_MOON_SAND: return "Moon Sand";
    case BLOCK_STAR_MATTER: return "Star Matter";
    case BLOCK_SPACESHIP: return "Spaceship";
    case BLOCK_STONE_STAIRS: return "Stone Stairs";
    case BLOCK_WOOD_STAIRS: return "Wood Stairs";
    case BLOCK_FENCE: return "Fence";
    case BLOCK_FENCE_GATE: return "Fence Gate";
    case BLOCK_FENCE_GATE_OPEN: return "Open Fence Gate";
    case BLOCK_GLASS_PANE: return "Glass Pane";
    case BLOCK_LAVA: return "Lava";
    case BLOCK_FLOWER: return "Flower";
    case BLOCK_MUSHROOM: return "Mushroom";
    case BLOCK_BOOKSHELF: return "Bookshelf";
    case BLOCK_HAY_BALE: return "Hay Bale";
    case BLOCK_PUMPKIN: return "Pumpkin";
    case BLOCK_NETHERRACK: return "Netherrack";
    case BLOCK_SOUL_SAND: return "Soul Sand";
    case BLOCK_GLOWSTONE: return "Glowstone";
    case BLOCK_STONE_BRICKS: return "Stone Bricks";
    case BLOCK_SANDSTONE: return "Sandstone";
    case BLOCK_OBSIDIAN: return "Obsidian";
    case BLOCK_NETHER_PORTAL: return "Nether Portal";
    case BLOCK_SPACESHIP_CORE_NORTH:
    case BLOCK_SPACESHIP_CORE_EAST:
    case BLOCK_SPACESHIP_CORE_SOUTH:
    case BLOCK_SPACESHIP_CORE_WEST:
    case BLOCK_SPACESHIP_OCCUPIED: return "Spaceship";
    default: return "Air";
    }
}

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

bool IsValidBlockType(BlockType type)
{
    return (type >= BLOCK_AIR && type <= BLOCK_SPACESHIP_OCCUPIED) ||
           IsColorBlock(type);
}

bool IsWaterBlock(BlockType type)
{
    return type == BLOCK_WATER;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

float BlockCollisionHeight(BlockType type)
{
    if (type == BLOCK_AIR || type == BLOCK_WATER || type == BLOCK_LAVA ||
        type == BLOCK_FLOWER || type == BLOCK_MUSHROOM || type == BLOCK_FENCE_GATE_OPEN ||
        type == BLOCK_DOOR_OPEN) return 0.0f;
    if (type == BLOCK_SLAB || type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) return 0.5f;
    return 1.0f;
}

float BlockCollisionHeightAt(int x, int y, int z)
{
    if (!WorldCanAccessBlockY(y)) return 0.0f;
    return BlockCollisionHeight(GetBlockAt(x, y, z));
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_GLASS || type == BLOCK_TORCH || type == BLOCK_ALBUM ||
           type == BLOCK_GLASS_PANE || type == BLOCK_LAVA || type == BLOCK_FLOWER || type == BLOCK_MUSHROOM ||
           type == BLOCK_NETHER_PORTAL;
}

int ColorBlockIndex(BlockType type)
{
    if (!IsColorBlock(type)) return 0;
    return (int)type - BLOCK_COLOR_START;
}

BlockType ColorBlockFromIndex(int index)
{
    if (index < 0) index = 0;
    if (index >= COLOR_BLOCK_COUNT) index = COLOR_BLOCK_COUNT - 1;
    return (BlockType)(BLOCK_COLOR_START + index);
}

Color ColorPalette256(int index)
{
    index &= 255;
    int r = (index >> 5) & 0x07;
    int g = (index >> 2) & 0x07;
    int b = index & 0x03;

    return (Color){
        (unsigned char)((r * 255 + 3) / 7),
        (unsigned char)((g * 255 + 3) / 7),
        (unsigned char)((b * 255 + 1) / 3),
        255
    };
}

Color BlockBaseColor(BlockType type)
{
    if (IsColorBlock(type)) return ColorPalette256(ColorBlockIndex(type));

    switch (type) {
    case BLOCK_GRASS: return (Color){ 84, 170, 67, 255 };
    case BLOCK_DIRT: return (Color){ 121, 77, 43, 255 };
    case BLOCK_WOOD: return (Color){ 142, 91, 42, 255 };
    case BLOCK_LEAVES: return (Color){ 46, 128, 55, 255 };
    case BLOCK_SAND: return (Color){ 214, 197, 132, 255 };
    case BLOCK_RED: return (Color){ 207, 55, 54, 255 };
    case BLOCK_ORANGE: return (Color){ 229, 126, 38, 255 };
    case BLOCK_YELLOW: return (Color){ 238, 207, 64, 255 };
    case BLOCK_BLUE: return (Color){ 51, 116, 220, 255 };
    case BLOCK_PURPLE: return (Color){ 143, 72, 202, 255 };
    case BLOCK_GREEN: return (Color){ 64, 185, 85, 255 };
    case BLOCK_CYAN: return (Color){ 47, 188, 207, 255 };
    case BLOCK_PINK: return (Color){ 226, 96, 161, 255 };
    case BLOCK_WHITE: return (Color){ 232, 235, 224, 255 };
    case BLOCK_GRAY: return (Color){ 112, 119, 126, 255 };
    case BLOCK_BLACK: return (Color){ 28, 31, 35, 255 };
    case BLOCK_PLANK: return (Color){ 156, 100, 48, 255 };
    case BLOCK_BRICK: return (Color){ 148, 62, 48, 255 };
    case BLOCK_GLASS: return (Color){ 205, 230, 235, 255 };
    case BLOCK_WATER: return (Color){ 52, 118, 205, 255 };
    case BLOCK_SNOW: return (Color){ 238, 244, 246, 255 };
    case BLOCK_ICE: return (Color){ 148, 205, 226, 255 };
    case BLOCK_CACTUS: return (Color){ 78, 152, 62, 255 };
    case BLOCK_BEDROCK: return (Color){ 58, 58, 64, 255 };
    case BLOCK_COAL_ORE: return (Color){ 90, 92, 96, 255 };
    case BLOCK_IRON_ORE: return (Color){ 190, 152, 108, 255 };
    case BLOCK_GOLD_ORE: return (Color){ 232, 196, 64, 255 };
    case BLOCK_DIAMOND_ORE: return (Color){ 92, 214, 232, 255 };
    case BLOCK_TORCH: return (Color){ 255, 186, 62, 255 };
    case BLOCK_ALBUM: return (Color){ 118, 76, 42, 255 };
    case BLOCK_SLAB: return (Color){ 118, 122, 124, 255 };
    case BLOCK_DOOR: return (Color){ 156, 104, 52, 255 };
    case BLOCK_DOOR_OPEN: return (Color){ 140, 92, 46, 255 };
    case BLOCK_MOON_ROCK: return (Color){ 138, 142, 148, 255 };
    case BLOCK_METEORITE: return (Color){ 92, 78, 70, 255 };
    case BLOCK_MOON_SAND: return (Color){ 190, 186, 176, 255 };
    case BLOCK_STAR_MATTER: return (Color){ 238, 236, 222, 255 };
    case BLOCK_SPACESHIP: return (Color){ 196, 202, 210, 255 };
    case BLOCK_STONE_STAIRS: return (Color){ 118, 122, 124, 255 };
    case BLOCK_WOOD_STAIRS: return (Color){ 156, 100, 48, 255 };
    case BLOCK_FENCE: return (Color){ 150, 98, 50, 255 };
    case BLOCK_FENCE_GATE: return (Color){ 150, 98, 50, 255 };
    case BLOCK_FENCE_GATE_OPEN: return (Color){ 138, 90, 46, 255 };
    case BLOCK_GLASS_PANE: return (Color){ 205, 230, 235, 255 };
    case BLOCK_LAVA: return (Color){ 224, 96, 24, 255 };
    case BLOCK_FLOWER: return (Color){ 208, 62, 54, 255 };
    case BLOCK_MUSHROOM: return (Color){ 196, 52, 46, 255 };
    case BLOCK_BOOKSHELF: return (Color){ 118, 76, 40, 255 };
    case BLOCK_HAY_BALE: return (Color){ 218, 172, 66, 255 };
    case BLOCK_PUMPKIN: return (Color){ 224, 138, 42, 255 };
    case BLOCK_NETHERRACK: return (Color){ 116, 48, 42, 255 };
    case BLOCK_SOUL_SAND: return (Color){ 124, 106, 88, 255 };
    case BLOCK_GLOWSTONE: return (Color){ 250, 220, 110, 255 };
    case BLOCK_STONE_BRICKS: return (Color){ 138, 140, 142, 255 };
    case BLOCK_SANDSTONE: return (Color){ 216, 200, 150, 255 };
    case BLOCK_OBSIDIAN: return (Color){ 22, 16, 30, 255 };
    case BLOCK_NETHER_PORTAL: return (Color){ 158, 52, 190, 255 };
    case BLOCK_SPACESHIP_CORE_NORTH:
    case BLOCK_SPACESHIP_CORE_EAST:
    case BLOCK_SPACESHIP_CORE_SOUTH:
    case BLOCK_SPACESHIP_CORE_WEST:
    case BLOCK_SPACESHIP_OCCUPIED: return (Color){ 196, 202, 210, 255 };
    case BLOCK_STONE:
    default: return (Color){ 118, 122, 124, 255 };
    }
}

unsigned int HashBlockCoord(uint32_t dimension, int x, int y, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ dimension) * 16777619u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ (unsigned int)y) * 16777619u;
    h = (h ^ (unsigned int)z) * 16777619u;
    h ^= h >> 16;
    return h;
}

int NextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value) result <<= 1;
    return result;
}

void ClearBlockEditIndex(void)
{
    if (blockEditIndex && blockEditIndexCapacity > 0) {
        memset(blockEditIndex, 0, (size_t)blockEditIndexCapacity * sizeof(*blockEditIndex));
    }
}

void InsertBlockEditIndex(int editIndex)
{
    if (!blockEditIndex || blockEditIndexCapacity <= 0) return;

    BlockEdit edit = blockEdits[editIndex];
    uint32_t dimension = blockEditDimensions[editIndex];
    unsigned int slot = HashBlockCoord(dimension, edit.x, edit.y, edit.z) &
                        (unsigned int)(blockEditIndexCapacity - 1);

    for (;;) {
        BlockEditIndex *entry = &blockEditIndex[slot];
        if (!entry->used) {
            *entry = (BlockEditIndex){ edit.x, edit.y, edit.z, dimension, editIndex, true };
            return;
        }

        if (entry->dimension == dimension && entry->x == edit.x &&
            entry->y == edit.y && entry->z == edit.z) {
            entry->editIndex = editIndex;
            return;
        }

        slot = (slot + 1u) & (unsigned int)(blockEditIndexCapacity - 1);
    }
}

bool RebuildBlockEditIndex(int wantedEditCapacity)
{
    int wantedIndexCapacity = NextPowerOfTwo((wantedEditCapacity * 2) + 1);
    if (wantedIndexCapacity < 16) wantedIndexCapacity = 16;

    if (wantedIndexCapacity != blockEditIndexCapacity) {
        BlockEditIndex *nextIndex = realloc(blockEditIndex, (size_t)wantedIndexCapacity * sizeof(*nextIndex));
        if (!nextIndex) return false;
        blockEditIndex = nextIndex;
        blockEditIndexCapacity = wantedIndexCapacity;
    }

    ClearBlockEditIndex();
    for (int i = 0; i < blockEditCount; i++) InsertBlockEditIndex(i);
    return true;
}

int FindBlockEditIndex(uint32_t dimension, int x, int y, int z)
{
    if (!blockEditIndex || blockEditIndexCapacity <= 0) return -1;

    unsigned int slot = HashBlockCoord(dimension, x, y, z) &
                        (unsigned int)(blockEditIndexCapacity - 1);
    for (;;) {
        BlockEditIndex *entry = &blockEditIndex[slot];
        if (!entry->used) return -1;
        if (entry->dimension == dimension && entry->x == x &&
            entry->y == y && entry->z == z) return entry->editIndex;
        slot = (slot + 1u) & (unsigned int)(blockEditIndexCapacity - 1);
    }
}

bool EnsureBlockEditCapacity(int capacity)
{
    if (capacity <= blockEditCapacity) return true;

    int nextCapacity = blockEditCapacity == 0 ? INITIAL_BLOCK_EDIT_CAPACITY : blockEditCapacity;
    while (nextCapacity < capacity) nextCapacity *= 2;

    BlockEdit *nextEdits = malloc((size_t)nextCapacity * sizeof(*nextEdits));
    uint32_t *nextDimensions = malloc((size_t)nextCapacity * sizeof(*nextDimensions));
    if (!nextEdits || !nextDimensions) {
        free(nextEdits);
        free(nextDimensions);
        return false;
    }
    if (blockEditCount > 0) {
        memcpy(nextEdits, blockEdits, (size_t)blockEditCount * sizeof(*nextEdits));
        memcpy(nextDimensions, blockEditDimensions,
               (size_t)blockEditCount * sizeof(*nextDimensions));
    }
    free(blockEdits);
    free(blockEditDimensions);
    blockEdits = nextEdits;
    blockEditDimensions = nextDimensions;
    blockEditCapacity = nextCapacity;
    return RebuildBlockEditIndex(blockEditCapacity);
}

typedef struct TorchLight {
    int x;
    int y;
    int z;
    bool used;
} TorchLight;

static TorchLight torchLights[MAX_TORCH_LIGHTS];

void TorchLightAdd(int x, int y, int z)
{
    for (int i = 0; i < MAX_TORCH_LIGHTS; i++) {
        if (torchLights[i].used && torchLights[i].x == x &&
            torchLights[i].y == y && torchLights[i].z == z) return;
    }
    for (int i = 0; i < MAX_TORCH_LIGHTS; i++) {
        if (!torchLights[i].used) {
            torchLights[i] = (TorchLight){ x, y, z, true };
            return;
        }
    }
}

void TorchLightRemove(int x, int y, int z)
{
    for (int i = 0; i < MAX_TORCH_LIGHTS; i++) {
        if (torchLights[i].used && torchLights[i].x == x &&
            torchLights[i].y == y && torchLights[i].z == z) {
            torchLights[i].used = false;
            return;
        }
    }
}

void RebuildTorchList(void)
{
    uint32_t dimension = WorldCurrentEditDimension();
    for (int i = 0; i < MAX_TORCH_LIGHTS; i++) torchLights[i].used = false;
    for (int i = 0; i < blockEditCount; i++) {
        if (blockEditDimensions[i] == dimension && blockEdits[i].type == BLOCK_TORCH) {
            TorchLightAdd(blockEdits[i].x, blockEdits[i].y, blockEdits[i].z);
        }
    }
}

float TorchLightAtBlockNearby(int x, int y, int z, const int *indices, int count)
{
    float total = 0.0f;
    for (int k = 0; k < count; k++) {
        const TorchLight *light = &torchLights[indices[k]];
        float dx = (float)x + 0.5f - (float)light->x - 0.5f;
        float dy = (float)y + 0.5f - (float)light->y - 0.5f;
        float dz = (float)z + 0.5f - (float)light->z - 0.5f;
        float distSqr = dx * dx + dy * dy + dz * dz;
        if (distSqr < TORCH_LIGHT_RADIUS * TORCH_LIGHT_RADIUS) {
            float dist = sqrtf(distSqr);
            float falloff = 1.0f - dist / TORCH_LIGHT_RADIUS;
            total += falloff * falloff * TORCH_LIGHT_STRENGTH;
        }
    }
    return total;
}

int CollectNearbyTorchLights(int chunkMinX, int chunkMaxX, int chunkMinZ, int chunkMaxZ, int *indices)
{
    int count = 0;
    for (int i = 0; i < MAX_TORCH_LIGHTS; i++) {
        if (!torchLights[i].used) continue;
        if (torchLights[i].x < chunkMinX || torchLights[i].x > chunkMaxX) continue;
        if (torchLights[i].z < chunkMinZ || torchLights[i].z > chunkMaxZ) continue;
        indices[count++] = i;
    }
    return count;
}

void RememberBlockEdit(int x, int y, int z, BlockType type)
{
    uint32_t dimension = WorldCurrentEditDimension();
    int existingIndex = FindBlockEditIndex(dimension, x, y, z);
    if (existingIndex >= 0) {
        if (blockEdits[existingIndex].type == type) return;
        blockEdits[existingIndex].type = type;
        BumpBlockEditRevision();
        return;
    }

    if (!EnsureBlockEditCapacity(blockEditCount + 1)) return;

    blockEdits[blockEditCount] = (BlockEdit){ x, y, z, type };
    blockEditDimensions[blockEditCount] = dimension;
    InsertBlockEditIndex(blockEditCount);
    blockEditCount++;
    BumpBlockEditRevision();
}

#define UNDO_STACK_CAPACITY 5000

typedef struct BlockUndo {
    int x;
    int y;
    int z;
    BlockType prev;
    BlockType next;
    FluidBlockDisplacement fluidDisplacement;
    bool groupStart;
} BlockUndo;

// Ring buffers: PushBlockUndo is O(1) even when the capacity is exhausted,
// which matters for large undo groups (image imports can push millions of
// entries). The head indexes the oldest live entry; count is the live size.
static BlockUndo undoStack[UNDO_STACK_CAPACITY];
static BlockUndo redoStack[UNDO_STACK_CAPACITY];
static int undoHead = 0;
static int undoCount = 0;
static int redoHead = 0;
static int redoCount = 0;
static bool pendingGroupStart = false;
static bool undoGroupActive = false;

static bool SetBlockCore(
    int x, int y, int z, BlockType type, bool recordUndo,
    const FluidBlockDisplacement *replay, bool replayAfter);
static bool SetBlockNoUndoReplay(
    int x, int y, int z, BlockType type,
    const FluidBlockDisplacement *replay, bool replayAfter);

void WorldBeginUndoGroup(void)
{
    undoGroupActive = true;
    pendingGroupStart = true;
}

void WorldEndUndoGroup(void)
{
    undoGroupActive = false;
    pendingGroupStart = false;
}

static void PushBlockUndo(
    int x, int y, int z, BlockType prev, BlockType next,
    const FluidBlockDisplacement *fluidDisplacement)
{
    BlockUndo entry = {
        .x = x,
        .y = y,
        .z = z,
        .prev = prev,
        .next = next,
        .groupStart = !undoGroupActive || pendingGroupStart
    };
    if (fluidDisplacement) {
        entry.fluidDisplacement = *fluidDisplacement;
    }
    pendingGroupStart = false;

    if (undoCount >= UNDO_STACK_CAPACITY) {
        // Ring full: evict the oldest entry. If it opened a group, promote
        // the successor so the group boundary is preserved as far as the
        // fixed capacity allows instead of merging into the older group.
        if (undoStack[undoHead].groupStart && undoCount > 1) {
            undoStack[(undoHead + 1) % UNDO_STACK_CAPACITY].groupStart = true;
        }
        undoHead = (undoHead + 1) % UNDO_STACK_CAPACITY;
        undoCount = UNDO_STACK_CAPACITY;
    } else {
        undoCount++;
    }

    undoStack[(undoHead + undoCount - 1) % UNDO_STACK_CAPACITY] = entry;
    redoHead = 0;
    redoCount = 0;
}

void ClearUndoHistory(void)
{
    undoHead = 0;
    undoCount = 0;
    redoHead = 0;
    redoCount = 0;
    pendingGroupStart = false;
    undoGroupActive = false;
}

void WorldReset(uint32_t seed)
{
    WorldExtensionReset();
    WorldSetNetherActive(false);
    blockEditCount = 0;
    BumpBlockEditRevision();
    ClearBlockEditIndex();
    EvolutionCatalogReset();
    memset(torchLights, 0, sizeof(torchLights));
    ClearUndoHistory();
    WorldSetSeed(seed);
}

bool UndoBlockEdit(void)
{
    if (undoCount <= 0) return false;

    int tail = (undoHead + undoCount - 1) % UNDO_STACK_CAPACITY;
    int start = tail;
    // Walk backwards over the live ring until the group-start marker of the
    // most recent group is found (or the oldest live entry).
    while (start != undoHead && !undoStack[start].groupStart) {
        start = (start - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY;
    }

    int groupSize = 0;
    for (int i = tail;; i = (i - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY) {
        const FluidBlockDisplacement *replay =
            undoStack[i].fluidDisplacement.count > 0u
                ? &undoStack[i].fluidDisplacement : NULL;
        if (!SetBlockNoUndoReplay(
                undoStack[i].x, undoStack[i].y, undoStack[i].z,
                undoStack[i].prev, replay, false)) {
            int rollback = (i + 1) % UNDO_STACK_CAPACITY;
            for (int restored = 0; restored < groupSize; restored++) {
                const FluidBlockDisplacement *rollbackReplay =
                    undoStack[rollback].fluidDisplacement.count > 0u
                        ? &undoStack[rollback].fluidDisplacement : NULL;
                SetBlockNoUndoReplay(
                    undoStack[rollback].x, undoStack[rollback].y,
                    undoStack[rollback].z, undoStack[rollback].next,
                    rollbackReplay, true);
                rollback = (rollback + 1) % UNDO_STACK_CAPACITY;
            }
            return false;
        }
        groupSize++;
        if (i == start) break;
    }
    for (int i = tail;; i = (i - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY) {
        redoStack[(redoHead + redoCount) % UNDO_STACK_CAPACITY] = undoStack[i];
        redoCount++;
        if (i == start) break;
    }
    undoCount -= groupSize;
    if (undoCount <= 0) undoHead = 0;
    return true;
}

bool RedoBlockEdit(void)
{
    if (redoCount <= 0) return false;

    int tail = (redoHead + redoCount - 1) % UNDO_STACK_CAPACITY;
    int start = tail;
    // The redo ring is reversed relative to the undo ring: the group-start
    // marker sits at the top (tail). Walk back to the entry after it so the
    // whole group is restored, not just its first entry.
    while (start != redoHead &&
           !redoStack[(start - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY].groupStart) {
        start = (start - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY;
    }

    int groupSize = 0;
    for (int i = tail;; i = (i - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY) {
        const FluidBlockDisplacement *replay =
            redoStack[i].fluidDisplacement.count > 0u
                ? &redoStack[i].fluidDisplacement : NULL;
        if (!SetBlockNoUndoReplay(
                redoStack[i].x, redoStack[i].y, redoStack[i].z,
                redoStack[i].next, replay, true)) {
            int rollback = (i + 1) % UNDO_STACK_CAPACITY;
            for (int restored = 0; restored < groupSize; restored++) {
                const FluidBlockDisplacement *rollbackReplay =
                    redoStack[rollback].fluidDisplacement.count > 0u
                        ? &redoStack[rollback].fluidDisplacement : NULL;
                SetBlockNoUndoReplay(
                    redoStack[rollback].x, redoStack[rollback].y,
                    redoStack[rollback].z, redoStack[rollback].prev,
                    rollbackReplay, false);
                rollback = (rollback + 1) % UNDO_STACK_CAPACITY;
            }
            return false;
        }
        groupSize++;
        if (i == start) break;
    }
    for (int i = tail;; i = (i - 1 + UNDO_STACK_CAPACITY) % UNDO_STACK_CAPACITY) {
        undoStack[(undoHead + undoCount) % UNDO_STACK_CAPACITY] = redoStack[i];
        undoCount++;
        if (i == start) break;
    }
    redoCount -= groupSize;
    if (redoCount <= 0) redoHead = 0;
    return true;
}
static BlockType GetSurfaceBlockAt(int x, int y, int z)
{
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;

    BlockType block = BLOCK_AIR;
    if (ChunkTryGetLocalBlock(chunk, lx, y, lz, &block)) return block;
    if (WorldGetBlockEditForCurrentDimensionAt(x, y, z, &block)) {
        return block;
    }
    return HomeWorldSurfaceIsActive()
        ? TerrainBaseBlockAt(x, y, z, WorldTerrainMode())
        : BLOCK_AIR;
}

static bool MaterializeHomeSurfaceSectionForWrite(
    Chunk *chunk, int sectionY)
{
    if (!chunk || ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY) ||
        !HomeWorldSurfaceIsActive()) {
        return chunk != NULL;
    }
    if (!GenerateChunkTerrainSectionBase(
            chunk, chunk->cx, chunk->cz, sectionY, WorldTerrainMode()) &&
        !ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }
    ApplyEditsToChunkSection(chunk, sectionY);
    return true;
}

static bool SetBlockCore(
    int x, int y, int z, BlockType type, bool recordUndo,
    const FluidBlockDisplacement *replay, bool replayAfter)
{
    if (!InHeight(y)) return false;

    BlockType previous = GetSurfaceBlockAt(x, y, z);
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    FluidBlockDisplacement displacement = { 0 };
    if (!replay && chunk && previous == BLOCK_WATER && type != BLOCK_WATER &&
        !WorldExtensionTryDisplaceBlock(x, y, z, &displacement)) {
        return false;
    }

    if (chunk) {
        int sectionY = y / SURFACE_SECTION_HEIGHT;
        if (!MaterializeHomeSurfaceSectionForWrite(chunk, sectionY)) {
            return false;
        }
        if (!ChunkSetLocalBlock(chunk, lx, y, lz, type)) return false;
        MarkChunkDirtyAtBlock(x, y, z);
        if (previous != type) {
            WorldExtensionOnBlockChanged(x, y, z, previous, type);
        }
        if (replay && replay->count > 0u &&
            !WorldExtensionReplayBlockDisplacement(replay, replayAfter)) {
            return false;
        }
    }

    BlockType persistedType = type;
    if (replay && !replayAfter && type == BLOCK_WATER && replay->count > 0u) {
        const FluidVolumeChange *source = &replay->cells[0];
        if (source->x == x && source->y == y && source->z == z &&
            source->baselineKnown) {
            persistedType = source->baseline > 0u ? BLOCK_WATER : BLOCK_AIR;
        }
    }
    RememberBlockEdit(x, y, z, persistedType);
    if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
    else TorchLightRemove(x, y, z);
    if (recordUndo && previous != type) {
        PushBlockUndo(x, y, z, previous, type,
                      displacement.count > 0u ? &displacement : NULL);
    }
    return true;
}

BlockType GetBlockAt(int x, int y, int z)
{
    WorldBlockRegion region = WorldBlockRegionAt(y);
    if (!WorldCanAccessBlockY(y)) return BLOCK_AIR;
    if (region == WORLD_BLOCK_REGION_SPACE) return SpaceBlockAt(x, y, z);
    if (region == WORLD_BLOCK_REGION_NETHER) return NetherBlockAt(x, y, z);
    if (region == WORLD_BLOCK_REGION_SURFACE) return GetSurfaceBlockAt(x, y, z);
    return BLOCK_AIR;
}

static bool SetBlockNoUndoReplay(
    int x, int y, int z, BlockType type,
    const FluidBlockDisplacement *replay, bool replayAfter)
{
    BlockType previous = GetBlockAt(x, y, z);
    WorldBlockRegion region = WorldBlockRegionAt(y);
    bool changed = true;
    if (region == WORLD_BLOCK_REGION_SPACE) {
        SpaceSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
    } else if (region == WORLD_BLOCK_REGION_NETHER) {
        NetherSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
    } else if (region == WORLD_BLOCK_REGION_SURFACE && WorldIsSurfaceActive()) {
        changed = SetBlockCore(
            x, y, z, type, false, replay, replayAfter);
    } else {
        return false;
    }
    if (!changed) return false;
    if ((previous == BLOCK_SPACESHIP || ShipBlockIsParkedCore(previous)) &&
        type != BLOCK_SPACESHIP && !ShipBlockIsParkedCore(type))
        ShipForgetParkedAt(x, y, z);
    else if (previous != BLOCK_SPACESHIP && !ShipBlockIsParkedCore(previous) &&
             (type == BLOCK_SPACESHIP || ShipBlockIsParkedCore(type)))
        ShipTrackParkedAt(x, y, z);
    return true;
}

bool SetBlockNoUndo(int x, int y, int z, BlockType type)
{
    return SetBlockNoUndoReplay(x, y, z, type, NULL, false);
}

bool SetBlock(int x, int y, int z, BlockType type)
{
    BlockType previous = GetBlockAt(x, y, z);
    WorldBlockRegion region = WorldBlockRegionAt(y);
    if (region == WORLD_BLOCK_REGION_SPACE) {
        BlockType previous = SpaceBlockAt(x, y, z);
        if (previous != type) PushBlockUndo(x, y, z, previous, type, NULL);
        SpaceSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
    } else if (!WorldIsSurfaceActive()) {
        return false;
    } else if (region == WORLD_BLOCK_REGION_NETHER) {
        BlockType previous = NetherBlockAt(x, y, z);
        if (previous != type) PushBlockUndo(x, y, z, previous, type, NULL);
        NetherSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
    } else if (region == WORLD_BLOCK_REGION_SURFACE) {
        if (!SetBlockCore(x, y, z, type, true, NULL, false)) return false;
    } else {
        return false;
    }
    if ((previous == BLOCK_SPACESHIP || ShipBlockIsParkedCore(previous)) &&
        type != BLOCK_SPACESHIP && !ShipBlockIsParkedCore(type))
        ShipForgetParkedAt(x, y, z);
    else if (previous != BLOCK_SPACESHIP && !ShipBlockIsParkedCore(previous) &&
             (type == BLOCK_SPACESHIP || ShipBlockIsParkedCore(type)))
        ShipTrackParkedAt(x, y, z);
    return true;
}

bool SetBlockForImport(int x, int y, int z, BlockType type)
{
    return SetBlockCore(x, y, z, type, true, NULL, false);
}

BlockType NearestImageBlock(Color color)
{
    int bestIndex = 0;
    int bestDistance = 1 << 30;

    for (int i = 0; i < COLOR_BLOCK_COUNT; i++) {
        Color paletteColor = ColorPalette256(i);
        int dr = (int)color.r - (int)paletteColor.r;
        int dg = (int)color.g - (int)paletteColor.g;
        int db = (int)color.b - (int)paletteColor.b;
        int distance = dr * dr + dg * dg + db * db;
        if (distance < bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    return ColorBlockFromIndex(bestIndex);
}

void SetImportMessage(const char *message)
{
    snprintf(importMessage, sizeof(importMessage), "%s", message);
    importMessageTimer = 6.0f;
}

typedef struct SaveMapContext {
    const Player *player;
} SaveMapContext;

static bool WriteSaveFile(FILE *file, void *opaque)
{
    const SaveMapContext *context = opaque;
    const Player *player = context ? context->player : NULL;
    if (!file || !player) return false;

    bool ok = fwrite(SAVE_MAGIC_V15, 1, SAVE_MAGIC_V15_LEN, file) ==
              SAVE_MAGIC_V15_LEN;
    uint32_t terrainGenerationVersion = TERRAIN_GENERATION_VERSION;
    uint32_t seed = WorldGetSeed();
    uint32_t terrain = (uint32_t)worldTerrainMode;
    float playerData[6] = {
        player->position.x, player->position.y, player->position.z,
        player->yaw, player->pitch, player->floating ? 1.0f : 0.0f
    };
    uint32_t editCount = (uint32_t)blockEditCount;
    ok = ok && fwrite(&terrainGenerationVersion,
                      sizeof(terrainGenerationVersion), 1, file) == 1;
    ok = ok && fwrite(&seed, sizeof(seed), 1, file) == 1;
    ok = ok && fwrite(&terrain, sizeof(terrain), 1, file) == 1;
    ok = ok && fwrite(playerData, sizeof(playerData), 1, file) == 1;
    ok = ok && fwrite(&editCount, sizeof(editCount), 1, file) == 1;
    if (blockEditCount > 0) {
        ok = ok && fwrite(blockEdits, sizeof(BlockEdit), (size_t)blockEditCount, file) ==
             (size_t)blockEditCount;
        ok = ok && fwrite(blockEditDimensions, sizeof(*blockEditDimensions),
                          (size_t)blockEditCount, file) == (size_t)blockEditCount;
    }

    ok = ok && InventorySave(file) && ShipSaveState(file) && PlanetWorldSaveState(file) &&
         HomeWorldSaveState(file);
    ok = ok && AlbumSave(file) && SpaceSaveEdits(file) && NetherSaveEdits(file);
    ok = ok && SpaceSaveState(file) && EntitiesSaveState(file) &&
         PlanetEcologySaveState(file) && EvolutionCatalogSaveState(file) &&
         ShipLocatorSaveState(file) && WorldExtensionSaveState(file) &&
         !ferror(file);
    return ok;
}

static bool SaveDataAvailable(FILE *file)
{
    int value = fgetc(file);
    if (value == EOF) return false;
    return ungetc(value, file) != EOF;
}

void SaveMap(const Player *player)
{
    if (!player) {
        SetImportMessage("Save failed: player state is unavailable.");
        return;
    }

    SaveMapContext context = { .player = player };
    if (!SaveIoWriteAtomic(SAVE_FILE, SAVE_FILE_BAK, WriteSaveFile, &context)) {
        SetImportMessage("Save failed: existing save was kept intact.");
        return;
    }
    SetImportMessage(TextFormat("Saved map to %s (%d edits).", SAVE_FILE, blockEditCount));
}

bool ReadSaveHeader(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer, int *editCount)
{
    char label[64] = { 0 };
    char magic[64] = { 0 };
    int terrain = 0;
    int floating = 0;

    if (fscanf(file, "%63s", magic) != 1 || strcmp(magic, "VOXELCRAFT_SAVE_V1") != 0) return false;
    if (fscanf(file, "%63s %d", label, &terrain) != 2 || strcmp(label, "terrain") != 0) return false;
    if (terrain != TERRAIN_VARIED && terrain != TERRAIN_FLAT) return false;

    if (fscanf(file, "%63s %f %f %f %f %f %d", label,
               &savedPlayer->position.x, &savedPlayer->position.y, &savedPlayer->position.z,
               &savedPlayer->yaw, &savedPlayer->pitch, &floating) != 7 ||
        strcmp(label, "player") != 0) {
        return false;
    }

    if (fscanf(file, "%63s %d", label, editCount) != 2 || strcmp(label, "edits") != 0) return false;
    if (*editCount < 0 || (uint32_t)*editCount > MAX_LOAD_EDIT_COUNT) return false;

    savedPlayer->velocity = Vector3Zero();
    savedPlayer->onGround = false;
    savedPlayer->floating = floating != 0;
    *savedTerrain = (TerrainMode)terrain;
    return true;
}

static bool LoadMapV2(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer, BlockEdit **outEdits, int *outCount)
{
    uint32_t terrain = 0;
    if (fread(&terrain, sizeof(terrain), 1, file) != 1 || terrain > 1) return false;
    float playerData[6];
    if (fread(playerData, sizeof(playerData), 1, file) != 1) return false;
    // Validate every player float before use: a corrupted save with NaN/Inf
    // here would otherwise flow into floorf casts and camera/collision math
    // (undefined behavior). This is the one reader in the chain that used to
    // skip validation.
    for (int i = 0; i < 6; i++) {
        if (!isfinite(playerData[i])) return false;
    }
    if (playerData[5] != 0.0f && playerData[5] != 1.0f) return false;
    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return false;
    if (count > MAX_LOAD_EDIT_COUNT) return false;

    BlockEdit *edits = NULL;
    if (count > 0) {
        edits = malloc((size_t)count * sizeof(*edits));
        if (!edits) return false;
        if (fread(edits, sizeof(BlockEdit), (size_t)count, file) != count) {
            free(edits);
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (!InHeight(edits[i].y) || !IsValidBlockType(edits[i].type)) {
                free(edits);
                return false;
            }
        }
    }

    *savedTerrain = (TerrainMode)terrain;
    savedPlayer->position = (Vector3){ playerData[0], playerData[1], playerData[2] };
    savedPlayer->yaw = playerData[3];
    savedPlayer->pitch = playerData[4];
    savedPlayer->floating = playerData[5] != 0.0f;
    savedPlayer->velocity = Vector3Zero();
    savedPlayer->onGround = false;
    *outEdits = edits;
    *outCount = (int)count;
    return true;
}

static bool LoadMapV3(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer,
                      BlockEdit **outEdits, int *outCount, uint32_t *outSeed)
{
    uint32_t seed = DEFAULT_WORLD_SEED;
    if (fread(&seed, sizeof(seed), 1, file) != 1) return false;
    if (!LoadMapV2(file, savedTerrain, savedPlayer, outEdits, outCount)) return false;
    *outSeed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
    return true;
}

static bool LoadMapV4(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer,
                      BlockEdit **outEdits, int *outCount, uint32_t *outSeed)
{
    if (!LoadMapV3(file, savedTerrain, savedPlayer, outEdits, outCount, outSeed)) return false;
    if (!InventoryLoad(file) || !ShipLoadState(file)) {
        free(*outEdits);
        *outEdits = NULL;
        return false;
    }
    return true;
}

static bool LoadMapV5(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer,
                      BlockEdit **outEdits, int *outCount, uint32_t *outSeed)
{
    if (!LoadMapV4(file, savedTerrain, savedPlayer, outEdits, outCount, outSeed)) return false;
    if (!PlanetWorldLoadState(file)) {
        free(*outEdits);
        *outEdits = NULL;
        return false;
    }
    return true;
}

static bool LoadMapV6(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer,
                      BlockEdit **outEdits, uint32_t **outDimensions,
                      int *outCount, uint32_t *outSeed)
{
    if (!LoadMapV3(file, savedTerrain, savedPlayer, outEdits, outCount, outSeed)) return false;

    uint32_t *dimensions = NULL;
    if (*outCount > 0) {
        dimensions = malloc((size_t)*outCount * sizeof(*dimensions));
        if (!dimensions ||
            fread(dimensions, sizeof(*dimensions), (size_t)*outCount, file) != (size_t)*outCount) {
            free(dimensions);
            free(*outEdits);
            *outEdits = NULL;
            return false;
        }
    }

    if (!InventoryLoad(file) || !ShipLoadState(file) || !PlanetWorldLoadState(file)) {
        free(dimensions);
        free(*outEdits);
        *outEdits = NULL;
        return false;
    }
    *outDimensions = dimensions;
    return true;
}

static bool LoadMapV7(FILE *file, TerrainMode *savedTerrain, Player *savedPlayer,
                      BlockEdit **outEdits, uint32_t **outDimensions,
                      int *outCount, uint32_t *outSeed)
{
    if (!LoadMapV6(file, savedTerrain, savedPlayer, outEdits, outDimensions,
                   outCount, outSeed)) {
        return false;
    }
    if (!HomeWorldLoadState(file)) {
        free(*outDimensions);
        free(*outEdits);
        *outDimensions = NULL;
        *outEdits = NULL;
        return false;
    }
    return true;
}

static bool LoadMapV13(FILE *file, TerrainMode *savedTerrain,
                       Player *savedPlayer, BlockEdit **outEdits,
                       uint32_t **outDimensions, int *outCount,
                       uint32_t *outSeed,
                       uint32_t *outTerrainGenerationVersion)
{
    uint32_t terrainGenerationVersion = 0;
    if (fread(&terrainGenerationVersion, sizeof(terrainGenerationVersion),
              1, file) != 1 ||
        terrainGenerationVersion < MIN_SUPPORTED_TERRAIN_GENERATION_VERSION ||
        terrainGenerationVersion > TERRAIN_GENERATION_VERSION) {
        return false;
    }
    if (outTerrainGenerationVersion) {
        *outTerrainGenerationVersion = terrainGenerationVersion;
    }
    return LoadMapV7(file, savedTerrain, savedPlayer, outEdits,
                     outDimensions, outCount, outSeed);
}

void LoadMap(Player *player)
{
    DrainChunkGen();
    UnloadAllSpaceChunks();
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file) {
        SetImportMessage("Load failed: voxelcraft_save.txt was not found.");
        return;
    }

    struct stat saveStat;
    if (fstat(fileno(file), &saveStat) != 0 || saveStat.st_size < 0 ||
        (uint64_t)saveStat.st_size > SAVE_MAX_FILE_BYTES) {
        fclose(file);
        SetImportMessage("Load failed: save file is too large or unreadable.");
        return;
    }

    TerrainMode savedTerrain = TERRAIN_VARIED;
    uint32_t savedSeed = DEFAULT_WORLD_SEED;
    bool loadedInventory = false;
    bool loadedPlanetWorld = false;
    bool loadedHomeWorld = false;
    bool loadedSpaceOrigin = false;
    bool legacyPlanetCoordinates = false;
    uint32_t loadedTerrainGenerationVersion = 0;
    Player savedPlayer = { 0 };
    int savedEditCount = 0;
    BlockEdit *loadedEdits = NULL;
    uint32_t *loadedDimensions = NULL;
    ShipLocatorRecord loadedShipLocator = { 0 };
    char magicV15[SAVE_MAGIC_V15_LEN] = { 0 };
    bool isV15 = fread(magicV15, 1, SAVE_MAGIC_V15_LEN, file) ==
                     SAVE_MAGIC_V15_LEN &&
                 memcmp(magicV15, SAVE_MAGIC_V15, SAVE_MAGIC_V15_LEN) == 0;
    char magicV14[SAVE_MAGIC_V14_LEN] = { 0 };
    bool isV14 = false;
    if (!isV15) {
        rewind(file);
        isV14 = fread(magicV14, 1, SAVE_MAGIC_V14_LEN, file) ==
                        SAVE_MAGIC_V14_LEN &&
                    memcmp(magicV14, SAVE_MAGIC_V14, SAVE_MAGIC_V14_LEN) == 0;
    }
    bool isV14Family = isV15 || isV14;
    char magicV13[SAVE_MAGIC_V13_LEN] = { 0 };
    bool isV13 = false;
    if (!isV14Family) {
        rewind(file);
        isV13 = fread(magicV13, 1, SAVE_MAGIC_V13_LEN, file) ==
                     SAVE_MAGIC_V13_LEN &&
                 memcmp(magicV13, SAVE_MAGIC_V13, SAVE_MAGIC_V13_LEN) == 0;
    }
    char magicV12[SAVE_MAGIC_V12_LEN] = { 0 };
    bool isV12 = false;
    if (!isV14Family && !isV13) {
        rewind(file);
        isV12 = fread(magicV12, 1, SAVE_MAGIC_V12_LEN, file) ==
                        SAVE_MAGIC_V12_LEN &&
                    memcmp(magicV12, SAVE_MAGIC_V12, SAVE_MAGIC_V12_LEN) == 0;
    }
    char magicV11[SAVE_MAGIC_V11_LEN] = { 0 };
    bool isV11 = false;
    if (!isV14Family && !isV13 && !isV12) {
        rewind(file);
        isV11 = fread(magicV11, 1, SAVE_MAGIC_V11_LEN, file) ==
                    SAVE_MAGIC_V11_LEN &&
                memcmp(magicV11, SAVE_MAGIC_V11, SAVE_MAGIC_V11_LEN) == 0;
    }
    char magicV10[SAVE_MAGIC_V10_LEN] = { 0 };
    bool isV10 = false;
    if (!isV14Family && !isV13 && !isV12 && !isV11) {
        rewind(file);
        isV10 = fread(magicV10, 1, SAVE_MAGIC_V10_LEN, file) ==
                    SAVE_MAGIC_V10_LEN &&
                memcmp(magicV10, SAVE_MAGIC_V10, SAVE_MAGIC_V10_LEN) == 0;
    }
    char magicV9[SAVE_MAGIC_V9_LEN] = { 0 };
    bool isV9 = false;
    if (!isV14Family && !isV13 && !isV12 && !isV11 && !isV10) {
        rewind(file);
        isV9 = fread(magicV9, 1, SAVE_MAGIC_V9_LEN, file) == SAVE_MAGIC_V9_LEN &&
               memcmp(magicV9, SAVE_MAGIC_V9, SAVE_MAGIC_V9_LEN) == 0;
    }
    char magicV8[SAVE_MAGIC_V8_LEN] = { 0 };
    bool isV8 = false;
    if (!isV14Family && !isV13 && !isV12 && !isV11 && !isV10 && !isV9) {
        rewind(file);
        isV8 = fread(magicV8, 1, SAVE_MAGIC_V8_LEN, file) == SAVE_MAGIC_V8_LEN &&
               memcmp(magicV8, SAVE_MAGIC_V8, SAVE_MAGIC_V8_LEN) == 0;
    }
    if (isV14Family || isV13 || isV12 || isV11 || isV10 || isV9 || isV8) {
        bool loaded = (isV14Family || isV13)
                          ? LoadMapV13(file, &savedTerrain, &savedPlayer,
                                       &loadedEdits, &loadedDimensions,
                                       &savedEditCount, &savedSeed,
                                       &loadedTerrainGenerationVersion)
                          : LoadMapV7(file, &savedTerrain, &savedPlayer,
                                      &loadedEdits, &loadedDimensions,
                                      &savedEditCount, &savedSeed);
        if (!loaded) {
            fclose(file);
            SetImportMessage("Load failed: save file is corrupted.");
            return;
        }
        loadedInventory = true;
        loadedPlanetWorld = true;
        loadedHomeWorld = true;
    } else {
        rewind(file);
        char magicV7[SAVE_MAGIC_V7_LEN] = { 0 };
        bool isV7 = fread(magicV7, 1, SAVE_MAGIC_V7_LEN, file) == SAVE_MAGIC_V7_LEN &&
                    memcmp(magicV7, SAVE_MAGIC_V7, SAVE_MAGIC_V7_LEN) == 0;
        if (isV7) {
            if (!LoadMapV7(file, &savedTerrain, &savedPlayer, &loadedEdits,
                           &loadedDimensions, &savedEditCount, &savedSeed)) {
                fclose(file);
                SetImportMessage("Load failed: save file is corrupted.");
                return;
            }
            loadedInventory = true;
            loadedPlanetWorld = true;
            loadedHomeWorld = true;
        } else {
            rewind(file);
            char magicV6[SAVE_MAGIC_V6_LEN] = { 0 };
            bool isV6 = fread(magicV6, 1, SAVE_MAGIC_V6_LEN, file) == SAVE_MAGIC_V6_LEN &&
                        memcmp(magicV6, SAVE_MAGIC_V6, SAVE_MAGIC_V6_LEN) == 0;
            if (isV6) {
                if (!LoadMapV6(file, &savedTerrain, &savedPlayer, &loadedEdits,
                               &loadedDimensions, &savedEditCount, &savedSeed)) {
                    fclose(file);
                    SetImportMessage("Load failed: save file is corrupted.");
                    return;
                }
                loadedInventory = true;
                loadedPlanetWorld = true;
            } else {
                rewind(file);
                char magicV5[SAVE_MAGIC_V5_LEN] = { 0 };
                bool isV5 = fread(magicV5, 1, SAVE_MAGIC_V5_LEN, file) == SAVE_MAGIC_V5_LEN &&
                            memcmp(magicV5, SAVE_MAGIC_V5, SAVE_MAGIC_V5_LEN) == 0;
                if (isV5) {
                    if (!LoadMapV5(file, &savedTerrain, &savedPlayer, &loadedEdits,
                                   &savedEditCount, &savedSeed)) {
                        free(loadedEdits);
                        fclose(file);
                        SetImportMessage("Load failed: save file is corrupted.");
                        return;
                    }
                    loadedInventory = true;
                    loadedPlanetWorld = true;
                    legacyPlanetCoordinates = true;
                } else {
                    rewind(file);
                    char magicV4[SAVE_MAGIC_V4_LEN] = { 0 };
                    bool isV4 = fread(magicV4, 1, SAVE_MAGIC_V4_LEN, file) == SAVE_MAGIC_V4_LEN &&
                                memcmp(magicV4, SAVE_MAGIC_V4, SAVE_MAGIC_V4_LEN) == 0;
                    if (isV4) {
                        if (!LoadMapV4(file, &savedTerrain, &savedPlayer, &loadedEdits,
                                       &savedEditCount, &savedSeed)) {
                            free(loadedEdits);
                            fclose(file);
                            SetImportMessage("Load failed: save file is corrupted.");
                            return;
                        }
                        loadedInventory = true;
                    } else {
                        rewind(file);
                        char magicV3[SAVE_MAGIC_V3_LEN] = { 0 };
                        bool isV3 = fread(magicV3, 1, SAVE_MAGIC_V3_LEN, file) == SAVE_MAGIC_V3_LEN &&
                                    memcmp(magicV3, SAVE_MAGIC_V3, SAVE_MAGIC_V3_LEN) == 0;
                        if (isV3) {
                            if (!LoadMapV3(file, &savedTerrain, &savedPlayer, &loadedEdits,
                                           &savedEditCount, &savedSeed)) {
                                fclose(file);
                                SetImportMessage("Load failed: save file is corrupted.");
                                return;
                            }
                        } else {
                            rewind(file);
                            char magicV2[SAVE_MAGIC_V2_PREFIX_LEN] = { 0 };
                            bool isV2 = fread(magicV2, 1, SAVE_MAGIC_V2_PREFIX_LEN, file) ==
                                            SAVE_MAGIC_V2_PREFIX_LEN &&
                                        memcmp(magicV2, SAVE_MAGIC_V2_PREFIX,
                                               SAVE_MAGIC_V2_PREFIX_LEN) == 0;
                            if (isV2) {
                                if (!LoadMapV2(file, &savedTerrain, &savedPlayer,
                                               &loadedEdits, &savedEditCount)) {
                                    free(loadedEdits);
                                    fclose(file);
                                    SetImportMessage("Load failed: save file is corrupted.");
                                    return;
                                }
                            } else {
                                rewind(file);
                                if (!ReadSaveHeader(file, &savedTerrain, &savedPlayer, &savedEditCount)) {
                                    fclose(file);
                                    SetImportMessage("Load failed: save file header is invalid.");
                                    return;
                                }

                                if (savedEditCount > 0) {
                                    loadedEdits = malloc((size_t)savedEditCount * sizeof(*loadedEdits));
                                    if (!loadedEdits) {
                                        fclose(file);
                                        SetImportMessage("Load failed: not enough memory for save edits.");
                                        return;
                                    }
                                }

                                for (int i = 0; i < savedEditCount; i++) {
                                    int type = 0;
                                    if (fscanf(file, "%d %d %d %d",
                                              &loadedEdits[i].x, &loadedEdits[i].y,
                                              &loadedEdits[i].z, &type) != 4 ||
                                        !InHeight(loadedEdits[i].y) ||
                                        !IsValidBlockType((BlockType)type)) {
                                        free(loadedEdits);
                                        fclose(file);
                                        SetImportMessage("Load failed: save file contains invalid block data.");
                                        return;
                                    }
                                    loadedEdits[i].type = (BlockType)type;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    int storedSpaceLayerY = (isV14Family || isV13) ? SPACE_LAYER_Y : LEGACY_SPACE_LAYER_Y;
    if (((isV14Family || isV13 || isV12 || isV11) && !SaveDataAvailable(file)) || !AlbumLoad(file) ||
        ((isV14Family || isV13 || isV12 || isV11) && !SaveDataAvailable(file)) ||
            !SpaceLoadEdits(file, storedSpaceLayerY) ||
        ((isV14Family || isV13 || isV12 || isV11) && !SaveDataAvailable(file)) || !NetherLoadEdits(file)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: save file is corrupted.");
        return;
    }
    if (isV14Family || isV13 || isV12 || isV11 || isV10 || isV9) {
        if (!SpaceLoadState(file)) {
            free(loadedDimensions);
            free(loadedEdits);
            fclose(file);
            SetImportMessage("Load failed: save file is corrupted.");
            return;
        }
        loadedSpaceOrigin = true;
    } else if (isV8) {
        if (!SpaceLoadOrigin(file)) {
            free(loadedDimensions);
            free(loadedEdits);
            fclose(file);
            SetImportMessage("Load failed: save file is corrupted.");
            return;
        }
        loadedSpaceOrigin = true;
    }
    if ((isV14Family || isV13 || isV12 || isV11 || isV10) && !EntitiesLoadState(file)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: entity state is corrupted.");
        return;
    }
    if ((isV14Family || isV13 || isV12 || isV11) && !PlanetEcologyLoadState(file)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: ecology state is corrupted.");
        return;
    }
    if (isV14Family && !EvolutionCatalogLoadState(file)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: evolution catalog state is corrupted.");
        return;
    }
    if ((isV14Family || isV13 || isV12) &&
        !ShipLocatorReadStateForSpaceLayer(file, &loadedShipLocator,
                                           storedSpaceLayerY)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: ship locator state is corrupted.");
        return;
    }
    if (isV15 && !WorldExtensionLoadState(file)) {
        free(loadedDimensions);
        free(loadedEdits);
        fclose(file);
        SetImportMessage("Load failed: fluid state is corrupted.");
        return;
    }
    fclose(file);

    if (!loadedPlanetWorld) PlanetWorldReset();
    if (!loadedHomeWorld) {
        HomeWorldRestoreLegacyStateForSpaceLayer(&savedPlayer,
                                                 storedSpaceLayerY);
    }
    if (!loadedSpaceOrigin) SpaceResetOrigin();

    if (savedEditCount > 0 && !loadedDimensions) {
        loadedDimensions = calloc((size_t)savedEditCount, sizeof(*loadedDimensions));
        if (!loadedDimensions) {
            free(loadedEdits);
            SetImportMessage("Load failed: not enough memory for edit dimensions.");
            return;
        }
    }

    if (legacyPlanetCoordinates && PlanetWorldIsActive()) {
        int originX = PlanetWorldOriginX();
        int originZ = PlanetWorldOriginZ();
        uint32_t dimension = PlanetWorldSeed();
        savedPlayer.position.x -= (float)originX;
        savedPlayer.position.z -= (float)originZ;
        for (int i = 0; i < savedEditCount; i++) {
            int64_t localX = (int64_t)loadedEdits[i].x - (int64_t)originX;
            int64_t localZ = (int64_t)loadedEdits[i].z - (int64_t)originZ;
            if (localX < -LEGACY_PLANET_REGION_RADIUS ||
                localX >= LEGACY_PLANET_REGION_RADIUS ||
                localZ < -LEGACY_PLANET_REGION_RADIUS ||
                localZ >= LEGACY_PLANET_REGION_RADIUS) {
                continue;
            }
            loadedEdits[i].x = (int)localX;
            loadedEdits[i].z = (int)localZ;
            loadedDimensions[i] = dimension;
        }
    }

    if (!EnsureBlockEditCapacity(savedEditCount)) {
        free(loadedEdits);
        free(loadedDimensions);
        SetImportMessage("Load failed: not enough memory to apply save.");
        return;
    }

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    UnloadAllNetherChunks();
    if (!isV15) WorldExtensionReset();
    worldTerrainMode = savedTerrain;
    WorldSetSeed(savedSeed);
    if (!isV14Family && !isV13) {
        PlanetWorldMigrateSpaceLayer(storedSpaceLayerY);
        if (!PlanetWorldIsActive() && !HomeWorldSurfaceIsActive()) {
            savedPlayer.position.y += (float)(SPACE_LAYER_Y - storedSpaceLayerY);
        }
    }
    if (!isV14Family && !isV13 && !isV12 && !isV11 && !isV10) EntitiesClear();
    if (!isV14Family && !isV13 && !isV12 && !isV11) PlanetEcologyResetState();
    if (!isV14Family) EvolutionCatalogReset();
    if (!loadedInventory) {
        InventoryReset();
        InventoryGrantStarterKit();
        ShipReset();
    }
    if (((!isV14Family && !isV13) || loadedTerrainGenerationVersion !=
                     TERRAIN_GENERATION_VERSION) &&
        WorldIsSurfaceActive()) {
        int landingX = (int)floorf(savedPlayer.position.x);
        int landingZ = (int)floorf(savedPlayer.position.z);
        int groundY = 0;
        if (FindSafeSurfaceLanding(landingX, landingZ, 128, 0,
                                   &landingX, &landingZ, &groundY)) {
            savedPlayer.position = (Vector3){
                (float)landingX + 0.5f, (float)groundY + 3.0f,
                (float)landingZ + 0.5f
            };
        }
    }
    *player = savedPlayer;
    WorldSetNetherActive(
        HomeWorldSurfaceIsActive() && !PlanetWorldIsActive() &&
        player->position.y >= (float)NETHER_LAYER_Y &&
        player->position.y < (float)NETHER_LAYER_TOP);
    PlayerResetRuntimeState(player);
    blockEditCount = savedEditCount;
    BumpBlockEditRevision();
    if (savedEditCount > 0) {
        memcpy(blockEdits, loadedEdits, (size_t)savedEditCount * sizeof(*loadedEdits));
        memcpy(blockEditDimensions, loadedDimensions,
               (size_t)savedEditCount * sizeof(*loadedDimensions));
    }
    free(loadedEdits);
    free(loadedDimensions);
    if (!RebuildBlockEditIndex(blockEditCapacity)) {
        SetImportMessage("Load warning: edit index rebuild failed.");
        return;
    }
    RebuildTorchList();
    SpaceRebuildTorchList();
    ClearUndoHistory();
    if (isV14Family || isV13 || isV12) ShipLocatorSetRecord(&loadedShipLocator);
    else ShipLocatorReset();

    if (WorldIsSurfaceActive()) {
        UpdateChunks(player->position,
                     EffectiveRenderDistanceForHeight(player->position.y + EYE_HEIGHT));
    }
    if (!isV14Family && !isV13) {
        SetImportMessage(TextFormat(
            "Upgraded terrain; moved player to safe ground and kept %d edits at original heights.",
            blockEditCount));
    } else {
        SetImportMessage(TextFormat("Loaded %s (%d edits).", SAVE_FILE,
                                    blockEditCount));
    }
}


int WorldGetEditCount(void)
{
    return blockEditCount;
}

uint64_t WorldGetEditRevision(void)
{
    return blockEditRevision;
}

const BlockEdit *WorldGetEditAt(int index)
{
    if (index < 0 || index >= blockEditCount) return NULL;
    return &blockEdits[index];
}

uint32_t WorldGetEditDimensionAt(int index)
{
    if (index < 0 || index >= blockEditCount) return 0u;
    return blockEditDimensions[index];
}

bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit)
{
    if (!outEdit || index < 0 || index >= blockEditCount ||
        blockEditDimensions[index] != WorldCurrentEditDimension()) {
        return false;
    }
    *outEdit = blockEdits[index];
    return true;
}

bool WorldGetBlockEditForCurrentDimensionAt(int x, int y, int z,
                                            BlockType *outType)
{
    if (!outType) return false;
    int index = FindBlockEditIndex(WorldCurrentEditDimension(), x, y, z);
    if (index < 0) return false;
    *outType = blockEdits[index].type;
    return true;
}

const char *WorldGetImportMessage(void)
{
    return importMessage;
}

float WorldGetImportMessageTimer(void)
{
    return importMessageTimer;
}

void WorldTickImportMessage(float dt)
{
    if (importMessageTimer > 0.0f) importMessageTimer -= dt;
}

void WorldCleanup(void)
{
    WorldExtensionCleanup();
    free(blockEditIndex);
    free(blockEditDimensions);
    free(blockEdits);
}
