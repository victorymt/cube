#include "world/world.h"

#include "raymath.h"
#include "world/block_catalog.h"
#include "world/chunks.h"
#include "space/space_chunks.h"
#include "space/space_state.h"
#include "world/nether.h"
#include "world/terrain.h"
#include "world/world_environment.h"
#include "world/world_extension.h"
#include "world/world_persistence.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

BlockEdit *blockEdits = NULL;
uint32_t *blockEditDimensions = NULL;
SurfaceAddress *blockEditSurfaceAddresses = NULL;
SurfaceMapCell *blockEditSurfaceMapCells = NULL;
BlockEditIndex *blockEditIndex = NULL;
int blockEditCount = 0;
int blockEditCapacity = 0;
static uint64_t blockEditRevision = 1u;
static uint32_t worldSeed = DEFAULT_WORLD_SEED;
static TerrainMode worldTerrainMode = TERRAIN_VARIED;
static WorldExtensionHooks worldExtensionHooks = { 0 };
static WorldMutationSource worldMutationSource = WORLD_MUTATION_PLAYER;

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

bool WorldPersistenceSaveExtension(FILE *file)
{
    return worldExtensionHooks.saveState &&
           worldExtensionHooks.saveState(file);
}

bool WorldPersistenceLoadExtension(FILE *file)
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

static void WorldExtensionOnBlockCommitted(
    int x, int y, int z, BlockType previous, BlockType next)
{
    if (worldExtensionHooks.onBlockCommitted) {
        worldExtensionHooks.onBlockCommitted(x, y, z, previous, next);
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

static void WorldCanonicalizeEditCoordinates(int *x, int *z)
{
    if (!x || !z || !WorldIsSurfaceActive()) return;
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + (float)*x,
        (float)originZ + (float)*z);
    *x = canonical.x - originX;
    *z = canonical.z - originZ;
}
int blockEditIndexCapacity = 0;
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
    return (type >= BLOCK_TALL_GRASS && type <= BLOCK_CHEMO_MAT) ||
           IsBiogenicBlock(type) || IsStage06Block(type);
}

bool IsStage05Block(BlockType type)
{
    return type >= BLOCK_STAGE05_START && type <= BLOCK_STAGE05_END;
}

bool IsStage06Block(BlockType type)
{
    return type >= BLOCK_STAGE06_START && type <= BLOCK_STAGE06_END;
}

bool IsStage05GeologyBlock(BlockType type)
{
    return type >= BLOCK_STAGE05_GEOLOGY_START &&
           type <= BLOCK_STAGE05_GEOLOGY_END;
}

bool IsBiogenicBlock(BlockType type)
{
    return type >= BLOCK_STAGE05_BIOGENIC_START &&
           type <= BLOCK_STAGE05_BIOGENIC_END;
}

bool IsFireResidueBlock(BlockType type)
{
    return type >= BLOCK_FIRE_RESIDUE_START &&
           type <= BLOCK_FIRE_RESIDUE_END;
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

BlockMaterialResponse BlockMaterialResponseFor(BlockType type)
{
    if (IsColorBlock(type)) {
        return (BlockMaterialResponse){ 0.82f, 0.78f, 0.08f, 0.04f };
    }
    const BlockCatalogEntry *entry = BlockCatalogGet(type);
    return (BlockMaterialResponse){
        entry->windResistance,
        entry->impactResistance,
        entry->flammability,
        entry->waterErodibility
    };
}

static unsigned int HashBlockCell(uint32_t bodyId, SurfaceMapCell cell,
                                  int radial)
{
    unsigned int h = 2166136261u;
    h = (h ^ bodyId) * 16777619u;
    h = (h ^ (unsigned int)cell.x) * 16777619u;
    h = (h ^ (unsigned int)cell.z) * 16777619u;
    h = (h ^ (unsigned int)radial) * 16777619u;
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

    uint32_t bodyId = blockEditDimensions[editIndex];
    SurfaceMapCell cell = blockEditSurfaceMapCells[editIndex];
    int radial = blockEdits[editIndex].y;
    unsigned int slot = HashBlockCell(bodyId, cell, radial) &
                        (unsigned int)(blockEditIndexCapacity - 1);

    for (;;) {
        BlockEditIndex *entry = &blockEditIndex[slot];
        if (!entry->used) {
            *entry = (BlockEditIndex){
                bodyId, cell.x, cell.z, radial, editIndex, true
            };
            return;
        }

        if (entry->bodyId == bodyId && entry->mapX == cell.x &&
            entry->mapZ == cell.z && entry->radial == radial) {
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

static int FindBlockEditIndex(uint32_t bodyId, SurfaceMapCell cell,
                              int radial)
{
    if (!blockEditIndex || blockEditIndexCapacity <= 0) return -1;

    unsigned int slot = HashBlockCell(bodyId, cell, radial) &
                        (unsigned int)(blockEditIndexCapacity - 1);
    for (;;) {
        BlockEditIndex *entry = &blockEditIndex[slot];
        if (!entry->used) return -1;
        if (entry->bodyId == bodyId && entry->mapX == cell.x &&
            entry->mapZ == cell.z && entry->radial == radial) {
            return entry->editIndex;
        }
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
    SurfaceMapCell *nextMapCells = malloc(
        (size_t)nextCapacity * sizeof(*nextMapCells));
    if (!nextEdits || !nextDimensions || !nextAddresses || !nextMapCells) {
        free(nextEdits);
        free(nextDimensions);
        free(nextAddresses);
        free(nextMapCells);
        return false;
    }
    if (blockEditCount > 0) {
        memcpy(nextEdits, blockEdits, (size_t)blockEditCount * sizeof(*nextEdits));
        memcpy(nextDimensions, blockEditDimensions,
               (size_t)blockEditCount * sizeof(*nextDimensions));
        memcpy(nextAddresses, blockEditSurfaceAddresses,
               (size_t)blockEditCount * sizeof(*nextAddresses));
        memcpy(nextMapCells, blockEditSurfaceMapCells,
               (size_t)blockEditCount * sizeof(*nextMapCells));
    }
    free(blockEdits);
    free(blockEditDimensions);
    free(blockEditSurfaceAddresses);
    free(blockEditSurfaceMapCells);
    blockEdits = nextEdits;
    blockEditDimensions = nextDimensions;
    blockEditSurfaceAddresses = nextAddresses;
    blockEditSurfaceMapCells = nextMapCells;
    blockEditCapacity = nextCapacity;
    return RebuildBlockEditIndex(blockEditCapacity);
}

bool WorldPersistenceReserveEdits(int capacity)
{
    return capacity >= 0 && EnsureBlockEditCapacity(capacity);
}

bool WorldPersistenceEditsValid(const BlockEdit *edits, int count)
{
    if (count < 0 || (count > 0 && !edits)) return false;
    for (int index = 0; index < count; index++) {
        if (!InHeight(edits[index].y) ||
            !IsValidBlockType(edits[index].type)) return false;
    }
    return true;
}

bool WorldPersistenceInstallEdits(const BlockEdit *edits,
                                  const uint32_t *dimensions,
                                  const SurfaceAddress *addresses,
                                  const SurfaceMapCell *mapCells,
                                  int count)
{
    if (!WorldPersistenceEditsValid(edits, count) ||
        (count > 0 && (!edits || !dimensions || !addresses || !mapCells)) ||
        !EnsureBlockEditCapacity(count)) {
        return false;
    }
    for (int index = 0; index < count; index++) {
        if (!SurfaceAddressIsValid(addresses[index]) ||
            addresses[index].bodyId != dimensions[index] ||
            addresses[index].radial != edits[index].y ||
            !SurfaceAddressEqual(
                addresses[index],
                SurfaceAddressFromMapCoordinates(
                    dimensions[index], (float)mapCells[index].x,
                    (float)mapCells[index].z, edits[index].y))) {
            return false;
        }
    }
    if (count > 0) {
        memcpy(blockEdits, edits, (size_t)count * sizeof(*edits));
        memcpy(blockEditDimensions, dimensions,
               (size_t)count * sizeof(*dimensions));
        memcpy(blockEditSurfaceAddresses, addresses,
               (size_t)count * sizeof(*addresses));
        memcpy(blockEditSurfaceMapCells, mapCells,
               (size_t)count * sizeof(*mapCells));
    }
    blockEditCount = count;
    BumpBlockEditRevision();
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
    WorldCanonicalizeEditCoordinates(&x, &z);
    SurfaceAddress address = SurfaceAddressAtWorld((float)x, (float)z, y);
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)WorldSurfaceMapOriginX() + (float)x,
        (float)WorldSurfaceMapOriginZ() + (float)z);
    int existingIndex = FindBlockEditIndex(dimension, cell, y);
    if (existingIndex >= 0) {
        if (blockEdits[existingIndex].type == type) return;
        blockEdits[existingIndex].type = type;
        BumpBlockEditRevision();
        return;
    }

    if (!EnsureBlockEditCapacity(blockEditCount + 1)) return;

    blockEdits[blockEditCount] = (BlockEdit){ x, y, z, type };
    blockEditDimensions[blockEditCount] = dimension;
    blockEditSurfaceAddresses[blockEditCount] = address;
    blockEditSurfaceMapCells[blockEditCount] = cell;
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
    WorldCanonicalizeEditCoordinates(&x, &z);
    BlockType block = BLOCK_AIR;
    if (WorldGetBlockEditForCurrentDimensionAt(x, y, z, &block)) {
        return block;
    }
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;

    if (ChunkTryGetLocalBlock(chunk, lx, y, lz, &block)) return block;
    return HomeWorldSurfaceIsActive()
        ? TerrainBaseBlockAt(x, y, z, WorldTerrainMode())
        : BLOCK_AIR;
}

bool SurfaceBlockReadyAt(int x, int y, int z)
{
    if (WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE) return true;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    (void)lx;
    (void)lz;
    const Chunk *chunk = FindChunk(cx, cz);
    return chunk && chunk->loaded;
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
    WorldCanonicalizeEditCoordinates(&x, &z);

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
    if (previous != type) {
        WorldExtensionOnBlockCommitted(x, y, z, previous, type);
    }
    return true;
}

bool SetBlockNoUndo(int x, int y, int z, BlockType type)
{
    return SetBlockNoUndoFromSource(
        x, y, z, type, WORLD_MUTATION_SYSTEM);
}

bool SetBlockNoUndoFromSource(int x, int y, int z, BlockType type,
                              WorldMutationSource source)
{
    if (source < WORLD_MUTATION_PLAYER ||
        source > WORLD_MUTATION_ENVIRONMENT) {
        return false;
    }
    WorldMutationSource previousSource = worldMutationSource;
    worldMutationSource = source;
    bool changed = SetBlockNoUndoReplay(x, y, z, type, NULL, false);
    worldMutationSource = previousSource;
    return changed;
}

WorldMutationSource WorldCurrentMutationSource(void)
{
    return worldMutationSource;
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
    if (previous != type) {
        WorldExtensionOnBlockCommitted(x, y, z, previous, type);
    }
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

bool WorldGetEditSurfaceMapCellAt(int index, SurfaceMapCell *outCell)
{
    if (!outCell || index < 0 || index >= blockEditCount) return false;
    *outCell = blockEditSurfaceMapCells[index];
    return outCell->x >= -SURFACE_EQUATOR_BLOCKS / 2 &&
           outCell->x < SURFACE_EQUATOR_BLOCKS / 2 &&
           outCell->z >= -SURFACE_POLE_TO_POLE_BLOCKS / 2 &&
           outCell->z < SURFACE_POLE_TO_POLE_BLOCKS / 2;
}

bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit)
{
    if (!outEdit || index < 0 || index >= blockEditCount ||
        blockEditDimensions[index] != WorldCurrentEditDimension()) {
        return false;
    }
    *outEdit = blockEdits[index];
    outEdit->x = blockEditSurfaceMapCells[index].x -
                 WorldSurfaceMapOriginX();
    outEdit->z = blockEditSurfaceMapCells[index].z -
                 WorldSurfaceMapOriginZ();
    return true;
}

bool WorldGetBlockEditForCurrentDimensionAt(int x, int y, int z,
                                            BlockType *outType)
{
    if (!outType) return false;
    WorldCanonicalizeEditCoordinates(&x, &z);
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)WorldSurfaceMapOriginX() + (float)x,
        (float)WorldSurfaceMapOriginZ() + (float)z);
    int index = FindBlockEditIndex(WorldCurrentEditDimension(), cell, y);
    if (index < 0) return false;
    *outType = blockEdits[index].type;
    return true;
}

void WorldCleanup(void)
{
    WorldExtensionCleanup();
    free(blockEditIndex);
    free(blockEditSurfaceAddresses);
    free(blockEditSurfaceMapCells);
    free(blockEditDimensions);
    free(blockEdits);
}
