#include "world/chunks_cube.h"
#include "world/chunks_internal.h"

static Chunk *Resident(SurfaceCubeChunkKey key)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++)
        if ((chunks[i].loaded || chunks[i].generating) && chunks[i].cubeGrid &&
            SurfaceCubeChunkKeyEqual(chunks[i].cubeKey, key)) return &chunks[i];
    return NULL;
}

static bool ContextMatches(TerrainCubeContext *context, SurfaceCubeChunkKey key)
{
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    return snapshot && SurfaceCubeChunkKeyIsValid(key) &&
        key.bodyId == snapshot->context.bodyId &&
        ((snapshot->context.kind == WORLD_SURFACE_HOME && key.bodyId == 0) ||
         (snapshot->context.kind == WORLD_SURFACE_PLANET && key.bodyId != 0)) &&
        TerrainCubeContextGeneration(context) == WorldCurrentGeneration();
}

static bool SectionPending(const Chunk *chunk, int sectionY)
{
    bool found = false;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        const ChunkGenJob *job = &chunkGenJobs[i];
        if (job->inUse && job->cubeGrid && job->scope == CHUNK_GEN_SCOPE_SECTION &&
            job->sectionY == sectionY && job->slotIndex == (int)(chunk - chunks) &&
            ChunkGenJobTargetMatchesChunk(job, chunk)) { found = true; break; }
    }
    pthread_mutex_unlock(&genMutex);
    return found;
}

static CubeChunkRequestResult Submit(TerrainCubeContext *context, Chunk *chunk,
                                      ChunkGenScope scope, int sectionY, bool preload)
{
    ChunkGenJob job = {.scope = scope, .sectionY = sectionY, .preload = preload,
        .slotIndex = (int)(chunk - chunks), .chunkGeneration = chunk->generation};
    if (!TerrainCubePrepareJob(context, chunk->cubeKey, &job)) return CUBE_CHUNK_UNAVAILABLE;
    if (ChunksStartedWorkerCount()) {
        if (QueueCubeChunkGenJob(&job, chunk)) return CUBE_CHUNK_PENDING;
        ReleaseChunkGenJob(&job);
        return CUBE_CHUNK_UNAVAILABLE;
    }
    double start = ChunkNowMs();
    GenerateChunkJobPayload(&job, preload);
    pthread_mutex_lock(&genMutex);
    streamingStats.generationSubmitted++;
    streamingStats.generationCompleted++;
    streamingStats.generationCpuMs += ChunkNowMs() - start;
    pthread_mutex_unlock(&genMutex);
    CompleteChunkGenJob(&job);
    bool ready = scope == CHUNK_GEN_SCOPE_COLUMN ? chunk->loaded :
        ChunkTerrainSectionIsResolved(chunk, sectionY) || ChunkGetSectionConst(chunk, sectionY);
    return ready ? CUBE_CHUNK_READY : CUBE_CHUNK_UNAVAILABLE;
}

bool ChunksReleaseCubeColumn(SurfaceCubeChunkKey key)
{
    if (!SurfaceCubeChunkKeyIsValid(key)) return false;
    Chunk *chunk = Resident(key);
    if (!chunk) return false;
    for (int i = 0; i < chunk->sectionCount; i++)
        MarkGeneratedSectionAndNeighborsDirty(chunk, chunk->sections[i]->sectionY);
    ChunkIndexRemove(chunk);
    chunk->generation++;
    if (!chunk->generation) chunk->generation = 1;
    chunk->loaded = chunk->generating = chunk->preloaded = false;
    ChunkClearBlockStorage(chunk);
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->running || job->slotIndex != (int)(chunk - chunks) ||
            !job->cubeGrid || !SurfaceCubeChunkKeyEqual(job->cubeKey, key)) continue;
        ChunkTaskQueueRemove(&generationTaskQueue, i, job->queueSequence);
        ChunkTaskQueueRemove(&generationDoneQueue, i, job->queueSequence);
        ReleaseChunkGenJob(job);
        *job = (ChunkGenJob){0};
        streamingStats.generationCanceled++;
    }
    pthread_mutex_unlock(&genMutex);
    return true;
}

CubeChunkRequestResult ChunksRequestCubeColumn(TerrainCubeContext *context,
                                               SurfaceCubeChunkKey key, bool preload)
{
    if (!ContextMatches(context, key)) return CUBE_CHUNK_UNAVAILABLE;
    Chunk *chunk = Resident(key);
    if (chunk && chunk->cubeWorldGeneration != WorldCurrentGeneration()) {
        ChunksReleaseCubeColumn(key);
        chunk = NULL;
    }
    if (chunk) {
        if (!preload) {
            chunk->preloaded = false;
            // Completion takes preload from the queued job, so promotion
            // must survive a worker that is already generating this column.
            pthread_mutex_lock(&genMutex);
            for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
                ChunkGenJob *job = &chunkGenJobs[i];
                if (job->inUse && job->cubeGrid &&
                    job->scope == CHUNK_GEN_SCOPE_COLUMN &&
                    job->slotIndex == (int)(chunk - chunks) &&
                    ChunkGenJobTargetMatchesChunk(job, chunk) && job->preload) {
                    job->preload = false;
                    if (job->queued) {
                        ChunkTaskQueueRemove(&generationTaskQueue, i, job->queueSequence);
                        job->queued = ChunkTaskQueuePushOrdered(
                            &generationTaskQueue, 0, i, job->queueSequence);
                    }
                }
            }
            pthread_mutex_unlock(&genMutex);
        }
        return chunk->loaded ? CUBE_CHUNK_READY : CUBE_CHUNK_PENDING;
    }
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++)
        if (!chunks[i].loaded && !chunks[i].generating) { chunk = &chunks[i]; break; }
    if (!chunk) return CUBE_CHUNK_UNAVAILABLE;
    uint32_t generation = chunk->generation + 1;
    ChunkIndexRemove(chunk);
    ChunkClearBlockStorage(chunk);
    *chunk = (Chunk){.cx = key.u, .cz = key.v, .spherical = true, .cubeGrid = true,
        .cubeKey = key, .cubeWorldGeneration = WorldCurrentGeneration(),
        .generation = generation ? generation : 1, .generating = true, .preloaded = preload,
        .floraActivity = 1, .floraCapacity = 1};
    CubeChunkRequestResult result = Submit(context, chunk, CHUNK_GEN_SCOPE_COLUMN, 0, preload);
    if (result == CUBE_CHUNK_UNAVAILABLE) chunk->generating = false;
    return result;
}

CubeChunkRequestResult ChunksRequestCubeSection(TerrainCubeContext *context,
                                                SurfaceCubeChunkKey key, int sectionY)
{
    if (!ContextMatches(context, key) ||
        sectionY < TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT ||
        sectionY >= SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT)
        return CUBE_CHUNK_UNAVAILABLE;
    Chunk *chunk = Resident(key);
    if (!chunk || !chunk->loaded || chunk->cubeWorldGeneration != WorldCurrentGeneration())
        return CUBE_CHUNK_UNAVAILABLE;
    if (ChunkTerrainSectionIsResolved(chunk, sectionY) || ChunkGetSectionConst(chunk, sectionY))
        return CUBE_CHUNK_READY;
    if (SectionPending(chunk, sectionY)) return CUBE_CHUNK_PENDING;
    return Submit(context, chunk, CHUNK_GEN_SCOPE_SECTION, sectionY, false);
}
