#include "world/chunks_internal.h"

Chunk chunks[MAX_ACTIVE_CHUNKS];
Texture2D blockAtlas = { 0 };
int renderDistanceChunks = DEFAULT_RENDER_DISTANCE_CHUNKS;
ChunkGenJob chunkGenJobs[MAX_CHUNK_GEN_JOBS];
pthread_mutex_t genMutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t genCond = PTHREAD_COND_INITIALIZER;
pthread_t genThread = 0;
bool genShutdown = false;
bool genWorkerActive = false;
ChunkStreamingStats streamingStats;

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
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded && !chunks[i].spherical &&
            chunks[i].cx == cx && chunks[i].cz == cz) return &chunks[i];
    }
    return NULL;
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
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded && chunks[i].spherical &&
            SurfaceChunkKeyEqual(chunks[i].surfaceKey, key)) {
            return &chunks[i];
        }
    }
    return NULL;
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
    if (section->hasWaterModel) {
        UnloadModel(section->waterModel);
        section->waterModel = (Model){ 0 };
        section->hasWaterModel = false;
    }
    if (section->hasFloraModel) {
        UnloadModel(section->floraModel);
        section->floraModel = (Model){ 0 };
        section->hasFloraModel = false;
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
