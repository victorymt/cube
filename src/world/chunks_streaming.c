#include "world/chunks_internal.h"

static uint64_t nextGenerationQueueSequence = 0u;

bool HasPendingGenJob(void)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) return true;
    }
    return false;
}

ChunkGenJob *NextPendingGenJob(void)
{
    ChunkGenJob *oldest = NULL;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->running || job->done) continue;
        if (!oldest || job->queueSequence < oldest->queueSequence) {
            oldest = job;
        }
    }
    return oldest;
}

void GenerateChunkJobPayload(ChunkGenJob *job)
{
    if (!job) return;
    job->succeeded = false;
    job->hasSectionBlocks = false;

    if (job->scope == CHUNK_GEN_SCOPE_COLUMN) {
        GenerateChunkTerrain(
            &chunks[job->slotIndex], job->cx, job->cz, job->terrainMode);
        job->succeeded = true;
        return;
    }

    Chunk staged = {
        .cx = job->cx,
        .cz = job->cz,
        .spherical = job->spherical,
        .surfaceAddress = job->surfaceAddress
    };
    job->succeeded = GenerateChunkTerrainSectionBase(
        &staged, job->cx, job->cz, job->sectionY, job->terrainMode);
    const ChunkSection *section = ChunkGetSectionConst(
        &staged, job->sectionY);
    /* Keep every non-empty generated section materialized. An implicit
       section cannot be reclassified when a neighbor is unloaded or edited. */
    if (job->succeeded && section) {
        memcpy(job->sectionBlocks, section->blocks,
               sizeof(job->sectionBlocks));
        job->hasSectionBlocks = true;
    }
    ChunkClearBlockStorage(&staged);
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
        bool havePriorityMesh = HasPendingPriorityMeshJob();
        ChunkGenJob *job = NULL;
        MeshJob *meshJob = NULL;
        if (havePriorityMesh ||
            (haveMesh && (preferMesh || !haveGeneration))) {
            meshJob = NextPendingMeshJob();
            preferMesh = false;
        } else {
            job = NextPendingGenJob();
            if (job) preferMesh = true;
        }
        if (job) {
            job->running = true;
            job->startedAtMs = ChunkNowMs();
            genWorkerActive = true;
            pthread_mutex_unlock(&genMutex);

            double startedMs = ChunkNowMs();
            GenerateChunkJobPayload(job);
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            job->running = false;
            job->done = true;
            job->completedAtMs = ChunkNowMs();
            genWorkerActive = false;
            streamingStats.generationCompleted++;
            streamingStats.generationCpuMs += elapsedMs;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        if (meshJob) {
            meshJob->running = true;
            meshJob->startedAtMs = ChunkNowMs();
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
                &meshJob->boundary, &meshJob->mesh);
            meshJob->hasWaterMesh = BuildChunkSurfaceWaterMeshDataWithSnapshot(
                meshJob->blocks,
                (const unsigned char *)meshJob->waterVolumes,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz,
                meshJob->floraStructures, meshJob->floraStructureCount,
                faces,
                meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->boundary,
                &meshJob->waterMesh);
            meshJob->hasFloraMesh = BuildChunkFloraMeshDataFromSnapshot(
                meshJob->blocks,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz,
                meshJob->floraStructures, meshJob->floraStructureCount,
                faces, meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->boundary, &meshJob->floraMesh,
                &meshJob->floraInstances,
                &meshJob->floraInstanceCount);
            if (meshJob->spherical) {
                if (meshJob->hasMesh) {
                    CurveChunkMeshData(
                        &meshJob->mesh, meshJob->cx, meshJob->cz,
                        meshJob->sectionY, meshJob->surfaceAddress.bodyId,
                        meshJob->surfaceMapOriginX,
                        meshJob->surfaceMapOriginZ);
                }
                if (meshJob->hasWaterMesh) {
                    CurveChunkMeshData(
                        &meshJob->waterMesh, meshJob->cx, meshJob->cz,
                        meshJob->sectionY, meshJob->surfaceAddress.bodyId,
                        meshJob->surfaceMapOriginX,
                        meshJob->surfaceMapOriginZ);
                }
                if (meshJob->hasFloraMesh) {
                    CurveChunkMeshData(
                        &meshJob->floraMesh, meshJob->cx, meshJob->cz,
                        meshJob->sectionY, meshJob->surfaceAddress.bodyId,
                        meshJob->surfaceMapOriginX,
                        meshJob->surfaceMapOriginZ);
                }
                CurveChunkFloraInstances(
                    meshJob->floraInstances, meshJob->floraInstanceCount,
                    meshJob->cx, meshJob->cz, meshJob->sectionY,
                    meshJob->surfaceAddress.bodyId,
                    meshJob->surfaceMapOriginX,
                    meshJob->surfaceMapOriginZ);
            } else {
                if (meshJob->hasMesh) {
                    LocalizeChunkMeshData(&meshJob->mesh,
                                          meshJob->cx, meshJob->cz);
                }
                if (meshJob->hasWaterMesh) {
                    LocalizeChunkMeshData(&meshJob->waterMesh,
                                          meshJob->cx, meshJob->cz);
                }
                if (meshJob->hasFloraMesh) {
                    LocalizeChunkMeshData(&meshJob->floraMesh,
                                          meshJob->cx, meshJob->cz);
                }
                LocalizeChunkFloraInstances(
                    meshJob->floraInstances, meshJob->floraInstanceCount,
                    meshJob->cx, meshJob->cz);
            }
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            meshJob->running = false;
            meshJob->done = true;
            meshJob->completedAtMs = ChunkNowMs();
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
        .scope = CHUNK_GEN_SCOPE_COLUMN,
        .cx = cx,
        .cz = cz,
        .slotIndex = (int)(chunk - chunks),
        .chunkGeneration = chunk->generation,
        .spherical = chunk->spherical,
        .surfaceAddress = chunk->surfaceAddress,
        .surfaceKey = chunk->surfaceKey,
        .terrainMode = mode,
        .queueSequence = ++nextGenerationQueueSequence,
        .submittedAtMs = ChunkNowMs()
    };
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

static bool SubmitChunkSectionGenJob(
    Chunk *chunk, int sectionY, TerrainMode mode)
{
    if (genThread == 0 || !chunk || !chunk->loaded ||
        !HomeWorldSurfaceIsActive() || !SurfaceSectionInBounds(sectionY) ||
        ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }

    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = NULL;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse &&
            chunkGenJobs[i].scope == CHUNK_GEN_SCOPE_SECTION &&
            chunkGenJobs[i].slotIndex == (int)(chunk - chunks) &&
            chunkGenJobs[i].chunkGeneration == chunk->generation &&
            chunkGenJobs[i].sectionY == sectionY) {
            pthread_mutex_unlock(&genMutex);
            return false;
        }
        if (!chunkGenJobs[i].inUse && !job) job = &chunkGenJobs[i];
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    *job = (ChunkGenJob){
        .inUse = true,
        .scope = CHUNK_GEN_SCOPE_SECTION,
        .cx = chunk->cx,
        .cz = chunk->cz,
        .sectionY = sectionY,
        .slotIndex = (int)(chunk - chunks),
        .chunkGeneration = chunk->generation,
        .spherical = chunk->spherical,
        .surfaceAddress = chunk->surfaceAddress,
        .surfaceKey = chunk->surfaceKey,
        .terrainMode = mode,
        .queueSequence = ++nextGenerationQueueSequence,
        .submittedAtMs = ChunkNowMs()
    };
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

static bool RequestLoadedChunkTerrainSection(Chunk *chunk, int sectionY)
{
    if (!chunk || ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }
    if (SubmitChunkSectionGenJob(chunk, sectionY, WorldTerrainMode())) {
        return true;
    }
    if (genThread != 0) return false;

    double startedMs = ChunkNowMs();
    bool generated = GenerateChunkTerrainSectionBase(
        chunk, chunk->cx, chunk->cz, sectionY, WorldTerrainMode());
    double elapsedMs = ChunkNowMs() - startedMs;
    if (!generated) return false;
    ApplyEditsToChunkSection(chunk, sectionY);
    WorldNotifyChunkSectionLoaded(chunk, sectionY);
    MarkGeneratedSectionAndNeighborsDirty(chunk, sectionY);
    pthread_mutex_lock(&genMutex);
    streamingStats.generationCompleted++;
    streamingStats.generationCpuMs += elapsedMs;
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool RequestChunkTerrainSection(int cx, int sectionY, int cz)
{
    if (!SurfaceSectionInBounds(sectionY) ||
        !HomeWorldSurfaceIsActive()) {
        return false;
    }
    return RequestLoadedChunkTerrainSection(FindChunk(cx, cz), sectionY);
}

bool FindPendingGenJob(int cx, int cz)
{
    bool spherical = WorldIsSurfaceActive();
    SurfaceChunkKey key = spherical
        ? ChunkSurfaceKeyAt(cx, cz) : (SurfaceChunkKey){ 0 };
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->spherical != spherical) continue;
        if (spherical ? SurfaceChunkKeyEqual(job->surfaceKey, key)
                      : (job->cx == cx && job->cz == cz)) return true;
    }
    return false;
}

void MarkGeneratedSectionAndNeighborsDirty(
    Chunk *chunk, int sectionY)
{
    if (!chunk) return;
    MarkSectionDirty(ChunkGetSection(chunk, sectionY, false));
    MarkSectionDirty(ChunkGetSection(chunk, sectionY - 1, false));
    MarkSectionDirty(ChunkGetSection(chunk, sectionY + 1, false));

    static const int offsets[4][2] = {
        { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 }
    };
    for (int i = 0; i < 4; i++) {
        Chunk *neighbor = FindHorizontalChunkNeighbor(
            chunk->cx, chunk->cz, offsets[i][0], offsets[i][1]);
        if (neighbor) {
            MarkSectionDirty(ChunkGetSection(neighbor, sectionY, false));
        }
    }
}

static void CompleteChunkSectionGenJob(ChunkGenJob *job)
{
    bool stale = !job || !job->succeeded || job->slotIndex < 0 ||
        job->slotIndex >= MAX_ACTIVE_CHUNKS;
    Chunk *chunk = stale ? NULL : &chunks[job->slotIndex];
    if (!stale && (!chunk->loaded || chunk->cx != job->cx ||
                   chunk->cz != job->cz ||
                   chunk->generation != job->chunkGeneration ||
                   chunk->spherical != job->spherical ||
                   (job->spherical && !SurfaceChunkKeyEqual(
                       chunk->surfaceKey, job->surfaceKey)) ||
                   ChunkTerrainSectionIsResolved(chunk, job->sectionY) ||
                   ChunkGetSectionConst(chunk, job->sectionY))) {
        stale = true;
    }
    if (stale) {
        pthread_mutex_lock(&genMutex);
        streamingStats.generationCanceled++;
        pthread_mutex_unlock(&genMutex);
        return;
    }

    if (job->hasSectionBlocks) {
        ChunkSection *section = ChunkGetSection(
            chunk, job->sectionY, true);
        if (!section) {
            pthread_mutex_lock(&genMutex);
            streamingStats.generationCanceled++;
            pthread_mutex_unlock(&genMutex);
            return;
        }
        memcpy(section->blocks, job->sectionBlocks,
               sizeof(job->sectionBlocks));
    }
    if (!ChunkMarkTerrainSectionResolved(chunk, job->sectionY) &&
        !job->hasSectionBlocks) {
        pthread_mutex_lock(&genMutex);
        streamingStats.generationCanceled++;
        pthread_mutex_unlock(&genMutex);
        return;
    }
    ApplyEditsToChunkSection(chunk, job->sectionY);
    WorldNotifyChunkSectionLoaded(chunk, job->sectionY);
    MarkGeneratedSectionAndNeighborsDirty(chunk, job->sectionY);
}

void CompleteChunkGenJob(ChunkGenJob *job)
{
    if (job && job->scope == CHUNK_GEN_SCOPE_SECTION) {
        CompleteChunkSectionGenJob(job);
        return;
    }
    bool stale = !job || !job->succeeded || job->slotIndex < 0 ||
        job->slotIndex >= MAX_ACTIVE_CHUNKS;
    Chunk *chunk = stale ? NULL : &chunks[job->slotIndex];
    if (!stale && (!chunk->generating || chunk->loaded ||
                   chunk->cx != job->cx || chunk->cz != job->cz ||
                   chunk->generation != job->chunkGeneration ||
                   chunk->spherical != job->spherical ||
                   (job->spherical && !SurfaceChunkKeyEqual(
                       chunk->surfaceKey, job->surfaceKey)))) {
        stale = true;
    }
    if (stale) {
        pthread_mutex_lock(&genMutex);
        streamingStats.generationCanceled++;
        pthread_mutex_unlock(&genMutex);
        return;
    }
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    WorldNotifyChunkLoaded(chunk);
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

        int distance = ChunkGridDistanceFrom(&chunks[i], nearCx, nearCz);
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
    bool spherical = WorldIsSurfaceActive();
    if (spherical) CanonicalizeSurfaceChunkCoordinates(&cx, &cz);
    SurfaceAddress surfaceAddress = spherical
        ? ChunkSurfaceAddressAt(cx, cz) : (SurfaceAddress){ 0 };
    SurfaceChunkKey surfaceKey = spherical
        ? ChunkSurfaceKeyAt(cx, cz) : (SurfaceChunkKey){ 0 };
    if ((spherical ? FindSurfaceChunk(surfaceKey) : FindChunk(cx, cz)) ||
        FindPendingGenJob(cx, cz)) return false;

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
    chunk->spherical = spherical;
    chunk->surfaceAddress = surfaceAddress;
    chunk->surfaceKey = surfaceKey;
    // New incarnation: invalidates any in-flight mesh jobs captured against
    // the previous occupant of this slot (stale terrain upload guard).
    chunk->generation++;
    if (chunk->generation == 0u) chunk->generation = 1u;
    chunk->generating = true;
    chunk->loaded = false;
    chunk->floraActivity = 1.0f;
    chunk->floraCapacity = 1.0f;
    chunk->floraSampleTimer = 0.0f;

    if (SubmitChunkGenJob(chunk, cx, cz, WorldTerrainMode())) return true;

    double startedMs = ChunkNowMs();
    GenerateChunkTerrain(chunk, cx, cz, WorldTerrainMode());
    double elapsedMs = ChunkNowMs() - startedMs;
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    WorldNotifyChunkLoaded(chunk);
    pthread_mutex_lock(&genMutex);
    streamingStats.generationCompleted++;
    streamingStats.generationCpuMs += elapsedMs;
    pthread_mutex_unlock(&genMutex);
    MarkChunkAndHorizontalNeighborsDirty(cx, cz);
    return true;
}

int ScheduleNearbyTerrainSections(Vector3 playerPosition,
                                  int effectiveRenderDistance)
{
    if (!HomeWorldSurfaceIsActive()) return 0;
    if (effectiveRenderDistance < MIN_RENDER_DISTANCE_CHUNKS) {
        effectiveRenderDistance = MIN_RENDER_DISTANCE_CHUNKS;
    }
    if (effectiveRenderDistance > MAX_RENDER_DISTANCE_CHUNKS) {
        effectiveRenderDistance = MAX_RENDER_DISTANCE_CHUNKS;
    }
    int playerY = (int)floorf(playerPosition.y);
    if (!InHeight(playerY)) return 0;

    int playerCx = 0;
    int playerCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x),
                      (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &localX, &localZ);
    int playerSectionY = SurfaceSectionYFromBlockY(playerY);
    static const int verticalOffsets[3] = { 0, -1, 1 };

    int submissions = 0;
    for (int distance = 0; distance <= effectiveRenderDistance; distance++) {
        for (int vertical = 0;
             vertical < (int)(sizeof(verticalOffsets) /
                              sizeof(verticalOffsets[0])); vertical++) {
            int sectionY = playerSectionY + verticalOffsets[vertical];
            if (!SurfaceSectionInBounds(sectionY)) continue;
            for (int dz = -distance; dz <= distance; dz++) {
                for (int dx = -distance; dx <= distance; dx++) {
                    if (abs(dx) != distance && abs(dz) != distance) {
                        continue;
                    }
                    Chunk *chunk = FindChunk(playerCx + dx,
                                             playerCz + dz);
                    if (RequestLoadedChunkTerrainSection(chunk, sectionY)) {
                        submissions++;
                        if (submissions >=
                            SECTION_GEN_SUBMISSIONS_PER_FRAME) {
                            return submissions;
                        }
                    }
                }
            }
        }
    }
    return submissions;
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
        if (ChunkGridDistanceFrom(&chunks[i], playerCx, playerCz) >
            effectiveRenderDistance) {
            MarkChunkAndHorizontalNeighborsDirty(chunks[i].cx, chunks[i].cz);
            ChunkClearBlockStorage(&chunks[i]);
            chunks[i].loaded = false;
        }
    }

    int playerY = (int)floorf(playerPosition.y);
    if (InHeight(playerY)) {
        int playerSectionY = SurfaceSectionYFromBlockY(playerY);
        CancelDistantNegativeSectionJobs(playerSectionY);
        PruneDistantNegativeTerrainSections(playerSectionY);
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
    ScheduleNearbyTerrainSections(playerPosition, effectiveRenderDistance);
}

#ifdef CHUNKS_TESTING
bool ChunksTestEnsureChunk(int cx, int cz)
{
    return EnsureChunk(cx, cz);
}

bool ChunksTestFindPendingGenerationJob(int cx, int cz)
{
    return FindPendingGenJob(cx, cz);
}

void ChunksTestSeedGenerationJob(int jobIndex, int cx, int cz,
                                 int sectionY, bool done)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    chunkGenJobs[jobIndex] = (ChunkGenJob){
        .inUse = true,
        .done = done,
        .scope = CHUNK_GEN_SCOPE_SECTION,
        .cx = cx,
        .cz = cz,
        .sectionY = sectionY,
        .queueSequence = ++nextGenerationQueueSequence,
        .submittedAtMs = ChunkNowMs(),
        .completedAtMs = done ? ChunkNowMs() : 0.0
    };
    pthread_mutex_unlock(&genMutex);
}

int ChunksTestNextGenerationJobIndex(void)
{
    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = NextPendingGenJob();
    int index = job ? (int)(job - chunkGenJobs) : -1;
    pthread_mutex_unlock(&genMutex);
    return index;
}
#endif
