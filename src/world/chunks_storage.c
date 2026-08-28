#include "world/chunks_internal.h"

Chunk chunks[MAX_ACTIVE_CHUNKS];
Texture2D blockAtlas = { 0 };
int renderDistanceChunks = DEFAULT_RENDER_DISTANCE_CHUNKS;
ChunkGenJob chunkGenJobs[MAX_CHUNK_GEN_JOBS];
pthread_mutex_t genMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t genCond = PTHREAD_COND_INITIALIZER;
pthread_cond_t genCompletionCond = PTHREAD_COND_INITIALIZER;
pthread_t genThreads[MAX_CHUNK_WORKER_THREADS];
bool genShutdown = false;
unsigned int genWorkerThreadsConfigured = 0u;
unsigned int genWorkerThreadsStarted = 0u;
unsigned int genWorkerThreadsActive = 0u;
ChunkStreamingStats streamingStats;
ChunkStreamingFocus chunkStreamingFocus;
ChunkTaskQueue generationTaskQueue;
ChunkTaskQueue generationDoneQueue;
ChunkTaskQueue meshTaskQueue;
ChunkTaskQueue meshDoneQueue;

typedef struct ChunkIndexEntry {
    bool occupied;
    bool tombstone;
    bool spherical;
    int slotIndex;
    int cx;
    int cz;
    SurfaceChunkKey surfaceKey;
} ChunkIndexEntry;

static ChunkIndexEntry chunkIndex[CHUNK_INDEX_CAPACITY];

static uint32_t ChunkIndexMix(uint32_t hash, uint32_t value)
{
    hash ^= value + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash *= 0x85ebca6bu;
    return hash ^ (hash >> 13);
}

static uint32_t ChunkIndexHashSurface(SurfaceChunkKey key)
{
    uint32_t hash = 2166136261u;
    hash = ChunkIndexMix(hash, key.bodyId);
    for (int index = 0; index < 4; index++) {
        hash = ChunkIndexMix(hash, (uint32_t)key.corners[index].x);
        hash = ChunkIndexMix(hash, (uint32_t)key.corners[index].z);
    }
    return hash;
}

static uint32_t ChunkIndexHashFlat(int cx, int cz)
{
    uint32_t hash = 2166136261u;
    hash = ChunkIndexMix(hash, (uint32_t)cx);
    hash = ChunkIndexMix(hash, (uint32_t)cz);
    return hash;
}

static bool ChunkIndexEntryMatches(
    const ChunkIndexEntry *entry, bool spherical, int cx, int cz,
    SurfaceChunkKey surfaceKey)
{
    if (!entry || !entry->occupied || entry->spherical != spherical) {
        return false;
    }
    return spherical
        ? SurfaceChunkKeyEqual(entry->surfaceKey, surfaceKey)
        : (entry->cx == cx && entry->cz == cz);
}

static int ChunkIndexFindEntry(
    bool spherical, int cx, int cz, SurfaceChunkKey surfaceKey,
    bool forInsert)
{
    uint32_t hash = spherical
        ? ChunkIndexHashSurface(surfaceKey)
        : ChunkIndexHashFlat(cx, cz);
    int firstTombstone = -1;
    for (int probe = 0; probe < CHUNK_INDEX_CAPACITY; probe++) {
        int index = (int)((hash + (uint32_t)probe) %
                          (uint32_t)CHUNK_INDEX_CAPACITY);
        ChunkIndexEntry *entry = &chunkIndex[index];
        if (entry->occupied) {
            if (ChunkIndexEntryMatches(
                    entry, spherical, cx, cz, surfaceKey)) {
                return index;
            }
            continue;
        }
        if (entry->tombstone) {
            if (firstTombstone < 0) firstTombstone = index;
            continue;
        }
        return forInsert && firstTombstone >= 0 ? firstTombstone :
               (forInsert ? index : -1);
    }
    return forInsert ? firstTombstone : -1;
}

static void ChunkIndexRemoveSlot(int slotIndex)
{
    if (slotIndex < 0 || slotIndex >= MAX_ACTIVE_CHUNKS) return;
    for (int index = 0; index < CHUNK_INDEX_CAPACITY; index++) {
        ChunkIndexEntry *entry = &chunkIndex[index];
        if (!entry->occupied || entry->slotIndex != slotIndex) continue;
        entry->occupied = false;
        entry->tombstone = true;
        entry->slotIndex = -1;
    }
}

void ChunkIndexReset(void)
{
    memset(chunkIndex, 0, sizeof(chunkIndex));
}

void ChunkIndexInsert(const Chunk *chunk)
{
    if (!chunk || chunk < chunks || chunk >= chunks + MAX_ACTIVE_CHUNKS ||
        !chunk->loaded) return;
    int slotIndex = (int)(chunk - chunks);
    ChunkIndexRemoveSlot(slotIndex);
    int index = ChunkIndexFindEntry(
        chunk->spherical, chunk->cx, chunk->cz, chunk->surfaceKey, true);
    if (index < 0) return;
    chunkIndex[index] = (ChunkIndexEntry){
        .occupied = true,
        .tombstone = false,
        .spherical = chunk->spherical,
        .slotIndex = slotIndex,
        .cx = chunk->cx,
        .cz = chunk->cz,
        .surfaceKey = chunk->surfaceKey
    };
}

void ChunkIndexRemove(const Chunk *chunk)
{
    if (!chunk || chunk < chunks || chunk >= chunks + MAX_ACTIVE_CHUNKS) {
        return;
    }
    ChunkIndexRemoveSlot((int)(chunk - chunks));
}

static Chunk *ChunkIndexFind(
    bool spherical, int cx, int cz, SurfaceChunkKey surfaceKey)
{
    int index = ChunkIndexFindEntry(
        spherical, cx, cz, surfaceKey, false);
    if (index >= 0) {
        ChunkIndexEntry *entry = &chunkIndex[index];
        if (entry->slotIndex >= 0 && entry->slotIndex < MAX_ACTIVE_CHUNKS) {
            Chunk *candidate = &chunks[entry->slotIndex];
            if (candidate->loaded && candidate->spherical == spherical &&
                (spherical
                     ? SurfaceChunkKeyEqual(candidate->surfaceKey, surfaceKey)
                     : (candidate->cx == cx && candidate->cz == cz))) {
                return candidate;
            }
        }
        entry->occupied = false;
        entry->tombstone = true;
        entry->slotIndex = -1;
    }

    for (int slotIndex = 0; slotIndex < MAX_ACTIVE_CHUNKS; slotIndex++) {
        Chunk *candidate = &chunks[slotIndex];
        if (!candidate->loaded || candidate->spherical != spherical) continue;
        bool matches = spherical
            ? SurfaceChunkKeyEqual(candidate->surfaceKey, surfaceKey)
            : (candidate->cx == cx && candidate->cz == cz);
        if (!matches) continue;
        ChunkIndexInsert(candidate);
        return candidate;
    }
    return NULL;
}

Chunk *ChunkIndexFindSurface(SurfaceChunkKey key)
{
    return ChunkIndexFind(true, 0, 0, key);
}

Chunk *ChunkIndexFindFlat(int cx, int cz)
{
    return ChunkIndexFind(false, cx, cz, (SurfaceChunkKey){ 0 });
}

static void ChunkTaskDequeCompact(ChunkTaskDeque *deque)
{
    if (!deque || deque->used <= 0) {
        if (deque) {
            deque->head = 0;
            deque->tail = 0;
            deque->used = 0;
            deque->validCount = 0;
        }
        return;
    }
    ChunkTaskQueueEntry compact[CHUNK_TASK_QUEUE_CAPACITY];
    int count = 0;
    for (int offset = 0; offset < deque->used; offset++) {
        int index = (deque->head + offset) % CHUNK_TASK_QUEUE_CAPACITY;
        if (deque->entries[index].valid) {
            compact[count++] = deque->entries[index];
        }
    }
    memcpy(deque->entries, compact, (size_t)count * sizeof(*compact));
    deque->head = 0;
    deque->tail = count % CHUNK_TASK_QUEUE_CAPACITY;
    deque->used = count;
    deque->validCount = count;
}

void ChunkTaskQueueReset(ChunkTaskQueue *queue)
{
    if (!queue) return;
    memset(queue, 0, sizeof(*queue));
}

bool ChunkTaskQueuePush(ChunkTaskQueue *queue, int lane,
                        int slotIndex, uint64_t sequence)
{
    if (!queue || lane < 0 || lane >= CHUNK_TASK_QUEUE_LANES ||
        slotIndex < 0 || sequence == 0u) return false;
    ChunkTaskDeque *deque = &queue->lanes[lane];
    if (deque->used >= CHUNK_TASK_QUEUE_CAPACITY) {
        ChunkTaskDequeCompact(deque);
    }
    if (deque->used >= CHUNK_TASK_QUEUE_CAPACITY) return false;
    deque->entries[deque->tail] = (ChunkTaskQueueEntry){
        .slotIndex = slotIndex,
        .sequence = sequence,
        .valid = true
    };
    deque->tail = (deque->tail + 1) % CHUNK_TASK_QUEUE_CAPACITY;
    deque->used++;
    deque->validCount++;
    return true;
}

bool ChunkTaskQueuePushOrdered(ChunkTaskQueue *queue, int lane,
                               int slotIndex, uint64_t sequence)
{
    if (!queue || lane < 0 || lane >= CHUNK_TASK_QUEUE_LANES ||
        slotIndex < 0 || sequence == 0u) return false;
    ChunkTaskDeque *deque = &queue->lanes[lane];
    ChunkTaskDequeCompact(deque);
    if (deque->used >= CHUNK_TASK_QUEUE_CAPACITY) return false;

    int insert = 0;
    while (insert < deque->used &&
           deque->entries[insert].sequence <= sequence) {
        insert++;
    }
    if (insert < deque->used) {
        memmove(&deque->entries[insert + 1], &deque->entries[insert],
                (size_t)(deque->used - insert) * sizeof(*deque->entries));
    }
    deque->entries[insert] = (ChunkTaskQueueEntry){
        .slotIndex = slotIndex,
        .sequence = sequence,
        .valid = true
    };
    deque->head = 0;
    deque->used++;
    deque->validCount++;
    deque->tail = deque->used % CHUNK_TASK_QUEUE_CAPACITY;
    return true;
}

bool ChunkTaskQueuePop(ChunkTaskQueue *queue, int *slotIndex,
                       uint64_t *sequence)
{
    if (!queue || !slotIndex || !sequence) return false;
    for (int lane = 0; lane < CHUNK_TASK_QUEUE_LANES; lane++) {
        ChunkTaskDeque *deque = &queue->lanes[lane];
        while (deque->used > 0) {
            ChunkTaskQueueEntry entry = deque->entries[deque->head];
            deque->entries[deque->head].valid = false;
            deque->head = (deque->head + 1) % CHUNK_TASK_QUEUE_CAPACITY;
            deque->used--;
            if (!entry.valid) continue;
            deque->validCount--;
            *slotIndex = entry.slotIndex;
            *sequence = entry.sequence;
            if (deque->used == 0) {
                deque->head = 0;
                deque->tail = 0;
            }
            return true;
        }
        deque->head = 0;
        deque->tail = 0;
        deque->validCount = 0;
    }
    return false;
}

bool ChunkTaskQueuePeek(const ChunkTaskQueue *queue, int *slotIndex,
                        uint64_t *sequence)
{
    if (!queue || !slotIndex || !sequence) return false;
    for (int lane = 0; lane < CHUNK_TASK_QUEUE_LANES; lane++) {
        const ChunkTaskDeque *deque = &queue->lanes[lane];
        for (int offset = 0; offset < deque->used; offset++) {
            int index = (deque->head + offset) % CHUNK_TASK_QUEUE_CAPACITY;
            if (!deque->entries[index].valid) continue;
            *slotIndex = deque->entries[index].slotIndex;
            *sequence = deque->entries[index].sequence;
            return true;
        }
    }
    return false;
}

void ChunkTaskQueueRemove(ChunkTaskQueue *queue, int slotIndex,
                          uint64_t sequence)
{
    if (!queue || slotIndex < 0 || sequence == 0u) return;
    for (int lane = 0; lane < CHUNK_TASK_QUEUE_LANES; lane++) {
        ChunkTaskDeque *deque = &queue->lanes[lane];
        for (int offset = 0; offset < deque->used; offset++) {
            int index = (deque->head + offset) % CHUNK_TASK_QUEUE_CAPACITY;
            ChunkTaskQueueEntry *entry = &deque->entries[index];
            if (!entry->valid || entry->slotIndex != slotIndex ||
                entry->sequence != sequence) continue;
            entry->valid = false;
            deque->validCount--;
        }
    }
}

bool ChunkTaskQueueHasPending(const ChunkTaskQueue *queue)
{
    if (!queue) return false;
    for (int lane = 0; lane < CHUNK_TASK_QUEUE_LANES; lane++) {
        if (queue->lanes[lane].validCount > 0) return true;
    }
    return false;
}

bool ChunkTaskQueueHasLane(const ChunkTaskQueue *queue, int lane)
{
    return queue && lane >= 0 && lane < CHUNK_TASK_QUEUE_LANES &&
           queue->lanes[lane].validCount > 0;
}

const Chunk *ChunksView(void)
{
    return chunks;
}

Texture2D ChunksBlockAtlas(void)
{
    return blockAtlas;
}

void ChunksSetBlockAtlas(Texture2D atlas)
{
    blockAtlas = atlas;
}

int ChunksRenderDistance(void)
{
    return renderDistanceChunks;
}

void ChunksSetRenderDistance(int distance)
{
    if (distance < MIN_RENDER_DISTANCE_CHUNKS) {
        distance = MIN_RENDER_DISTANCE_CHUNKS;
    }
    if (distance > MAX_RENDER_DISTANCE_CHUNKS) {
        distance = MAX_RENDER_DISTANCE_CHUNKS;
    }
    renderDistanceChunks = distance;
}

#ifdef CHUNKS_TESTING
Chunk *ChunksMutableForTesting(void)
{
    return chunks;
}
#endif

double ChunkNowMs(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

double ChunkThreadCpuNowMs(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

bool NegativeTerrainSectionOutsideWindow(
    int sectionY, int playerSectionY)
{
    return sectionY < 0 &&
           (sectionY < playerSectionY -
                           NEGATIVE_TERRAIN_SECTION_RETAIN_RADIUS ||
            sectionY > playerSectionY +
                           NEGATIVE_TERRAIN_SECTION_RETAIN_RADIUS);
}

bool InHeight(int y)
{
    return y >= SURFACE_MIN_Y && y < SURFACE_MAX_Y_EXCLUSIVE;
}

int FloorDivInt(int value, int divisor)
{
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

int PositiveMod(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

bool SurfaceSectionInBounds(int sectionY)
{
    return sectionY >= SURFACE_SECTION_MIN_Y &&
           sectionY < SURFACE_SECTION_MAX_Y_EXCLUSIVE;
}

int SurfaceSectionYFromBlockY(int y)
{
    return FloorDivInt(y, SURFACE_SECTION_HEIGHT);
}

int SurfaceSectionLocalYFromBlockY(int y)
{
    return PositiveMod(y, SURFACE_SECTION_HEIGHT);
}

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    if (!cx || !cz || !lx || !lz) return;

    if (WorldIsSurfaceActive()) {
        int originX = WorldSurfaceMapOriginX();
        int originZ = WorldSurfaceMapOriginZ();
        SurfaceMapCell canonical = SurfaceCanonicalMapCell(
            (float)originX + (float)x,
            (float)originZ + (float)z);
        x = canonical.x - originX;
        z = canonical.z - originZ;
    }
    *cx = FloorDivInt(x, CHUNK_SIZE);
    *cz = FloorDivInt(z, CHUNK_SIZE);
    *lx = PositiveMod(x, CHUNK_SIZE);
    *lz = PositiveMod(z, CHUNK_SIZE);
}

void CanonicalizeSurfaceChunkCoordinates(int *cx, int *cz)
{
    if (!cx || !cz || !WorldIsSurfaceActive()) return;
    int centerX = *cx * CHUNK_SIZE + CHUNK_SIZE / 2;
    int centerZ = *cz * CHUNK_SIZE + CHUNK_SIZE / 2;
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)WorldSurfaceMapOriginX() + (float)centerX,
        (float)WorldSurfaceMapOriginZ() + (float)centerZ);
    *cx = FloorDivInt(canonical.x - WorldSurfaceMapOriginX(), CHUNK_SIZE);
    *cz = FloorDivInt(canonical.z - WorldSurfaceMapOriginZ(), CHUNK_SIZE);
}

Chunk *FindChunk(int cx, int cz)
{
    if (WorldIsSurfaceActive()) {
        return FindSurfaceChunk(ChunkSurfaceKeyAt(cx, cz));
    }
    return ChunkIndexFindFlat(cx, cz);
}

Chunk *FindHorizontalChunkNeighbor(int cx, int cz, int deltaCx, int deltaCz)
{
    cx += deltaCx;
    cz += deltaCz;
    CanonicalizeSurfaceChunkCoordinates(&cx, &cz);
    return FindChunk(cx, cz);
}

SurfaceAddress SurfaceAddressAtWorld(float x, float z, int radial)
{
    x += (float)WorldSurfaceMapOriginX();
    z += (float)WorldSurfaceMapOriginZ();
    return SurfaceAddressFromMapCoordinates(
        WorldCurrentSurfaceId(), x, z, radial);
}

SurfaceAddress ChunkSurfaceAddressAt(int cx, int cz)
{
    return SurfaceAddressAtWorld(
        ((float)cx + 0.5f) * (float)CHUNK_SIZE,
        ((float)cz + 0.5f) * (float)CHUNK_SIZE, 0);
}

static void SortSurfaceChunkCorners(SurfaceMapCell corners[4])
{
    for (int index = 1; index < 4; index++) {
        SurfaceMapCell value = corners[index];
        int insert = index;
        while (insert > 0 &&
               (corners[insert - 1].z > value.z ||
                (corners[insert - 1].z == value.z &&
                 corners[insert - 1].x > value.x))) {
            corners[insert] = corners[insert - 1];
            insert--;
        }
        corners[insert] = value;
    }
}

SurfaceChunkKey ChunkSurfaceKeyAt(int cx, int cz)
{
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    int minX = originX + cx * CHUNK_SIZE;
    int minZ = originZ + cz * CHUNK_SIZE;
    int maxX = minX + CHUNK_SIZE - 1;
    int maxZ = minZ + CHUNK_SIZE - 1;
    SurfaceChunkKey key = {
        .bodyId = WorldCurrentSurfaceId(),
        .corners = {
            SurfaceCanonicalMapCell((float)minX, (float)minZ),
            SurfaceCanonicalMapCell((float)maxX, (float)minZ),
            SurfaceCanonicalMapCell((float)minX, (float)maxZ),
            SurfaceCanonicalMapCell((float)maxX, (float)maxZ)
        }
    };
    SortSurfaceChunkCorners(key.corners);
    return key;
}

Chunk *FindSurfaceChunk(SurfaceChunkKey key)
{
    return ChunkIndexFindSurface(key);
}

int ChunkGridDistanceFrom(const Chunk *chunk, int cx, int cz)
{
    if (!chunk) return INT_MAX;
    if (!chunk->spherical) {
        int dx = abs(chunk->cx - cx);
        int dz = abs(chunk->cz - cz);
        return dx > dz ? dx : dz;
    }
    float originX = (float)WorldSurfaceMapOriginX();
    float originZ = (float)WorldSurfaceMapOriginZ();
    float half = (float)CHUNK_SIZE * 0.5f;
    SurfaceMapOffset offset = SurfaceShortestMapOffset(
        originX + ((float)cx * (float)CHUNK_SIZE) + half,
        originZ + ((float)cz * (float)CHUNK_SIZE) + half,
        originX + ((float)chunk->cx * (float)CHUNK_SIZE) + half,
        originZ + ((float)chunk->cz * (float)CHUNK_SIZE) + half);
    int dx = (int)lroundf(fabsf(offset.x) / (float)CHUNK_SIZE);
    int dz = (int)lroundf(fabsf(offset.z) / (float)CHUNK_SIZE);
    return dx > dz ? dx : dz;
}

int ChunkSectionLowerBound(const Chunk *chunk, int sectionY)
{
    int low = 0;
    int high = chunk ? chunk->sectionCount : 0;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (chunk->sections[middle]->sectionY < sectionY) low = middle + 1;
        else high = middle;
    }
    return low;
}

ChunkSection *ChunkGetSection(Chunk *chunk, int sectionY, bool create)
{
    if (!chunk) return NULL;
    int index = ChunkSectionLowerBound(chunk, sectionY);
    if (index < chunk->sectionCount &&
        chunk->sections[index]->sectionY == sectionY) {
        return chunk->sections[index];
    }
    if (!create) return NULL;

    ChunkSection *section = calloc(1, sizeof(*section));
    if (!section) return NULL;
    if (chunk->sectionCount == chunk->sectionCapacity) {
        if (chunk->sectionCapacity > INT_MAX / 2) {
            free(section);
            return NULL;
        }
        int capacity = chunk->sectionCapacity > 0
            ? chunk->sectionCapacity * 2 : 4;
        ChunkSection **sections = realloc(
            chunk->sections, (size_t)capacity * sizeof(*sections));
        if (!sections) {
            free(section);
            return NULL;
        }
        chunk->sections = sections;
        chunk->sectionCapacity = capacity;
    }
    if (index < chunk->sectionCount) {
        memmove(&chunk->sections[index + 1], &chunk->sections[index],
                (size_t)(chunk->sectionCount - index) *
                    sizeof(*chunk->sections));
    }
    section->sectionY = sectionY;
    section->floraVisualScale = 1.0f;
    chunk->sections[index] = section;
    chunk->sectionCount++;
    return section;
}

const ChunkSection *ChunkGetSectionConst(const Chunk *chunk, int sectionY)
{
    if (!chunk) return NULL;
    int index = ChunkSectionLowerBound(chunk, sectionY);
    return index < chunk->sectionCount &&
           chunk->sections[index]->sectionY == sectionY
        ? chunk->sections[index] : NULL;
}

int ResolvedTerrainSectionLowerBound(
    const Chunk *chunk, int sectionY)
{
    int low = 0;
    int high = chunk ? chunk->resolvedTerrainSectionCount : 0;
    while (low < high) {
        int middle = low + (high - low) / 2;
        if (chunk->resolvedTerrainSectionYs[middle] < sectionY) {
            low = middle + 1;
        } else {
            high = middle;
        }
    }
    return low;
}

bool ChunkTerrainSectionIsResolved(const Chunk *chunk, int sectionY)
{
    if (!chunk) return false;
    int index = ResolvedTerrainSectionLowerBound(chunk, sectionY);
    return index < chunk->resolvedTerrainSectionCount &&
           chunk->resolvedTerrainSectionYs[index] == sectionY;
}

bool ChunkMarkTerrainSectionResolved(Chunk *chunk, int sectionY)
{
    if (!chunk) return false;
    int index = ResolvedTerrainSectionLowerBound(chunk, sectionY);
    if (index < chunk->resolvedTerrainSectionCount &&
        chunk->resolvedTerrainSectionYs[index] == sectionY) {
        return true;
    }
    if (chunk->resolvedTerrainSectionCount ==
        chunk->resolvedTerrainSectionCapacity) {
        if (chunk->resolvedTerrainSectionCapacity > INT_MAX / 2) {
            return false;
        }
        int capacity = chunk->resolvedTerrainSectionCapacity > 0
            ? chunk->resolvedTerrainSectionCapacity * 2 : 4;
        int *sectionYs = realloc(
            chunk->resolvedTerrainSectionYs,
            (size_t)capacity * sizeof(*sectionYs));
        if (!sectionYs) return false;
        chunk->resolvedTerrainSectionYs = sectionYs;
        chunk->resolvedTerrainSectionCapacity = capacity;
    }
    if (index < chunk->resolvedTerrainSectionCount) {
        memmove(&chunk->resolvedTerrainSectionYs[index + 1],
                &chunk->resolvedTerrainSectionYs[index],
                (size_t)(chunk->resolvedTerrainSectionCount - index) *
                    sizeof(*chunk->resolvedTerrainSectionYs));
    }
    chunk->resolvedTerrainSectionYs[index] = sectionY;
    chunk->resolvedTerrainSectionCount++;
    return true;
}

bool ChunkTryGetLocalBlock(const Chunk *chunk, int lx, int y, int lz,
                           BlockType *outBlock)
{
    if (!chunk || lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        !InHeight(y) || !outBlock) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    const ChunkSection *section = ChunkGetSectionConst(chunk, sectionY);
    if (!section) return false;
    *outBlock = (BlockType)section->blocks[
        lx][SurfaceSectionLocalYFromBlockY(y)][lz];
    return true;
}

BlockType ChunkGetLocalBlock(const Chunk *chunk, int lx, int y, int lz)
{
    BlockType block = BLOCK_AIR;
    return ChunkTryGetLocalBlock(chunk, lx, y, lz, &block)
        ? block : BLOCK_AIR;
}

bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type)
{
    if (!chunk || lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        !InHeight(y)) return false;
    int sectionY = SurfaceSectionYFromBlockY(y);
    ChunkSection *section = ChunkGetSection(chunk, sectionY, type != BLOCK_AIR);
    if (!section) return type == BLOCK_AIR;
    section->blocks[lx][SurfaceSectionLocalYFromBlockY(y)][lz] =
        (unsigned short)type;
    return true;
}

void ClearSectionFloraRuntime(ChunkSection *section)
{
    if (!section) return;
    free(section->floraTargetScales);
    section->floraTargetScales = NULL;
    free(section->floraTargetWind);
    section->floraTargetWind = NULL;
    free(section->floraTargetWindAngle);
    section->floraTargetWindAngle = NULL;
    free(section->floraTargetPresence);
    section->floraTargetPresence = NULL;
    free(section->floraBaseVertices);
    section->floraBaseVertices = NULL;
    free(section->floraBaseColors);
    section->floraBaseColors = NULL;
    free(section->floraVisualInstances);
    section->floraVisualInstances = NULL;
    section->floraTargetScaleCount = 0;
}

static void UnloadChunkSectionModels(ChunkSection *section)
{
    if (!section) return;
    if (section->hasModel) {
        UnloadModel(section->model);
        section->model = (Model){ 0 };
        section->hasModel = false;
    }
    if (section->hasLodModel) {
        UnloadModel(section->lodModel);
        section->lodModel = (Model){ 0 };
        section->hasLodModel = false;
    }
    if (section->hasWaterModel) {
        UnloadModel(section->waterModel);
        section->waterModel = (Model){ 0 };
        section->hasWaterModel = false;
    }
    if (section->hasLodWaterModel) {
        UnloadModel(section->lodWaterModel);
        section->lodWaterModel = (Model){ 0 };
        section->hasLodWaterModel = false;
    }
    if (section->hasFloraModel) {
        UnloadModel(section->floraModel);
        section->floraModel = (Model){ 0 };
        section->hasFloraModel = false;
    }
    if (section->hasLodFloraModel) {
        UnloadModel(section->lodFloraModel);
        section->lodFloraModel = (Model){ 0 };
        section->hasLodFloraModel = false;
    }
    ClearSectionFloraRuntime(section);
}

void FreeChunkSectionStorage(ChunkSection *section)
{
    if (!section) return;
    UnloadChunkSectionModels(section);
    free(section->waterVolumes);
    free(section->fluidQueuedBits);
    free(section->fluidDeferredBits);
    free(section->fluidFlow);
    free(section);
}

void UnloadChunkModel(Chunk *chunk)
{
    if (!chunk) return;
    for (int index = 0; index < chunk->sectionCount; index++) {
        UnloadChunkSectionModels(chunk->sections[index]);
    }
}

void ChunkClearBlockStorage(Chunk *chunk)
{
    if (!chunk) return;
    for (int index = 0; index < chunk->sectionCount; index++) {
        FreeChunkSectionStorage(chunk->sections[index]);
    }
    free(chunk->sections);
    chunk->sections = NULL;
    chunk->sectionCount = 0;
    chunk->sectionCapacity = 0;
    free(chunk->resolvedTerrainSectionYs);
    chunk->resolvedTerrainSectionYs = NULL;
    chunk->resolvedTerrainSectionCount = 0;
    chunk->resolvedTerrainSectionCapacity = 0;
}

void MarkSectionDirty(ChunkSection *section)
{
    if (!section) return;
    if (!section->dirty) section->dirtySinceMs = ChunkNowMs();
    section->dirty = true;
    section->dirtyStamp++;
    if (section->dirtyStamp == 0u) section->dirtyStamp = 1u;
}

void ReplaceChunkModel(Model *model, bool *hasModel,
                       Mesh *mesh, bool hasMesh, bool dynamic)
{
    if (!hasMesh) {
        if (*hasModel) {
            UnloadModel(*model);
            *model = (Model){ 0 };
            *hasModel = false;
        }
        FreeMeshData(mesh);
        return;
    }

    UploadMesh(mesh, dynamic);
    Model replacement = LoadModelFromMesh(*mesh);
    if (replacement.materialCount > 0 && replacement.materials) {
        SetMaterialTexture(&replacement.materials[0], MATERIAL_MAP_DIFFUSE,
                           blockAtlas);
    }
    if (*hasModel) UnloadModel(*model);
    *model = replacement;
    *hasModel = true;
}

void MarkChunkDirty(int cx, int cz)
{
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return;
    for (int index = 0; index < chunk->sectionCount; index++) {
        MarkSectionDirty(chunk->sections[index]);
    }
}

static void MarkChunkDirtyAtHorizontalOffset(int cx, int cz,
                                             int deltaCx, int deltaCz)
{
    Chunk *chunk = FindHorizontalChunkNeighbor(cx, cz, deltaCx, deltaCz);
    if (!chunk) return;
    for (int index = 0; index < chunk->sectionCount; index++) {
        MarkSectionDirty(chunk->sections[index]);
    }
}

void MarkChunkDirtyAtBlock(int x, int y, int z)
{
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (chunk && InHeight(y)) {
        int sectionY = SurfaceSectionYFromBlockY(y);
        int localY = SurfaceSectionLocalYFromBlockY(y);
        ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
        MarkSectionDirty(section);
        if (localY == 0 && SurfaceSectionInBounds(sectionY - 1)) {
            section = ChunkGetSection(chunk, sectionY - 1, false);
            MarkSectionDirty(section);
        }
        if (localY == SURFACE_SECTION_HEIGHT - 1 &&
            InHeight(y + 1)) {
            section = ChunkGetSection(chunk, sectionY + 1, false);
            MarkSectionDirty(section);
        }
    }
    if (lx == 0) MarkChunkDirtyAtHorizontalOffset(cx, cz, -1, 0);
    if (lx == CHUNK_SIZE - 1) {
        MarkChunkDirtyAtHorizontalOffset(cx, cz, 1, 0);
    }
    if (lz == 0) MarkChunkDirtyAtHorizontalOffset(cx, cz, 0, -1);
    if (lz == CHUNK_SIZE - 1) {
        MarkChunkDirtyAtHorizontalOffset(cx, cz, 0, 1);
    }
}

void MarkChunkAndHorizontalNeighborsDirty(int cx, int cz)
{
    MarkChunkDirty(cx, cz);
    MarkChunkDirtyAtHorizontalOffset(cx, cz, -1, 0);
    MarkChunkDirtyAtHorizontalOffset(cx, cz, 1, 0);
    MarkChunkDirtyAtHorizontalOffset(cx, cz, 0, -1);
    MarkChunkDirtyAtHorizontalOffset(cx, cz, 0, 1);
}

unsigned int Hash3D(int x, int y, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ (unsigned int)y) * 16777619u;
    h = (h ^ (unsigned int)z) * 16777619u;
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}
