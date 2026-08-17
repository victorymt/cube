#define _POSIX_C_SOURCE 200809L
#include "world/world.h"

#include "raymath.h"
#include "world/block_catalog.h"
#include "world/chunks.h"
#include "gameplay/player.h"
#include "space/space.h"
#include "world/nether.h"
#include "gameplay/album.h"
#include "gameplay/inventory.h"
#include "gameplay/map_markers.h"
#include "gameplay/ship.h"
#include "ecology/entity.h"
#include "ecology/ecology.h"
#include "ecology/evolution_catalog.h"
#include "world/terrain.h"
#include "world/world_environment.h"
#include "world/world_extension.h"
#include "world/surface_save.h"
#include "world/save_format_internal.h"
#include "core/save_io.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define SAVE_FILE_BAK "voxelcraft_save.bak"
#define TERRAIN_GENERATION_VERSION 6u
#define MIN_SUPPORTED_TERRAIN_GENERATION_VERSION 2u
#define SAVE_MAX_FILE_BYTES (256u * 1024u * 1024u)
// Upper bound for edit counts read from save files. Keeps transient
// allocations (edits + dimensions + the edit-index hash) bounded for
// crafted/corrupt files; normal saves are far below this.
#define MAX_LOAD_EDIT_COUNT 1000000u

BlockEdit *blockEdits = NULL;
uint32_t *blockEditDimensions = NULL;
SurfaceAddress *blockEditSurfaceAddresses = NULL;
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

void WorldNotifyChunkSectionLoaded(Chunk *chunk, int sectionY)
{
    if (worldExtensionHooks.onChunkSectionLoaded) {
        worldExtensionHooks.onChunkSectionLoaded(chunk, sectionY);
    }
}

bool WorldPrepareChunkSectionUnload(Chunk *chunk, int sectionY)
{
    return worldExtensionHooks.prepareChunkSectionUnload &&
           worldExtensionHooks.prepareChunkSectionUnload(chunk, sectionY);
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
    return BlockCatalogGet(type)->name;
}

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

bool IsValidBlockType(BlockType type)
{
    return (type >= BLOCK_AIR && type <= BLOCK_NATURAL_END) ||
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

BlockRenderShape BlockRenderShapeFor(BlockType type)
{
    if (IsColorBlock(type)) return BLOCK_RENDER_CUBE;
    return BlockCatalogGet(type)->renderShape;
}

bool IsPlantBlock(BlockType type)
{
    BlockRenderShape shape = BlockRenderShapeFor(type);
    return shape == BLOCK_RENDER_CROSS || shape == BLOCK_RENDER_CARPET;
}

bool IsEcologyBlock(BlockType type)
{
    return type >= BLOCK_TALL_GRASS && type <= BLOCK_CHEMO_MAT;
}

float BlockCollisionHeight(BlockType type)
{
    if (IsColorBlock(type)) return 1.0f;
    return BlockCatalogGet(type)->collisionHeight;
}

float BlockCollisionHeightAt(int x, int y, int z)
{
    if (!WorldCanAccessBlockY(y)) return 0.0f;
    return BlockCollisionHeight(GetBlockAt(x, y, z));
}

bool IsTranslucentBlock(BlockType type)
{
    if (IsColorBlock(type)) return false;
    return BlockCatalogGet(type)->translucent;
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
    return BlockCatalogGet(type)->baseColor;
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
    SurfaceAddress *nextAddresses = malloc(
        (size_t)nextCapacity * sizeof(*nextAddresses));
    if (!nextEdits || !nextDimensions || !nextAddresses) {
        free(nextEdits);
        free(nextDimensions);
        free(nextAddresses);
        return false;
    }
    if (blockEditCount > 0) {
        memcpy(nextEdits, blockEdits, (size_t)blockEditCount * sizeof(*nextEdits));
        memcpy(nextDimensions, blockEditDimensions,
               (size_t)blockEditCount * sizeof(*nextDimensions));
        memcpy(nextAddresses, blockEditSurfaceAddresses,
               (size_t)blockEditCount * sizeof(*nextAddresses));
    }
    free(blockEdits);
    free(blockEditDimensions);
    free(blockEditSurfaceAddresses);
    blockEdits = nextEdits;
    blockEditDimensions = nextDimensions;
    blockEditSurfaceAddresses = nextAddresses;
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
    blockEditSurfaceAddresses[blockEditCount] = SurfaceAddressAtWorld(
        (float)x, (float)z, y);
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
    MapMarkersReset();
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
    if (!ChunkGetSection(chunk, sectionY, true)) return false;
    ApplyEditsToChunkSection(chunk, sectionY);
    WorldNotifyChunkSectionLoaded(chunk, sectionY);
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
        int sectionY = SurfaceSectionYFromBlockY(y);
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

static bool WriteSphericalSaveTrailer(FILE *file, const Player *player)
{
    if (!file || !player) return false;
    WorldDimension dimension = WorldCurrentDimension();
    bool playerHasSurfaceAddress = WorldIsSurfaceDimension(dimension);
    SurfaceAddress playerAddress = playerHasSurfaceAddress
        ? SurfaceAddressAtWorld(player->position.x, player->position.z,
                                (int)floorf(player->position.y))
        : SurfaceAddressFromMapCoordinates(0u, 0.0f, 0.0f, 0);
    bool ok = true;
    for (int index = 0; ok && index < blockEditCount; index++) {
        SurfaceAddress address = blockEditSurfaceAddresses[index];
        ok = address.bodyId == blockEditDimensions[index] &&
             address.radial == blockEdits[index].y;
    }
    return ok && SurfaceSaveWriteTrailer(
        file, playerHasSurfaceAddress, playerAddress,
        blockEditSurfaceAddresses, (uint32_t)blockEditCount);
}

static bool ReadSphericalSaveTrailer(
    FILE *file, WorldDimension savedDimension, const Player *savedPlayer,
    const BlockEdit *loadedEdits, const uint32_t *loadedDimensions,
    int editCount, SurfaceAddress *outPlayerAddress,
    SurfaceAddress **outEditAddresses)
{
    if (!file || !savedPlayer || !outPlayerAddress || !outEditAddresses ||
        editCount < 0) return false;
    bool playerHasSurfaceAddress = false;
    SurfaceAddress playerAddress = { 0 };
    SurfaceAddress *addresses = NULL;
    if (!SurfaceSaveReadTrailer(
            file, (uint32_t)editCount, &playerHasSurfaceAddress,
            &playerAddress, &addresses)) {
        return false;
    }

    bool surfaceDimension = WorldIsSurfaceDimension(savedDimension);
    uint32_t expectedBodyId = savedDimension == WORLD_DIMENSION_PLANET
        ? PlanetWorldSeed() : 0u;
    if (playerHasSurfaceAddress != surfaceDimension ||
        (surfaceDimension && playerAddress.bodyId != expectedBodyId)) {
        free(addresses);
        return false;
    }
    if (surfaceDimension) {
        SurfaceAddress expected = SurfaceAddressFromMapCoordinates(
            expectedBodyId,
            savedPlayer->position.x + (float)WorldSurfaceMapOriginX(),
            savedPlayer->position.z + (float)WorldSurfaceMapOriginZ(),
            (int)floorf(savedPlayer->position.y));
        if (!SurfaceAddressEqual(playerAddress, expected)) {
            free(addresses);
            return false;
        }
    }

    for (int index = 0; index < editCount; index++) {
        if (!loadedEdits || !loadedDimensions ||
            addresses[index].bodyId != loadedDimensions[index] ||
            addresses[index].radial != loadedEdits[index].y) {
            free(addresses);
            return false;
        }
    }
    *outPlayerAddress = playerAddress;
    *outEditAddresses = addresses;
    return true;
}

static bool WriteSaveFile(FILE *file, void *opaque)
{
    const SaveMapContext *context = opaque;
    const Player *player = context ? context->player : NULL;
    if (!file || !player) return false;

    bool ok = WorldSaveFormatWriteCurrent(file);
    uint32_t terrainGenerationVersion = TERRAIN_GENERATION_VERSION;
    uint32_t activeDimension = (uint32_t)WorldCurrentDimension();
    uint32_t seed = WorldGetSeed();
    uint32_t terrain = (uint32_t)worldTerrainMode;
    float playerData[6] = {
        player->position.x, player->position.y, player->position.z,
        player->yaw, player->pitch, player->floating ? 1.0f : 0.0f
    };
    uint32_t editCount = (uint32_t)blockEditCount;
    ok = ok && fwrite(&terrainGenerationVersion,
                      sizeof(terrainGenerationVersion), 1, file) == 1;
    ok = ok && fwrite(&activeDimension, sizeof(activeDimension), 1, file) == 1;
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
         MapMarkersSaveState(file) &&
         WriteSphericalSaveTrailer(file, player) &&
         !ferror(file);
    return ok;
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

typedef struct LoadedMapData {
    TerrainMode terrain;
    uint32_t seed;
    uint32_t terrainGenerationVersion;
    WorldDimension dimension;
    Player player;
    int editCount;
    BlockEdit *edits;
    uint32_t *dimensions;
    SurfaceAddress playerAddress;
    SurfaceAddress *editAddresses;
    ShipLocatorRecord shipLocator;
    MapMarkerState mapMarkers;
} LoadedMapData;

static void LoadedMapDataRelease(LoadedMapData *data)
{
    if (!data) return;
    free(data->edits);
    free(data->dimensions);
    free(data->editAddresses);
    data->edits = NULL;
    data->dimensions = NULL;
    data->editAddresses = NULL;
}

static bool LoadBlockEditPayload(FILE *file, LoadedMapData *data)
{
    uint32_t seed = DEFAULT_WORLD_SEED;
    uint32_t terrain = 0;
    float playerData[6];
    uint32_t count = 0;
    if (!file || !data ||
        fread(&seed, sizeof(seed), 1, file) != 1 ||
        fread(&terrain, sizeof(terrain), 1, file) != 1 ||
        terrain > (uint32_t)TERRAIN_FLAT ||
        fread(playerData, sizeof(playerData), 1, file) != 1 ||
        fread(&count, sizeof(count), 1, file) != 1 ||
        count > MAX_LOAD_EDIT_COUNT) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (!isfinite(playerData[i])) return false;
    }
    if (playerData[5] != 0.0f && playerData[5] != 1.0f) return false;

    BlockEdit *edits = NULL;
    uint32_t *dimensions = NULL;
    if (count > 0) {
        edits = malloc((size_t)count * sizeof(*edits));
        dimensions = malloc((size_t)count * sizeof(*dimensions));
        if (!edits || !dimensions ||
            fread(edits, sizeof(*edits), (size_t)count, file) != count ||
            fread(dimensions, sizeof(*dimensions), (size_t)count, file) != count) {
            free(edits);
            free(dimensions);
            return false;
        }
        for (uint32_t i = 0; i < count; i++) {
            if (!InHeight(edits[i].y) || !IsValidBlockType(edits[i].type)) {
                free(edits);
                free(dimensions);
                return false;
            }
        }
    }

    data->terrain = (TerrainMode)terrain;
    data->seed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
    data->player.position = (Vector3){
        playerData[0], playerData[1], playerData[2]
    };
    data->player.yaw = playerData[3];
    data->player.pitch = playerData[4];
    data->player.floating = playerData[5] != 0.0f;
    data->player.velocity = Vector3Zero();
    data->player.onGround = false;
    data->editCount = (int)count;
    data->edits = edits;
    data->dimensions = dimensions;
    return true;
}

static bool LoadCurrentCorePayload(FILE *file, LoadedMapData *data)
{
    uint32_t terrainGenerationVersion = 0;
    uint32_t activeDimension = 0;
    if (!file || !data ||
        fread(&terrainGenerationVersion, sizeof(terrainGenerationVersion),
              1, file) != 1 ||
        terrainGenerationVersion < MIN_SUPPORTED_TERRAIN_GENERATION_VERSION ||
        terrainGenerationVersion > TERRAIN_GENERATION_VERSION ||
        fread(&activeDimension, sizeof(activeDimension), 1, file) != 1 ||
        activeDimension > (uint32_t)WORLD_DIMENSION_NETHER ||
        !LoadBlockEditPayload(file, data)) {
        return false;
    }
    if (!InventoryLoad(file) || !ShipLoadState(file) ||
        !PlanetWorldLoadState(file) || !HomeWorldLoadState(file)) {
        LoadedMapDataRelease(data);
        return false;
    }
    data->terrainGenerationVersion = terrainGenerationVersion;
    data->dimension = (WorldDimension)activeDimension;
    return true;
}

static const char *LoadCurrentExtendedPayload(
    FILE *file, WorldSaveFormat format, LoadedMapData *data)
{
    if (!AlbumLoad(file) || !SpaceLoadEdits(file, SPACE_LAYER_Y) ||
        !NetherLoadEdits(file)) {
        return "Load failed: save file is corrupted.";
    }
    if (!SpaceLoadState(file)) {
        return SpaceLastLoadError() == SPACE_LOAD_ERROR_INCOMPATIBLE_SCALE
            ? "Load failed: save uses the retired 20 u/AU space scale."
            : "Load failed: save file is corrupted.";
    }
    if (!EntitiesLoadState(file)) {
        return "Load failed: entity state is corrupted.";
    }
    if (!PlanetEcologyLoadState(file)) {
        return "Load failed: ecology state is corrupted.";
    }
    if (!EvolutionCatalogLoadState(file)) {
        return "Load failed: evolution catalog state is corrupted.";
    }
    if (!ShipLocatorReadStateForSpaceLayer(
            file, &data->shipLocator, SPACE_LAYER_Y)) {
        return "Load failed: ship locator state is corrupted.";
    }
    if (!WorldExtensionLoadState(file)) {
        return "Load failed: fluid state is corrupted.";
    }
    if (WorldSaveFormatHasMapMarkers(format) &&
        !MapMarkersReadState(file, &data->mapMarkers)) {
        return "Load failed: map marker state is corrupted.";
    }
    if (!ReadSphericalSaveTrailer(
            file, data->dimension, &data->player, data->edits,
            data->dimensions, data->editCount, &data->playerAddress,
            &data->editAddresses)) {
        return "Load failed: spherical save state is corrupted.";
    }
    return NULL;
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

    LoadedMapData data = {
        .terrain = TERRAIN_VARIED,
        .seed = DEFAULT_WORLD_SEED,
        .dimension = WORLD_DIMENSION_HOME
    };
    MapMarkersEmptyState(&data.mapMarkers);

    WorldSaveFormat format = WorldSaveFormatRead(file);
    if (format == WORLD_SAVE_FORMAT_UNSUPPORTED) {
        fclose(file);
        SetImportMessage(
            "Load failed: V17 and older flat saves are incompatible with spherical worlds.");
        return;
    }
    if (!LoadCurrentCorePayload(file, &data)) {
        fclose(file);
        LoadedMapDataRelease(&data);
        SetImportMessage("Load failed: save file is corrupted.");
        return;
    }

    const char *loadError = LoadCurrentExtendedPayload(file, format, &data);
    fclose(file);
    if (loadError) {
        LoadedMapDataRelease(&data);
        SetImportMessage(loadError);
        return;
    }
    if (!EnsureBlockEditCapacity(data.editCount)) {
        LoadedMapDataRelease(&data);
        SetImportMessage("Load failed: not enough memory to apply save.");
        return;
    }

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    UnloadAllNetherChunks();
    worldTerrainMode = data.terrain;
    WorldSetSeed(data.seed);

    bool savedInNether = data.dimension == WORLD_DIMENSION_NETHER;
    if (data.terrainGenerationVersion != TERRAIN_GENERATION_VERSION &&
        WorldIsSurfaceActive() && !savedInNether) {
        int landingX = (int)floorf(data.player.position.x);
        int landingZ = (int)floorf(data.player.position.z);
        int groundY = 0;
        if (FindSafeSurfaceLanding(landingX, landingZ, 128, 0,
                                   &landingX, &landingZ, &groundY)) {
            data.player.position = (Vector3){
                (float)landingX + 0.5f, (float)groundY + 3.0f,
                (float)landingZ + 0.5f
            };
        }
    }

    *player = data.player;
    WorldSetNetherActive(savedInNether);
    PlayerResetRuntimeState(player);
    blockEditCount = data.editCount;
    BumpBlockEditRevision();
    if (data.editCount > 0) {
        memcpy(blockEdits, data.edits,
               (size_t)data.editCount * sizeof(*data.edits));
        memcpy(blockEditDimensions, data.dimensions,
               (size_t)data.editCount * sizeof(*data.dimensions));
        memcpy(blockEditSurfaceAddresses, data.editAddresses,
               (size_t)data.editCount * sizeof(*data.editAddresses));
    }
    LoadedMapDataRelease(&data);

    if (!RebuildBlockEditIndex(blockEditCapacity)) {
        SetImportMessage("Load warning: edit index rebuild failed.");
        return;
    }
    RebuildTorchList();
    SpaceRebuildTorchList();
    ClearUndoHistory();
    ShipLocatorSetRecord(&data.shipLocator);
    MapMarkersInstallState(&data.mapMarkers);

    if (WorldIsSurfaceActive()) {
        UpdateChunks(player->position,
                     EffectiveRenderDistanceForHeight(
                         player->position.y + EYE_HEIGHT));
    }
    SetImportMessage(TextFormat("Loaded %s (%d edits).", SAVE_FILE,
                                blockEditCount));
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

bool WorldGetEditSurfaceAddressAt(int index, SurfaceAddress *outAddress)
{
    if (!outAddress || index < 0 || index >= blockEditCount) return false;
    *outAddress = blockEditSurfaceAddresses[index];
    return SurfaceAddressIsValid(*outAddress);
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
    free(blockEditSurfaceAddresses);
    free(blockEditDimensions);
    free(blockEdits);
}
