#include "world/chunks_cube.h"
#include "world/chunks_internal.h"

static Chunk *Resident(SurfaceCubeChunkKey key)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++)
        if ((chunks[i].loaded || chunks[i].generating) && chunks[i].cubeGrid &&
            SurfaceCubeChunkKeyEqual(chunks[i].cubeKey, key)) return &chunks[i];
    return NULL;
}

bool ChunksCubeColumnIsLoaded(SurfaceCubeChunkKey key)
{
    Chunk *chunk = Resident(key);
    return chunk && chunk->loaded;
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

static bool ReleaseCubeResident(Chunk *chunk)
{
    if (!chunk || !chunk->cubeGrid || (!chunk->loaded && !chunk->generating))
        return false;
    SurfaceCubeChunkKey key = chunk->cubeKey;
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

bool ChunksReleaseCubeColumn(SurfaceCubeChunkKey key)
{
    if (!SurfaceCubeChunkKeyIsValid(key)) return false;
    return ReleaseCubeResident(Resident(key));
}

CubeChunkRequestResult ChunksRequestCubeColumn(TerrainCubeContext *context,
                                               SurfaceCubeChunkKey key, bool preload)
{
    if (!ContextMatches(context, key)) return CUBE_CHUNK_UNAVAILABLE;
    Chunk *chunk = Resident(key);
    if (chunk && chunk->cubeWorldGeneration != WorldCurrentGeneration()) {
        ReleaseCubeResident(chunk);
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

/* Last coverage observation, retained for read-only diagnostics. The counters
 * describe the most recent ChunksUpdateCubeCoverage call; they never claim
 * full visual coverage and a saturated observation stays saturated. */
static CubeCoverageStatus lastCubeCoverage;
static CubeCoverageObservation lastCubeCoverageObservation;
static SurfaceCubeCoverageCandidate lastCubeCoverageAdmission[MAX_ACTIVE_CHUNKS];
static uint64_t lastCubeCoverageWorldGeneration;
static bool lastCubeCoverageValid;
static bool cubeCoverageActive;

typedef struct CubeCoveragePlan {
    Chunk *releases[MAX_ACTIVE_CHUNKS];
    bool requests[MAX_ACTIVE_CHUNKS];
    unsigned releaseCount;
    unsigned duplicates;
    unsigned stale;
} CubeCoveragePlan;

static int CandidateIndexForKey(const SurfaceCubeCoverageCandidate *candidates,
                                unsigned count, SurfaceCubeChunkKey key)
{
    for (unsigned i = 0; i < count; i++)
        if (SurfaceCubeChunkKeyEqual(candidates[i].key, key)) return (int)i;
    return -1;
}

static bool BuildCubeCoveragePlan(TerrainCubeContext *context,
                                  const SurfaceCubeCoverageCandidate *candidates,
                                  unsigned admitted, CubeCoveragePlan *plan)
{
    if (!context || !candidates || !plan || admitted > MAX_ACTIVE_CHUNKS)
        return false;
    *plan = (CubeCoveragePlan){ 0 };
    for (unsigned i = 0; i < admitted; i++) {
        if (!ContextMatches(context, candidates[i].key)) return false;
        for (unsigned previous = 0; previous < i; previous++)
            if (SurfaceCubeChunkKeyEqual(candidates[previous].key,
                                         candidates[i].key)) return false;
    }

    Chunk *residentForCandidate[MAX_ACTIVE_CHUNKS] = { 0 };
    uint64_t generation = WorldCurrentGeneration();
    for (int slot = 0; slot < MAX_ACTIVE_CHUNKS; slot++) {
        Chunk *chunk = &chunks[slot];
        if (!chunk->cubeGrid || (!chunk->loaded && !chunk->generating)) continue;
        int candidate = CandidateIndexForKey(candidates, admitted, chunk->cubeKey);
        if (candidate < 0) {
            plan->releases[plan->releaseCount++] = chunk;
        } else if (chunk->cubeWorldGeneration != generation) {
            plan->stale++;
            plan->releases[plan->releaseCount++] = chunk;
        } else if (residentForCandidate[candidate]) {
            plan->duplicates++;
            plan->releases[plan->releaseCount++] = chunk;
        } else {
            residentForCandidate[candidate] = chunk;
        }
    }
    for (unsigned i = 0; i < admitted; i++) {
        if (residentForCandidate[i]) plan->duplicates++;
        else plan->requests[i] = true;
    }
    return true;
}

bool ChunksLastCubeCoverage(CubeCoverageStatus *out)
{
    if (!out || !lastCubeCoverageValid) return false;
    *out = lastCubeCoverage;
    return true;
}

bool ChunksCubeCoverageIsActive(void)
{
    return cubeCoverageActive;
}

bool ChunksCubeCoverageObservation(CubeCoverageObservation *outObservation)
{
    if (!outObservation || !lastCubeCoverageValid) return false;
    *outObservation = lastCubeCoverageObservation;
    return true;
}

bool ChunksCubeCoverageAdmissionAt(unsigned index,
                                   SurfaceCubeCoverageCandidate *outCandidate)
{
    if (!outCandidate || !lastCubeCoverageValid ||
        index >= lastCubeCoverageObservation.admitted) return false;
    *outCandidate = lastCubeCoverageAdmission[index];
    return true;
}

bool ChunksCubeCoverageContains(SurfaceCubeChunkKey key)
{
    if (!lastCubeCoverageValid || !SurfaceCubeChunkKeyIsValid(key) ||
        lastCubeCoverageWorldGeneration != WorldCurrentGeneration() ||
        key.bodyId != lastCubeCoverageObservation.bodyId) return false;
    return CandidateIndexForKey(lastCubeCoverageAdmission,
                                lastCubeCoverageObservation.admitted, key) >= 0;
}

bool ChunksUpdateCubeCoverage(TerrainCubeContext *context, SurfaceVector focus,
                              double radius, unsigned budget,
                              CubeCoverageStatus *out)
{
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    SurfaceAddress cell;
    SurfaceCubeChunkKey focusKey;
    SurfaceCubeCoverageCandidate candidates[MAX_ACTIVE_CHUNKS];
    SurfaceCubeCoverageResult coverage;
    if (!out || !snapshot || !SurfaceCellFromPoint(snapshot->context.bodyId, focus, &cell) ||
        !SurfaceCubeChunkAt(cell, &focusKey) || !ContextMatches(context, focusKey) ||
        cell.radial < TERRAIN_CUBE_MIN_RADIAL || cell.radial >= SURFACE_MAX_Y_EXCLUSIVE ||
        !SurfaceCoverageCollect(focusKey.bodyId, focus, radius, candidates,
                                 MAX_ACTIVE_CHUNKS, &coverage)) return false;
    CubeCoveragePlan plan;
    if (!BuildCubeCoveragePlan(context, candidates, coverage.admitted, &plan))
        return false;
    CubeCoverageStatus status = {.requested = coverage.requested,
        .admitted = coverage.admitted, .saturated = coverage.saturated,
        .duplicates = plan.duplicates, .stale = plan.stale};
    for (unsigned i = 0; i < plan.releaseCount; i++)
        if (ReleaseCubeResident(plan.releases[i])) status.released++;

    // The physical result selects both retention and nearest-first scheduling.
    // A current-generation resident absorbs its request; stale or missing
    // columns consume the caller's submission budget in candidate order.
    for (unsigned i = 0; i < coverage.admitted; i++) {
        if (plan.requests[i] && status.submitted < budget) {
            CubeChunkRequestResult result = ChunksRequestCubeColumn(context, candidates[i].key, false);
            if (result != CUBE_CHUNK_UNAVAILABLE) status.submitted++;
        }
        Chunk *chunk = Resident(candidates[i].key);
        if (chunk && chunk->loaded) status.ready++;
        else if (chunk && chunk->generating) status.pending++;
        else status.missing++;
    }
    // Collision needs the player's radial layer even when it lies far below
    // the initially generated ground shell. Keep this request in the same budget.
    int sectionY = SurfaceSectionYFromBlockY(cell.radial);
    for (unsigned i = 0; i < coverage.admitted && status.submitted < budget; i++) {
        if (candidates[i].minimumDistance > 2.0 * CHUNK_SIZE) continue;
        Chunk *chunk = Resident(candidates[i].key);
        if (!chunk || !chunk->loaded) continue;
        for (int offset = -1; offset <= 1 && status.submitted < budget; offset++) {
            int y = sectionY + offset;
            if (y < TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT ||
                y >= SURFACE_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT ||
                ChunkTerrainSectionIsResolved(chunk, y) || ChunkGetSectionConst(chunk, y) ||
                SectionPending(chunk, y)) continue;
            if (ChunksRequestCubeSection(context, chunk->cubeKey, y) != CUBE_CHUNK_UNAVAILABLE)
                status.submitted++;
        }
    }
    lastCubeCoverage = status;
    lastCubeCoverageObservation = (CubeCoverageObservation){
        .sequence = lastCubeCoverageObservation.sequence == UINT64_MAX
            ? 1u : lastCubeCoverageObservation.sequence + 1u,
        .bodyId = focusKey.bodyId,
        .requested = status.requested,
        .admitted = status.admitted,
        .saturated = status.saturated
    };
    memcpy(lastCubeCoverageAdmission, candidates,
           status.admitted * sizeof(*lastCubeCoverageAdmission));
    lastCubeCoverageWorldGeneration = WorldCurrentGeneration();
    lastCubeCoverageValid = true;
    cubeCoverageActive = true;
    *out = status;
    return true;
}

unsigned ChunksCubeTargetLodReadyCount(void)
{
    unsigned ready = 0;
    for (int slot = 0; slot < MAX_ACTIVE_CHUNKS; slot++) {
        const Chunk *chunk = &chunks[slot];
        if (!chunk->cubeGrid || !chunk->loaded) continue;
        if (chunk->cubeWorldGeneration != WorldCurrentGeneration()) continue;
        if (!ChunksCubeCoverageContains(chunk->cubeKey)) continue;
        if (ChunkTargetLodReady(chunk)) ready++;
    }
    return ready;
}

unsigned ChunksCubeTargetLodMissingSections(void)
{
    unsigned missing = 0;
    for (int slot = 0; slot < MAX_ACTIVE_CHUNKS; slot++) {
        const Chunk *chunk = &chunks[slot];
        if (!chunk->cubeGrid || !chunk->loaded) continue;
        if (chunk->cubeWorldGeneration != WorldCurrentGeneration() ||
            !ChunksCubeCoverageContains(chunk->cubeKey)) continue;
        missing += ChunkTargetLodMissingSections(chunk);
    }
    return missing;
}
