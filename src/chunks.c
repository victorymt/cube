#include "chunks.h"

#include "block_atlas.h"

#include "raymath.h"
#include "ecology.h"
#include "space.h"
#include "terrain.h"
#include "world.h"
#include "weather.h"

#include <math.h>
#include <limits.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef CHUNKS_TESTING
#include <assert.h>
#endif

#include "raymath.h"
Chunk chunks[MAX_ACTIVE_CHUNKS];
Texture2D blockAtlas = { 0 };
int renderDistanceChunks = DEFAULT_RENDER_DISTANCE_CHUNKS;
static ChunkGenJob chunkGenJobs[MAX_CHUNK_GEN_JOBS];
static pthread_mutex_t genMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t genCond = PTHREAD_COND_INITIALIZER;
static pthread_t genThread = 0;
static bool genShutdown = false;
static bool genWorkerActive = false;
static ChunkStreamingStats streamingStats;

static double ChunkNowMs(void)
{
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 + (double)value.tv_nsec / 1000000.0;
}

static void UpdateQueuePeaksLocked(void);

struct MeshJob {
    bool inUse;
    bool done;
    int slotIndex;
    int cx;
    int cz;
    int sectionY;
    // Snapshot consistency stamps: the section content revision and the
    // chunk incarnation captured when the snapshot was taken. An upload is
    // only applied when both still match the live chunk state, so edits made
    // after the snapshot keep the section dirty (rebuild) instead of being
    // silently cleared, and jobs from a previous chunk incarnation are
    // discarded instead of overwriting fresh terrain.
    uint32_t sectionStamp;
    uint32_t chunkGeneration;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    int nearbyIndices[MAX_TORCH_LIGHTS];
    int nearbyCount;
    Mesh mesh;
    Mesh waterMesh;
    Mesh floraMesh;
    FloraVisualInstance *floraInstances;
    int floraInstanceCount;
    bool hasMesh;
    bool hasWaterMesh;
    bool hasFloraMesh;
};
typedef struct MeshJob MeshJob;
static bool HasPendingMeshJob(void);
static MeshJob *NextPendingMeshJob(void);
static void FreeMeshData(Mesh *mesh);
static bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh);
static bool BuildChunkFloraMeshDataFromSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount);
bool BuildMeshData(const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   bool transparent, const int faces[6][3],
                   const int *nearbyTorchIndices, int nearbyTorchCount,
                   Mesh *outMesh);

bool InHeight(int y)
{
    return y >= 0 && y < SURFACE_WORLD_HEIGHT;
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

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    *cx = FloorDivInt(x, CHUNK_SIZE);
    *cz = FloorDivInt(z, CHUNK_SIZE);
    *lx = PositiveMod(x, CHUNK_SIZE);
    *lz = PositiveMod(z, CHUNK_SIZE);
}

Chunk *FindChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded && chunks[i].cx == cx && chunks[i].cz == cz) return &chunks[i];
    }
    return NULL;
}

ChunkSection *ChunkGetSection(Chunk *chunk, int sectionY, bool create)
{
    if (!chunk || sectionY < 0 || sectionY >= SURFACE_SECTION_COUNT) return NULL;
    ChunkSection *section = chunk->sections[sectionY];
    if (!section && create) {
        section = calloc(1, sizeof(*section));
        if (!section) return NULL;
        section->sectionY = sectionY;
        section->floraVisualScale = 1.0f;
        chunk->sections[sectionY] = section;
    }
    return section;
}

const ChunkSection *ChunkGetSectionConst(const Chunk *chunk, int sectionY)
{
    if (!chunk || sectionY < 0 || sectionY >= SURFACE_SECTION_COUNT) return NULL;
    return chunk->sections[sectionY];
}

BlockType ChunkGetLocalBlock(const Chunk *chunk, int lx, int y, int lz)
{
    if (!chunk || lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        !InHeight(y)) return BLOCK_AIR;
    int sectionY = y / SURFACE_SECTION_HEIGHT;
    const ChunkSection *section = ChunkGetSectionConst(chunk, sectionY);
    return section ? (BlockType)section->blocks[lx][y % SURFACE_SECTION_HEIGHT][lz]
                   : BLOCK_AIR;
}

bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type)
{
    if (!chunk || lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        !InHeight(y)) return false;
    int sectionY = y / SURFACE_SECTION_HEIGHT;
    ChunkSection *section = ChunkGetSection(chunk, sectionY, type != BLOCK_AIR);
    if (!section) return type == BLOCK_AIR;
    section->blocks[lx][y % SURFACE_SECTION_HEIGHT][lz] = (unsigned short)type;
    return true;
}

static void ClearSectionFloraRuntime(ChunkSection *section)
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

void UnloadChunkModel(Chunk *chunk)
{
    if (!chunk) return;
    for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
        UnloadChunkSectionModels(chunk->sections[sy]);
    }
}

void ChunkClearBlockStorage(Chunk *chunk)
{
    if (!chunk) return;
    for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
        ChunkSection *section = chunk->sections[sy];
        if (!section) continue;
        UnloadChunkSectionModels(section);
        free(section);
        chunk->sections[sy] = NULL;
    }
}

static void MarkSectionDirty(ChunkSection *section)
{
    if (!section) return;
    section->dirty = true;
    section->dirtyStamp++;
    if (section->dirtyStamp == 0u) section->dirtyStamp = 1u;
}

void MarkChunkDirty(int cx, int cz)
{
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return;
    for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
        MarkSectionDirty(chunk->sections[sy]);
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
        int sectionY = y / SURFACE_SECTION_HEIGHT;
        ChunkSection *section = ChunkGetSection(chunk, sectionY, false);
        MarkSectionDirty(section);
        if (y % SURFACE_SECTION_HEIGHT == 0 && sectionY > 0) {
            section = ChunkGetSection(chunk, sectionY - 1, false);
            MarkSectionDirty(section);
        }
        if (y % SURFACE_SECTION_HEIGHT == SURFACE_SECTION_HEIGHT - 1 &&
            sectionY + 1 < SURFACE_SECTION_COUNT) {
            section = ChunkGetSection(chunk, sectionY + 1, false);
            MarkSectionDirty(section);
        }
    }
    if (lx == 0) MarkChunkDirty(cx - 1, cz);
    if (lx == CHUNK_SIZE - 1) MarkChunkDirty(cx + 1, cz);
    if (lz == 0) MarkChunkDirty(cx, cz - 1);
    if (lz == CHUNK_SIZE - 1) MarkChunkDirty(cx, cz + 1);
}

void MarkChunkAndHorizontalNeighborsDirty(int cx, int cz)
{
    MarkChunkDirty(cx, cz);
    MarkChunkDirty(cx - 1, cz);
    MarkChunkDirty(cx + 1, cz);
    MarkChunkDirty(cx, cz - 1);
    MarkChunkDirty(cx, cz + 1);
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

bool HasPendingGenJob(void)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) return true;
    }
    return false;
}

ChunkGenJob *NextPendingGenJob(void)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) return &chunkGenJobs[i];
    }
    return NULL;
}

void *ChunkGenWorker(void *arg)
{
    (void)arg;
    bool preferMesh = false;

    for (;;) {
        pthread_mutex_lock(&genMutex);
        while (!genShutdown && !HasPendingGenJob() && !HasPendingMeshJob()) {
            pthread_cond_wait(&genCond, &genMutex);
        }
        if (genShutdown) {
            pthread_mutex_unlock(&genMutex);
            break;
        }

        bool haveGeneration = HasPendingGenJob();
        bool haveMesh = HasPendingMeshJob();
        ChunkGenJob *job = NULL;
        MeshJob *meshJob = NULL;
        if (haveMesh && (preferMesh || !haveGeneration)) {
            meshJob = NextPendingMeshJob();
            preferMesh = false;
        } else {
            job = NextPendingGenJob();
            if (job) preferMesh = true;
        }
        if (job) {
            genWorkerActive = true;
            pthread_mutex_unlock(&genMutex);

            double startedMs = ChunkNowMs();
            GenerateChunkTerrain(&chunks[job->slotIndex], job->cx, job->cz, job->terrainMode);
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            job->done = true;
            genWorkerActive = false;
            streamingStats.generationCompleted++;
            streamingStats.generationCpuMs += elapsedMs;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        if (meshJob) {
            genWorkerActive = true;
            pthread_mutex_unlock(&genMutex);

            static const int faces[6][3] = {
                { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
            };
            double startedMs = ChunkNowMs();
            meshJob->hasMesh = BuildChunkSurfaceSolidMeshData(
                meshJob->blocks,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz,
                meshJob->floraStructures, meshJob->floraStructureCount,
                faces, meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->mesh);
            meshJob->hasWaterMesh = BuildSurfaceWaterMeshData(
                (const unsigned short (*)[CHUNK_SIZE])meshJob->blocks,
                SURFACE_SECTION_HEIGHT,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz, faces,
                meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->waterMesh);
            meshJob->hasFloraMesh = BuildChunkFloraMeshDataFromSnapshot(
                meshJob->blocks,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz,
                meshJob->floraStructures, meshJob->floraStructureCount,
                faces, meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->floraMesh, &meshJob->floraInstances,
                &meshJob->floraInstanceCount);
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            meshJob->done = true;
            genWorkerActive = false;
            streamingStats.meshCompleted++;
            streamingStats.meshCpuMs += elapsedMs;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        pthread_mutex_unlock(&genMutex);
    }

    return NULL;
}

bool SubmitChunkGenJob(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    if (genThread == 0) return false;

    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = NULL;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (!chunkGenJobs[i].inUse) {
            job = &chunkGenJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    *job = (ChunkGenJob){
        .inUse = true,
        .done = false,
        .cx = cx,
        .cz = cz,
        .slotIndex = (int)(chunk - chunks),
        .terrainMode = mode
    };
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool FindPendingGenJob(int cx, int cz)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && chunkGenJobs[i].cx == cx && chunkGenJobs[i].cz == cz) return true;
    }
    return false;
}

void CompleteChunkGenJob(ChunkGenJob *job)
{
    Chunk *chunk = &chunks[job->slotIndex];
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    MarkChunkAndHorizontalNeighborsDirty(chunk->cx, chunk->cz);
}

void ProcessFinishedChunkJobs(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        ChunkGenJob *job = NULL;
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (chunkGenJobs[i].inUse && chunkGenJobs[i].done) {
                job = &chunkGenJobs[i];
                break;
            }
        }
        if (job) job->inUse = false;
        pthread_mutex_unlock(&genMutex);

        if (!job) return;
        CompleteChunkGenJob(job);
    }
}

void DrainChunkGen(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        for (;;) {
            ChunkGenJob *job = NULL;
            for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
                if (chunkGenJobs[i].inUse && chunkGenJobs[i].done) {
                    job = &chunkGenJobs[i];
                    break;
                }
            }
            if (!job) break;
            job->inUse = false;
            pthread_mutex_unlock(&genMutex);
            CompleteChunkGenJob(job);
            pthread_mutex_lock(&genMutex);
        }

        bool busy = false;
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (chunkGenJobs[i].inUse) {
                busy = true;
                break;
            }
        }
        if (!busy) {
            pthread_mutex_unlock(&genMutex);
            return;
        }
        pthread_cond_wait(&genCond, &genMutex);
        pthread_mutex_unlock(&genMutex);
    }
}

Chunk *AllocateChunkSlot(int nearCx, int nearCz)
{
    int bestIndex = -1;
    int bestDistance = -1;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded && !chunks[i].generating) return &chunks[i];

        int dx = abs(chunks[i].cx - nearCx);
        int dz = abs(chunks[i].cz - nearCz);
        int distance = dx > dz ? dx : dz;
        if (!chunks[i].generating && distance > bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0) {
        MarkChunkAndHorizontalNeighborsDirty(chunks[bestIndex].cx, chunks[bestIndex].cz);
    }
    return bestIndex >= 0 ? &chunks[bestIndex] : NULL;
}

bool EnsureChunk(int cx, int cz)
{
    if (FindChunk(cx, cz) || FindPendingGenJob(cx, cz)) return false;

    if (genThread != 0) {
        bool haveQueueSlot = false;
        pthread_mutex_lock(&genMutex);
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (!chunkGenJobs[i].inUse) {
                haveQueueSlot = true;
                break;
            }
        }
        pthread_mutex_unlock(&genMutex);
        if (!haveQueueSlot) return false;
    }

    Chunk *chunk = AllocateChunkSlot(cx, cz);
    if (!chunk) return false;
    ChunkClearBlockStorage(chunk);
    chunk->cx = cx;
    chunk->cz = cz;
    // New incarnation: invalidates any in-flight mesh jobs captured against
    // the previous occupant of this slot (stale terrain upload guard).
    chunk->generation++;
    if (chunk->generation == 0u) chunk->generation = 1u;
    chunk->generating = true;
    chunk->loaded = false;
    chunk->floraActivity = 1.0f;
    chunk->floraCapacity = 1.0f;
    chunk->floraSampleTimer = 0.0f;

    if (SubmitChunkGenJob(chunk, cx, cz, terrainMode)) return true;

    double startedMs = ChunkNowMs();
    GenerateChunkTerrain(chunk, cx, cz, terrainMode);
    double elapsedMs = ChunkNowMs() - startedMs;
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    pthread_mutex_lock(&genMutex);
    streamingStats.generationCompleted++;
    streamingStats.generationCpuMs += elapsedMs;
    pthread_mutex_unlock(&genMutex);
    MarkChunkAndHorizontalNeighborsDirty(cx, cz);
    return true;
}

void UpdateChunks(Vector3 playerPosition, int effectiveRenderDistance)
{
    if (effectiveRenderDistance < MIN_RENDER_DISTANCE_CHUNKS) effectiveRenderDistance = MIN_RENDER_DISTANCE_CHUNKS;
    if (effectiveRenderDistance > MAX_RENDER_DISTANCE_CHUNKS) effectiveRenderDistance = MAX_RENDER_DISTANCE_CHUNKS;

    int playerX = (int)floorf(playerPosition.x);
    int playerZ = (int)floorf(playerPosition.z);
    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal(playerX, playerZ, &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded) continue;
        if (abs(chunks[i].cx - playerCx) > effectiveRenderDistance ||
            abs(chunks[i].cz - playerCz) > effectiveRenderDistance) {
            MarkChunkAndHorizontalNeighborsDirty(chunks[i].cx, chunks[i].cz);
            ChunkClearBlockStorage(&chunks[i]);
            chunks[i].loaded = false;
        }
    }

    int missingChunks[MAX_ACTIVE_CHUNKS][2];
    int missingCount = 0;
    for (int dz = -effectiveRenderDistance; dz <= effectiveRenderDistance; dz++) {
        for (int dx = -effectiveRenderDistance; dx <= effectiveRenderDistance; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindChunk(cx, cz) || FindPendingGenJob(cx, cz)) continue;

            int insert = missingCount;
            int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            while (insert > 0) {
                int prevDx = missingChunks[insert - 1][0] - playerCx;
                int prevDz = missingChunks[insert - 1][1] - playerCz;
                int prevDistance = abs(prevDx) > abs(prevDz) ? abs(prevDx) : abs(prevDz);
                if (prevDistance <= distance) break;
                missingChunks[insert][0] = missingChunks[insert - 1][0];
                missingChunks[insert][1] = missingChunks[insert - 1][1];
                insert--;
            }
            missingChunks[insert][0] = cx;
            missingChunks[insert][1] = cz;
            missingCount++;
        }
    }

    int submissions = 0;
    for (int i = 0; i < missingCount && submissions < CHUNK_GEN_SUBMISSIONS_PER_FRAME; i++) {
        if (EnsureChunk(missingChunks[i][0], missingChunks[i][1])) submissions++;
    }
}

bool DeformFloraMeshInstance(
    float *vertices, const float *baseVertices, int vertexCount,
    const FloraVisualInstance *instance, float targetScale, float blend,
    float sway, float windAngle, float *outScale, bool *outChanged)
{
    if (!outScale || !outChanged) return false;
    *outScale = 1.0f;
    *outChanged = false;
    if (!vertices || !baseVertices || !instance || vertexCount <= 0 ||
        !isfinite(targetScale) || !isfinite(blend) || !isfinite(sway) ||
        !isfinite(windAngle)) {
        return false;
    }

    int firstVertex = instance->firstVertex;
    if (firstVertex < 0 || firstVertex >= vertexCount ||
        instance->vertexCount <= 0) {
        return false;
    }
    int count = instance->vertexCount;
    int available = vertexCount - firstVertex;
    if (count > available) count = available;
    int lastVertex = firstVertex + count;

    float baseTopY = -INFINITY;
    float currentTopY = -INFINITY;
    for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
        const float *base = &baseVertices[vertex * 3];
        const float *current = &vertices[vertex * 3];
        if (!isfinite(base[0]) || !isfinite(base[1]) ||
            !isfinite(base[2]) || !isfinite(current[1])) {
            return false;
        }
        baseTopY = fmaxf(baseTopY, base[1]);
        currentTopY = fmaxf(currentTopY, current[1]);
    }

    float localBaseHeight = baseTopY - instance->anchor.y;
    float instanceHeight = instance->height > 0.001f &&
                           isfinite(instance->height) ?
                           instance->height : localBaseHeight;
    if (!(localBaseHeight > 0.001f) || !isfinite(localBaseHeight) ||
        !(instanceHeight > 0.001f) || !isfinite(instanceHeight)) {
        return false;
    }
    float oldScale = (currentTopY - instance->anchor.y) / localBaseHeight;
    if (!(oldScale > 0.01f) || !isfinite(oldScale)) return false;

    float amount = fminf(fmaxf(blend, 0.0f), 1.0f);
    float boundedTargetScale = fmaxf(targetScale, 0.01f);
    float newScale = amount >= 1.0f ? boundedTargetScale :
                     oldScale + (boundedTargetScale - oldScale) * amount;
    float swayX = cosf(windAngle) * sway;
    float swayZ = sinf(windAngle) * sway;
    bool changed = false;
    for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
        float *current = &vertices[vertex * 3];
        const float *base = &baseVertices[vertex * 3];
        float heightFraction = fminf(fmaxf(
            (base[1] - instance->anchor.y) / instanceHeight, 0.0f), 1.0f);
        float targetX = base[0] + swayX * heightFraction;
        float targetY = instance->anchor.y +
                        (base[1] - instance->anchor.y) * newScale;
        float targetZ = base[2] + swayZ * heightFraction;
        if (fabsf(current[0] - targetX) >= 0.0001f ||
            fabsf(current[1] - targetY) >= 0.0001f ||
            fabsf(current[2] - targetZ) >= 0.0001f) {
            changed = true;
        }
        current[0] = targetX;
        current[1] = targetY;
        current[2] = targetZ;
    }
    *outScale = newScale;
    *outChanged = changed;
    return true;
}

static void UpdateChunkSectionFloraScale(ChunkSection *section,
                                  float elapsed,
                                  float daylight, bool refreshTargets)
{
    if (!section || !section->hasFloraModel ||
        section->floraModel.meshCount <= 0) return;

    Mesh *mesh = &section->floraModel.meshes[0];
    if (!mesh->vertices || mesh->vertexCount <= 0 ||
        !section->floraTargetScales || !section->floraTargetWind ||
        !section->floraTargetWindAngle || !section->floraTargetPresence ||
        !section->floraBaseVertices ||
        !section->floraBaseColors || !section->floraVisualInstances ||
        !mesh->colors ||
        section->floraTargetScaleCount <= 0) return;

    float blend = fminf(elapsed * 1.8f, 1.0f);
    float colorBlend = fminf(elapsed * 2.2f, 1.0f);
    float scaleSum = 0.0f;
    int scaleCount = 0;
    bool changed = false;
    for (int group = 0; group < section->floraTargetScaleCount; group++) {
        const FloraVisualInstance *instance =
            &section->floraVisualInstances[group];
        if (!isfinite(instance->anchor.x) ||
            !isfinite(instance->anchor.z)) continue;
        int cellX = (int)floorf(instance->anchor.x);
        int cellZ = (int)floorf(instance->anchor.z);

        if (refreshTargets && PlanetWorldIsActive()) {
            PlanetLocalEcology local = PlanetEcologyLocalAt(cellX, cellZ, daylight);
            PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
                local.suitability.floraActivity,
                local.suitability.floraCapacity);
            section->floraTargetScales[group] = runtime.growthScale;
            section->floraTargetPresence[group] = runtime.visualPresence;
            section->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
            section->floraTargetWindAngle[group] = WeatherWindAngleAtWorld(
                cellX, cellZ);
        } else if (refreshTargets) {
            section->floraTargetScales[group] = 1.0f;
            section->floraTargetPresence[group] = 1.0f;
            section->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
            section->floraTargetWindAngle[group] = WeatherWindAngleAtWorld(
                cellX, cellZ);
        }

        float phase = (float)(Hash3D(cellX, 0, cellZ) & 4095u) * 0.0015339808f;
        float sway = sinf((float)SpacePeriodicSimulationTime(
                              SpaceElapsedSimulationTime()) * 1.7f + phase) *
                         fmaxf(section->floraTargetWind[group], 0.0f) * 0.07f *
                         fmaxf(instance->windResponse, 0.0f);
        float newScale = 1.0f;
        bool instanceChanged = false;
        if (!DeformFloraMeshInstance(
                mesh->vertices, section->floraBaseVertices, mesh->vertexCount,
                instance, section->floraTargetScales[group], blend, sway,
                section->floraTargetWindAngle[group], &newScale,
                &instanceChanged)) {
            continue;
        }
        scaleSum += newScale;
        scaleCount++;
        if (instanceChanged) changed = true;
    }
    if (changed) {
        UpdateMeshBuffer(*mesh, 0, mesh->vertices,
                         mesh->vertexCount * 3 * (int)sizeof(float), 0);
    }
    if (ApplyFloraMeshInstancePresenceColors(
            mesh->colors, section->floraBaseColors, mesh->vertexCount,
            section->floraTargetPresence, section->floraVisualInstances,
            section->floraTargetScaleCount, colorBlend)) {
        UpdateMeshBuffer(*mesh, 3, mesh->colors,
                         mesh->vertexCount * 4 * (int)sizeof(unsigned char), 0);
    }
    if (scaleCount > 0) section->floraVisualScale = scaleSum / (float)scaleCount;
}

static bool ApplyFloraMeshColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int targetCount, float blend)
{
    static const float dormantFactors[3] = { 0.55f, 0.42f, 0.32f };
    if (!colors || !baseColors || !targetPresence || vertexCount <= 0 ||
        targetCount <= 0) {
        return false;
    }

    float amount = fminf(fmaxf(blend, 0.0f), 1.0f);
    bool changed = false;
    for (int group = 0; group < targetCount; group++) {
        int firstVertex = instances ? instances[group].firstVertex : group * 12;
        int count = instances ? instances[group].vertexCount : 12;
        if (firstVertex < 0 || firstVertex >= vertexCount || count <= 0) {
            continue;
        }
        int lastVertex = firstVertex + count;
        if (lastVertex > vertexCount) lastVertex = vertexCount;
        float presence = fminf(fmaxf(targetPresence[group], 0.0f), 1.0f);
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            int colorIndex = vertex * 4;
            for (int channel = 0; channel < 3; channel++) {
                float base = (float)baseColors[colorIndex + channel];
                float dormant = base * dormantFactors[channel];
                float target = dormant + (base - dormant) * presence;
                float current = (float)colors[colorIndex + channel];
                unsigned char next = (unsigned char)lroundf(
                    current + (target - current) * amount);
                if (next != colors[colorIndex + channel]) {
                    colors[colorIndex + channel] = next;
                    changed = true;
                }
            }
            if (colors[colorIndex + 3] != baseColors[colorIndex + 3]) {
                colors[colorIndex + 3] = baseColors[colorIndex + 3];
                changed = true;
            }
        }
    }
    return changed;
}

bool ApplyFloraMeshPresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, int targetCount, float blend)
{
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, NULL, targetCount, blend);
}

bool ApplyFloraMeshInstancePresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int instanceCount, float blend)
{
    if (!instances) return false;
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, instances, instanceCount,
                                blend);
}

void ChunksUpdateEcologyVisuals(float dt, float daylight)
{
    bool planetWorld = PlanetWorldIsActive();
    float elapsed = fmaxf(dt, 0.0f);
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;

        chunk->floraSampleTimer -= elapsed;
        bool refreshTargets = chunk->floraSampleTimer <= 0.0f;
        if (refreshTargets) {
            int centerX = chunk->cx * CHUNK_SIZE + CHUNK_SIZE / 2;
            int centerZ = chunk->cz * CHUNK_SIZE + CHUNK_SIZE / 2;
            chunk->floraWindAngle = WeatherWindAngleAtWorld(centerX, centerZ);
            if (planetWorld) {
                PlanetLocalEcology local = PlanetEcologyLocalAt(
                    centerX, centerZ, daylight);
                chunk->floraActivity = local.suitability.floraActivity;
                chunk->floraCapacity = local.suitability.floraCapacity;
            } else {
                chunk->floraActivity = 1.0f;
                chunk->floraCapacity = 1.0f;
            }

            unsigned int stagger = Hash3D(chunk->cx, 0, chunk->cz) & 255u;
            chunk->floraSampleTimer = 0.75f + (float)stagger / 510.0f;
        }

        for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
            UpdateChunkSectionFloraScale(chunk->sections[sy], elapsed,
                                         daylight, refreshTargets);
        }
    }
}

BlockType GetBlock(int x, int y, int z)
{
    if (!InHeight(y)) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return ChunkGetLocalBlock(chunk, lx, y, lz);
}

bool FaceIsVisible(int x, int y, int z, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (!InHeight(neighborY)) return true;
    BlockType neighbor = GetBlock(x + nx, neighborY, z + nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED;
}

static BlockType ChunkFaceNeighbor(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (neighborY < 0 || neighborY >= height) {
        int worldY = layerY + neighborY;
        if (!InHeight(worldY)) return BLOCK_AIR;
        return GetBlockAt(chunkX * CHUNK_SIZE + lx + nx, worldY,
                          chunkZ * CHUNK_SIZE + lz + nz);
    }

    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        return (BlockType)blocks[neighborLx * height + neighborY][neighborLz];
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    return GetBlockAt(wx, layerY + neighborY, wz);
}

bool ChunkFaceIsVisible(const unsigned short (*blocks)[CHUNK_SIZE],
                        int height, int layerY, int chunkX, int chunkZ,
                        int lx, int y, int lz, int nx, int ny, int nz)
{
    BlockType neighbor = ChunkFaceNeighbor(
        blocks, height, layerY, chunkX, chunkZ, lx, y, lz, nx, ny, nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED ||
           IsTranslucentBlock(neighbor);
}

static bool ChunkTransparentFaceIsVisible(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz, int nx, int ny, int nz,
    BlockType current)
{
    BlockType neighbor = ChunkFaceNeighbor(
        blocks, height, layerY, chunkX, chunkZ, lx, y, lz, nx, ny, nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED ||
           (IsTranslucentBlock(neighbor) && neighbor != current);
}

Color ShadeColor(Color color, float brightness)
{
    return (Color){
        (unsigned char)Clamp((float)color.r * brightness, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.g * brightness, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.b * brightness, 0.0f, 255.0f),
        color.a
    };
}

typedef struct ChunkMeshEmitter {
    Mesh *mesh;
    int vertexIndex;
    int vertexCapacity;
    bool dynamicCapacity;
    bool failed;
} ChunkMeshEmitter;

static bool GrowMeshVertexCapacity(Mesh *mesh, int populatedVertices,
                                   int requiredCapacity)
{
    int oldCapacity = mesh->vertexCount;
    int newCapacity = oldCapacity > 0 ? oldCapacity * 2 : 6;
    if (newCapacity < requiredCapacity) newCapacity = requiredCapacity;

    float *vertices = malloc((size_t)newCapacity * 3 * sizeof(float));
    float *normals = malloc((size_t)newCapacity * 3 * sizeof(float));
    float *texcoords = malloc((size_t)newCapacity * 2 * sizeof(float));
    float *texcoords2 = malloc((size_t)newCapacity * 2 * sizeof(float));
    unsigned char *colors = malloc((size_t)newCapacity * 4 * sizeof(unsigned char));
    if (!vertices || !normals || !texcoords || !texcoords2 || !colors) {
        free(vertices);
        free(normals);
        free(texcoords);
        free(texcoords2);
        free(colors);
        return false;
    }

    memcpy(vertices, mesh->vertices, (size_t)populatedVertices * 3 * sizeof(float));
    memcpy(normals, mesh->normals, (size_t)populatedVertices * 3 * sizeof(float));
    memcpy(texcoords, mesh->texcoords, (size_t)populatedVertices * 2 * sizeof(float));
    if (mesh->texcoords2) {
        memcpy(texcoords2, mesh->texcoords2,
               (size_t)populatedVertices * 2 * sizeof(float));
    } else {
        for (int vertex = 0; vertex < populatedVertices; vertex++) {
            texcoords2[vertex * 2] = 1.0f;
            texcoords2[vertex * 2 + 1] = 0.0f;
        }
    }
    memcpy(colors, mesh->colors, (size_t)populatedVertices * 4 * sizeof(unsigned char));
    free(mesh->vertices);
    free(mesh->normals);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->colors);
    mesh->vertices = vertices;
    mesh->normals = normals;
    mesh->texcoords = texcoords;
    mesh->texcoords2 = texcoords2;
    mesh->colors = colors;
    mesh->vertexCount = newCapacity;
    mesh->triangleCount = newCapacity / 3;
    return true;
}

static void AddMeshFaceLighting(ChunkMeshEmitter *emitter,
                                Vector3 corners[6], Vector3 normal,
                                Vector2 uvs[6], Color color,
                                const float ambientOcclusion[6],
                                float localLight)
{
    if (emitter->failed) return;
    if (emitter->vertexIndex > INT_MAX - 6) {
        emitter->failed = true;
        return;
    }
    int requiredCapacity = emitter->vertexIndex + 6;
    if (!emitter->mesh) {
        emitter->vertexIndex = requiredCapacity;
        return;
    }
    if (requiredCapacity > emitter->vertexCapacity) {
        if (!emitter->dynamicCapacity ||
            !GrowMeshVertexCapacity(emitter->mesh, emitter->vertexIndex,
                                    requiredCapacity)) {
            emitter->failed = true;
            return;
        }
        emitter->vertexCapacity = emitter->mesh->vertexCount;
    }

    for (int i = 0; i < 6; i++) {
        int vertex = emitter->vertexIndex + i;
        emitter->mesh->vertices[vertex * 3 + 0] = corners[i].x;
        emitter->mesh->vertices[vertex * 3 + 1] = corners[i].y;
        emitter->mesh->vertices[vertex * 3 + 2] = corners[i].z;
        emitter->mesh->normals[vertex * 3 + 0] = normal.x;
        emitter->mesh->normals[vertex * 3 + 1] = normal.y;
        emitter->mesh->normals[vertex * 3 + 2] = normal.z;
        emitter->mesh->texcoords[vertex * 2 + 0] = uvs[i].x;
        emitter->mesh->texcoords[vertex * 2 + 1] = uvs[i].y;
        if (emitter->mesh->texcoords2) {
            float ao = ambientOcclusion ? ambientOcclusion[i] : 1.0f;
            emitter->mesh->texcoords2[vertex * 2 + 0] = Clamp(ao, 0.0f, 1.0f);
            emitter->mesh->texcoords2[vertex * 2 + 1] =
                fmaxf(localLight, 0.0f);
        }
        emitter->mesh->colors[vertex * 4 + 0] = color.r;
        emitter->mesh->colors[vertex * 4 + 1] = color.g;
        emitter->mesh->colors[vertex * 4 + 2] = color.b;
        emitter->mesh->colors[vertex * 4 + 3] = color.a;
    }
    emitter->vertexIndex = requiredCapacity;
}

static void AddMeshFace(ChunkMeshEmitter *emitter, Vector3 corners[6],
                        Vector3 normal, Vector2 uvs[6], Color color)
{
    AddMeshFaceLighting(emitter, corners, normal, uvs, color, NULL, 0.0f);
}

static void CountMeshFace(ChunkMeshEmitter *emitter)
{
    emitter->vertexIndex += 6;
}

void UnloadAllChunks(void)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        ChunkClearBlockStorage(&chunks[i]);
        chunks[i].loaded = false;
    }
}

static bool BlockOccludesAmbient(BlockType block)
{
    return block != BLOCK_AIR && block != BLOCK_SPACESHIP_OCCUPIED &&
           !IsTranslucentBlock(block);
}

static BlockType SnapshotBlockAt(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int worldX, int worldY, int worldZ)
{
    int lx = worldX - chunkX * CHUNK_SIZE;
    int lz = worldZ - chunkZ * CHUNK_SIZE;
    int localY = worldY - layerY;
    if (blocks && lx >= 0 && lx < CHUNK_SIZE && lz >= 0 &&
        lz < CHUNK_SIZE && localY >= 0 && localY < height) {
        return (BlockType)blocks[lx * height + localY][lz];
    }
    return GetBlockAt(worldX, worldY, worldZ);
}

static float BlockCornerAmbientOcclusion(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int x, int y, int z, Vector3 normal,
    Vector3 corner)
{
    int nx = (int)normal.x;
    int ny = (int)normal.y;
    int nz = (int)normal.z;
    int t1x = 0, t1y = 0, t1z = 0;
    int t2x = 0, t2y = 0, t2z = 0;
    if (nx != 0) {
        t1y = corner.y > (float)y + 0.5f ? 1 : -1;
        t2z = corner.z > (float)z + 0.5f ? 1 : -1;
    } else if (ny != 0) {
        t1x = corner.x > (float)x + 0.5f ? 1 : -1;
        t2z = corner.z > (float)z + 0.5f ? 1 : -1;
    } else {
        t1x = corner.x > (float)x + 0.5f ? 1 : -1;
        t2y = corner.y > (float)y + 0.5f ? 1 : -1;
    }
    int outsideX = x + nx;
    int outsideY = layerY + y + ny;
    int outsideZ = z + nz;
    bool side1 = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t1x, outsideY + t1y, outsideZ + t1z));
    bool side2 = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t2x, outsideY + t2y, outsideZ + t2z));
    bool diagonal = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t1x + t2x, outsideY + t1y + t2y,
        outsideZ + t1z + t2z));
    int occlusion = side1 && side2 ? 3 :
                    (int)side1 + (int)side2 + (int)diagonal;
    static const float factors[4] = { 1.0f, 0.84f, 0.66f, 0.48f };
    return factors[occlusion];
}

static void AddBlockFaceInternal(
    ChunkMeshEmitter *emitter, int x, int y, int z, int face,
    BlockType type, Color baseColor, float extraLight,
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool realtimeLighting)
{
    if (!emitter->mesh) {
        CountMeshFace(emitter);
        return;
    }
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float x1 = x0 + 1.0f;
    float y1 = y0 + 1.0f;
    float z1 = z0 + 1.0f;
    Vector3 normal = Vector3Zero();
    Vector3 corners[6] = { 0 };
    Vector2 uvs[6] = { 0 };
    float shade = 1.0f;

    switch (face) {
    case 0:
        normal = (Vector3){ 1.0f, 0.0f, 0.0f };
        shade = 0.82f;
        corners[0] = (Vector3){ x1, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = (Vector3){ x1, y0, z1 };
        corners[4] = (Vector3){ x1, y1, z0 };
        corners[5] = (Vector3){ x1, y1, z1 };
        break;
    case 1:
        normal = (Vector3){ -1.0f, 0.0f, 0.0f };
        shade = 0.72f;
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z1 };
        corners[2] = (Vector3){ x0, y1, z1 };
        corners[3] = (Vector3){ x0, y0, z0 };
        corners[4] = (Vector3){ x0, y1, z1 };
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 2:
        normal = (Vector3){ 0.0f, 1.0f, 0.0f };
        shade = 1.08f;
        corners[0] = (Vector3){ x0, y1, z1 };
        corners[1] = (Vector3){ x1, y1, z1 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = (Vector3){ x0, y1, z1 };
        corners[4] = (Vector3){ x1, y1, z0 };
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 3:
        normal = (Vector3){ 0.0f, -1.0f, 0.0f };
        shade = 0.56f;
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y0, z1 };
        corners[3] = (Vector3){ x0, y0, z0 };
        corners[4] = (Vector3){ x1, y0, z1 };
        corners[5] = (Vector3){ x0, y0, z1 };
        break;
    case 4:
        normal = (Vector3){ 0.0f, 0.0f, 1.0f };
        shade = 0.90f;
        corners[0] = (Vector3){ x0, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z1 };
        corners[2] = (Vector3){ x1, y1, z1 };
        corners[3] = (Vector3){ x0, y0, z1 };
        corners[4] = (Vector3){ x1, y1, z1 };
        corners[5] = (Vector3){ x0, y1, z1 };
        break;
    default:
        normal = (Vector3){ 0.0f, 0.0f, -1.0f };
        shade = 0.66f;
        corners[0] = (Vector3){ x1, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z0 };
        corners[2] = (Vector3){ x0, y1, z0 };
        corners[3] = (Vector3){ x1, y0, z0 };
        corners[4] = (Vector3){ x0, y1, z0 };
        corners[5] = (Vector3){ x1, y1, z0 };
        break;
    }

    AtlasUVs(TextureForBlockFace(type, face), uvs);
    if (realtimeLighting) {
        float ambientOcclusion[6];
        for (int i = 0; i < 3; i++) {
            ambientOcclusion[i] = BlockCornerAmbientOcclusion(
                blocks, height, layerY, chunkX, chunkZ,
                x, y, z, normal, corners[i]);
        }
        ambientOcclusion[3] = ambientOcclusion[0];
        ambientOcclusion[4] = ambientOcclusion[2];
        ambientOcclusion[5] = BlockCornerAmbientOcclusion(
            blocks, height, layerY, chunkX, chunkZ,
            x, y, z, normal, corners[5]);
        AddMeshFaceLighting(emitter, corners, normal, uvs, baseColor,
                            ambientOcclusion, extraLight);
    } else {
        float brightness = shade * (1.0f + extraLight);
        if (type == BLOCK_STAR_MATTER) brightness *= 2.1f;
        else if (type == BLOCK_LAVA || type == BLOCK_GLOWSTONE) brightness *= 1.8f;
        AddMeshFace(emitter, corners, normal, uvs,
                    ShadeColor(baseColor, brightness));
    }
}

void AddBlockFace(Mesh *mesh, int *vertexIndex, int x, int y, int z,
                  int face, BlockType type, Color baseColor, float extraLight)
{
    ChunkMeshEmitter emitter = {
        .mesh = mesh,
        .vertexIndex = *vertexIndex,
        .vertexCapacity = mesh->vertexCount,
        .dynamicCapacity = true
    };
    AddBlockFaceInternal(&emitter, x, y, z, face, type, baseColor,
                         extraLight, NULL, 0, 0, 0, 0, false);
    *vertexIndex = emitter.vertexIndex;
    if (emitter.failed) {
        mesh->vertexCount = -1;
        mesh->triangleCount = 0;
    }
}

static void AddTorchMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.62f;
    float w = 0.13f;
    float brightness = 1.0f + extraLight;

    Vector2 stickUvs[6];
    Rectangle stickRect = AtlasSourceRect(TEX_TORCH);
    float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    float u0 = (stickRect.x + 0.25f) / atlasWidth;
    float u1 = (stickRect.x + stickRect.width - 0.25f) / atlasWidth;
    float vTop = (stickRect.y + stickRect.height * 0.375f) / atlasHeight;
    float vBot = (stickRect.y + stickRect.height - 0.25f) / atlasHeight;
    stickUvs[0] = (Vector2){ u0, vBot };
    stickUvs[1] = (Vector2){ u1, vBot };
    stickUvs[2] = (Vector2){ u1, vTop };
    stickUvs[3] = (Vector2){ u0, vBot };
    stickUvs[4] = (Vector2){ u1, vTop };
    stickUvs[5] = (Vector2){ u0, vTop };

    Color stickColor = ShadeColor((Color){ 112, 74, 40, 255 }, brightness);
    Vector3 stickFaces[4][2] = {
        { { cx + w, y0, cz + w }, { cx - w, y0, cz + w } },
        { { cx + w, y0, cz - w }, { cx - w, y0, cz - w } },
        { { cx + w, y0, cz - w }, { cx + w, y0, cz + w } },
        { { cx - w, y0, cz + w }, { cx - w, y0, cz - w } }
    };
    Vector3 stickNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }
    };
    for (int face = 0; face < 4; face++) {
        Vector3 a = stickFaces[face][0];
        Vector3 b = stickFaces[face][1];
        Vector3 corners[6] = {
            a, b, { b.x, y1, b.z },
            a, { b.x, y1, b.z }, { a.x, y1, a.z }
        };
        AddMeshFace(emitter, corners, stickNormals[face], stickUvs, stickColor);
    }

    float fy = (float)y + 0.80f;
    float hs = 0.15f;
    float flameU0 = u0;
    float flameU1 = u1;
    float flameV0 = (stickRect.y + 0.25f) / atlasHeight;
    float flameV1 = (stickRect.y + stickRect.height * 0.4375f) / atlasHeight;
    Vector2 flameUvs[6] = {
        { flameU0, flameV1 }, { flameU1, flameV1 }, { flameU1, flameV0 },
        { flameU0, flameV1 }, { flameU1, flameV0 }, { flameU0, flameV0 }
    };
    Color flameColor = ShadeColor((Color){ 255, 214, 128, 255 }, brightness);

    Vector3 flameCornersA[6] = {
        { cx - hs, fy - 0.12f, cz - hs }, { cx + hs, fy - 0.12f, cz + hs },
        { cx + hs, fy + 0.14f, cz + hs },
        { cx - hs, fy - 0.12f, cz - hs }, { cx + hs, fy + 0.14f, cz + hs },
        { cx - hs, fy + 0.14f, cz - hs }
    };
    Vector3 normalA = Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f });
    AddMeshFace(emitter, flameCornersA, normalA, flameUvs, flameColor);

    Vector3 flameCornersB[6] = {
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy - 0.12f, cz - hs },
        { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy + 0.14f, cz + hs }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(emitter, flameCornersB, normalB, flameUvs, flameColor);
}


static void AddAlbumMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.72f;
    float w = 0.22f;
    float t = 0.06f;
    float brightness = 1.0f + extraLight;

    Vector2 uvs[6];
    AtlasUVs(TEX_ALBUM, uvs);

    Vector3 faces[5][6] = {
        { { cx - w, y0, cz + t }, { cx + w, y0, cz + t }, { cx + w, y1, cz + t },
          { cx - w, y0, cz + t }, { cx + w, y1, cz + t }, { cx - w, y1, cz + t } },
        { { cx + w, y0, cz - t }, { cx - w, y0, cz - t }, { cx - w, y1, cz - t },
          { cx + w, y0, cz - t }, { cx - w, y1, cz - t }, { cx + w, y1, cz - t } },
        { { cx + w, y0, cz + t }, { cx + w, y0, cz - t }, { cx + w, y1, cz - t },
          { cx + w, y0, cz + t }, { cx + w, y1, cz - t }, { cx + w, y1, cz + t } },
        { { cx - w, y0, cz - t }, { cx - w, y0, cz + t }, { cx - w, y1, cz + t },
          { cx - w, y0, cz - t }, { cx - w, y1, cz + t }, { cx - w, y1, cz - t } },
        { { cx - w, y1, cz + t }, { cx + w, y1, cz + t }, { cx + w, y1, cz - t },
          { cx - w, y1, cz + t }, { cx + w, y1, cz - t }, { cx - w, y1, cz - t } }
    };
    Vector3 normals[5] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    float shades[5] = { 1.0f, 0.45f, 0.70f, 0.70f, 0.88f };

    for (int face = 0; face < 5; face++) {
        Color color = ShadeColor(WHITE, shades[face] * brightness);
        AddMeshFace(emitter, faces[face], normals[face], uvs, color);
    }
}

static void AddSpaceshipQuad(ChunkMeshEmitter *emitter,
                             Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                             BlockTexture texture, Color color,
                             float brightness)
{
    if (!emitter->mesh) {
        CountMeshFace(emitter);
        return;
    }
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(c, a)));
    float shade = 0.80f;
    if (normal.y > 0.5f) shade = 1.06f;
    else if (normal.y < -0.5f) shade = 0.58f;
    else if (normal.z > 0.5f) shade = 0.94f;
    else if (normal.z < -0.5f) shade = 0.70f;
    else shade = normal.x > 0.0f ? 0.86f : 0.76f;

    Vector3 corners[6] = { a, b, c, a, c, d };
    Vector2 uvs[6];
    AtlasUVs(texture, uvs);
    AddMeshFace(emitter, corners, normal, uvs,
                ShadeColor(color, shade * brightness));
}

static void AddSpaceshipBox(ChunkMeshEmitter *emitter,
                            Vector3 min, Vector3 max,
                            BlockTexture texture, Color color,
                            float brightness)
{
    AddSpaceshipQuad(
        emitter,
        (Vector3){ max.x, min.y, max.z }, (Vector3){ max.x, min.y, min.z },
        (Vector3){ max.x, max.y, min.z }, (Vector3){ max.x, max.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, min.z }, (Vector3){ min.x, min.y, max.z },
        (Vector3){ min.x, max.y, max.z }, (Vector3){ min.x, max.y, min.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, max.y, max.z }, (Vector3){ max.x, max.y, max.z },
        (Vector3){ max.x, max.y, min.z }, (Vector3){ min.x, max.y, min.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, min.z }, (Vector3){ max.x, min.y, min.z },
        (Vector3){ max.x, min.y, max.z }, (Vector3){ min.x, min.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, max.z }, (Vector3){ max.x, min.y, max.z },
        (Vector3){ max.x, max.y, max.z }, (Vector3){ min.x, max.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ max.x, min.y, min.z }, (Vector3){ min.x, min.y, min.z },
        (Vector3){ min.x, max.y, min.z }, (Vector3){ max.x, max.y, min.z },
        texture, color, brightness);
}

static void AddSpaceshipTaperedSection(
    ChunkMeshEmitter *emitter, float centerX,
    float rearZ, float rearY, float rearHalfWidth, float rearHalfHeight,
    float frontZ, float frontY, float frontHalfWidth, float frontHalfHeight,
    BlockTexture texture, Color color, float brightness)
{
    Vector3 rear[4] = {
        { centerX - rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { centerX + rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { centerX + rearHalfWidth, rearY + rearHalfHeight, rearZ },
        { centerX - rearHalfWidth, rearY + rearHalfHeight, rearZ }
    };
    Vector3 front[4] = {
        { centerX - frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { centerX + frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { centerX + frontHalfWidth, frontY + frontHalfHeight, frontZ },
        { centerX - frontHalfWidth, frontY + frontHalfHeight, frontZ }
    };
    AddSpaceshipQuad(emitter, front[0], front[1], front[2], front[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[1], rear[0], rear[3], rear[2],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, front[1], rear[1], rear[2], front[2],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[0], front[0], front[3], rear[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, front[3], front[2], rear[2], rear[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[0], rear[1], front[1], front[0],
                     texture, color, brightness);
}

// Points wind clockwise from above so the top surface faces upward.
static void AddSpaceshipWing(ChunkMeshEmitter *emitter,
                             const Vector2 points[4], float bottomY, float topY,
                             BlockTexture texture, Color color,
                             float brightness)
{
    Vector3 bottom[4];
    Vector3 top[4];
    for (int point = 0; point < 4; point++) {
        bottom[point] = (Vector3){ points[point].x, bottomY, points[point].y };
        top[point] = (Vector3){ points[point].x, topY, points[point].y };
    }
    AddSpaceshipQuad(emitter, top[0], top[1], top[2], top[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, bottom[3], bottom[2], bottom[1], bottom[0],
                     texture, color, brightness);
    for (int point = 0; point < 4; point++) {
        int next = (point + 1) % 4;
        AddSpaceshipQuad(emitter,
                         bottom[point], bottom[next], top[next], top[point],
                         texture, color, brightness);
    }
}

static void AddSpaceshipMesh(ChunkMeshEmitter *emitter,
                             int x, int y, int z, BlockType type,
                             float extraLight)
{
    int firstVertex = emitter->vertexIndex;
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float cx = x0 + 0.5f;
    float brightness = 1.0f + extraLight;
    const Color hull = { 226, 232, 238, 255 };
    const Color hullDark = { 132, 145, 158, 255 };
    const Color canopy = { 132, 194, 232, 255 };
    const Vector2 leftWing[4] = {
        { x0 + 0.41f, z0 + 0.22f }, { x0 + 0.08f, z0 + 0.29f },
        { x0 + 0.04f, z0 + 0.43f }, { x0 + 0.41f, z0 + 0.60f }
    };
    const Vector2 rightWing[4] = {
        { x0 + 0.59f, z0 + 0.60f }, { x0 + 0.96f, z0 + 0.43f },
        { x0 + 0.92f, z0 + 0.29f }, { x0 + 0.59f, z0 + 0.22f }
    };

    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.18f, y0 + 0.36f, 0.15f, 0.16f,
        z0 + 0.68f, y0 + 0.37f, 0.12f, 0.14f,
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.68f, y0 + 0.37f, 0.12f, 0.14f,
        z0 + 0.93f, y0 + 0.34f, 0.02f, 0.03f,
        TEX_WHITE, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.42f, y0 + 0.15f, z0 + 0.22f },
        (Vector3){ x0 + 0.58f, y0 + 0.27f, z0 + 0.72f },
        TEX_GRAY, hullDark, brightness);
    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.43f, y0 + 0.62f, 0.09f, 0.10f,
        z0 + 0.70f, y0 + 0.57f, 0.04f, 0.04f,
        TEX_GLASS, canopy, brightness);
    AddSpaceshipWing(emitter, leftWing, y0 + 0.32f, y0 + 0.39f,
                     TEX_SPACESHIP, hull, brightness);
    AddSpaceshipWing(emitter, rightWing, y0 + 0.32f, y0 + 0.39f,
                     TEX_SPACESHIP, hull, brightness);

    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.29f, y0 + 0.25f, z0 + 0.10f },
        (Vector3){ x0 + 0.39f, y0 + 0.49f, z0 + 0.32f },
        TEX_BLACK, (Color){ 155, 166, 178, 255 }, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.61f, y0 + 0.25f, z0 + 0.10f },
        (Vector3){ x0 + 0.71f, y0 + 0.49f, z0 + 0.32f },
        TEX_BLACK, (Color){ 155, 166, 178, 255 }, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.30f, y0 + 0.27f, z0 + 0.07f },
        (Vector3){ x0 + 0.38f, y0 + 0.46f, z0 + 0.11f },
        TEX_GLOWSTONE, (Color){ 255, 198, 112, 255 }, brightness * 1.55f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.27f, z0 + 0.07f },
        (Vector3){ x0 + 0.70f, y0 + 0.46f, z0 + 0.11f },
        TEX_GLOWSTONE, (Color){ 255, 198, 112, 255 }, brightness * 1.55f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.33f, y0 + 0.43f, z0 + 0.13f },
        (Vector3){ x0 + 0.38f, y0 + 0.78f, z0 + 0.25f },
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.43f, z0 + 0.13f },
        (Vector3){ x0 + 0.67f, y0 + 0.78f, z0 + 0.25f },
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.04f, y0 + 0.38f, z0 + 0.34f },
        (Vector3){ x0 + 0.09f, y0 + 0.47f, z0 + 0.40f },
        TEX_RED, WHITE, brightness * 1.25f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.91f, y0 + 0.38f, z0 + 0.34f },
        (Vector3){ x0 + 0.96f, y0 + 0.47f, z0 + 0.40f },
        TEX_GREEN, WHITE, brightness * 1.25f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.36f, y0 + 0.37f, z0 + 0.38f },
        (Vector3){ x0 + 0.38f, y0 + 0.45f, z0 + 0.66f },
        TEX_ORANGE, WHITE, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.37f, z0 + 0.38f },
        (Vector3){ x0 + 0.64f, y0 + 0.45f, z0 + 0.66f },
        TEX_ORANGE, WHITE, brightness);

    if (!emitter->mesh || type == BLOCK_SPACESHIP) return;

    int direction = (int)type - (int)BLOCK_SPACESHIP_CORE_NORTH;
    if (direction < 0 || direction > 3) direction = 0;
    float angle = (float)direction * PI * 0.5f;
    float sine = sinf(angle);
    float cosine = cosf(angle);
    float sourceCenterX = x0 + 0.5f;
    float sourceCenterZ = z0 + 0.5f;
    const float scaleX = 3.78f / 0.92f;
    const float scaleY = 1.10f / 0.63f;
    const float scaleZ = 3.90f / 0.86f;
    float targetCenterX = x0 + 1.0f;
    float targetCenterZ = z0 + 1.0f;
    for (int vertex = firstVertex; vertex < emitter->vertexIndex; vertex++) {
        Mesh *mesh = emitter->mesh;
        float localX = (mesh->vertices[vertex * 3] - sourceCenterX) * scaleX;
        float localZ = (mesh->vertices[vertex * 3 + 2] - sourceCenterZ) * scaleZ;
        mesh->vertices[vertex * 3] = targetCenterX + localX * cosine + localZ * sine;
        mesh->vertices[vertex * 3 + 1] =
            y0 + (mesh->vertices[vertex * 3 + 1] - (y0 + 0.15f)) * scaleY;
        mesh->vertices[vertex * 3 + 2] = targetCenterZ - localX * sine + localZ * cosine;

        Vector3 normal = {
            mesh->normals[vertex * 3] / scaleX,
            mesh->normals[vertex * 3 + 1] / scaleY,
            mesh->normals[vertex * 3 + 2] / scaleZ
        };
        normal = Vector3Normalize(normal);
        mesh->normals[vertex * 3] = normal.x * cosine + normal.z * sine;
        mesh->normals[vertex * 3 + 1] = normal.y;
        mesh->normals[vertex * 3 + 2] = -normal.x * sine + normal.z * cosine;
    }
}

static void AddSlabMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz,
                 const int faces[6][3], float extraLight)
{
    static const float shades[6] = { 0.82f, 0.72f, 1.08f, 0.56f, 0.90f, 0.66f };
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_STONE, uvs);

    for (int face = 0; face < 6; face++) {
        if (!ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) continue;

        int x = chunkX * CHUNK_SIZE + lx;
        int z = chunkZ * CHUNK_SIZE + lz;
        float x0 = (float)x;
        float y0 = (float)y;
        float z0 = (float)z;
        float x1 = x0 + 1.0f;
        float y1 = y0 + 0.5f;
        float z1 = z0 + 1.0f;
        Vector3 normal = Vector3Zero();
        Vector3 corners[6] = { 0 };
        Vector2 faceUvs[6] = { uvs[0], uvs[1], uvs[2], uvs[3], uvs[4], uvs[5] };

        switch (face) {
        case 0:
            normal = (Vector3){ 1.0f, 0.0f, 0.0f };
            corners[0] = (Vector3){ x1, y0, z1 }; corners[1] = (Vector3){ x1, y0, z0 };
            corners[2] = (Vector3){ x1, y1, z0 }; corners[3] = (Vector3){ x1, y0, z1 };
            corners[4] = (Vector3){ x1, y1, z0 }; corners[5] = (Vector3){ x1, y1, z1 };
            break;
        case 1:
            normal = (Vector3){ -1.0f, 0.0f, 0.0f };
            corners[0] = (Vector3){ x0, y0, z0 }; corners[1] = (Vector3){ x0, y0, z1 };
            corners[2] = (Vector3){ x0, y1, z1 }; corners[3] = (Vector3){ x0, y0, z0 };
            corners[4] = (Vector3){ x0, y1, z1 }; corners[5] = (Vector3){ x0, y1, z0 };
            break;
        case 2:
            normal = (Vector3){ 0.0f, 1.0f, 0.0f };
            corners[0] = (Vector3){ x0, y1, z1 }; corners[1] = (Vector3){ x1, y1, z1 };
            corners[2] = (Vector3){ x1, y1, z0 }; corners[3] = (Vector3){ x0, y1, z1 };
            corners[4] = (Vector3){ x1, y1, z0 }; corners[5] = (Vector3){ x0, y1, z0 };
            break;
        case 3:
            normal = (Vector3){ 0.0f, -1.0f, 0.0f };
            corners[0] = (Vector3){ x0, y0, z0 }; corners[1] = (Vector3){ x1, y0, z0 };
            corners[2] = (Vector3){ x1, y0, z1 }; corners[3] = (Vector3){ x0, y0, z0 };
            corners[4] = (Vector3){ x1, y0, z1 }; corners[5] = (Vector3){ x0, y0, z1 };
            break;
        case 4:
            normal = (Vector3){ 0.0f, 0.0f, 1.0f };
            corners[0] = (Vector3){ x0, y0, z1 }; corners[1] = (Vector3){ x1, y0, z1 };
            corners[2] = (Vector3){ x1, y1, z1 }; corners[3] = (Vector3){ x0, y0, z1 };
            corners[4] = (Vector3){ x1, y1, z1 }; corners[5] = (Vector3){ x0, y1, z1 };
            break;
        default:
            normal = (Vector3){ 0.0f, 0.0f, -1.0f };
            corners[0] = (Vector3){ x1, y0, z0 }; corners[1] = (Vector3){ x0, y0, z0 };
            corners[2] = (Vector3){ x0, y1, z0 }; corners[3] = (Vector3){ x1, y0, z0 };
            corners[4] = (Vector3){ x0, y1, z0 }; corners[5] = (Vector3){ x1, y1, z0 };
            break;
        }

        AddMeshFace(emitter, corners, normal, faceUvs,
                    ShadeColor(WHITE, shades[face] * brightness));
    }
}

static void AddDoorMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz,
                 const int faces[6][3], BlockType type, float extraLight)
{
    bool open = type == BLOCK_DOOR_OPEN;
    float brightness = 1.0f + extraLight;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 1.0f;
    float w = 0.44f;
    float t = 0.06f;

    Vector2 uvs[6];
    AtlasUVs(TEX_DOOR, uvs);

    static const int faceOrder[5] = { 4, 5, 0, 1, 2 };
    Vector3 normals[5] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    float shades[5] = { 1.0f, 0.55f, 0.75f, 0.75f, 0.90f };

    Vector3 faceCorners[5][6];
    if (!open) {
        faceCorners[0][0] = (Vector3){ cx - w, y0, cz + t }; faceCorners[0][1] = (Vector3){ cx + w, y0, cz + t };
        faceCorners[0][2] = (Vector3){ cx + w, y1, cz + t }; faceCorners[0][3] = (Vector3){ cx - w, y0, cz + t };
        faceCorners[0][4] = (Vector3){ cx + w, y1, cz + t }; faceCorners[0][5] = (Vector3){ cx - w, y1, cz + t };
        faceCorners[1][0] = (Vector3){ cx + w, y0, cz - t }; faceCorners[1][1] = (Vector3){ cx - w, y0, cz - t };
        faceCorners[1][2] = (Vector3){ cx - w, y1, cz - t }; faceCorners[1][3] = (Vector3){ cx + w, y0, cz - t };
        faceCorners[1][4] = (Vector3){ cx - w, y1, cz - t }; faceCorners[1][5] = (Vector3){ cx + w, y1, cz - t };
        faceCorners[2][0] = (Vector3){ cx + w, y0, cz + t }; faceCorners[2][1] = (Vector3){ cx + w, y0, cz - t };
        faceCorners[2][2] = (Vector3){ cx + w, y1, cz - t }; faceCorners[2][3] = (Vector3){ cx + w, y0, cz + t };
        faceCorners[2][4] = (Vector3){ cx + w, y1, cz - t }; faceCorners[2][5] = (Vector3){ cx + w, y1, cz + t };
        faceCorners[3][0] = (Vector3){ cx - w, y0, cz - t }; faceCorners[3][1] = (Vector3){ cx - w, y0, cz + t };
        faceCorners[3][2] = (Vector3){ cx - w, y1, cz + t }; faceCorners[3][3] = (Vector3){ cx - w, y0, cz - t };
        faceCorners[3][4] = (Vector3){ cx - w, y1, cz + t }; faceCorners[3][5] = (Vector3){ cx - w, y1, cz - t };
    } else {
        faceCorners[0][0] = (Vector3){ cx - t, y0, cz + w }; faceCorners[0][1] = (Vector3){ cx + t, y0, cz + w };
        faceCorners[0][2] = (Vector3){ cx + t, y1, cz + w }; faceCorners[0][3] = (Vector3){ cx - t, y0, cz + w };
        faceCorners[0][4] = (Vector3){ cx + t, y1, cz + w }; faceCorners[0][5] = (Vector3){ cx - t, y1, cz + w };
        faceCorners[1][0] = (Vector3){ cx + t, y0, cz - w }; faceCorners[1][1] = (Vector3){ cx - t, y0, cz - w };
        faceCorners[1][2] = (Vector3){ cx - t, y1, cz - w }; faceCorners[1][3] = (Vector3){ cx + t, y0, cz - w };
        faceCorners[1][4] = (Vector3){ cx - t, y1, cz - w }; faceCorners[1][5] = (Vector3){ cx + t, y1, cz - w };
        faceCorners[2][0] = (Vector3){ cx + t, y0, cz + w }; faceCorners[2][1] = (Vector3){ cx + t, y0, cz - w };
        faceCorners[2][2] = (Vector3){ cx + t, y1, cz - w }; faceCorners[2][3] = (Vector3){ cx + t, y0, cz + w };
        faceCorners[2][4] = (Vector3){ cx + t, y1, cz - w }; faceCorners[2][5] = (Vector3){ cx + t, y1, cz + w };
        faceCorners[3][0] = (Vector3){ cx - t, y0, cz - w }; faceCorners[3][1] = (Vector3){ cx - t, y0, cz + w };
        faceCorners[3][2] = (Vector3){ cx - t, y1, cz + w }; faceCorners[3][3] = (Vector3){ cx - t, y0, cz - w };
        faceCorners[3][4] = (Vector3){ cx - t, y1, cz + w }; faceCorners[3][5] = (Vector3){ cx - t, y1, cz - w };
    }
    faceCorners[4][0] = (Vector3){ cx - w, y1, cz + t }; faceCorners[4][1] = (Vector3){ cx + w, y1, cz + t };
    faceCorners[4][2] = (Vector3){ cx + w, y1, cz - t }; faceCorners[4][3] = (Vector3){ cx - w, y1, cz + t };
    faceCorners[4][4] = (Vector3){ cx + w, y1, cz - t }; faceCorners[4][5] = (Vector3){ cx - w, y1, cz - t };

    for (int f = 0; f < 5; f++) {
        int face = faceOrder[f];
        if (!ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) continue;
        Color color = ShadeColor(WHITE, shades[f] * brightness);
        AddMeshFace(emitter, faceCorners[f], normals[f], uvs, color);
    }
}

static void AddStairsMesh(ChunkMeshEmitter *emitter,
                   const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   int lx, int y, int lz, BlockType type, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    float x0 = (float)(chunkX * CHUNK_SIZE + lx);
    float z0 = (float)(chunkZ * CHUNK_SIZE + lz);
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs((type == BLOCK_WOOD_STAIRS) ? TEX_PLANK : TEX_STONE, uvs);

    for (int step = 0; step < 3; step++) {
        float zLow = z0 + (float)step / 3.0f;
        float zHigh = z0 + (float)(step + 1) / 3.0f;
        float yHigh = y0 + (float)(step + 1) / 3.0f;

        Vector3 top[6] = {
            { x0, yHigh, zHigh }, { x0 + 1.0f, yHigh, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0, yHigh, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0, yHigh, zLow }
        };
        AddMeshFace(emitter, top, (Vector3){ 0.0f, 1.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 1.08f * brightness));

        Vector3 front[6] = {
            { x0, y0, zHigh }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, yHigh, zHigh }
        };
        AddMeshFace(emitter, front, (Vector3){ 0.0f, 0.0f, 1.0f }, uvs,
                    ShadeColor(WHITE, 0.90f * brightness));

        Vector3 sideA[6] = {
            { x0, y0, zLow }, { x0, y0, zHigh },
            { x0, yHigh, zHigh }, { x0, y0, zLow },
            { x0, yHigh, zHigh }, { x0, yHigh, zLow }
        };
        AddMeshFace(emitter, sideA, (Vector3){ -1.0f, 0.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 0.72f * brightness));

        Vector3 sideB[6] = {
            { x0 + 1.0f, y0, zHigh }, { x0 + 1.0f, y0, zLow },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, yHigh, zHigh }
        };
        AddMeshFace(emitter, sideB, (Vector3){ 1.0f, 0.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 0.82f * brightness));
    }
}

static BlockType FenceNeighborBlock(const unsigned short (*blocks)[CHUNK_SIZE],
                                    int height, int layerY, int chunkX, int chunkZ,
                                    int lx, int y, int lz, int nx, int nz)
{
    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE && y >= 0 && y < height) {
        return (BlockType)blocks[neighborLx * height + y][neighborLz];
    }
    return GetBlockAt(chunkX * CHUNK_SIZE + lx + nx, layerY + y, chunkZ * CHUNK_SIZE + lz + nz);
}

static bool FenceShouldConnect(BlockType type)
{
    return type == BLOCK_FENCE || type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN;
}

static void AddFenceMesh(ChunkMeshEmitter *emitter,
                  const unsigned short (*blocks)[CHUNK_SIZE],
                  int height, int layerY, int chunkX, int chunkZ,
                  int lx, int y, int lz, float extraLight)
{
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_FENCE, uvs);

    static const int dirs[4][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int d = 0; d < 4; d++) {
        BlockType neighbor = FenceNeighborBlock(blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                                                dirs[d][0], dirs[d][2]);
        if (neighbor != BLOCK_AIR && !FenceShouldConnect(neighbor)) continue;

        float px = cx + (float)dirs[d][0] * 0.5f;
        float pz = cz + (float)dirs[d][2] * 0.5f;
        Vector3 corners[6] = {
            { px - 0.06f, y0 + 0.55f, pz - 0.06f }, { px + 0.06f, y0 + 0.55f, pz + 0.06f },
            { px + 0.06f, y0 + 0.95f, pz + 0.06f }, { px - 0.06f, y0 + 0.55f, pz - 0.06f },
            { px + 0.06f, y0 + 0.95f, pz + 0.06f }, { px - 0.06f, y0 + 0.95f, pz - 0.06f }
        };
        if (dirs[d][0] != 0) {
            for (int i = 0; i < 6; i++) {
                float tmp = corners[i].x;
                corners[i].x = corners[i].z;
                corners[i].z = tmp;
            }
        }
        Vector3 normal = { (float)dirs[d][0], 0.0f, (float)dirs[d][2] };
        AddMeshFace(emitter, corners, normal, uvs,
                    ShadeColor(WHITE, 0.85f * brightness));
    }

    Vector3 postCorners[6] = {
        { cx - 0.06f, y0, cz - 0.06f }, { cx + 0.06f, y0, cz + 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0, cz - 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0 + 1.0f, cz - 0.06f }
    };
    AddMeshFace(emitter, postCorners, (Vector3){ 0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 1.0f * brightness));
    AddMeshFace(emitter, postCorners, (Vector3){ -0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 0.85f * brightness));
}

static void AddGateMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz, bool open, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    (void)chunkZ;
    (void)lz;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.9f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_FENCE, uvs);

    float w = 0.36f;
    float t = 0.05f;
    Vector3 faces[3][6];
    if (!open) {
        faces[0][0] = (Vector3){ cx - w, y0, cz + t }; faces[0][1] = (Vector3){ cx + w, y0, cz + t };
        faces[0][2] = (Vector3){ cx + w, y1, cz + t }; faces[0][3] = (Vector3){ cx - w, y0, cz + t };
        faces[0][4] = (Vector3){ cx + w, y1, cz + t }; faces[0][5] = (Vector3){ cx - w, y1, cz + t };
        faces[1][0] = (Vector3){ cx + w, y0, cz - t }; faces[1][1] = (Vector3){ cx - w, y0, cz - t };
        faces[1][2] = (Vector3){ cx - w, y1, cz - t }; faces[1][3] = (Vector3){ cx + w, y0, cz - t };
        faces[1][4] = (Vector3){ cx - w, y1, cz - t }; faces[1][5] = (Vector3){ cx + w, y1, cz - t };
        faces[2][0] = (Vector3){ cx - w, y1, cz + t }; faces[2][1] = (Vector3){ cx + w, y1, cz + t };
        faces[2][2] = (Vector3){ cx + w, y1, cz - t }; faces[2][3] = (Vector3){ cx - w, y1, cz + t };
        faces[2][4] = (Vector3){ cx + w, y1, cz - t }; faces[2][5] = (Vector3){ cx - w, y1, cz - t };
    } else {
        faces[0][0] = (Vector3){ cx + t, y0, cz - w }; faces[0][1] = (Vector3){ cx + t, y0, cz + w };
        faces[0][2] = (Vector3){ cx + t, y1, cz + w }; faces[0][3] = (Vector3){ cx + t, y0, cz - w };
        faces[0][4] = (Vector3){ cx + t, y1, cz + w }; faces[0][5] = (Vector3){ cx + t, y1, cz - w };
        faces[1][0] = (Vector3){ cx - t, y0, cz + w }; faces[1][1] = (Vector3){ cx - t, y0, cz - w };
        faces[1][2] = (Vector3){ cx - t, y1, cz - w }; faces[1][3] = (Vector3){ cx - t, y0, cz + w };
        faces[1][4] = (Vector3){ cx - t, y1, cz - w }; faces[1][5] = (Vector3){ cx - t, y1, cz + w };
        faces[2][0] = (Vector3){ cx - t, y1, cz - w }; faces[2][1] = (Vector3){ cx + t, y1, cz - w };
        faces[2][2] = (Vector3){ cx + t, y1, cz + w }; faces[2][3] = (Vector3){ cx - t, y1, cz - w };
        faces[2][4] = (Vector3){ cx + t, y1, cz + w }; faces[2][5] = (Vector3){ cx - t, y1, cz + w };
    }
    Vector3 normals[3] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }
    };
    float shades[3] = { 1.0f, 0.55f, 0.90f };
    for (int f = 0; f < 3; f++) {
        AddMeshFace(emitter, faces[f], normals[f], uvs,
                    ShadeColor(WHITE, shades[f] * brightness));
    }
}

static void AddPaneMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 1.0f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_GLASS, uvs);

    float zNear = cz - 0.45f;
    float zFar = cz + 0.45f;
    Vector3 faces[5][6];
    faces[0][0] = (Vector3){ cx + 0.07f, y0, zFar }; faces[0][1] = (Vector3){ cx + 0.07f, y0, zNear };
    faces[0][2] = (Vector3){ cx + 0.07f, y1, zNear }; faces[0][3] = (Vector3){ cx + 0.07f, y0, zFar };
    faces[0][4] = (Vector3){ cx + 0.07f, y1, zNear }; faces[0][5] = (Vector3){ cx + 0.07f, y1, zFar };
    faces[1][0] = (Vector3){ cx - 0.07f, y0, zNear }; faces[1][1] = (Vector3){ cx - 0.07f, y0, zFar };
    faces[1][2] = (Vector3){ cx - 0.07f, y1, zFar }; faces[1][3] = (Vector3){ cx - 0.07f, y0, zNear };
    faces[1][4] = (Vector3){ cx - 0.07f, y1, zFar }; faces[1][5] = (Vector3){ cx - 0.07f, y1, zNear };
    faces[2][0] = (Vector3){ cx + 0.07f, y1, zFar }; faces[2][1] = (Vector3){ cx - 0.07f, y1, zFar };
    faces[2][2] = (Vector3){ cx - 0.07f, y1, zNear }; faces[2][3] = (Vector3){ cx + 0.07f, y1, zFar };
    faces[2][4] = (Vector3){ cx - 0.07f, y1, zNear }; faces[2][5] = (Vector3){ cx + 0.07f, y1, zNear };
    faces[3][0] = (Vector3){ cx + 0.07f, y0, zNear }; faces[3][1] = (Vector3){ cx - 0.07f, y0, zNear };
    faces[3][2] = (Vector3){ cx - 0.07f, y1, zNear }; faces[3][3] = (Vector3){ cx + 0.07f, y0, zNear };
    faces[3][4] = (Vector3){ cx - 0.07f, y1, zNear }; faces[3][5] = (Vector3){ cx + 0.07f, y1, zNear };
    faces[4][0] = (Vector3){ cx - 0.07f, y0, zFar }; faces[4][1] = (Vector3){ cx + 0.07f, y0, zFar };
    faces[4][2] = (Vector3){ cx + 0.07f, y1, zFar }; faces[4][3] = (Vector3){ cx - 0.07f, y0, zFar };
    faces[4][4] = (Vector3){ cx + 0.07f, y1, zFar }; faces[4][5] = (Vector3){ cx - 0.07f, y1, zFar };

    Vector3 normals[5] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }
    };
    float shades[5] = { 0.85f, 0.75f, 1.0f, 0.80f, 0.85f };
    for (int f = 0; f < 5; f++) {
        AddMeshFace(emitter, faces[f], normals[f], uvs,
                    ShadeColor(WHITE, shades[f] * brightness));
    }
}

static void AddPlantMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         BlockType type, float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.4f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs((type == BLOCK_FLOWER) ? TEX_FLOWER : TEX_MUSHROOM, uvs);

    Vector3 quadA[6] = {
        { cx - 0.16f, y0, cz - 0.16f }, { cx + 0.16f, y0, cz + 0.16f },
        { cx + 0.16f, y1, cz + 0.16f }, { cx - 0.16f, y0, cz - 0.16f },
        { cx + 0.16f, y1, cz + 0.16f }, { cx - 0.16f, y1, cz - 0.16f }
    };
    Vector3 normalA = Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f });
    AddMeshFace(emitter, quadA, normalA, uvs,
                ShadeColor(WHITE, 0.95f * brightness));

    Vector3 quadB[6] = {
        { cx - 0.16f, y0, cz + 0.16f }, { cx + 0.16f, y0, cz - 0.16f },
        { cx + 0.16f, y1, cz - 0.16f }, { cx - 0.16f, y0, cz + 0.16f },
        { cx + 0.16f, y1, cz - 0.16f }, { cx - 0.16f, y1, cz + 0.16f }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(emitter, quadB, normalB, uvs,
                ShadeColor(WHITE, 0.85f * brightness));
}
bool ChunkBlockHasTransparentMesh(BlockType type)
{
    return IsTranslucentBlock(type);
}

typedef struct ChunkMeshBuildContext {
    const unsigned short (*blocks)[CHUNK_SIZE];
    int height;
    int layerY;
    int chunkX;
    int chunkZ;
    bool transparent;
    bool includePlants;
    bool plantsOnly;
    const int (*faces)[3];
    const int *nearbyTorchIndices;
    int nearbyTorchCount;
} ChunkMeshBuildContext;

static void EmitChunkBlocksFiltered(const ChunkMeshBuildContext *context,
                                    ChunkMeshEmitter *emitter)
{
    const unsigned short (*blocks)[CHUNK_SIZE] = context->blocks;
    int height = context->height;
    int layerY = context->layerY;
    int chunkX = context->chunkX;
    int chunkZ = context->chunkZ;
    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    const int (*faces)[3] = context->faces;
    bool transparent = context->transparent;
    bool includePlants = context->includePlants;
    bool plantsOnly = context->plantsOnly;
    bool counting = emitter->mesh == NULL;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < height; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                if (emitter->failed) return;
                BlockType type = (BlockType)blocks[lx * height + y][lz];
                bool plant = type == BLOCK_FLOWER || type == BLOCK_MUSHROOM;
                if (plantsOnly) {
                    if (!plant) continue;
                } else {
                    if (type == BLOCK_AIR ||
                        ChunkBlockHasTransparentMesh(type) !=
                            transparent) continue;
                    if (plant && !includePlants) continue;
                }

                int x = lx;
                int z = lz;
                float blockLight = 0.0f;
                if (!counting) {
                    x += startX;
                    z += startZ;
                    blockLight = TorchLightAtBlockNearby(
                        x, layerY + y, z, context->nearbyTorchIndices,
                        context->nearbyTorchCount);
                }
                if (type == BLOCK_TORCH) {
                    AddTorchMesh(emitter, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_ALBUM) {
                    AddAlbumMesh(emitter, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_SPACESHIP ||
                    (type >= BLOCK_SPACESHIP_CORE_NORTH &&
                     type <= BLOCK_SPACESHIP_CORE_WEST)) {
                    AddSpaceshipMesh(emitter, x, y, z, type, blockLight);
                    continue;
                }
                if (type == BLOCK_SPACESHIP_OCCUPIED) continue;
                if (type == BLOCK_SLAB) {
                    AddSlabMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, faces, blockLight);
                    continue;
                }
                if (type == BLOCK_DOOR || type == BLOCK_DOOR_OPEN) {
                    AddDoorMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, faces, type, blockLight);
                    continue;
                }
                if (type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) {
                    AddStairsMesh(emitter, blocks, height, layerY, chunkX,
                                  chunkZ, lx, y, lz, type, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE) {
                    AddFenceMesh(emitter, blocks, height, layerY, chunkX,
                                 chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN) {
                    AddGateMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz,
                                type == BLOCK_FENCE_GATE_OPEN, blockLight);
                    continue;
                }
                if (type == BLOCK_GLASS_PANE) {
                    AddPaneMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (plant) {
                    AddPlantMesh(emitter, x, y, z, type, blockLight);
                    continue;
                }
                for (int face = 0; face < 6; face++) {
                    bool visible = transparent ?
                        ChunkTransparentFaceIsVisible(
                            blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                            faces[face][0], faces[face][1], faces[face][2],
                            type) :
                        ChunkFaceIsVisible(
                            blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                            faces[face][0], faces[face][1], faces[face][2]);
                    if (visible) {
                        if (!counting) {
                            AddBlockFaceInternal(
                                emitter, x, y, z, face, type, WHITE,
                                blockLight, blocks, height, layerY, chunkX,
                                chunkZ, true);
                        } else {
                            CountMeshFace(emitter);
                        }
                    }
                }
            }
        }
    }
}

static bool BuildMeshDataFiltered(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    ChunkMeshBuildContext context = {
        .blocks = blocks,
        .height = height,
        .layerY = layerY,
        .chunkX = chunkX,
        .chunkZ = chunkZ,
        .transparent = transparent,
        .includePlants = includePlants,
        .plantsOnly = plantsOnly,
        .faces = faces,
        .nearbyTorchIndices = nearbyTorchIndices,
        .nearbyTorchCount = nearbyTorchCount
    };
    ChunkMeshEmitter counter = { 0 };
    EmitChunkBlocksFiltered(&context, &counter);
    if (counter.failed || counter.vertexIndex == 0) return false;

    Mesh mesh = { 0 };
    mesh.vertexCount = counter.vertexIndex;
    mesh.triangleCount = counter.vertexIndex / 3;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.texcoords2 = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));

    if (!mesh.vertices || !mesh.texcoords || !mesh.texcoords2 ||
        !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.texcoords2);
        free(mesh.normals);
        free(mesh.colors);
        return false;
    }

    ChunkMeshEmitter writer = {
        .mesh = &mesh,
        .vertexCapacity = mesh.vertexCount
    };
    EmitChunkBlocksFiltered(&context, &writer);
    if (writer.failed || writer.vertexIndex != counter.vertexIndex) {
        FreeMeshData(&mesh);
        return false;
    }
    *outMesh = mesh;
    return true;
}

bool BuildMeshData(const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   bool transparent, const int faces[6][3],
                   const int *nearbyTorchIndices, int nearbyTorchCount,
                   Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, transparent, true, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildSurfaceSolidMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildSurfaceWaterMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, true, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildFloraMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, true,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

static bool FloraStructureExpectedBlockAt(
    const FloraStructureInstance *structure,
    int worldX, int y, int worldZ, BlockType *outBlock)
{
    if (worldX < structure->minX || worldX > structure->maxX ||
        y < structure->minY || y > structure->maxY ||
        worldZ < structure->minZ || worldZ > structure->maxZ) return false;

    int base = structure->groundY + 1;
    int offsetX = worldX - structure->rootX;
    int offsetZ = worldZ - structure->rootZ;
    switch (structure->kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = 3 + (int)(structure->shapeHash % 3u);
        int distance = abs(offsetX) + abs(offsetZ);
        if (offsetX == 0 && offsetZ == 0 &&
            y == base + trunkHeight + 1) {
            *outBlock = BLOCK_GLOWSTONE;
            return true;
        }
        if ((y == base + trunkHeight - 1 && distance <= 3) ||
            (y == base + trunkHeight && distance < 2)) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + trunkHeight) {
            *outBlock = structure->primaryBlock;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_CRYSTAL: {
        int height = 2 + (int)(structure->shapeHash % 4u);
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + height) {
            *outBlock = y == base + height - 1 ?
                        structure->accentBlock : structure->primaryBlock;
            return true;
        }
        bool branch = structure->shapeHash & 1u ?
            ((offsetX == -1 && offsetZ == 0 && y == base + 1) ||
             (offsetX == 1 && offsetZ == 0 && y == base)) :
            ((offsetX == 0 && offsetZ == -1 && y == base + 1) ||
             (offsetX == 0 && offsetZ == 1 && y == base));
        if (branch) {
            *outBlock = structure->accentBlock;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_SPORE: {
        int stemHeight = 2 + (int)(structure->shapeHash % 2u);
        int distance = abs(offsetX) + abs(offsetZ);
        if (offsetX == 0 && offsetZ == 0 &&
            y == base + stemHeight + 1) {
            *outBlock = structure->primaryBlock;
            return true;
        }
        if (y == base + stemHeight && distance <= 1) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + stemHeight) {
            *outBlock = BLOCK_MUSHROOM;
            return true;
        }
    } break;
    case FLORA_STRUCTURE_THERMAL_VENT: {
        int height = 2 + (int)(structure->shapeHash % 3u);
        if (offsetX == 0 && offsetZ == 1 && y == base + height) {
            *outBlock = structure->accentBlock;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 && y == base + height) {
            *outBlock = BLOCK_GLOWSTONE;
            return true;
        }
        if (abs(offsetX) == 1 && offsetZ == 0 && y == base) {
            *outBlock = BLOCK_OBSIDIAN;
            return true;
        }
        if (offsetX == 0 && offsetZ == 0 &&
            y >= base && y < base + height) {
            *outBlock = BLOCK_NETHERRACK;
            return true;
        }
    } break;
    }
    return false;
}

static int FloraStructureOwnerAt(
    const FloraStructureInstance *structures, int structureCount,
    int worldX, int y, int worldZ, BlockType actualBlock)
{
    for (int index = structureCount - 1; index >= 0; index--) {
        BlockType expectedBlock = BLOCK_AIR;
        if (FloraStructureExpectedBlockAt(
                &structures[index], worldX, y, worldZ, &expectedBlock) &&
            actualBlock == expectedBlock) return index;
    }
    return -1;
}

static void CopyBlocksWithoutFloraStructures(
    unsigned short destination[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned short source[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount)
{
    memcpy(destination, source,
           sizeof(unsigned short) * CHUNK_SIZE * SURFACE_SECTION_HEIGHT * CHUNK_SIZE);
    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                if (FloraStructureOwnerAt(
                        structures, structureCount,
                        startX + lx, layerY + y, startZ + lz,
                        (BlockType)source[lx][y][lz]) >= 0) {
                    destination[lx][y][lz] = (unsigned short)BLOCK_AIR;
                }
            }
        }
    }
}

static bool MergeMeshData(Mesh *target, Mesh *source)
{
    if (source->vertexCount <= 0) {
        FreeMeshData(source);
        return true;
    }
    if (target->vertexCount <= 0) {
        *target = *source;
        *source = (Mesh){ 0 };
        return true;
    }

    int targetVertexCount = target->vertexCount;
    int sourceVertexCount = source->vertexCount;
    int vertexCount = targetVertexCount + sourceVertexCount;
    float *vertices = malloc((size_t)vertexCount * 3u * sizeof(float));
    float *texcoords = malloc((size_t)vertexCount * 2u * sizeof(float));
    float *texcoords2 = malloc((size_t)vertexCount * 2u * sizeof(float));
    float *normals = malloc((size_t)vertexCount * 3u * sizeof(float));
    unsigned char *colors = malloc((size_t)vertexCount * 4u);
    if (!vertices || !texcoords || !texcoords2 || !normals || !colors) {
        free(vertices);
        free(texcoords);
        free(texcoords2);
        free(normals);
        free(colors);
        FreeMeshData(source);
        return false;
    }

    memcpy(vertices, target->vertices,
           (size_t)targetVertexCount * 3u * sizeof(float));
    memcpy(vertices + targetVertexCount * 3, source->vertices,
           (size_t)sourceVertexCount * 3u * sizeof(float));
    memcpy(texcoords, target->texcoords,
           (size_t)targetVertexCount * 2u * sizeof(float));
    memcpy(texcoords + targetVertexCount * 2, source->texcoords,
           (size_t)sourceVertexCount * 2u * sizeof(float));
    memcpy(texcoords2, target->texcoords2,
           (size_t)targetVertexCount * 2u * sizeof(float));
    memcpy(texcoords2 + targetVertexCount * 2, source->texcoords2,
           (size_t)sourceVertexCount * 2u * sizeof(float));
    memcpy(normals, target->normals,
           (size_t)targetVertexCount * 3u * sizeof(float));
    memcpy(normals + targetVertexCount * 3, source->normals,
           (size_t)sourceVertexCount * 3u * sizeof(float));
    memcpy(colors, target->colors, (size_t)targetVertexCount * 4u);
    memcpy(colors + targetVertexCount * 4, source->colors,
           (size_t)sourceVertexCount * 4u);

    int triangleCount = target->triangleCount + source->triangleCount;
    FreeMeshData(target);
    FreeMeshData(source);
    *target = (Mesh){
        .vertexCount = vertexCount,
        .triangleCount = triangleCount,
        .vertices = vertices,
        .texcoords = texcoords,
        .texcoords2 = texcoords2,
        .normals = normals,
        .colors = colors
    };
    return true;
}

static bool AppendFloraVisualInstance(
    FloraVisualInstance **instances, int *instanceCount,
    FloraVisualInstance instance)
{
    FloraVisualInstance *resized = realloc(
        *instances,
        (size_t)(*instanceCount + 1) * sizeof(FloraVisualInstance));
    if (!resized) return false;
    resized[*instanceCount] = instance;
    *instances = resized;
    (*instanceCount)++;
    return true;
}

static bool AppendPlantMeshInstances(
    FloraVisualInstance **instances, int *instanceCount,
    const Mesh *mesh, int firstVertexOffset)
{
    for (int firstVertex = 0; firstVertex < mesh->vertexCount;
         firstVertex += 12) {
        int vertexCount = mesh->vertexCount - firstVertex;
        if (vertexCount > 12) vertexCount = 12;
        float groundY = INFINITY;
        for (int vertex = firstVertex;
             vertex < firstVertex + vertexCount; vertex++) {
            groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
        }
        float firstX = mesh->vertices[firstVertex * 3];
        float firstZ = mesh->vertices[firstVertex * 3 + 2];
        if (!AppendFloraVisualInstance(
                instances, instanceCount, (FloraVisualInstance){
                    .firstVertex = firstVertexOffset + firstVertex,
                    .vertexCount = vertexCount,
                    .anchor = {
                        floorf(firstX) + 0.5f,
                        groundY,
                        floorf(firstZ) + 0.5f
                    },
                    .height = 0.4f,
                    .windResponse = 1.0f
                })) {
            return false;
        }
    }
    return true;
}

static bool BuildChunkSurfaceSolidMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh)
{
    unsigned short solidBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        solidBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);
    return BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])solidBlocks,
        SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
        nearbyTorchIndices, nearbyTorchCount, outMesh);
}

static bool BuildChunkFloraMeshDataFromSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    *outMesh = (Mesh){ 0 };
    *outInstances = NULL;
    *outInstanceCount = 0;

    unsigned short floraBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    CopyBlocksWithoutFloraStructures(
        floraBlocks, blocks, layerY, chunkX, chunkZ, structures, structureCount);

    Mesh combined = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    Mesh plants = { 0 };
    if (BuildFloraMeshData(
            (const unsigned short (*)[CHUNK_SIZE])floraBlocks,
            SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
            nearbyTorchIndices, nearbyTorchCount, &plants)) {
        if (!AppendPlantMeshInstances(
                &instances, &instanceCount, &plants, 0) ||
            !MergeMeshData(&combined, &plants)) {
            FreeMeshData(&plants);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }

    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    for (int structureIndex = 0; structureIndex < structureCount;
         structureIndex++) {
        unsigned short instanceBlocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE] = { 0 };
        bool hasBlocks = false;
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    unsigned short block = blocks[lx][y][lz];
                    if (FloraStructureOwnerAt(
                            structures, structureCount,
                            startX + lx, layerY + y, startZ + lz,
                            (BlockType)block) != structureIndex) continue;
                    if (block == (unsigned short)BLOCK_AIR) continue;
                    instanceBlocks[lx][y][lz] = block;
                    hasBlocks = true;
                }
            }
        }
        if (!hasBlocks) continue;

        Mesh instanceMesh = { 0 };
        Mesh solid = { 0 };
        Mesh crossed = { 0 };
        if (BuildSurfaceSolidMeshData(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
                nearbyTorchIndices, nearbyTorchCount, &solid) &&
            !MergeMeshData(&instanceMesh, &solid)) {
            FreeMeshData(&solid);
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
        if (BuildFloraMeshData(
                (const unsigned short (*)[CHUNK_SIZE])instanceBlocks,
                SURFACE_SECTION_HEIGHT, layerY, chunkX, chunkZ, faces,
                nearbyTorchIndices, nearbyTorchCount, &crossed) &&
            !MergeMeshData(&instanceMesh, &crossed)) {
            FreeMeshData(&crossed);
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
        if (instanceMesh.vertexCount <= 0) continue;

        int firstVertex = combined.vertexCount;
        int vertexCount = instanceMesh.vertexCount;
        const FloraStructureInstance *structure = &structures[structureIndex];
        if (!MergeMeshData(&combined, &instanceMesh) ||
            !AppendFloraVisualInstance(
                &instances, &instanceCount, (FloraVisualInstance){
                    .firstVertex = firstVertex,
                    .vertexCount = vertexCount,
                    .anchor = {
                        (float)structure->rootX + 0.5f,
                        (float)(structure->groundY - layerY) + 1.0f,
                        (float)structure->rootZ + 0.5f
                    },
                    .height = (float)(structure->maxY - structure->groundY),
                    .windResponse = structure->windResponse
                })) {
            FreeMeshData(&instanceMesh);
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }

    if (combined.vertexCount <= 0) {
        free(instances);
        return false;
    }
    *outMesh = combined;
    *outInstances = instances;
    *outInstanceCount = instanceCount;
    return true;
}

bool BuildChunkFloraMeshData(
    const Chunk *chunk, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    if (!chunk || !outMesh || !outInstances || !outInstanceCount) return false;
    Mesh combined = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
        const ChunkSection *section = ChunkGetSectionConst(chunk, sy);
        if (!section) continue;
        Mesh part = { 0 };
        FloraVisualInstance *partInstances = NULL;
        int partCount = 0;
        if (!BuildChunkFloraMeshDataFromSnapshot(
                section->blocks, sy * SURFACE_SECTION_HEIGHT,
                chunk->cx, chunk->cz, chunk->floraStructures,
                chunk->floraStructureCount, faces, nearbyTorchIndices,
                nearbyTorchCount, &part, &partInstances, &partCount)) continue;
        float layerY = (float)(sy * SURFACE_SECTION_HEIGHT);
        for (int vertex = 0; vertex < part.vertexCount; vertex++) {
            part.vertices[vertex * 3 + 1] += layerY;
        }
        for (int i = 0; i < partCount; i++) {
            partInstances[i].anchor.y += layerY;
        }
        int firstVertex = combined.vertexCount;
        for (int i = 0; i < partCount; i++) {
            partInstances[i].firstVertex += firstVertex;
            if (!AppendFloraVisualInstance(&instances, &instanceCount,
                                           partInstances[i])) {
                free(partInstances);
                FreeMeshData(&part);
                FreeMeshData(&combined);
                free(instances);
                return false;
            }
        }
        free(partInstances);
        if (!MergeMeshData(&combined, &part)) {
            FreeMeshData(&combined);
            free(instances);
            return false;
        }
    }
    if (combined.vertexCount <= 0) {
        free(instances);
        return false;
    }
    *outMesh = combined;
    *outInstances = instances;
    *outInstanceCount = instanceCount;
    return true;
}

#define MAX_MESH_JOBS MAX_CHUNK_MESH_JOBS
#define MAX_MESH_SUBMITS_PER_FRAME 4

static MeshJob meshJobs[MAX_MESH_JOBS];

static void UpdateQueuePeaksLocked(void)
{
    uint64_t generation = 0;
    uint64_t mesh = 0;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) generation++;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) mesh++;
    }
    if (generation > streamingStats.generationQueuePeak) {
        streamingStats.generationQueuePeak = generation;
    }
    if (mesh > streamingStats.meshQueuePeak) streamingStats.meshQueuePeak = mesh;
    uint64_t snapshotBytes = 0;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse) snapshotBytes += sizeof(meshJobs[i].blocks);
    }
    streamingStats.pendingMeshSnapshotBytes = snapshotBytes;
    if (snapshotBytes > streamingStats.pendingMeshSnapshotBytesPeak) {
        streamingStats.pendingMeshSnapshotBytesPeak = snapshotBytes;
    }
}

static bool HasPendingMeshJob(void)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) return true;
    }
    return false;
}

static MeshJob *NextPendingMeshJob(void)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) return &meshJobs[i];
    }
    return NULL;
}

static bool FindPendingMeshJob(int slotIndex, int sectionY)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && meshJobs[i].slotIndex == slotIndex &&
            meshJobs[i].sectionY == sectionY) return true;
    }
    return false;
}

static void FreeMeshData(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static void ReplaceChunkModel(Model *model, bool *hasModel,
                              Mesh *mesh, bool hasMesh, bool dynamic)
{
    if (*hasModel) {
        UnloadModel(*model);
        *model = (Model){ 0 };
        *hasModel = false;
    }
    if (!hasMesh) {
        FreeMeshData(mesh);
        return;
    }

    UploadMesh(mesh, dynamic);
    *model = LoadModelFromMesh(*mesh);
    SetMaterialTexture(&model->materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
    *hasModel = true;
}

static void InitializeFloraTargets(
    ChunkSection *section, const FloraVisualInstance *sourceInstances,
    int sourceInstanceCount)
{
    ClearSectionFloraRuntime(section);
    if (!section || !section->hasFloraModel || section->floraModel.meshCount <= 0) return;

    Mesh *mesh = &section->floraModel.meshes[0];
    if (mesh->vertexCount <= 0 || !mesh->vertices || !mesh->colors) return;
    bool hasSourceInstances = sourceInstances && sourceInstanceCount > 0;
    int count = hasSourceInstances ? sourceInstanceCount :
                (mesh->vertexCount + 11) / 12;
    section->floraTargetScales = malloc((size_t)count * sizeof(float));
    section->floraTargetWind = malloc((size_t)count * sizeof(float));
    section->floraTargetWindAngle = malloc((size_t)count * sizeof(float));
    section->floraTargetPresence = malloc((size_t)count * sizeof(float));
    section->floraBaseVertices = malloc(
        (size_t)mesh->vertexCount * 3u * sizeof(float));
    section->floraBaseColors = malloc((size_t)mesh->vertexCount * 4u);
    section->floraVisualInstances = malloc(
        (size_t)count * sizeof(FloraVisualInstance));
    if (!section->floraTargetScales || !section->floraTargetWind ||
        !section->floraTargetWindAngle || !section->floraTargetPresence ||
        !section->floraBaseVertices ||
        !section->floraBaseColors || !section->floraVisualInstances) {
        ClearSectionFloraRuntime(section);
        return;
    }
    memcpy(section->floraBaseVertices, mesh->vertices,
           (size_t)mesh->vertexCount * 3u * sizeof(float));
    memcpy(section->floraBaseColors, mesh->colors,
           (size_t)mesh->vertexCount * 4u);
    if (hasSourceInstances) {
        memcpy(section->floraVisualInstances, sourceInstances,
               (size_t)count * sizeof(FloraVisualInstance));
    }
    for (int index = 0; index < count; index++) {
        if (!hasSourceInstances) {
            int firstVertex = index * 12;
            int vertexCount = mesh->vertexCount - firstVertex;
            if (vertexCount > 12) vertexCount = 12;
            float groundY = INFINITY;
            for (int vertex = firstVertex;
                 vertex < firstVertex + vertexCount; vertex++) {
                groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
            }
            float firstX = mesh->vertices[firstVertex * 3];
            float firstZ = mesh->vertices[firstVertex * 3 + 2];
            section->floraVisualInstances[index] = (FloraVisualInstance){
                .firstVertex = firstVertex,
                .vertexCount = vertexCount,
                .anchor = {
                    floorf(firstX) + 0.5f,
                    groundY,
                    floorf(firstZ) + 0.5f
                },
                .height = 0.4f,
                .windResponse = 1.0f
            };
        }
        section->floraTargetScales[index] = 1.0f;
        section->floraTargetWind[index] = 0.0f;
        section->floraTargetWindAngle[index] = 0.0f;
        section->floraTargetPresence[index] = 1.0f;
    }
    section->floraTargetScaleCount = count;
}

static bool UploadMeshJob(MeshJob *job)
{
    Chunk *chunk = NULL;
    bool valid = false;
    if (job && job->slotIndex >= 0 && job->slotIndex < MAX_ACTIVE_CHUNKS) {
        chunk = &chunks[job->slotIndex];
        valid = chunk->loaded && chunk->cx == job->cx && chunk->cz == job->cz &&
                chunk->generation == job->chunkGeneration &&
                ChunkGetSection(chunk, job->sectionY, false) != NULL;
    }

    if (job && valid) {
        ChunkSection *section = ChunkGetSection(chunk, job->sectionY, false);
        ReplaceChunkModel(&section->model, &section->hasModel,
                          &job->mesh, job->hasMesh, false);
        ReplaceChunkModel(&section->waterModel, &section->hasWaterModel,
                          &job->waterMesh, job->hasWaterMesh, false);
        ReplaceChunkModel(&section->floraModel, &section->hasFloraModel,
                          &job->floraMesh, job->hasFloraMesh, true);
        InitializeFloraTargets(section, job->floraInstances,
                               job->floraInstanceCount);
        section->floraVisualScale = 1.0f;
    } else if (job) {
        FreeMeshData(&job->mesh);
        FreeMeshData(&job->waterMesh);
        FreeMeshData(&job->floraMesh);
    }

    if (job) {
        free(job->floraInstances);
        job->floraInstances = NULL;
        job->floraInstanceCount = 0;
    }

    pthread_mutex_lock(&genMutex);
    if (job) job->inUse = false;
    bool pending = job && valid && FindPendingMeshJob(job->slotIndex,
                                                      job->sectionY);
    if (job && !valid) streamingStats.meshCanceled++;
    pthread_mutex_unlock(&genMutex);
    if (valid && !pending) {
        ChunkSection *section = ChunkGetSection(chunk, job->sectionY, false);
        // Only clear the dirty flag when the uploaded snapshot still matches
        // the current content revision. If the section was edited after the
        // snapshot was taken, keep it dirty so a fresh job rebuilds it with
        // the new content instead of silently losing the edit.
        if (section && section->dirtyStamp == job->sectionStamp) {
            section->dirty = false;
        }
    }
    return valid;
}

static void PrepareMeshJob(MeshJob *job, const Chunk *chunk,
                           const ChunkSection *section)
{
    memcpy(job->blocks, section->blocks, sizeof(job->blocks));
    job->floraStructureCount = chunk->floraStructureCount;
    if (job->floraStructureCount < 0) job->floraStructureCount = 0;
    if (job->floraStructureCount > MAX_CHUNK_FLORA_STRUCTURES) {
        job->floraStructureCount = MAX_CHUNK_FLORA_STRUCTURES;
    }
    memcpy(job->floraStructures, chunk->floraStructures,
           (size_t)job->floraStructureCount * sizeof(FloraStructureInstance));
    job->nearbyCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        job->nearbyIndices);
    job->inUse = true;
    job->done = false;
    job->slotIndex = (int)(chunk - chunks);
    job->cx = chunk->cx;
    job->cz = chunk->cz;
    job->sectionY = section->sectionY;
    job->sectionStamp = section->dirtyStamp;
    job->chunkGeneration = chunk->generation;
    job->mesh = (Mesh){ 0 };
    job->waterMesh = (Mesh){ 0 };
    job->floraMesh = (Mesh){ 0 };
    free(job->floraInstances);
    job->floraInstances = NULL;
    job->floraInstanceCount = 0;
    job->hasMesh = false;
    job->hasWaterMesh = false;
    job->hasFloraMesh = false;
}

static bool SubmitMeshJobs(Chunk *chunk, ChunkSection *section)
{
    if (!chunk || !section || genThread == 0) return false;

    pthread_mutex_lock(&genMutex);
    MeshJob *job = NULL;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (!meshJobs[i].inUse) {
            job = &meshJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    PrepareMeshJob(job, chunk, section);
    streamingStats.meshSubmitted++;
    streamingStats.meshSnapshotBytes += sizeof(job->blocks);
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

void ProcessFinishedMeshJobs(double uploadBudgetMs)
{
    int uploaded = 0;
    double startedMs = ChunkNowMs();
    if (!isfinite(uploadBudgetMs) || uploadBudgetMs < 0.0) {
        uploadBudgetMs = 0.0;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        MeshJob *job = &meshJobs[i];
        pthread_mutex_lock(&genMutex);
        bool ready = job->inUse && job->done;
        pthread_mutex_unlock(&genMutex);
        if (!ready) continue;
        if (uploaded > 0 && ChunkNowMs() - startedMs >= uploadBudgetMs) {
            pthread_mutex_lock(&genMutex);
            streamingStats.uploadBudgetDeferrals++;
            pthread_mutex_unlock(&genMutex);
            break;
        }
        double uploadStartedMs = ChunkNowMs();
        bool uploadedJob = UploadMeshJob(job);
        double uploadElapsedMs = ChunkNowMs() - uploadStartedMs;
        pthread_mutex_lock(&genMutex);
        if (uploadedJob) streamingStats.uploadedMeshes++;
        streamingStats.uploadCpuMs += uploadElapsedMs;
        if (uploadElapsedMs > streamingStats.maxUploadCpuMs) {
            streamingStats.maxUploadCpuMs = uploadElapsedMs;
        }
        pthread_mutex_unlock(&genMutex);
        if (++uploaded >= MAX_MESH_REBUILDS_PER_FRAME) break;
    }
}

static void RebuildChunkSectionMeshSync(Chunk *chunk, ChunkSection *section)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int nearbyTorchIndices[MAX_TORCH_LIGHTS];
    int nearbyTorchCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        nearbyTorchIndices);

    UnloadChunkSectionModels(section);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    Mesh floraMesh = { 0 };
    FloraVisualInstance *floraInstances = NULL;
    int floraInstanceCount = 0;
    bool hasSolid = BuildChunkSurfaceSolidMeshData(
        section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz,
        chunk->floraStructures, chunk->floraStructureCount,
        faces, nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildSurfaceWaterMeshData(
        (const unsigned short (*)[CHUNK_SIZE])section->blocks,
        SURFACE_SECTION_HEIGHT,
        section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz, faces,
        nearbyTorchIndices, nearbyTorchCount, &waterMesh);
    bool hasFlora = BuildChunkFloraMeshDataFromSnapshot(
        section->blocks, section->sectionY * SURFACE_SECTION_HEIGHT,
        chunk->cx, chunk->cz,
        chunk->floraStructures, chunk->floraStructureCount,
        faces, nearbyTorchIndices, nearbyTorchCount, &floraMesh,
        &floraInstances, &floraInstanceCount);

    ReplaceChunkModel(&section->model, &section->hasModel,
                      &solidMesh, hasSolid, false);
    ReplaceChunkModel(&section->waterModel, &section->hasWaterModel,
                      &waterMesh, hasWater, false);
    ReplaceChunkModel(&section->floraModel, &section->hasFloraModel,
                      &floraMesh, hasFlora, true);
    InitializeFloraTargets(section, floraInstances, floraInstanceCount);
    free(floraInstances);
    section->floraVisualScale = 1.0f;
    section->dirty = false;
}

void RebuildDirtyChunkMeshes(Vector3 focusPosition)
{
    int submitted = 0;
    int focusCx = 0;
    int focusCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(focusPosition.x),
                      (int)floorf(focusPosition.z),
                      &focusCx, &focusCz, &localX, &localZ);

    bool selected[MAX_ACTIVE_CHUNKS][SURFACE_SECTION_COUNT] = { 0 };
    while (submitted < MAX_MESH_SUBMITS_PER_FRAME) {
        int best = -1;
        int bestSectionY = -1;
        int bestDistance = 0;
        for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
            if (!chunks[i].loaded) continue;
            int dx = abs(chunks[i].cx - focusCx);
            int dz = abs(chunks[i].cz - focusCz);
            int distance = dx > dz ? dx : dz;
            for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
                ChunkSection *section = chunks[i].sections[sy];
                if (!section || selected[i][sy] || !section->dirty ||
                    FindPendingMeshJob(i, sy)) continue;
                int verticalDistance = abs(
                    sy - (int)floorf(focusPosition.y / SURFACE_SECTION_HEIGHT));
                int priority = distance * SURFACE_SECTION_COUNT + verticalDistance;
                if (best < 0 || priority < bestDistance) {
                    best = i;
                    bestSectionY = sy;
                    bestDistance = priority;
                }
            }
        }
        if (best < 0) break;
        selected[best][bestSectionY] = true;
        ChunkSection *section = chunks[best].sections[bestSectionY];

        if (genThread == 0) {
            double startedMs = ChunkNowMs();
            RebuildChunkSectionMeshSync(&chunks[best], section);
            double elapsedMs = ChunkNowMs() - startedMs;
            pthread_mutex_lock(&genMutex);
            streamingStats.syncRebuilds++;
            streamingStats.meshCpuMs += elapsedMs;
            pthread_mutex_unlock(&genMutex);
            submitted++;
            continue;
        }

        if (!SubmitMeshJobs(&chunks[best], section)) break;
        submitted++;
    }
}

bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance)
{
    int cameraX = (int)floorf(cameraPosition.x);
    int cameraZ = (int)floorf(cameraPosition.z);
    int cameraCx = 0;
    int cameraCz = 0;
    int cameraLx = 0;
    int cameraLz = 0;
    WorldToChunkLocal(cameraX, cameraZ, &cameraCx, &cameraCz, &cameraLx, &cameraLz);

    return abs(chunk->cx - cameraCx) <= effectiveRenderDistance &&
           abs(chunk->cz - cameraCz) <= effectiveRenderDistance;
}

bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    Vector3 toCenter = Vector3Subtract(center, camera->position);

    float depth = Vector3DotProduct(toCenter, forward);
    if (depth + radius < CAMERA_NEAR_CULL_DISTANCE) return false;

    float visibleDepth = fmaxf(depth, CAMERA_NEAR_CULL_DISTANCE);
    float verticalTan = tanf(camera->fovy * DEG2RAD * 0.5f);
    int screenHeight = GetScreenHeight();
    float aspect = screenHeight > 0 ? (float)GetScreenWidth() / (float)screenHeight : 1.0f;
    float horizontalTan = verticalTan * aspect;
    float horizontalOffset = fabsf(Vector3DotProduct(toCenter, right));
    float verticalOffset = fabsf(Vector3DotProduct(toCenter, up));

    return horizontalOffset <= visibleDepth * horizontalTan + radius &&
           verticalOffset <= visibleDepth * verticalTan + radius;
}

bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera)
{
    Vector3 center = {
        (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        (float)WORLD_HEIGHT * 0.5f,
        (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
    float halfChunk = (float)CHUNK_SIZE * 0.5f;
    float halfHeight = (float)WORLD_HEIGHT * 0.5f;
    float radius = sqrtf(halfChunk * halfChunk * 2.0f + halfHeight * halfHeight);

    return SphereInFrustum(camera, center, radius);
}

bool ChunkSectionIntersectsCameraView(const Chunk *chunk,
                                      const ChunkSection *section,
                                      const Camera3D *camera)
{
    if (!chunk || !section || !camera) return false;
    float half = (float)SURFACE_SECTION_HEIGHT * 0.5f;
    Vector3 center = {
        (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        (float)(section->sectionY * SURFACE_SECTION_HEIGHT) + half,
        (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
    return SphereInFrustum(camera, center, sqrtf(half * half * 3.0f));
}


bool ChunksStartGenThread(void)
{
    if (genThread != 0) return true;
    pthread_mutex_lock(&genMutex);
    genShutdown = false;
    genWorkerActive = false;
    pthread_mutex_unlock(&genMutex);
    return pthread_create(&genThread, NULL, ChunkGenWorker, NULL) == 0;
}

void ChunksShutdownGenThread(void)
{
    DrainChunkGen();
    if (genThread != 0) {
        pthread_mutex_lock(&genMutex);
        genShutdown = true;
        pthread_cond_broadcast(&genCond);
        pthread_mutex_unlock(&genMutex);
        pthread_join(genThread, NULL);
        genThread = 0;
        genWorkerActive = false;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse) {
            FreeMeshData(&meshJobs[i].mesh);
            FreeMeshData(&meshJobs[i].waterMesh);
            FreeMeshData(&meshJobs[i].floraMesh);
            free(meshJobs[i].floraInstances);
            meshJobs[i].floraInstances = NULL;
            meshJobs[i].floraInstanceCount = 0;
            meshJobs[i].inUse = false;
        }
    }
}

int GetActiveChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded) count++;
    }
    return count;
}

int GetPendingGenJobCount(void)
{
    int count = 0;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) count++;
    }
    pthread_mutex_unlock(&genMutex);
    return count;
}

int GetPendingMeshJobCount(void)
{
    int count = 0;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) count++;
    }
    pthread_mutex_unlock(&genMutex);
    return count;
}

void ChunksResetStreamingStats(void)
{
    pthread_mutex_lock(&genMutex);
    streamingStats = (ChunkStreamingStats){ 0 };
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

ChunkStreamingStats ChunksGetStreamingStats(void)
{
    pthread_mutex_lock(&genMutex);
    UpdateQueuePeaksLocked();
    ChunkStreamingStats result = streamingStats;
    pthread_mutex_unlock(&genMutex);
    return result;
}

RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = { 0 };
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        const Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
            const ChunkSection *section = chunk->sections[sy];
            if (!section) continue;
            if (section->hasModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->model,
                                               RENDER_RESOURCE_SOLID);
            }
            if (section->hasFloraModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->floraModel,
                                               RENDER_RESOURCE_FLORA);
            }
            if (section->hasWaterModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->waterModel,
                                               RENDER_RESOURCE_TRANSPARENT);
            }
        }
    }
    pthread_mutex_lock(&genMutex);
    UpdateQueuePeaksLocked();
    snapshot.pendingMeshSnapshotBytes = streamingStats.pendingMeshSnapshotBytes;
    snapshot.workerThreadsConfigured = 1;
    snapshot.workerThreadsStarted = genThread != 0 ? 1u : 0u;
    snapshot.workerThreadsActive = genWorkerActive ? 1u : 0u;
    pthread_mutex_unlock(&genMutex);
    return snapshot;
}

#ifdef CHUNKS_TESTING
void ChunksTestResetScheduler(void)
{
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        ChunkClearBlockStorage(&chunks[i]);
    }
    memset(chunks, 0, sizeof(chunks));
    memset(chunkGenJobs, 0, sizeof(chunkGenJobs));
    memset(meshJobs, 0, sizeof(meshJobs));
    streamingStats = (ChunkStreamingStats){ 0 };
    genThread = pthread_self();
    genShutdown = false;
    genWorkerActive = false;
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestConfigureChunk(int slotIndex, int cx, int cz, bool loaded, bool dirty)
{
    assert(slotIndex >= 0 && slotIndex < MAX_ACTIVE_CHUNKS);
    chunks[slotIndex] = (Chunk){ 0 };
    chunks[slotIndex].cx = cx;
    chunks[slotIndex].cz = cz;
    chunks[slotIndex].loaded = loaded;
    if (dirty) {
        ChunkSection *section = ChunkGetSection(&chunks[slotIndex], 0, true);
        if (section) section->dirty = true;
    }
}

bool ChunksTestChunkDirty(int slotIndex)
{
    assert(slotIndex >= 0 && slotIndex < MAX_ACTIVE_CHUNKS);
    for (int sy = 0; sy < SURFACE_SECTION_COUNT; sy++) {
        if (chunks[slotIndex].sections[sy] &&
            chunks[slotIndex].sections[sy]->dirty) return true;
    }
    return false;
}

int ChunksTestMeshJobSlot(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    return meshJobs[jobIndex].inUse ? meshJobs[jobIndex].slotIndex : -1;
}

int ChunksTestMeshJobSectionY(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    return meshJobs[jobIndex].inUse ? meshJobs[jobIndex].sectionY : -1;
}

void ChunksTestCompleteMeshJob(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    pthread_mutex_lock(&genMutex);
    if (meshJobs[jobIndex].inUse) meshJobs[jobIndex].done = true;
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestSeedMeshJob(int jobIndex, int slotIndex, int cx, int cz, bool done)
{
    assert(jobIndex >= 0 && jobIndex < MAX_MESH_JOBS);
    pthread_mutex_lock(&genMutex);
    meshJobs[jobIndex] = (MeshJob){
        .inUse = true,
        .done = done,
        .slotIndex = slotIndex,
        .cx = cx,
        .cz = cz
    };
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestFillGenerationQueue(void)
{
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        chunkGenJobs[i] = (ChunkGenJob){
            .inUse = true,
            .cx = 10000 + i,
            .cz = 10000
        };
    }
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}
#endif
