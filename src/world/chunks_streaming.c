#include "world/chunks_internal.h"

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

    Chunk staged = { .cx = job->cx, .cz = job->cz };
    job->succeeded = GenerateChunkTerrainSectionBase(
        &staged, job->cx, job->cz, job->sectionY, job->terrainMode);
    const ChunkSection *section = ChunkGetSectionConst(
        &staged, job->sectionY);
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
            job->running = true;
            genWorkerActive = true;
            pthread_mutex_unlock(&genMutex);

            double startedMs = ChunkNowMs();
            GenerateChunkJobPayload(job);
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            job->running = false;
            job->done = true;
            genWorkerActive = false;
            streamingStats.generationCompleted++;
            streamingStats.generationCpuMs += elapsedMs;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        if (meshJob) {
            meshJob->running = true;
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
            meshJob->hasWaterMesh = BuildSurfaceWaterMeshDataWithSnapshot(
                (const unsigned short (*)[CHUNK_SIZE])meshJob->blocks,
                (const unsigned char *)meshJob->waterVolumes,
                SURFACE_SECTION_HEIGHT,
                meshJob->sectionY * SURFACE_SECTION_HEIGHT,
                meshJob->cx, meshJob->cz, faces,
                meshJob->nearbyIndices, meshJob->nearbyCount,
                &meshJob->waterBoundary,
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
            meshJob->running = false;
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
        .scope = CHUNK_GEN_SCOPE_COLUMN,
        .cx = cx,
        .cz = cz,
        .slotIndex = (int)(chunk - chunks),
        .chunkGeneration = chunk->generation,
        .terrainMode = mode
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
        .terrainMode = mode
    };
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool RequestChunkTerrainSection(int cx, int sectionY, int cz)
{
    if (!SurfaceSectionInBounds(sectionY) ||
        !HomeWorldSurfaceIsActive()) {
        return false;
    }
    Chunk *chunk = FindChunk(cx, cz);
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
        chunk, cx, cz, sectionY, WorldTerrainMode());
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

bool FindPendingGenJob(int cx, int cz)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && chunkGenJobs[i].cx == cx && chunkGenJobs[i].cz == cz) return true;
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
        Chunk *neighbor = FindChunk(
            chunk->cx + offsets[i][0], chunk->cz + offsets[i][1]);
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
    Chunk *chunk = &chunks[job->slotIndex];
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

int ScheduleNearbyTerrainSections(Vector3 playerPosition)
{
    if (!HomeWorldSurfaceIsActive()) return 0;
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
    static const int horizontalOffsets[9][2] = {
        { 0, 0 }, { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 },
        { -1, -1 }, { 1, -1 }, { -1, 1 }, { 1, 1 }
    };
    static const int verticalOffsets[3] = { 0, -1, 1 };

    int submissions = 0;
    for (int vertical = 0;
         vertical < (int)(sizeof(verticalOffsets) /
                          sizeof(verticalOffsets[0])); vertical++) {
        int sectionY = playerSectionY + verticalOffsets[vertical];
        if (!SurfaceSectionInBounds(sectionY)) continue;
        for (int horizontal = 0;
             horizontal < (int)(sizeof(horizontalOffsets) /
                                sizeof(horizontalOffsets[0])); horizontal++) {
            int cx = playerCx + horizontalOffsets[horizontal][0];
            int cz = playerCz + horizontalOffsets[horizontal][1];
            if (RequestChunkTerrainSection(cx, sectionY, cz)) {
                submissions++;
                if (submissions >= SECTION_GEN_SUBMISSIONS_PER_FRAME) {
                    return submissions;
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
        if (abs(chunks[i].cx - playerCx) > effectiveRenderDistance ||
            abs(chunks[i].cz - playerCz) > effectiveRenderDistance) {
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
    ScheduleNearbyTerrainSections(playerPosition);
}
