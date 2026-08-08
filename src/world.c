#include "world.h"

#include "raymath.h"
#include "chunks.h"
#include "player.h"
#include "space.h"
#include "nether.h"
#include "album.h"
#include "inventory.h"
#include "ship.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SAVE_FILE_BAK "voxelcraft_save.bak"
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
static uint32_t worldSeed = DEFAULT_WORLD_SEED;

uint32_t WorldGetSeed(void)
{
    return worldSeed;
}

void WorldSetSeed(uint32_t seed)
{
    worldSeed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
}

static uint32_t WorldCurrentEditDimension(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : 0u;
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
    default: return "Air";
    }
}

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

bool IsValidBlockType(BlockType type)
{
    return (type >= BLOCK_AIR && type <= BLOCK_NETHER_PORTAL) || IsColorBlock(type);
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
    bool inSurface = y >= 0 && y < WORLD_HEIGHT;
    bool inNether = y >= NETHER_LAYER_Y && y < 0;
    bool inSpace = y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP;
    if (!inSurface && !inNether && !inSpace) return 0.0f;
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
        blockEdits[existingIndex].type = type;
        return;
    }

    if (!EnsureBlockEditCapacity(blockEditCount + 1)) return;

    blockEdits[blockEditCount] = (BlockEdit){ x, y, z, type };
    blockEditDimensions[blockEditCount] = dimension;
    InsertBlockEditIndex(blockEditCount);
    blockEditCount++;
}

#define UNDO_STACK_CAPACITY 5000

typedef struct BlockUndo {
    int x;
    int y;
    int z;
    BlockType prev;
    BlockType next;
    bool groupStart;
} BlockUndo;

static BlockUndo undoStack[UNDO_STACK_CAPACITY];
static BlockUndo redoStack[UNDO_STACK_CAPACITY];
static int undoCount = 0;
static int redoCount = 0;
static bool pendingGroupStart = false;

void SetBlockCore(int x, int y, int z, BlockType type, bool recordUndo);
static void SetBlockNoUndo(int x, int y, int z, BlockType type);

void WorldBeginUndoGroup(void)
{
    pendingGroupStart = true;
}

void PushBlockUndo(int x, int y, int z, BlockType prev, BlockType next)
{
    if (undoCount >= UNDO_STACK_CAPACITY) {
        memmove(undoStack, undoStack + 1, (size_t)(UNDO_STACK_CAPACITY - 1) * sizeof(*undoStack));
        undoCount--;
    }
    undoStack[undoCount] = (BlockUndo){ x, y, z, prev, next, pendingGroupStart };
    pendingGroupStart = false;
    undoCount++;
    redoCount = 0;
}

void ClearUndoHistory(void)
{
    undoCount = 0;
    redoCount = 0;
    pendingGroupStart = false;
}

void WorldReset(uint32_t seed)
{
    blockEditCount = 0;
    ClearBlockEditIndex();
    memset(torchLights, 0, sizeof(torchLights));
    ClearUndoHistory();
    WorldSetSeed(seed);
}

bool UndoBlockEdit(void)
{
    if (undoCount <= 0) return false;

    int start = undoCount - 1;
    while (start > 0 && !undoStack[start].groupStart) start--;

    for (int i = undoCount - 1; i >= start; i--) {
        redoStack[redoCount++] = undoStack[i];
        SetBlockNoUndo(undoStack[i].x, undoStack[i].y, undoStack[i].z, undoStack[i].prev);
    }
    undoCount = start;
    return true;
}

bool RedoBlockEdit(void)
{
    if (redoCount <= 0) return false;

    int start = redoCount - 1;
    while (start > 0 && !redoStack[start].groupStart) start--;

    for (int i = redoCount - 1; i >= start; i--) {
        undoStack[undoCount++] = redoStack[i];
        SetBlockNoUndo(redoStack[i].x, redoStack[i].y, redoStack[i].z, redoStack[i].next);
    }
    redoCount = start;
    return true;
}
void SetBlockCore(int x, int y, int z, BlockType type, bool recordUndo)
{
    if (!InHeight(y)) return;

    if (recordUndo) {
        BlockType previous = GetBlock(x, y, z);
        if (previous != type) PushBlockUndo(x, y, z, previous, type);
    }

    RememberBlockEdit(x, y, z, type);

    if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
    else TorchLightRemove(x, y, z);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (chunk) {
        chunk->blocks[lx][y][lz] = (unsigned short)type;
        MarkChunkDirtyAtBlock(x, z);
    }
}

BlockType GetBlockAt(int x, int y, int z)
{
    if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP) return SpaceBlockAt(x, y, z);
    if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) return BLOCK_AIR;
    if (y < 0 && y >= NETHER_LAYER_Y) return NetherBlockAt(x, y, z);
    return GetBlock(x, y, z);
}

static void SetBlockNoUndo(int x, int y, int z, BlockType type)
{
    if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP) {
        SpaceSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
        return;
    }
    if (y < 0 && y >= NETHER_LAYER_Y) {
        NetherSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
        return;
    }
    SetBlockCore(x, y, z, type, false);
}

void SetBlock(int x, int y, int z, BlockType type)
{
    if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP) {
        BlockType previous = SpaceBlockAt(x, y, z);
        if (previous != type) PushBlockUndo(x, y, z, previous, type);
        SpaceSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
        return;
    }
    if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) return;
    if (y < 0 && y >= NETHER_LAYER_Y) {
        BlockType previous = NetherBlockAt(x, y, z);
        if (previous != type) PushBlockUndo(x, y, z, previous, type);
        NetherSetBlock(x, y, z, type);
        if (type == BLOCK_TORCH) TorchLightAdd(x, y, z);
        else TorchLightRemove(x, y, z);
        return;
    }
    SetBlockCore(x, y, z, type, true);
}

void SetBlockForImport(int x, int y, int z, BlockType type)
{
    SetBlockCore(x, y, z, type, true);
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

static void BackupSaveFile(void)
{
    FILE *source = fopen(SAVE_FILE, "rb");
    if (!source) return;
    FILE *backup = fopen(SAVE_FILE_BAK, "wb");
    if (!backup) {
        fclose(source);
        return;
    }
    char buffer[8192];
    size_t read = 0;
    while ((read = fread(buffer, 1, sizeof(buffer), source)) > 0) {
        fwrite(buffer, 1, read, backup);
    }
    fclose(source);
    fclose(backup);
}

void SaveMap(const Player *player)
{
    BackupSaveFile();
    FILE *file = fopen(SAVE_FILE, "wb");
    if (!file) {
        SetImportMessage("Save failed: could not open voxelcraft_save.txt.");
        return;
    }

    fwrite(SAVE_MAGIC_V7, 1, SAVE_MAGIC_V7_LEN, file);
    uint32_t seed = WorldGetSeed();
    fwrite(&seed, sizeof(seed), 1, file);
    uint32_t terrain = (uint32_t)terrainMode;
    fwrite(&terrain, sizeof(terrain), 1, file);
    float playerData[6] = {
        player->position.x, player->position.y, player->position.z,
        player->yaw, player->pitch, player->floating ? 1.0f : 0.0f
    };
    fwrite(playerData, sizeof(playerData), 1, file);
    uint32_t editCount = (uint32_t)blockEditCount;
    fwrite(&editCount, sizeof(editCount), 1, file);
    if (blockEditCount > 0) {
        if (fwrite(blockEdits, sizeof(BlockEdit), (size_t)blockEditCount, file) !=
                (size_t)blockEditCount ||
            fwrite(blockEditDimensions, sizeof(*blockEditDimensions),
                   (size_t)blockEditCount, file) != (size_t)blockEditCount) {
            fclose(file);
            SetImportMessage("Save failed: could not write block edits.");
            return;
        }
    }

    if (!InventorySave(file) || !ShipSaveState(file) || !PlanetWorldSaveState(file) ||
        !HomeWorldSaveState(file)) {
        fclose(file);
        SetImportMessage("Save failed: could not write player state.");
        return;
    }
    AlbumSave(file);
    SpaceSaveEdits(file);
    NetherSaveEdits(file);
    fclose(file);
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
    if (*editCount < 0) return false;

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
    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return false;
    if (count > 5000000u) return false;

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

void LoadMap(Player *player)
{
    DrainChunkGen();
    FILE *file = fopen(SAVE_FILE, "rb");
    if (!file) {
        SetImportMessage("Load failed: voxelcraft_save.txt was not found.");
        return;
    }

    TerrainMode savedTerrain = TERRAIN_VARIED;
    uint32_t savedSeed = DEFAULT_WORLD_SEED;
    bool loadedInventory = false;
    bool loadedPlanetWorld = false;
    bool loadedHomeWorld = false;
    bool legacyPlanetCoordinates = false;
    Player savedPlayer = { 0 };
    int savedEditCount = 0;
    BlockEdit *loadedEdits = NULL;
    uint32_t *loadedDimensions = NULL;
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
                            free(loadedEdits);
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

    AlbumLoad(file);
    SpaceLoadEdits(file);
    NetherLoadEdits(file);
    fclose(file);

    if (!loadedPlanetWorld) PlanetWorldReset();
    if (!loadedHomeWorld) HomeWorldRestoreLegacyState(&savedPlayer);

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
    terrainMode = savedTerrain;
    WorldSetSeed(savedSeed);
    if (!loadedInventory) {
        InventoryReset();
        InventoryGrantStarterKit();
        ShipReset();
    }
    *player = savedPlayer;
    blockEditCount = savedEditCount;
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

    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) {
        UpdateChunks(player->position,
                     EffectiveRenderDistanceForHeight(player->position.y + EYE_HEIGHT));
    }
    SetImportMessage(TextFormat("Loaded %s (%d edits).", SAVE_FILE, blockEditCount));
}


int WorldGetEditCount(void)
{
    return blockEditCount;
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
    free(blockEditIndex);
    free(blockEditDimensions);
    free(blockEdits);
}
