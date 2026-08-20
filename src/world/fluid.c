#include "world/fluid.h"

#include "world/chunks_internal.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define FLUID_SECTION_CELLS \
    (CHUNK_SIZE * SURFACE_SECTION_HEIGHT * CHUNK_SIZE)
#define FLUID_QUEUE_CAPACITY 131072u
#define FLUID_INITIAL_EDIT_CAPACITY 1024u
#define FLUID_MAX_EDIT_COUNT 2000000u
#define FLUID_HORIZONTAL_FLOW_LIMIT 64u
#define FLUID_FLOW_COMPONENTS 3
#define FLUID_UPDATE_CPU_BUDGET_MS 4.0
#define FLUID_TIME_CHECK_INTERVAL 32u

typedef struct FluidQueueCell {
    int x;
    int y;
    int z;
    uint32_t surfaceId;
} FluidQueueCell;

typedef struct FluidEdit {
    int x;
    int y;
    int z;
    uint32_t surfaceId;
    uint8_t volume;
    uint8_t baseline;
    bool baselineKnown;
    uint32_t chunkListIndex;
} FluidEdit;

typedef struct FluidEditIndex {
    int x;
    int y;
    int z;
    uint32_t surfaceId;
    uint32_t editIndex;
    bool used;
} FluidEditIndex;

typedef struct FluidMutation {
    int x;
    int y;
    int z;
    uint8_t volume;
} FluidMutation;

typedef struct FluidChunkEditBucket {
    int cx;
    int cz;
    uint32_t surfaceId;
    uint32_t *editIndices;
    uint32_t count;
    uint32_t capacity;
    unsigned char state;
} FluidChunkEditBucket;

enum {
    FLUID_CHUNK_BUCKET_EMPTY = 0,
    FLUID_CHUNK_BUCKET_USED = 1,
    FLUID_CHUNK_BUCKET_DELETED = 2
};

static FluidQueueCell fluidQueue[FLUID_QUEUE_CAPACITY];
static uint32_t fluidQueueHead = 0u;
static uint32_t fluidQueueCount = 0u;
static bool fluidDeferredPending = false;
static FluidEdit *fluidEdits = NULL;
static uint32_t fluidEditCount = 0u;
static uint32_t fluidEditCapacity = 0u;
static FluidEditIndex *fluidEditIndex = NULL;
static uint32_t fluidEditIndexCapacity = 0u;
static FluidChunkEditBucket *fluidChunkEditIndex = NULL;
static uint32_t fluidChunkEditIndexCapacity = 0u;
static uint32_t fluidChunkEditCount = 0u;
static uint32_t fluidChunkEditDeleted = 0u;
static float fluidTickAccumulator = 0.0f;
static double fluidCellBudget = 0.0;
static FluidStats fluidStats = { 0 };

static bool FluidSetVolumeInternal(int x, int y, int z, uint8_t volume,
                                   bool remember);
static void FluidClearLoadedRuntime(void);

static double FluidThreadCpuNowMs(void)
{
    struct timespec value = { 0 };
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 +
           (double)value.tv_nsec / 1000000.0;
}

static void FluidCanonicalizeXZ(int *x, int *z)
{
    if (!x || !z) return;
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + (float)*x,
        (float)originZ + (float)*z);
    *x = canonical.x - originX;
    *z = canonical.z - originZ;
}

static FluidMutation FluidCanonicalMutation(FluidMutation mutation)
{
    FluidCanonicalizeXZ(&mutation.x, &mutation.z);
    return mutation;
}

static int FluidCellIndex(int lx, int ly, int lz)
{
    return (lx * SURFACE_SECTION_HEIGHT + ly) * CHUNK_SIZE + lz;
}

static uint32_t FluidHash(uint32_t surfaceId, int x, int y, int z)
{
    uint32_t hash = surfaceId ^ 0x9e3779b9u;
    hash ^= (uint32_t)x * 0x85ebca6bu;
    hash = (hash << 13) | (hash >> 19);
    hash ^= (uint32_t)y * 0xc2b2ae35u;
    hash = (hash << 11) | (hash >> 21);
    hash ^= (uint32_t)z * 0x27d4eb2du;
    hash ^= hash >> 16;
    return hash;
}

static uint32_t FluidChunkHash(uint32_t surfaceId, int cx, int cz)
{
    uint32_t hash = surfaceId ^ 0x517cc1b7u;
    hash ^= (uint32_t)cx * 0x9e3779b9u;
    hash = (hash << 13) | (hash >> 19);
    hash ^= (uint32_t)cz * 0x85ebca6bu;
    hash ^= hash >> 16;
    return hash;
}

static void FluidFreeChunkEditIndex(FluidChunkEditBucket *index,
                                    uint32_t capacity)
{
    if (!index) return;
    for (uint32_t slot = 0u; slot < capacity; slot++) {
        if (index[slot].state == FLUID_CHUNK_BUCKET_USED) {
            free(index[slot].editIndices);
        }
    }
    free(index);
}

static int FluidFindChunkEditBucket(uint32_t surfaceId, int cx, int cz)
{
    if (!fluidChunkEditIndex || fluidChunkEditIndexCapacity == 0u) return -1;
    uint32_t slot = FluidChunkHash(surfaceId, cx, cz) &
                    (fluidChunkEditIndexCapacity - 1u);
    uint32_t start = slot;
    while (fluidChunkEditIndex[slot].state != FLUID_CHUNK_BUCKET_EMPTY) {
        const FluidChunkEditBucket *bucket = &fluidChunkEditIndex[slot];
        if (bucket->state == FLUID_CHUNK_BUCKET_USED &&
            bucket->surfaceId == surfaceId && bucket->cx == cx &&
            bucket->cz == cz) {
            return (int)slot;
        }
        slot = (slot + 1u) & (fluidChunkEditIndexCapacity - 1u);
        if (slot == start) break;
    }
    return -1;
}

static uint32_t FluidFindChunkEditInsertSlot(uint32_t surfaceId, int cx,
                                             int cz)
{
    uint32_t slot = FluidChunkHash(surfaceId, cx, cz) &
                    (fluidChunkEditIndexCapacity - 1u);
    uint32_t firstDeleted = UINT32_MAX;
    for (;;) {
        const FluidChunkEditBucket *bucket = &fluidChunkEditIndex[slot];
        if (bucket->state == FLUID_CHUNK_BUCKET_EMPTY) {
            return firstDeleted != UINT32_MAX ? firstDeleted : slot;
        }
        if (bucket->state == FLUID_CHUNK_BUCKET_DELETED &&
            firstDeleted == UINT32_MAX) {
            firstDeleted = slot;
        } else if (bucket->state == FLUID_CHUNK_BUCKET_USED &&
                   bucket->surfaceId == surfaceId && bucket->cx == cx &&
                   bucket->cz == cz) {
            return slot;
        }
        slot = (slot + 1u) & (fluidChunkEditIndexCapacity - 1u);
    }
}

static bool FluidRebuildChunkEditIndex(uint32_t capacity)
{
    FluidChunkEditBucket *next = calloc(capacity, sizeof(*next));
    if (!next) return false;
    FluidChunkEditBucket *previous = fluidChunkEditIndex;
    uint32_t previousCapacity = fluidChunkEditIndexCapacity;
    fluidChunkEditIndex = next;
    fluidChunkEditIndexCapacity = capacity;
    fluidChunkEditDeleted = 0u;
    if (previous) {
        for (uint32_t slot = 0u; slot < previousCapacity; slot++) {
            FluidChunkEditBucket *bucket = &previous[slot];
            if (bucket->state != FLUID_CHUNK_BUCKET_USED) continue;
            uint32_t destination = FluidFindChunkEditInsertSlot(
                bucket->surfaceId, bucket->cx, bucket->cz);
            fluidChunkEditIndex[destination] = *bucket;
        }
        free(previous);
    }
    return true;
}

static bool FluidEnsureChunkEditIndex(void)
{
    uint32_t capacity = fluidChunkEditIndexCapacity > 0u
        ? fluidChunkEditIndexCapacity : 256u;
    bool rebuild = fluidChunkEditIndexCapacity == 0u ||
        (fluidChunkEditCount + fluidChunkEditDeleted + 1u) * 2u >= capacity;
    if (!rebuild) return true;
    while ((fluidChunkEditCount + 1u) * 2u >= capacity) capacity <<= 1u;
    return FluidRebuildChunkEditIndex(capacity);
}

static bool FluidReserveChunkEditCapacity(uint32_t surfaceId, int cx, int cz,
                                          uint32_t wanted)
{
    int existing = FluidFindChunkEditBucket(surfaceId, cx, cz);
    if (existing < 0 && !FluidEnsureChunkEditIndex()) return false;
    uint32_t slot = existing >= 0 ? (uint32_t)existing :
        FluidFindChunkEditInsertSlot(surfaceId, cx, cz);
    FluidChunkEditBucket *bucket = &fluidChunkEditIndex[slot];
    if (bucket->state != FLUID_CHUNK_BUCKET_USED) {
        if (bucket->state == FLUID_CHUNK_BUCKET_DELETED) {
            fluidChunkEditDeleted--;
        }
        *bucket = (FluidChunkEditBucket){
            .cx = cx,
            .cz = cz,
            .surfaceId = surfaceId,
            .state = FLUID_CHUNK_BUCKET_USED
        };
        fluidChunkEditCount++;
    }
    if (wanted > bucket->capacity) {
        uint32_t capacity = bucket->capacity > 0u ? bucket->capacity * 2u : 8u;
        while (capacity < wanted) capacity *= 2u;
        uint32_t *indices = realloc(
            bucket->editIndices, (size_t)capacity * sizeof(*indices));
        if (!indices) {
            if (bucket->count == 0u) {
                bucket->state = FLUID_CHUNK_BUCKET_DELETED;
                fluidChunkEditCount--;
                fluidChunkEditDeleted++;
            }
            return false;
        }
        bucket->editIndices = indices;
        bucket->capacity = capacity;
    }
    return true;
}

static bool FluidAppendChunkEdit(uint32_t editIndex)
{
    FluidEdit *edit = &fluidEdits[editIndex];
    int cx = FloorDivInt(edit->x, CHUNK_SIZE);
    int cz = FloorDivInt(edit->z, CHUNK_SIZE);
    int existing = FluidFindChunkEditBucket(edit->surfaceId, cx, cz);
    uint32_t wanted = existing >= 0
        ? fluidChunkEditIndex[existing].count + 1u : 1u;
    if (!FluidReserveChunkEditCapacity(
            edit->surfaceId, cx, cz, wanted)) return false;
    uint32_t slot = (uint32_t)FluidFindChunkEditBucket(
        edit->surfaceId, cx, cz);
    FluidChunkEditBucket *bucket = &fluidChunkEditIndex[slot];
    edit->chunkListIndex = bucket->count;
    bucket->editIndices[bucket->count++] = editIndex;
    return true;
}

static void FluidRemoveChunkEdit(uint32_t editIndex)
{
    FluidEdit *edit = &fluidEdits[editIndex];
    int cx = FloorDivInt(edit->x, CHUNK_SIZE);
    int cz = FloorDivInt(edit->z, CHUNK_SIZE);
    int slot = FluidFindChunkEditBucket(edit->surfaceId, cx, cz);
    if (slot < 0) return;
    FluidChunkEditBucket *bucket = &fluidChunkEditIndex[slot];
    uint32_t listIndex = edit->chunkListIndex;
    if (listIndex >= bucket->count) return;
    uint32_t movedEdit = bucket->editIndices[bucket->count - 1u];
    bucket->editIndices[listIndex] = movedEdit;
    fluidEdits[movedEdit].chunkListIndex = listIndex;
    bucket->count--;
    if (bucket->count == 0u) {
        free(bucket->editIndices);
        bucket->editIndices = NULL;
        bucket->capacity = 0u;
        bucket->state = FLUID_CHUNK_BUCKET_DELETED;
        fluidChunkEditCount--;
        fluidChunkEditDeleted++;
    }
}

static bool FluidBuildAllChunkEditLists(void)
{
    for (uint32_t index = 0u; index < fluidEditCount; index++) {
        if (!FluidAppendChunkEdit(index)) return false;
    }
    return true;
}

static bool FluidLocateCell(int x, int y, int z, Chunk **outChunk,
                            ChunkSection **outSection, int *outIndex,
                            BlockType *outBlock)
{
    if (!InHeight(y)) return false;
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
    int ly = SurfaceSectionLocalYFromBlockY(y);
    BlockType block = section
        ? (BlockType)section->blocks[lx][ly][lz] : BLOCK_AIR;
    if (outChunk) *outChunk = chunk;
    if (outSection) *outSection = section;
    if (outIndex) *outIndex = FluidCellIndex(lx, ly, lz);
    if (outBlock) *outBlock = block;
    return true;
}

static bool FluidEnsureVolumes(ChunkSection *section)
{
    if (!section) return false;
    if (section->waterVolumes) return true;
    section->waterVolumes = malloc(FLUID_SECTION_CELLS);
    if (!section->waterVolumes) return false;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int index = FluidCellIndex(lx, ly, lz);
                section->waterVolumes[index] =
                    section->blocks[lx][ly][lz] == BLOCK_WATER
                        ? (uint8_t)FLUID_CAPACITY : 0u;
            }
        }
    }
    return true;
}

static bool FluidEnsureQueuedBits(ChunkSection *section)
{
    if (!section) return false;
    size_t bytes = (FLUID_SECTION_CELLS + 7u) / 8u;
    if (!section->fluidQueuedBits) {
        section->fluidQueuedBits = calloc(bytes, 1u);
        if (!section->fluidQueuedBits) return false;
    }
    if (!section->fluidDeferredBits) {
        section->fluidDeferredBits = calloc(bytes, 1u);
        if (!section->fluidDeferredBits) return false;
    }
    return true;
}

static bool FluidEnsureFlow(ChunkSection *section)
{
    if (!section) return false;
    if (section->fluidFlow) return true;
    section->fluidFlow = calloc(
        FLUID_SECTION_CELLS * FLUID_FLOW_COMPONENTS, sizeof(*section->fluidFlow));
    return section->fluidFlow != NULL;
}

static bool FluidQueuedBit(const ChunkSection *section, int index)
{
    if (!section || !section->fluidQueuedBits) return false;
    return (section->fluidQueuedBits[index >> 3] &
            (unsigned char)(1u << (index & 7))) != 0u;
}

static bool FluidDeferredBit(const ChunkSection *section, int index)
{
    if (!section || !section->fluidDeferredBits) return false;
    return (section->fluidDeferredBits[index >> 3] &
            (unsigned char)(1u << (index & 7))) != 0u;
}

static void FluidSetQueuedBit(ChunkSection *section, int index, bool queued)
{
    if (!section || !section->fluidQueuedBits) return;
    unsigned char mask = (unsigned char)(1u << (index & 7));
    if (queued) section->fluidQueuedBits[index >> 3] |= mask;
    else section->fluidQueuedBits[index >> 3] &= (unsigned char)~mask;
}

static void FluidSetDeferredBit(ChunkSection *section, int index, bool deferred)
{
    if (!section || !section->fluidDeferredBits) return;
    unsigned char mask = (unsigned char)(1u << (index & 7));
    if (deferred) section->fluidDeferredBits[index >> 3] |= mask;
    else section->fluidDeferredBits[index >> 3] &= (unsigned char)~mask;
}

static bool FluidRebuildEditIndex(uint32_t wantedCapacity)
{
    uint32_t capacity = FLUID_INITIAL_EDIT_CAPACITY;
    while (capacity < wantedCapacity * 2u && capacity < (1u << 30)) {
        capacity <<= 1;
    }
    if (capacity == fluidEditIndexCapacity && fluidEditIndex) {
        memset(fluidEditIndex, 0,
               (size_t)capacity * sizeof(*fluidEditIndex));
    } else {
        FluidEditIndex *next = calloc(capacity, sizeof(*next));
        if (!next) return false;
        free(fluidEditIndex);
        fluidEditIndex = next;
        fluidEditIndexCapacity = capacity;
    }
    for (uint32_t index = 0; index < fluidEditCount; index++) {
        const FluidEdit *edit = &fluidEdits[index];
        uint32_t slot = FluidHash(edit->surfaceId, edit->x, edit->y, edit->z) &
                        (fluidEditIndexCapacity - 1u);
        while (fluidEditIndex[slot].used) {
            slot = (slot + 1u) & (fluidEditIndexCapacity - 1u);
        }
        fluidEditIndex[slot] = (FluidEditIndex){
            edit->x, edit->y, edit->z, edit->surfaceId, index, true
        };
    }
    return true;
}

static int FluidFindEditSlot(uint32_t surfaceId, int x, int y, int z)
{
    if (!fluidEditIndex || fluidEditIndexCapacity == 0u) return -1;
    uint32_t slot = FluidHash(surfaceId, x, y, z) &
                    (fluidEditIndexCapacity - 1u);
    uint32_t start = slot;
    while (fluidEditIndex[slot].used) {
        const FluidEditIndex *entry = &fluidEditIndex[slot];
        if (entry->surfaceId == surfaceId && entry->x == x && entry->y == y &&
            entry->z == z) {
            return (int)slot;
        }
        slot = (slot + 1u) & (fluidEditIndexCapacity - 1u);
        if (slot == start) break;
    }
    return -1;
}

static int FluidFindEdit(uint32_t surfaceId, int x, int y, int z)
{
    FluidCanonicalizeXZ(&x, &z);
    int slot = FluidFindEditSlot(surfaceId, x, y, z);
    return slot >= 0 ? (int)fluidEditIndex[slot].editIndex : -1;
}

static void FluidEraseEditIndexSlot(uint32_t slot)
{
    uint32_t mask = fluidEditIndexCapacity - 1u;
    uint32_t hole = slot;
    uint32_t next = (hole + 1u) & mask;
    while (fluidEditIndex[next].used) {
        const FluidEditIndex *entry = &fluidEditIndex[next];
        uint32_t ideal = FluidHash(
            entry->surfaceId, entry->x, entry->y, entry->z) & mask;
        uint32_t holeDistance = (hole - ideal) & mask;
        uint32_t nextDistance = (next - ideal) & mask;
        if (holeDistance < nextDistance) {
            fluidEditIndex[hole] = fluidEditIndex[next];
            hole = next;
        }
        next = (next + 1u) & mask;
    }
    fluidEditIndex[hole] = (FluidEditIndex){ 0 };
}

static void FluidRemoveEdit(uint32_t editIndex)
{
    if (editIndex >= fluidEditCount) return;
    FluidEdit removed = fluidEdits[editIndex];
    int slot = FluidFindEditSlot(
        removed.surfaceId, removed.x, removed.y, removed.z);
    if (slot < 0) return;

    FluidRemoveChunkEdit(editIndex);
    FluidEraseEditIndexSlot((uint32_t)slot);
    uint32_t last = fluidEditCount - 1u;
    if (editIndex != last) {
        fluidEdits[editIndex] = fluidEdits[last];
        FluidEdit *moved = &fluidEdits[editIndex];
        int movedSlot = FluidFindEditSlot(
            moved->surfaceId, moved->x, moved->y, moved->z);
        if (movedSlot >= 0) {
            fluidEditIndex[movedSlot].editIndex = editIndex;
        }
        int cx = FloorDivInt(moved->x, CHUNK_SIZE);
        int cz = FloorDivInt(moved->z, CHUNK_SIZE);
        int bucketSlot = FluidFindChunkEditBucket(
            moved->surfaceId, cx, cz);
        if (bucketSlot >= 0) {
            FluidChunkEditBucket *bucket = &fluidChunkEditIndex[bucketSlot];
            if (moved->chunkListIndex < bucket->count) {
                bucket->editIndices[moved->chunkListIndex] = editIndex;
            }
        }
    }
    fluidEditCount--;
    fluidStats.editCount = fluidEditCount;
}

static bool FluidEnsureEditCapacity(uint32_t wanted)
{
    if (wanted > FLUID_MAX_EDIT_COUNT) return false;
    if (wanted <= fluidEditCapacity && fluidEditIndexCapacity >= wanted * 2u) {
        return true;
    }
    uint32_t capacity = fluidEditCapacity > 0u
        ? fluidEditCapacity : FLUID_INITIAL_EDIT_CAPACITY;
    while (capacity < wanted) {
        if (capacity > FLUID_MAX_EDIT_COUNT / 2u) {
            capacity = FLUID_MAX_EDIT_COUNT;
            break;
        }
        capacity *= 2u;
    }
    if (capacity != fluidEditCapacity) {
        FluidEdit *next = realloc(
            fluidEdits, (size_t)capacity * sizeof(*next));
        if (!next) return false;
        fluidEdits = next;
        fluidEditCapacity = capacity;
    }
    return FluidRebuildEditIndex(wanted);
}

static bool FluidRememberEdit(uint32_t surfaceId, int x, int y, int z,
                              uint8_t volume, uint8_t baseline)
{
    int existing = FluidFindEdit(surfaceId, x, y, z);
    if (existing >= 0) {
        if (fluidEdits[existing].baselineKnown &&
            volume == fluidEdits[existing].baseline) {
            FluidRemoveEdit((uint32_t)existing);
            return true;
        }
        fluidEdits[existing].volume = volume;
        return true;
    }
    if (volume == baseline) return true;
    if (!FluidEnsureEditCapacity(fluidEditCount + 1u)) return false;
    uint32_t index = fluidEditCount++;
    fluidEdits[index] = (FluidEdit){
        .x = x,
        .y = y,
        .z = z,
        .surfaceId = surfaceId,
        .volume = volume,
        .baseline = baseline,
        .baselineKnown = true
    };
    uint32_t slot = FluidHash(surfaceId, x, y, z) &
                    (fluidEditIndexCapacity - 1u);
    while (fluidEditIndex[slot].used) {
        slot = (slot + 1u) & (fluidEditIndexCapacity - 1u);
    }
    fluidEditIndex[slot] = (FluidEditIndex){
        x, y, z, surfaceId, index, true
    };
    if (!FluidAppendChunkEdit(index)) {
        FluidEraseEditIndexSlot(slot);
        fluidEditCount--;
        return false;
    }
    fluidStats.editCount = fluidEditCount;
    return true;
}

static bool FluidPrepareMutations(const FluidMutation *mutations, int count)
{
    if (!mutations || count <= 0 || !WorldIsSurfaceActive()) return false;
    uint32_t surfaceId = WorldCurrentSurfaceId();
    uint32_t newEdits = 0u;
    for (int mutationIndex = 0; mutationIndex < count; mutationIndex++) {
        FluidMutation mutation = FluidCanonicalMutation(
            mutations[mutationIndex]);
        Chunk *chunk = NULL;
        ChunkSection *section = NULL;
        BlockType block = BLOCK_AIR;
        if (!FluidLocateCell(mutation.x, mutation.y, mutation.z, &chunk,
                             &section, NULL, &block) ||
            (mutation.volume > 0u && block != BLOCK_AIR &&
             block != BLOCK_WATER)) {
            return false;
        }
        if (!section) {
            section = ChunkGetSection(
                chunk, SurfaceSectionYFromBlockY(mutation.y), true);
        }
        if (!section || !FluidEnsureVolumes(section)) return false;

        if (FluidFindEdit(surfaceId, mutation.x, mutation.y,
                          mutation.z) >= 0) {
            continue;
        }
        bool duplicate = false;
        for (int previous = 0; previous < mutationIndex; previous++) {
            FluidMutation prior = FluidCanonicalMutation(
                mutations[previous]);
            duplicate = prior.x == mutation.x && prior.y == mutation.y &&
                        prior.z == mutation.z;
            if (duplicate) break;
        }
        if (!duplicate) newEdits++;
    }
    if (!FluidEnsureEditCapacity(fluidEditCount + newEdits)) return false;
    for (int mutationIndex = 0; mutationIndex < count; mutationIndex++) {
        FluidMutation mutation = FluidCanonicalMutation(
            mutations[mutationIndex]);
        if (FluidFindEdit(surfaceId, mutation.x, mutation.y,
                          mutation.z) >= 0 ||
            FluidGetVolumeAt(mutation.x, mutation.y, mutation.z) ==
                mutation.volume) {
            continue;
        }
        bool duplicate = false;
        for (int previous = 0; previous < mutationIndex; previous++) {
            FluidMutation prior = FluidCanonicalMutation(
                mutations[previous]);
            if (prior.x == mutation.x && prior.y == mutation.y &&
                prior.z == mutation.z) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;

        int cx = FloorDivInt(mutation.x, CHUNK_SIZE);
        int cz = FloorDivInt(mutation.z, CHUNK_SIZE);
        uint32_t additions = 1u;
        for (int previous = 0; previous < mutationIndex; previous++) {
            FluidMutation prior = FluidCanonicalMutation(
                mutations[previous]);
            if (FloorDivInt(prior.x, CHUNK_SIZE) == cx &&
                FloorDivInt(prior.z, CHUNK_SIZE) == cz) {
                additions++;
            }
        }
        int bucketSlot = FluidFindChunkEditBucket(surfaceId, cx, cz);
        uint32_t current = bucketSlot >= 0
            ? fluidChunkEditIndex[bucketSlot].count : 0u;
        if (!FluidReserveChunkEditCapacity(
                surfaceId, cx, cz, current + additions)) {
            return false;
        }
    }
    return true;
}

static bool FluidApplyMutations(const FluidMutation *mutations, int count)
{
    if (!FluidPrepareMutations(mutations, count)) return false;
    for (int index = 0; index < count; index++) {
        const FluidMutation *mutation = &mutations[index];
        if (!FluidSetVolumeInternal(mutation->x, mutation->y, mutation->z,
                                    mutation->volume, true)) {
            return false;
        }
    }
    return true;
}

uint8_t FluidGetVolumeAt(int x, int y, int z)
{
    ChunkSection *section = NULL;
    int index = 0;
    BlockType block = BLOCK_AIR;
    if (!FluidLocateCell(x, y, z, NULL, &section, &index, &block)) return 0u;
    if (section && section->waterVolumes) return section->waterVolumes[index];
    return block == BLOCK_WATER ? (uint8_t)FLUID_CAPACITY : 0u;
}

static bool FluidSetVolumeInternal(int x, int y, int z, uint8_t volume,
                                   bool remember)
{
    FluidCanonicalizeXZ(&x, &z);
    Chunk *chunk = NULL;
    ChunkSection *section = NULL;
    int index = 0;
    BlockType block = BLOCK_AIR;
    if (!FluidLocateCell(x, y, z, &chunk, &section, &index, &block)) {
        return false;
    }
    if (volume > 0u && block != BLOCK_AIR && block != BLOCK_WATER) return false;
    if (!section && volume > 0u) {
        int cx = 0;
        int cz = 0;
        int lx = 0;
        int lz = 0;
        WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
        section = ChunkGetSection(
            chunk, SurfaceSectionYFromBlockY(y), true);
        if (!section) return false;
        index = FluidCellIndex(
            lx, SurfaceSectionLocalYFromBlockY(y), lz);
    }
    uint8_t previous = section && section->waterVolumes
        ? section->waterVolumes[index]
        : (block == BLOCK_WATER ? (uint8_t)FLUID_CAPACITY : 0u);
    if (volume < FLUID_CAPACITY && block == BLOCK_WATER &&
        WorldIsProceduralOceanWaterAt(x, y, z)) {
        volume = FLUID_CAPACITY;
    }
    if (previous == volume) return true;
    if (!section || !FluidEnsureVolumes(section)) return false;
    if (remember && !FluidRememberEdit(
            WorldCurrentSurfaceId(), x, y, z, volume, previous)) {
        return false;
    }
    section->waterVolumes[index] = volume;
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    if (block == BLOCK_AIR || block == BLOCK_WATER) {
        section->blocks[lx][SurfaceSectionLocalYFromBlockY(y)][lz] =
            volume > 0u ? BLOCK_WATER : BLOCK_AIR;
    }
    section->fluidDirty = true;
    MarkChunkDirtyAtBlock(x, y, z);
    return true;
}

bool FluidSetVolumeAt(int x, int y, int z, uint8_t volume)
{
    if (!WorldIsSurfaceActive()) return false;
    if (!FluidSetVolumeInternal(x, y, z, volume, true)) return false;
    FluidWakeCell(x, y, z);
    return true;
}

void FluidWakeCell(int x, int y, int z)
{
    if (!WorldIsSurfaceActive() || !InHeight(y)) return;
    FluidCanonicalizeXZ(&x, &z);
    ChunkSection *section = NULL;
    int index = 0;
    if (!FluidLocateCell(x, y, z, NULL, &section, &index, NULL)) return;
    if (!section) return;
    if (!FluidEnsureQueuedBits(section) || FluidQueuedBit(section, index) ||
        FluidDeferredBit(section, index)) {
        return;
    }
    if (fluidQueueCount >= FLUID_QUEUE_CAPACITY) {
        FluidSetDeferredBit(section, index, true);
        fluidDeferredPending = true;
        fluidStats.queueOverflows++;
        return;
    }
    uint32_t tail = (fluidQueueHead + fluidQueueCount) % FLUID_QUEUE_CAPACITY;
    fluidQueue[tail] = (FluidQueueCell){
        x, y, z, WorldCurrentSurfaceId()
    };
    fluidQueueCount++;
    FluidSetQueuedBit(section, index, true);
    fluidStats.activeCells = fluidQueueCount;
    if (fluidQueueCount > fluidStats.maxActiveCells) {
        fluidStats.maxActiveCells = fluidQueueCount;
    }
}

static void FluidRefillDeferredQueue(void)
{
    if (!fluidDeferredPending || !WorldIsSurfaceActive()) return;
    for (int chunkIndex = 0; chunkIndex < MAX_ACTIVE_CHUNKS; chunkIndex++) {
        Chunk *chunk = &chunks[chunkIndex];
        if (!chunk->loaded) continue;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            ChunkSection *section = chunk->sections[sectionIndex];
            if (!section->fluidDeferredBits) continue;
            int sectionY = section->sectionY;
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int index = FluidCellIndex(lx, ly, lz);
                        if (!FluidDeferredBit(section, index)) continue;
                        if (fluidQueueCount >= FLUID_QUEUE_CAPACITY) {
                            fluidStats.activeCells = fluidQueueCount;
                            return;
                        }
                        FluidSetDeferredBit(section, index, false);
                        if (FluidQueuedBit(section, index)) continue;
                        uint32_t tail = (fluidQueueHead + fluidQueueCount) %
                                        FLUID_QUEUE_CAPACITY;
                        fluidQueue[tail] = (FluidQueueCell){
                            chunk->cx * CHUNK_SIZE + lx,
                            sectionY * SURFACE_SECTION_HEIGHT + ly,
                            chunk->cz * CHUNK_SIZE + lz,
                            WorldCurrentSurfaceId()
                        };
                        fluidQueueCount++;
                        FluidSetQueuedBit(section, index, true);
                    }
                }
            }
        }
    }
    fluidDeferredPending = false;
    fluidStats.activeCells = fluidQueueCount;
    if (fluidQueueCount > fluidStats.maxActiveCells) {
        fluidStats.maxActiveCells = fluidQueueCount;
    }
}

static void FluidWakeNeighborhood(int x, int y, int z)
{
    static const int offsets[7][3] = {
        { 0, 0, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }
    };
    for (int index = 0; index < 7; index++) {
        FluidWakeCell(x + offsets[index][0], y + offsets[index][1],
                      z + offsets[index][2]);
    }
}

static bool FluidCellCanHold(int x, int y, int z)
{
    BlockType block = BLOCK_AIR;
    if (!FluidLocateCell(x, y, z, NULL, NULL, NULL, &block)) return false;
    return block == BLOCK_AIR || block == BLOCK_WATER;
}

static signed char FluidClampFlow(int value)
{
    if (value < -127) return -127;
    if (value > 127) return 127;
    return (signed char)value;
}

static void FluidRecordFlow(int x, int y, int z, int dx, int dy, int dz,
                            unsigned amount)
{
    ChunkSection *section = NULL;
    int index = 0;
    if (!FluidLocateCell(x, y, z, NULL, &section, &index, NULL) || !section ||
        !FluidEnsureFlow(section)) {
        return;
    }
    int magnitude = (int)amount / 2 + 1;
    int base = index * FLUID_FLOW_COMPONENTS;
    section->fluidFlow[base] = FluidClampFlow(
        (int)section->fluidFlow[base] + dx * magnitude);
    section->fluidFlow[base + 1] = FluidClampFlow(
        (int)section->fluidFlow[base + 1] + dy * magnitude);
    section->fluidFlow[base + 2] = FluidClampFlow(
        (int)section->fluidFlow[base + 2] + dz * magnitude);
}

static bool FluidTransfer(int sx, int sy, int sz, int tx, int ty, int tz,
                          unsigned limit)
{
    uint8_t source = FluidGetVolumeAt(sx, sy, sz);
    if (source == 0u || !FluidCellCanHold(tx, ty, tz)) return false;
    uint8_t target = FluidGetVolumeAt(tx, ty, tz);
    unsigned capacity = FLUID_CAPACITY - target;
    unsigned amount = source < capacity ? source : capacity;
    if (amount > limit) amount = limit;
    if (amount == 0u) return false;
    FluidMutation mutations[2] = {
        { sx, sy, sz, (uint8_t)(source - amount) },
        { tx, ty, tz, (uint8_t)(target + amount) }
    };
    if (!FluidApplyMutations(mutations, 2)) return false;
    int dx = tx - sx;
    int dy = ty - sy;
    int dz = tz - sz;
    FluidRecordFlow(sx, sy, sz, dx, dy, dz, amount);
    FluidRecordFlow(tx, ty, tz, dx, dy, dz, amount);
    fluidStats.transferredVolume += amount;
    FluidWakeNeighborhood(sx, sy, sz);
    FluidWakeNeighborhood(tx, ty, tz);
    return true;
}

static bool FluidEqualize(int x, int y, int z, int nx, int nz)
{
    if (!FluidCellCanHold(x + nx, y, z + nz)) return false;
    uint8_t source = FluidGetVolumeAt(x, y, z);
    uint8_t target = FluidGetVolumeAt(x + nx, y, z + nz);
    if (source <= target + 1u) return false;
    unsigned amount = (unsigned)(source - target) / 2u;
    if (amount > FLUID_HORIZONTAL_FLOW_LIMIT) {
        amount = FLUID_HORIZONTAL_FLOW_LIMIT;
    }
    return FluidTransfer(x, y, z, x + nx, y, z + nz, amount);
}

static void FluidProcessCell(const FluidQueueCell *cell)
{
    if (!cell || cell->surfaceId != WorldCurrentSurfaceId()) return;
    ChunkSection *section = NULL;
    int index = 0;
    if (!FluidLocateCell(cell->x, cell->y, cell->z, NULL, &section, &index,
                         NULL) || !section) {
        return;
    }
    FluidSetQueuedBit(section, index, false);
    if (section->fluidFlow) {
        int base = index * FLUID_FLOW_COMPONENTS;
        section->fluidFlow[base] = (signed char)(section->fluidFlow[base] * 3 / 4);
        section->fluidFlow[base + 1] =
            (signed char)(section->fluidFlow[base + 1] * 3 / 4);
        section->fluidFlow[base + 2] =
            (signed char)(section->fluidFlow[base + 2] * 3 / 4);
    }
    if (FluidGetVolumeAt(cell->x, cell->y, cell->z) == 0u) {
        if (section->fluidFlow) {
            int base = index * FLUID_FLOW_COMPONENTS;
            section->fluidFlow[base] = 0;
            section->fluidFlow[base + 1] = 0;
            section->fluidFlow[base + 2] = 0;
        }
        return;
    }
    FluidTransfer(cell->x, cell->y, cell->z,
                  cell->x, cell->y - 1, cell->z, FLUID_CAPACITY);
    if (FluidGetVolumeAt(cell->x, cell->y, cell->z) == 0u) {
        if (section->fluidFlow) {
            int base = index * FLUID_FLOW_COMPONENTS;
            section->fluidFlow[base] = 0;
            section->fluidFlow[base + 1] = 0;
            section->fluidFlow[base + 2] = 0;
        }
        return;
    }

    static const int directions[4][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };
    unsigned start = FluidHash(cell->surfaceId, cell->x, cell->y,
                               cell->z) + (uint32_t)fluidStats.ticks;
    for (unsigned offset = 0; offset < 4u; offset++) {
        unsigned direction = (start + offset) & 3u;
        FluidEqualize(cell->x, cell->y, cell->z,
                      directions[direction][0], directions[direction][1]);
    }
    if (section->fluidFlow) {
        int base = index * FLUID_FLOW_COMPONENTS;
        if (section->fluidFlow[base] != 0 ||
            section->fluidFlow[base + 1] != 0 ||
            section->fluidFlow[base + 2] != 0) {
            FluidWakeCell(cell->x, cell->y, cell->z);
        }
    }
}

static uint32_t FluidProcessCells(uint32_t limit, bool countTick,
                                  double cpuBudgetMs, bool *hitCpuBudget)
{
    if (hitCpuBudget) *hitCpuBudget = false;
    FluidRefillDeferredQueue();
    uint32_t available = fluidQueueCount;
    uint32_t count = available < limit ? available : limit;
    fluidStats.lastProcessedCells = 0u;
    if (countTick) fluidStats.ticks++;
    double deadlineMs = cpuBudgetMs > 0.0
        ? FluidThreadCpuNowMs() + cpuBudgetMs : 0.0;
    for (uint32_t processed = 0u; processed < count; processed++) {
        FluidQueueCell cell = fluidQueue[fluidQueueHead];
        fluidQueueHead = (fluidQueueHead + 1u) % FLUID_QUEUE_CAPACITY;
        fluidQueueCount--;
        FluidProcessCell(&cell);
        fluidStats.processedCells++;
        fluidStats.lastProcessedCells++;
        if (cpuBudgetMs > 0.0 &&
            fluidStats.lastProcessedCells % FLUID_TIME_CHECK_INTERVAL == 0u &&
            FluidThreadCpuNowMs() >= deadlineMs) {
            if (hitCpuBudget) *hitCpuBudget = true;
            break;
        }
    }
    FluidRefillDeferredQueue();
    fluidStats.activeCells = fluidQueueCount;
    return fluidStats.lastProcessedCells;
}

static void FluidProcessTick(void)
{
    FluidProcessCells(FLUID_MAX_CELLS_PER_TICK, true, 0.0, NULL);
}

void FluidUpdate(float dt)
{
    if (!WorldIsSurfaceActive() || !isfinite(dt) || dt <= 0.0f) return;

    fluidTickAccumulator += dt;
    if (fluidTickAccumulator > 0.5f) fluidTickAccumulator = 0.5f;
    const float tickSeconds = 1.0f / FLUID_TICK_RATE;
    while (fluidTickAccumulator >= tickSeconds) {
        fluidStats.ticks++;
        fluidTickAccumulator -= tickSeconds;
    }

    fluidCellBudget += (double)dt * (double)FLUID_TICK_RATE *
                       (double)FLUID_MAX_CELLS_PER_TICK;
    if (fluidCellBudget > (double)FLUID_MAX_CELLS_PER_TICK) {
        fluidCellBudget = (double)FLUID_MAX_CELLS_PER_TICK;
    }
    uint32_t cellBudget = (uint32_t)fluidCellBudget;
    if (cellBudget > FLUID_MAX_CELLS_PER_UPDATE) {
        cellBudget = FLUID_MAX_CELLS_PER_UPDATE;
    }
    fluidCellBudget -= (double)cellBudget;
    bool hitCpuBudget = false;
    uint32_t processed = FluidProcessCells(
        cellBudget, false, FLUID_UPDATE_CPU_BUDGET_MS, &hitCpuBudget);
    if (hitCpuBudget) fluidCellBudget += (double)(cellBudget - processed);
}

void FluidStepTicks(unsigned ticks)
{
    if (!WorldIsSurfaceActive()) return;
    for (unsigned tick = 0u; tick < ticks; tick++) FluidProcessTick();
}

FluidSample FluidSampleAt(Vector3 position)
{
    FluidSample sample = { 0 };
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z)) {
        return sample;
    }
    int x = (int)floorf(position.x);
    int y = (int)floorf(position.y);
    int z = (int)floorf(position.z);
    sample.volume = FluidGetVolumeAt(x, y, z);
    sample.surfaceY = (float)y + (float)sample.volume / (float)FLUID_CAPACITY;
    ChunkSection *section = NULL;
    int index = 0;
    if (FluidLocateCell(x, y, z, NULL, &section, &index, NULL) && section &&
        section->fluidFlow) {
        int base = index * FLUID_FLOW_COMPONENTS;
        const float scale = 2.5f / 127.0f;
        sample.velocity = (Vector3){
            section->fluidFlow[base] * scale,
            section->fluidFlow[base + 1] * scale,
            section->fluidFlow[base + 2] * scale
        };
    }
    return sample;
}

static int FluidCollectCandidates(int x, int y, int z,
                                  FluidQueueCell *cells, int capacity,
                                  unsigned *outVolume)
{
    if (!cells || capacity <= 0 || !outVolume) return 0;
    FluidCanonicalizeXZ(&x, &z);
    int head = 0;
    int count = 1;
    *outVolume = 0u;
    cells[0] = (FluidQueueCell){ x, y, z, WorldCurrentSurfaceId() };
    static const int offsets[6][3] = {
        { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }
    };
    while (head < count && *outVolume < FLUID_CAPACITY) {
        FluidQueueCell current = cells[head++];
        uint8_t volume = FluidGetVolumeAt(current.x, current.y, current.z);
        if (volume == 0u) continue;
        *outVolume += volume;
        for (int direction = 0; direction < 6 && count < capacity; direction++) {
            FluidQueueCell candidate = {
                current.x + offsets[direction][0],
                current.y + offsets[direction][1],
                current.z + offsets[direction][2],
                current.surfaceId
            };
            FluidCanonicalizeXZ(&candidate.x, &candidate.z);
            if (FluidGetVolumeAt(candidate.x, candidate.y, candidate.z) == 0u) {
                continue;
            }
            bool seen = false;
            for (int index = 0; index < count; index++) {
                if (cells[index].x == candidate.x &&
                    cells[index].y == candidate.y &&
                    cells[index].z == candidate.z) {
                    seen = true;
                    break;
                }
            }
            if (!seen) cells[count++] = candidate;
        }
    }
    return count;
}

bool FluidTryDepositUnit(int x, int y, int z)
{
    if (!WorldIsSurfaceActive() || !FluidCellCanHold(x, y, z)) return false;
    static const int offsets[7][3] = {
        { 0, 0, 0 }, { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }
    };
    unsigned freeVolume = 0u;
    for (int index = 0; index < 7; index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        int nz = z + offsets[index][2];
        if (!FluidCellCanHold(nx, ny, nz)) continue;
        freeVolume += FLUID_CAPACITY - FluidGetVolumeAt(nx, ny, nz);
    }
    if (freeVolume < FLUID_CAPACITY) return false;
    FluidMutation mutations[7];
    int mutationCount = 0;
    unsigned remaining = FLUID_CAPACITY;
    for (int index = 0; index < 7 && remaining > 0u; index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        int nz = z + offsets[index][2];
        if (!FluidCellCanHold(nx, ny, nz)) continue;
        uint8_t volume = FluidGetVolumeAt(nx, ny, nz);
        unsigned amount = FLUID_CAPACITY - volume;
        if (amount > remaining) amount = remaining;
        if (amount > 0u) {
            mutations[mutationCount++] = (FluidMutation){
                nx, ny, nz, (uint8_t)(volume + amount)
            };
        }
        remaining -= amount;
    }
    if (remaining != 0u || !FluidApplyMutations(mutations, mutationCount)) {
        return false;
    }
    for (int index = 0; index < mutationCount; index++) {
        FluidWakeNeighborhood(mutations[index].x, mutations[index].y,
                              mutations[index].z);
    }
    return true;
}

bool FluidTryCollectUnit(int x, int y, int z)
{
    FluidQueueCell cells[1024];
    unsigned available = 0u;
    int count = FluidCollectCandidates(x, y, z, cells, 1024, &available);
    if (available < FLUID_CAPACITY) return false;
    FluidMutation mutations[1024];
    int mutationCount = 0;
    unsigned remaining = FLUID_CAPACITY;
    for (int index = 0; index < count && remaining > 0u; index++) {
        uint8_t volume = FluidGetVolumeAt(cells[index].x, cells[index].y,
                                          cells[index].z);
        unsigned amount = volume < remaining ? volume : remaining;
        mutations[mutationCount++] = (FluidMutation){
            cells[index].x, cells[index].y, cells[index].z,
            (uint8_t)(volume - amount)
        };
        remaining -= amount;
    }
    if (remaining != 0u || !FluidApplyMutations(mutations, mutationCount)) {
        return false;
    }
    for (int index = 0; index < mutationCount; index++) {
        FluidWakeNeighborhood(mutations[index].x, mutations[index].y,
                              mutations[index].z);
    }
    return true;
}

bool FluidTryDisplaceForBlockTracked(int x, int y, int z,
                                     FluidBlockDisplacement *outDisplacement)
{
    if (outDisplacement) *outDisplacement = (FluidBlockDisplacement){ 0 };
    uint8_t source = FluidGetVolumeAt(x, y, z);
    if (source == 0u) return true;
    static const int offsets[6][3] = {
        { 0, -1, 0 }, { -1, 0, 0 }, { 1, 0, 0 },
        { 0, 0, -1 }, { 0, 0, 1 }, { 0, 1, 0 }
    };
    unsigned freeVolume = 0u;
    for (int index = 0; index < 6; index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        int nz = z + offsets[index][2];
        if (!FluidCellCanHold(nx, ny, nz)) continue;
        freeVolume += FLUID_CAPACITY - FluidGetVolumeAt(nx, ny, nz);
    }
    if (freeVolume < source) return false;
    FluidMutation mutations[7];
    int mutationCount = 1;
    mutations[0] = (FluidMutation){ x, y, z, 0u };
    unsigned remaining = source;
    for (int index = 0; index < 6 && remaining > 0u; index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        int nz = z + offsets[index][2];
        if (!FluidCellCanHold(nx, ny, nz)) continue;
        uint8_t volume = FluidGetVolumeAt(nx, ny, nz);
        unsigned amount = FLUID_CAPACITY - volume;
        if (amount > remaining) amount = remaining;
        if (amount > 0u) {
            mutations[mutationCount++] = (FluidMutation){
                nx, ny, nz, (uint8_t)(volume + amount)
            };
            remaining -= amount;
        }
    }
    FluidBlockDisplacement captured = { .count = (uint8_t)mutationCount };
    if (outDisplacement) {
        for (int index = 0; index < mutationCount; index++) {
            uint8_t before = FluidGetVolumeAt(
                mutations[index].x, mutations[index].y, mutations[index].z);
            int existing = FluidFindEdit(
                WorldCurrentSurfaceId(), mutations[index].x,
                mutations[index].y, mutations[index].z);
            bool baselineKnown = existing < 0 ||
                fluidEdits[existing].baselineKnown;
            uint8_t baseline = existing >= 0
                ? fluidEdits[existing].baseline : before;
            captured.cells[index] = (FluidVolumeChange){
                .x = mutations[index].x,
                .y = mutations[index].y,
                .z = mutations[index].z,
                .before = before,
                .after = mutations[index].volume,
                .baseline = baseline,
                .baselineKnown = baselineKnown
            };
        }
    }
    if (remaining != 0u || !FluidApplyMutations(mutations, mutationCount)) {
        return false;
    }
    if (outDisplacement) *outDisplacement = captured;
    for (int index = 0; index < mutationCount; index++) {
        FluidWakeNeighborhood(mutations[index].x, mutations[index].y,
                              mutations[index].z);
    }
    return true;
}

bool FluidTryDisplaceForBlock(int x, int y, int z)
{
    return FluidTryDisplaceForBlockTracked(x, y, z, NULL);
}

bool FluidReplayBlockDisplacement(
    const FluidBlockDisplacement *displacement, bool after)
{
    if (!displacement ||
        displacement->count > FLUID_BLOCK_DISPLACEMENT_MAX_CELLS) {
        return false;
    }
    if (displacement->count == 0u) return true;
    FluidMutation mutations[FLUID_BLOCK_DISPLACEMENT_MAX_CELLS];
    for (uint8_t index = 0u; index < displacement->count; index++) {
        const FluidVolumeChange *change = &displacement->cells[index];
        mutations[index] = (FluidMutation){
            change->x, change->y, change->z,
            after ? change->after : change->before
        };
    }
    if (!FluidApplyMutations(mutations, displacement->count)) return false;
    for (uint8_t index = 0u; index < displacement->count; index++) {
        FluidWakeNeighborhood(mutations[index].x, mutations[index].y,
                              mutations[index].z);
    }
    return true;
}

void FluidOnBlockChanged(int x, int y, int z, BlockType previous,
                         BlockType next)
{
    if (!WorldIsSurfaceActive()) return;
    if (next == BLOCK_WATER) {
        FluidSetVolumeInternal(x, y, z, FLUID_CAPACITY, true);
    } else if (previous == BLOCK_WATER) {
        FluidSetVolumeInternal(x, y, z, 0u, true);
    }
    FluidWakeNeighborhood(x, y, z);
}

static void FluidPruneReservoirDeficitsInSection(Chunk *chunk, int sectionY)
{
    uint32_t surfaceId = WorldCurrentSurfaceId();
    int bucketSlot = FluidFindChunkEditBucket(
        surfaceId, chunk->cx, chunk->cz);
    uint32_t listIndex = 0u;
    while (bucketSlot >= 0) {
        FluidChunkEditBucket *bucket = &fluidChunkEditIndex[bucketSlot];
        if (listIndex >= bucket->count) return;
        uint32_t editIndex = bucket->editIndices[listIndex];
        if (editIndex >= fluidEditCount) {
            listIndex++;
            continue;
        }
        FluidEdit edit = fluidEdits[editIndex];
        bool staleReservoirDeficit =
            edit.surfaceId == surfaceId && edit.baselineKnown &&
            edit.baseline == FLUID_CAPACITY &&
            edit.volume < FLUID_CAPACITY && InHeight(edit.y) &&
            SurfaceSectionYFromBlockY(edit.y) == sectionY &&
            WorldIsProceduralOceanWaterAt(edit.x, edit.y, edit.z);
        if (!staleReservoirDeficit) {
            listIndex++;
            continue;
        }
        FluidRemoveEdit(editIndex);
        bucketSlot = FluidFindChunkEditBucket(
            surfaceId, chunk->cx, chunk->cz);
    }
}

void FluidApplyEditsToChunkSection(Chunk *chunk, int sectionY)
{
    if (!chunk || !WorldIsSurfaceActive()) return;
    FluidPruneReservoirDeficitsInSection(chunk, sectionY);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
    uint32_t surfaceId = WorldCurrentSurfaceId();
    int bucketSlot = FluidFindChunkEditBucket(
        surfaceId, chunk->cx, chunk->cz);
    if (bucketSlot < 0) return;
    const FluidChunkEditBucket *bucket = &fluidChunkEditIndex[bucketSlot];
    for (uint32_t listIndex = 0u; listIndex < bucket->count; listIndex++) {
        uint32_t index = bucket->editIndices[listIndex];
        if (index >= fluidEditCount) continue;
        const FluidEdit *edit = &fluidEdits[index];
        int cx = 0;
        int cz = 0;
        int lx = 0;
        int lz = 0;
        WorldToChunkLocal(edit->x, edit->z, &cx, &cz, &lx, &lz);
        if (cx != chunk->cx || cz != chunk->cz || !InHeight(edit->y) ||
            SurfaceSectionYFromBlockY(edit->y) != sectionY) {
            continue;
        }
        if (!section) {
            if (edit->volume == 0u) continue;
            section = ChunkGetSection(chunk, sectionY, true);
            if (!section) return;
        }
        int ly = SurfaceSectionLocalYFromBlockY(edit->y);
        BlockType block = (BlockType)section->blocks[lx][ly][lz];
        if (edit->volume > 0u && block != BLOCK_AIR && block != BLOCK_WATER) {
            continue;
        }
        FluidSetVolumeInternal(edit->x, edit->y, edit->z,
                               edit->volume, false);
        FluidWakeNeighborhood(edit->x, edit->y, edit->z);
    }
}

void FluidApplyEditsToChunk(Chunk *chunk)
{
    if (!chunk || !WorldIsSurfaceActive()) return;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        FluidApplyEditsToChunkSection(
            chunk, chunk->sections[sectionIndex]->sectionY);
    }
}

static void FluidWakeLoadedBoundaryPair(int ax, int ay, int az,
                                        int bx, int by, int bz)
{
    uint32_t surfaceId = WorldCurrentSurfaceId();
    if (FluidFindEdit(surfaceId, ax, ay, az) < 0 &&
        FluidFindEdit(surfaceId, bx, by, bz) < 0) {
        return;
    }
    BlockType aBlock = BLOCK_AIR;
    BlockType bBlock = BLOCK_AIR;
    if (!FluidLocateCell(ax, ay, az, NULL, NULL, NULL, &aBlock) ||
        !FluidLocateCell(bx, by, bz, NULL, NULL, NULL, &bBlock)) {
        return;
    }

    unsigned aVolume = FluidGetVolumeAt(ax, ay, az);
    unsigned bVolume = FluidGetVolumeAt(bx, by, bz);
    if (aVolume > bVolume + 1u &&
        (bBlock == BLOCK_AIR || bBlock == BLOCK_WATER)) {
        FluidWakeCell(ax, ay, az);
    } else if (bVolume > aVolume + 1u &&
               (aBlock == BLOCK_AIR || aBlock == BLOCK_WATER)) {
        FluidWakeCell(bx, by, bz);
    }
}

static void FluidWakeLoadedBoundarySection(Chunk *chunk, int sectionY)
{
    int westX = chunk->cx * CHUNK_SIZE;
    int eastX = westX + CHUNK_SIZE - 1;
    int northZ = chunk->cz * CHUNK_SIZE;
    int southZ = northZ + CHUNK_SIZE - 1;
    int firstY = sectionY * SURFACE_SECTION_HEIGHT;
    int lastY = firstY + SURFACE_SECTION_HEIGHT;
    for (int y = firstY; y < lastY; y++) {
        for (int edge = 0; edge < CHUNK_SIZE; edge++) {
            int z = northZ + edge;
            int x = westX + edge;
            FluidWakeLoadedBoundaryPair(
                westX, y, z, westX - 1, y, z);
            FluidWakeLoadedBoundaryPair(
                eastX, y, z, eastX + 1, y, z);
            FluidWakeLoadedBoundaryPair(
                x, y, northZ, x, y, northZ - 1);
            FluidWakeLoadedBoundaryPair(
                x, y, southZ, x, y, southZ + 1);
        }
    }
}

static void FluidWakeLoadedVerticalBoundarySection(
    Chunk *chunk, int sectionY)
{
    int firstX = chunk->cx * CHUNK_SIZE;
    int firstZ = chunk->cz * CHUNK_SIZE;
    int firstY = sectionY * SURFACE_SECTION_HEIGHT;
    int lastY = firstY + SURFACE_SECTION_HEIGHT - 1;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int x = firstX + lx;
            int z = firstZ + lz;
            FluidWakeLoadedBoundaryPair(
                x, firstY, z, x, firstY - 1, z);
            FluidWakeLoadedBoundaryPair(
                x, lastY, z, x, lastY + 1, z);
        }
    }
}

void FluidOnChunkSectionLoaded(Chunk *chunk, int sectionY)
{
    if (!chunk || !chunk->loaded || !WorldIsSurfaceActive()) {
        return;
    }
    FluidApplyEditsToChunkSection(chunk, sectionY);
    if (!ChunkGetSectionConst(chunk, sectionY)) return;
    if (fluidEditCount == 0u) return;
    FluidWakeLoadedBoundarySection(chunk, sectionY);
    FluidWakeLoadedVerticalBoundarySection(chunk, sectionY);
}

void FluidOnChunkLoaded(Chunk *chunk)
{
    if (!chunk || !chunk->loaded || !WorldIsSurfaceActive()) return;
    FluidApplyEditsToChunk(chunk);
    if (fluidEditCount == 0u) return;

    Chunk *sources[5] = {
        chunk,
        FindHorizontalChunkNeighbor(chunk->cx, chunk->cz, -1, 0),
        FindHorizontalChunkNeighbor(chunk->cx, chunk->cz, 1, 0),
        FindHorizontalChunkNeighbor(chunk->cx, chunk->cz, 0, -1),
        FindHorizontalChunkNeighbor(chunk->cx, chunk->cz, 0, 1)
    };
    for (int sourceIndex = 0; sourceIndex < 5; sourceIndex++) {
        Chunk *source = sources[sourceIndex];
        if (!source) continue;
        for (int sectionIndex = 0; sectionIndex < source->sectionCount;
             sectionIndex++) {
            FluidWakeLoadedBoundarySection(
                chunk, source->sections[sectionIndex]->sectionY);
        }
    }
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        FluidWakeLoadedVerticalBoundarySection(
            chunk, chunk->sections[sectionIndex]->sectionY);
    }
}

static bool FluidQueueCellMatchesSection(
    const FluidQueueCell *cell, const Chunk *chunk, int sectionY,
    uint32_t surfaceId)
{
    return cell && chunk && cell->surfaceId == surfaceId &&
           FloorDivInt(cell->x, CHUNK_SIZE) == chunk->cx &&
           FloorDivInt(cell->z, CHUNK_SIZE) == chunk->cz &&
           SurfaceSectionYFromBlockY(cell->y) == sectionY;
}

bool FluidPrepareChunkSectionUnload(Chunk *chunk, int sectionY)
{
    if (!chunk || !chunk->loaded || !WorldIsSurfaceActive() ||
        !ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }

    uint32_t previousCount = fluidQueueCount;
    uint32_t kept = 0u;
    uint32_t surfaceId = WorldCurrentSurfaceId();
    for (uint32_t read = 0u; read < previousCount; read++) {
        uint32_t source = (fluidQueueHead + read) % FLUID_QUEUE_CAPACITY;
        FluidQueueCell cell = fluidQueue[source];
        if (FluidQueueCellMatchesSection(
                &cell, chunk, sectionY, surfaceId)) {
            continue;
        }
        uint32_t destination =
            (fluidQueueHead + kept) % FLUID_QUEUE_CAPACITY;
        fluidQueue[destination] = cell;
        kept++;
    }
    for (uint32_t index = kept; index < previousCount; index++) {
        uint32_t slot = (fluidQueueHead + index) % FLUID_QUEUE_CAPACITY;
        fluidQueue[slot] = (FluidQueueCell){ 0 };
    }
    fluidQueueCount = kept;
    fluidStats.activeCells = kept;
    return true;
}

uint64_t FluidLoadedVolume(void)
{
    uint64_t total = 0u;
    for (int chunkIndex = 0; chunkIndex < MAX_ACTIVE_CHUNKS; chunkIndex++) {
        const Chunk *chunk = &chunks[chunkIndex];
        if (!chunk->loaded) continue;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            const ChunkSection *section = chunk->sections[sectionIndex];
            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int index = FluidCellIndex(lx, ly, lz);
                        total += section->waterVolumes
                            ? section->waterVolumes[index]
                            : (section->blocks[lx][ly][lz] == BLOCK_WATER
                                   ? FLUID_CAPACITY : 0u);
                    }
                }
            }
        }
    }
    return total;
}

FluidStats FluidGetStats(void)
{
    FluidStats stats = fluidStats;
    stats.activeCells = fluidQueueCount;
    stats.editCount = fluidEditCount;
    return stats;
}

bool FluidSaveState(FILE *file)
{
    static const char magic[4] = { 'F', 'L', 'D', '2' };
    if (!file || fwrite(magic, 1, sizeof(magic), file) != sizeof(magic) ||
        fwrite(&fluidEditCount, sizeof(fluidEditCount), 1, file) != 1) {
        return false;
    }
    for (uint32_t index = 0u; index < fluidEditCount; index++) {
        const FluidEdit *edit = &fluidEdits[index];
        uint8_t flags = edit->baselineKnown ? 1u : 0u;
        if (fwrite(&edit->x, sizeof(edit->x), 1, file) != 1 ||
            fwrite(&edit->y, sizeof(edit->y), 1, file) != 1 ||
            fwrite(&edit->z, sizeof(edit->z), 1, file) != 1 ||
            fwrite(&edit->surfaceId, sizeof(edit->surfaceId), 1, file) != 1 ||
            fwrite(&edit->volume, sizeof(edit->volume), 1, file) != 1 ||
            fwrite(&edit->baseline, sizeof(edit->baseline), 1, file) != 1 ||
            fwrite(&flags, sizeof(flags), 1, file) != 1) {
            return false;
        }
    }
    return !ferror(file);
}

bool FluidLoadState(FILE *file)
{
    static const char legacyMagic[4] = { 'F', 'L', 'D', '1' };
    static const char currentMagic[4] = { 'F', 'L', 'D', '2' };
    char magic[4] = { 0 };
    uint32_t count = 0u;
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return false;
    }
    bool legacy = memcmp(magic, legacyMagic, sizeof(magic)) == 0;
    if ((!legacy && memcmp(magic, currentMagic, sizeof(magic)) != 0) ||
        fread(&count, sizeof(count), 1, file) != 1 ||
        count > FLUID_MAX_EDIT_COUNT) {
        return false;
    }
    FluidEdit *loaded = count > 0u
        ? malloc((size_t)count * sizeof(*loaded)) : NULL;
    if (count > 0u && !loaded) return false;
    for (uint32_t index = 0u; index < count; index++) {
        FluidEdit *edit = &loaded[index];
        *edit = (FluidEdit){ 0 };
        uint8_t flags = 0u;
        if (fread(&edit->x, sizeof(edit->x), 1, file) != 1 ||
            fread(&edit->y, sizeof(edit->y), 1, file) != 1 ||
            fread(&edit->z, sizeof(edit->z), 1, file) != 1 ||
            fread(&edit->surfaceId, sizeof(edit->surfaceId), 1, file) != 1 ||
            fread(&edit->volume, sizeof(edit->volume), 1, file) != 1 ||
            (!legacy &&
             (fread(&edit->baseline, sizeof(edit->baseline), 1, file) != 1 ||
              fread(&flags, sizeof(flags), 1, file) != 1 || flags > 1u)) ||
            !InHeight(edit->y)) {
            free(loaded);
            return false;
        }
        edit->baselineKnown = !legacy && flags != 0u;
        if (legacy) edit->baseline = edit->volume;
        FluidCanonicalizeXZ(&edit->x, &edit->z);
    }
    uint32_t indexCapacity = FLUID_INITIAL_EDIT_CAPACITY;
    while (indexCapacity < count * 2u && indexCapacity < (1u << 30)) {
        indexCapacity <<= 1;
    }
    FluidEditIndex *loadedIndex = calloc(indexCapacity, sizeof(*loadedIndex));
    if (!loadedIndex) {
        free(loaded);
        return false;
    }
    for (uint32_t index = 0u; index < count; index++) {
        FluidEdit *edit = &loaded[index];
        uint32_t slot = FluidHash(edit->surfaceId, edit->x, edit->y, edit->z) &
                        (indexCapacity - 1u);
        while (loadedIndex[slot].used) {
            const FluidEditIndex *entry = &loadedIndex[slot];
            if (entry->surfaceId == edit->surfaceId && entry->x == edit->x &&
                entry->y == edit->y && entry->z == edit->z) {
                free(loadedIndex);
                free(loaded);
                return false;
            }
            slot = (slot + 1u) & (indexCapacity - 1u);
        }
        loadedIndex[slot] = (FluidEditIndex){
            edit->x, edit->y, edit->z, edit->surfaceId, index, true
        };
    }

    FluidEdit *previousEdits = fluidEdits;
    uint32_t previousEditCount = fluidEditCount;
    uint32_t previousEditCapacity = fluidEditCapacity;
    FluidEditIndex *previousEditIndex = fluidEditIndex;
    uint32_t previousEditIndexCapacity = fluidEditIndexCapacity;
    FluidChunkEditBucket *previousChunkIndex = fluidChunkEditIndex;
    uint32_t previousChunkIndexCapacity = fluidChunkEditIndexCapacity;
    uint32_t previousChunkCount = fluidChunkEditCount;
    uint32_t previousChunkDeleted = fluidChunkEditDeleted;

    fluidEdits = loaded;
    fluidEditCount = count;
    fluidEditCapacity = count;
    fluidEditIndex = loadedIndex;
    fluidEditIndexCapacity = indexCapacity;
    fluidChunkEditIndex = NULL;
    fluidChunkEditIndexCapacity = 0u;
    fluidChunkEditCount = 0u;
    fluidChunkEditDeleted = 0u;
    if (!FluidBuildAllChunkEditLists()) {
        FluidFreeChunkEditIndex(
            fluidChunkEditIndex, fluidChunkEditIndexCapacity);
        fluidEdits = previousEdits;
        fluidEditCount = previousEditCount;
        fluidEditCapacity = previousEditCapacity;
        fluidEditIndex = previousEditIndex;
        fluidEditIndexCapacity = previousEditIndexCapacity;
        fluidChunkEditIndex = previousChunkIndex;
        fluidChunkEditIndexCapacity = previousChunkIndexCapacity;
        fluidChunkEditCount = previousChunkCount;
        fluidChunkEditDeleted = previousChunkDeleted;
        free(loadedIndex);
        free(loaded);
        return false;
    }
    free(previousEdits);
    free(previousEditIndex);
    FluidFreeChunkEditIndex(
        previousChunkIndex, previousChunkIndexCapacity);
    FluidClearLoadedRuntime();
    fluidQueueHead = 0u;
    fluidQueueCount = 0u;
    fluidDeferredPending = false;
    fluidTickAccumulator = 0.0f;
    fluidCellBudget = 0.0;
    fluidStats = (FluidStats){ .editCount = count };
    fluidStats.editCount = fluidEditCount;
    return true;
}

static void FluidClearLoadedRuntime(void)
{
    for (int chunkIndex = 0; chunkIndex < MAX_ACTIVE_CHUNKS; chunkIndex++) {
        Chunk *chunk = &chunks[chunkIndex];
        if (!chunk->loaded) continue;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            ChunkSection *section = chunk->sections[sectionIndex];
            if (section->fluidQueuedBits) {
                memset(section->fluidQueuedBits, 0,
                       (FLUID_SECTION_CELLS + 7u) / 8u);
            }
            if (section->fluidDeferredBits) {
                memset(section->fluidDeferredBits, 0,
                       (FLUID_SECTION_CELLS + 7u) / 8u);
            }
            if (section->fluidFlow) {
                memset(section->fluidFlow, 0,
                       FLUID_SECTION_CELLS * FLUID_FLOW_COMPONENTS *
                           sizeof(*section->fluidFlow));
            }
        }
    }
}

void FluidReset(void)
{
    FluidClearLoadedRuntime();
    free(fluidEdits);
    free(fluidEditIndex);
    FluidFreeChunkEditIndex(
        fluidChunkEditIndex, fluidChunkEditIndexCapacity);
    fluidEdits = NULL;
    fluidEditIndex = NULL;
    fluidChunkEditIndex = NULL;
    fluidEditCount = 0u;
    fluidEditCapacity = 0u;
    fluidEditIndexCapacity = 0u;
    fluidChunkEditIndexCapacity = 0u;
    fluidChunkEditCount = 0u;
    fluidChunkEditDeleted = 0u;
    fluidQueueHead = 0u;
    fluidQueueCount = 0u;
    fluidDeferredPending = false;
    fluidTickAccumulator = 0.0f;
    fluidCellBudget = 0.0;
    fluidStats = (FluidStats){ 0 };
}

void FluidCleanup(void)
{
    FluidReset();
}
