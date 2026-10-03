#include "world/chunks_internal.h"

static uint64_t nextGenerationQueueSequence = 0u;
static int requestedChunkWorkerCount = 0;

#ifdef CHUNKS_TESTING
static bool failNextColumnAllocation = false;
#endif

static bool ChunkInStreamingRegion(
    const Chunk *chunk, int centerCx, int centerCz, int radius);

static void QueueGenerationDoneLocked(ChunkGenJob *job)
{
    if (!job || job->completionQueued) return;
    int slotIndex = (int)(job - chunkGenJobs);
    if (ChunkTaskQueuePush(&generationDoneQueue, 0, slotIndex,
                           job->queueSequence)) {
        job->completionQueued = true;
    }
}

static bool TakeCompletedGenerationJob(ChunkGenJob *out)
{
    if (!out) return false;
    int slotIndex = -1;
    uint64_t sequence = 0u;
    while (ChunkTaskQueuePop(
        &generationDoneQueue, &slotIndex, &sequence)) {
        if (slotIndex < 0 || slotIndex >= MAX_CHUNK_GEN_JOBS) continue;
        ChunkGenJob *job = &chunkGenJobs[slotIndex];
        if (!job->inUse || !job->done ||
            job->queueSequence != sequence) continue;
        job->completionQueued = false;
        *out = *job;
        *job = (ChunkGenJob){ 0 };
        return true;
    }
    return false;
}

static bool ChunkStreamingCoordinateIsValid(float value)
{
    return isfinite(value) && (double)value >= (double)INT_MIN &&
           (double)value < (double)INT_MAX;
}

bool ChunkStreamingPositionIsValid(Vector3 position)
{
    return ChunkStreamingCoordinateIsValid(position.x) &&
           ChunkStreamingCoordinateIsValid(position.y) &&
           ChunkStreamingCoordinateIsValid(position.z);
}

bool HasPendingGenJob(void)
{
    return ChunkTaskQueueHasPending(&generationTaskQueue);
}

static int ChunkGenQueueLane(const ChunkGenJob *job)
{
    return job && job->preload ? 3 : 0;
}

static ChunkGenJob *TakePendingGenJob(void)
{
    int slotIndex = -1;
    uint64_t sequence = 0u;
    while (ChunkTaskQueuePop(
        &generationTaskQueue, &slotIndex, &sequence)) {
        if (slotIndex < 0 || slotIndex >= MAX_CHUNK_GEN_JOBS) continue;
        ChunkGenJob *job = &chunkGenJobs[slotIndex];
        if (!job->inUse || job->running || job->done ||
            job->queueSequence != sequence) continue;
        job->queued = false;
        return job;
    }
    return NULL;
}

ChunkGenJob *NextPendingGenJob(void)
{
    int slotIndex = -1;
    uint64_t sequence = 0u;
    while (ChunkTaskQueuePeek(
        &generationTaskQueue, &slotIndex, &sequence)) {
        if (slotIndex >= 0 && slotIndex < MAX_CHUNK_GEN_JOBS) {
            ChunkGenJob *job = &chunkGenJobs[slotIndex];
            if (job->inUse && !job->running && !job->done &&
                job->queueSequence == sequence) {
                return job;
            }
        }
        if (!ChunkTaskQueuePop(
                &generationTaskQueue, &slotIndex, &sequence)) break;
    }
    return NULL;
}

void GenerateChunkJobPayload(ChunkGenJob *job, bool preload)
{
    if (!job) return;
    FreeChunkGenJobResult(job);
    job->succeeded = false;
    job->hasSectionBlocks = false;

    if (job->scope == CHUNK_GEN_SCOPE_COLUMN) {
#ifdef CHUNKS_TESTING
        if (failNextColumnAllocation) {
            failNextColumnAllocation = false;
            return;
        }
#endif
        Chunk *staged = calloc(1, sizeof(*staged));
        if (!staged) return;
        *staged = (Chunk){
            .generating = true,
            .preloaded = preload,
            .cx = job->cx,
            .cz = job->cz,
            .spherical = job->spherical,
            .surfaceAddress = job->surfaceAddress,
            .surfaceKey = job->surfaceKey,
            .generation = job->chunkGeneration,
            .floraActivity = 1.0f,
            .floraCapacity = 1.0f
        };
        GenerateChunkTerrain(staged, job->cx, job->cz, job->terrainMode);
        job->columnResult = staged;
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

void FreeChunkGenJobResult(ChunkGenJob *job)
{
    if (!job || !job->columnResult) return;
    ChunkClearBlockStorage(job->columnResult);
    free(job->columnResult);
    job->columnResult = NULL;
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
            meshJob = TakePendingMeshJob();
            preferMesh = false;
        } else {
            job = TakePendingGenJob();
            if (job) preferMesh = true;
        }
        if (job) {
            bool preload = job->preload;
            job->running = true;
            job->startedAtMs = ChunkNowMs();
            genWorkerThreadsActive++;
            pthread_mutex_unlock(&genMutex);

            double startedMs = ChunkNowMs();
            GenerateChunkJobPayload(job, preload);
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            job->running = false;
            job->done = true;
            job->completedAtMs = ChunkNowMs();
            if (genWorkerThreadsActive > 0u) genWorkerThreadsActive--;
            streamingStats.generationCompleted++;
            QueueGenerationDoneLocked(job);
            streamingStats.generationCpuMs += elapsedMs;
            pthread_cond_signal(&genCompletionCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        if (meshJob) {
            meshJob->running = true;
            meshJob->startedAtMs = ChunkNowMs();
            genWorkerThreadsActive++;
            pthread_mutex_unlock(&genMutex);

            double startedMs = ChunkNowMs();
            ChunkMeshWorkerScratchBegin();
            BuildMeshJobPayload(meshJob);
            ChunkMeshWorkerScratchEnd();
            double elapsedMs = ChunkNowMs() - startedMs;

            pthread_mutex_lock(&genMutex);
            meshJob->running = false;
            meshJob->done = true;
            meshJob->completedAtMs = ChunkNowMs();
            if (genWorkerThreadsActive > 0u) genWorkerThreadsActive--;
            streamingStats.meshCompleted++;
            if (!meshJob->completionQueued &&
                ChunkTaskQueuePush(&meshDoneQueue, MeshQueueLane(meshJob),
                                   (int)(meshJob - meshJobs),
                                   meshJob->queueSequence)) {
                meshJob->completionQueued = true;
            }
            streamingStats.meshCpuMs += elapsedMs;
            pthread_cond_signal(&genCompletionCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        pthread_mutex_unlock(&genMutex);
    }

    ChunkMeshWorkerScratchShutdown();
    return NULL;
}

static int ChunkWorkerCountForCpu(int onlineCpus, int requestedWorkers)
{
    if (requestedWorkers > 0) {
        return requestedWorkers > MAX_CHUNK_WORKER_THREADS
            ? MAX_CHUNK_WORKER_THREADS : requestedWorkers;
    }
    if (onlineCpus <= 2) return 1;
    int workers = onlineCpus / 2;
    if (workers < 2) workers = 2;
    if (workers > MAX_CHUNK_WORKER_THREADS) {
        workers = MAX_CHUNK_WORKER_THREADS;
    }
    return workers;
}

bool ChunksConfigureWorkerCount(int requestedWorkers)
{
    if (requestedWorkers < 0 ||
        requestedWorkers > MAX_CHUNK_WORKER_THREADS) return false;
    pthread_mutex_lock(&genMutex);
    bool configurable = genWorkerThreadsStarted == 0u;
    if (configurable) requestedChunkWorkerCount = requestedWorkers;
    pthread_mutex_unlock(&genMutex);
    return configurable;
}

int ChunksConfiguredWorkerCount(void)
{
    pthread_mutex_lock(&genMutex);
    int result = (int)genWorkerThreadsConfigured;
    pthread_mutex_unlock(&genMutex);
    return result;
}

int ChunksStartedWorkerCount(void)
{
    pthread_mutex_lock(&genMutex);
    int result = (int)genWorkerThreadsStarted;
    pthread_mutex_unlock(&genMutex);
    return result;
}

int ChunksActiveWorkerCount(void)
{
    pthread_mutex_lock(&genMutex);
    int result = (int)genWorkerThreadsActive;
    pthread_mutex_unlock(&genMutex);
    return result;
}

bool ChunksStartGenThread(void)
{
    pthread_mutex_lock(&genMutex);
    if (genWorkerThreadsStarted != 0u) {
        pthread_mutex_unlock(&genMutex);
        return true;
    }
    long detected = sysconf(_SC_NPROCESSORS_ONLN);
    int onlineCpus = detected > 0 && detected <= INT_MAX ? (int)detected : 1;
    int target = ChunkWorkerCountForCpu(
        onlineCpus, requestedChunkWorkerCount);
    genShutdown = false;
    genWorkerThreadsConfigured = (unsigned int)target;
    genWorkerThreadsActive = 0u;
    pthread_mutex_unlock(&genMutex);

    for (int index = 0; index < target; index++) {
        if (pthread_create(&genThreads[index], NULL,
                           ChunkGenWorker, NULL) != 0) break;
        pthread_mutex_lock(&genMutex);
        genWorkerThreadsStarted++;
        pthread_mutex_unlock(&genMutex);
    }
    return ChunksStartedWorkerCount() > 0;
}

void ChunksShutdownGenThread(void)
{
    pthread_mutex_lock(&genMutex);
    unsigned int started = genWorkerThreadsStarted;
    if (started == 0u) {
        for (int index = 0; index < MAX_CHUNK_GEN_JOBS; index++) {
            if (!chunkGenJobs[index].inUse) continue;
            chunkGenJobs[index].running = false;
            chunkGenJobs[index].done = true;
            chunkGenJobs[index].succeeded = false;
            QueueGenerationDoneLocked(&chunkGenJobs[index]);
        }
    }
    pthread_mutex_unlock(&genMutex);
    if (started > 0u) DrainChunkGen();
    else ProcessFinishedChunkJobs();

    pthread_mutex_lock(&genMutex);
    if (started > 0u) {
        genShutdown = true;
        pthread_cond_broadcast(&genCond);
    }
    pthread_mutex_unlock(&genMutex);
    for (unsigned int index = 0u; index < started; index++) {
        pthread_join(genThreads[index], NULL);
    }
    if (started > 0u) {
        pthread_mutex_lock(&genMutex);
        memset(genThreads, 0, sizeof(genThreads));
        genWorkerThreadsStarted = 0u;
        genWorkerThreadsActive = 0u;
        pthread_mutex_unlock(&genMutex);
    }
    for (int index = 0; index < MAX_CHUNK_GEN_JOBS; index++) {
        FreeChunkGenJobResult(&chunkGenJobs[index]);
    }
    for (int index = 0; index < MAX_MESH_JOBS; index++) {
        if (meshJobs[index].inUse) {
            FreeMeshData(&meshJobs[index].mesh);
            FreeMeshData(&meshJobs[index].waterMesh);
            FreeMeshData(&meshJobs[index].floraMesh);
            FreeMeshData(&meshJobs[index].lodFloraMesh);
            free(meshJobs[index].floraInstances);
            meshJobs[index].floraInstances = NULL;
            meshJobs[index].floraInstanceCount = 0;
            meshJobs[index].inUse = false;
        }
    }
    pthread_mutex_lock(&genMutex);
    ChunkTaskQueueReset(&generationTaskQueue);
    ChunkTaskQueueReset(&generationDoneQueue);
    ChunkTaskQueueReset(&meshTaskQueue);
    ChunkTaskQueueReset(&meshDoneQueue);
    pthread_mutex_unlock(&genMutex);
}

bool ChunksRestartGenThreads(int requestedWorkers)
{
    if (requestedWorkers < 0 ||
        requestedWorkers > MAX_CHUNK_WORKER_THREADS) return false;

    ChunksShutdownGenThread();
    if (!ChunksConfigureWorkerCount(requestedWorkers)) return false;
    if (!ChunksStartGenThread()) return false;
    if (ChunksStartedWorkerCount() == ChunksConfiguredWorkerCount()) {
        return true;
    }
    ChunksShutdownGenThread();
    return false;
}

static bool SubmitChunkGenJob(
    Chunk *chunk, int cx, int cz, TerrainMode mode, bool preload)
{
    if (genWorkerThreadsStarted == 0u) {
        ChunkDiagnosticsRecordReason("generation_workers_unavailable");
        return false;
    }

    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = NULL;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (!chunkGenJobs[i].inUse) {
            job = &chunkGenJobs[i];
            break;
        }
    }
    if (!job) {
        ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    *job = (ChunkGenJob){
        .inUse = true,
        .done = false,
        .preload = preload,
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
    job->queued = ChunkTaskQueuePush(
        &generationTaskQueue, ChunkGenQueueLane(job),
        (int)(job - chunkGenJobs), job->queueSequence);
    if (!job->queued) {
        *job = (ChunkGenJob){ 0 };
        ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        pthread_mutex_unlock(&genMutex);
        return false;
    }
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    ChunkDiagnosticsRecordReasonLocked("generation_pending");
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

static bool SubmitChunkSectionGenJob(
    Chunk *chunk, int sectionY, TerrainMode mode, bool preload)
{
    if (!chunk || !chunk->loaded ||
        WorldCurrentSurfaceContext().kind != WORLD_SURFACE_HOME ||
        !SurfaceSectionInBounds(sectionY) ||
        ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }
    if (genWorkerThreadsStarted == 0u) {
        ChunkDiagnosticsRecordReason("generation_workers_unavailable");
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
            if (!preload) chunkGenJobs[i].preload = false;
            ChunkDiagnosticsRecordReasonLocked("generation_pending");
            pthread_mutex_unlock(&genMutex);
            return false;
        }
        if (!chunkGenJobs[i].inUse && !job) job = &chunkGenJobs[i];
    }
    if (!job) {
        ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    *job = (ChunkGenJob){
        .inUse = true,
        .preload = preload,
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
    job->queued = ChunkTaskQueuePush(
        &generationTaskQueue, ChunkGenQueueLane(job),
        (int)(job - chunkGenJobs), job->queueSequence);
    if (!job->queued) {
        *job = (ChunkGenJob){ 0 };
        ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        pthread_mutex_unlock(&genMutex);
        return false;
    }
    streamingStats.generationSubmitted++;
    UpdateQueuePeaksLocked();
    ChunkDiagnosticsRecordReasonLocked("generation_pending");
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

static bool RequestLoadedChunkTerrainSection(
    Chunk *chunk, int sectionY, bool preload)
{
    if (!chunk || ChunkTerrainSectionIsResolved(chunk, sectionY) ||
        ChunkGetSectionConst(chunk, sectionY)) {
        return false;
    }
    if (SubmitChunkSectionGenJob(
            chunk, sectionY, WorldTerrainMode(), preload)) {
        return true;
    }
    if (genWorkerThreadsStarted != 0u) return false;

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
        WorldCurrentSurfaceContext().kind != WORLD_SURFACE_HOME) {
        return false;
    }
    return RequestLoadedChunkTerrainSection(
        FindChunk(cx, cz), sectionY, false);
}

static bool FindPendingGenJob(int cx, int cz, bool promote)
{
    bool spherical = WorldIsSurfaceActive();
    SurfaceChunkKey key = spherical
        ? ChunkSurfaceKeyAt(cx, cz) : (SurfaceChunkKey){ 0 };
    bool found = false;
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->scope != CHUNK_GEN_SCOPE_COLUMN ||
            job->spherical != spherical) {
            continue;
        }
        if (spherical ? SurfaceChunkKeyEqual(job->surfaceKey, key)
                      : (job->cx == cx && job->cz == cz)) {
            if (promote && !job->running) {
                if (job->queued) {
                    ChunkTaskQueueRemove(
                        &generationTaskQueue, i, job->queueSequence);
                    job->queued = false;
                }
                job->preload = false;
                job->queued = ChunkTaskQueuePushOrdered(
                    &generationTaskQueue, ChunkGenQueueLane(job), i,
                    job->queueSequence);
            }
            found = true;
            ChunkDiagnosticsRecordReasonLocked("generation_pending");
            break;
        }
    }
    pthread_mutex_unlock(&genMutex);
    return found;
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
    bool validSlot = job && job->slotIndex >= 0 &&
        job->slotIndex < MAX_ACTIVE_CHUNKS;
    Chunk *chunk = validSlot ? &chunks[job->slotIndex] : NULL;
    bool targetMatches = chunk && chunk->generating && !chunk->loaded &&
        chunk->cx == job->cx && chunk->cz == job->cz &&
        chunk->generation == job->chunkGeneration &&
        chunk->spherical == job->spherical &&
        (!job->spherical || SurfaceChunkKeyEqual(
            chunk->surfaceKey, job->surfaceKey));
    bool stale = !job || !job->succeeded || !job->columnResult ||
        !targetMatches;
    if (stale) {
        if (targetMatches && (!job->succeeded || !job->columnResult)) {
            chunk->generating = false;
        }
        FreeChunkGenJobResult(job);
        pthread_mutex_lock(&genMutex);
        streamingStats.generationCanceled++;
        pthread_mutex_unlock(&genMutex);
        return;
    }
    uint32_t generation = chunk->generation;
    Chunk generated = *job->columnResult;
    free(job->columnResult);
    job->columnResult = NULL;
    ChunkClearBlockStorage(chunk);
    *chunk = generated;
    chunk->generation = generation;
    chunk->preloaded = !ChunkInStreamingRegion(
        chunk, chunkStreamingFocus.currentCx, chunkStreamingFocus.currentCz,
        chunkStreamingFocus.renderDistance);
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    ChunkIndexInsert(chunk);
    WorldNotifyChunkLoaded(chunk);
    MarkChunkAndHorizontalNeighborsDirty(chunk->cx, chunk->cz);
}

void ProcessFinishedChunkJobs(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        ChunkGenJob completed = { 0 };
        bool haveJob = TakeCompletedGenerationJob(&completed);
        pthread_mutex_unlock(&genMutex);

        if (!haveJob) return;
        CompleteChunkGenJob(&completed);
    }
}

void DrainChunkGen(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        for (;;) {
            ChunkGenJob completed = { 0 };
            bool haveJob = TakeCompletedGenerationJob(&completed);
            if (!haveJob) break;
            pthread_mutex_unlock(&genMutex);
            CompleteChunkGenJob(&completed);
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
        pthread_cond_wait(&genCompletionCond, &genMutex);
        pthread_mutex_unlock(&genMutex);
    }
}

Chunk *AllocateChunkSlot(int nearCx, int nearCz)
{
    int bestIndex = -1;
    int bestDistance = -1;
    int bestPreloadIndex = -1;
    int bestPreloadDistance = -1;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded && !chunks[i].generating) return &chunks[i];

        int distance = ChunkGridDistanceFrom(&chunks[i], nearCx, nearCz);
        if (!chunks[i].generating && chunks[i].preloaded &&
            distance > bestPreloadDistance) {
            bestPreloadDistance = distance;
            bestPreloadIndex = i;
        }
        if (!chunks[i].generating && distance > bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    int selected = bestPreloadIndex >= 0 ? bestPreloadIndex : bestIndex;
    if (selected >= 0) {
        MarkChunkAndHorizontalNeighborsDirty(
            chunks[selected].cx, chunks[selected].cz);
    }
    return selected >= 0 ? &chunks[selected] : NULL;
}

static bool EnsureChunk(int cx, int cz, bool preload)
{
    bool spherical = WorldIsSurfaceActive();
    if (spherical) CanonicalizeSurfaceChunkCoordinates(&cx, &cz);
    SurfaceAddress surfaceAddress = spherical
        ? ChunkSurfaceAddressAt(cx, cz) : (SurfaceAddress){ 0 };
    SurfaceChunkKey surfaceKey = spherical
        ? ChunkSurfaceKeyAt(cx, cz) : (SurfaceChunkKey){ 0 };
    Chunk *existing = spherical
        ? FindSurfaceChunk(surfaceKey) : FindChunk(cx, cz);
    if (existing) {
        if (!preload) existing->preloaded = false;
        return false;
    }
    if (FindPendingGenJob(cx, cz, !preload)) return false;

    if (genWorkerThreadsStarted != 0u) {
        bool haveQueueSlot = false;
        pthread_mutex_lock(&genMutex);
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (!chunkGenJobs[i].inUse) {
                haveQueueSlot = true;
                break;
            }
        }
        if (!haveQueueSlot) {
            ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        }
        pthread_mutex_unlock(&genMutex);
        if (!haveQueueSlot) return false;
    }

    Chunk *chunk = AllocateChunkSlot(cx, cz);
    if (!chunk) return false;
    ChunkIndexRemove(chunk);
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
    chunk->preloaded = preload;
    chunk->targetLod = CHUNK_LOD_EXACT;
    chunk->activeLod = CHUNK_LOD_EXACT;
    chunk->lodInitialized = false;
    chunk->floraActivity = 1.0f;
    chunk->floraCapacity = 1.0f;
    chunk->floraSampleTimer = 0.0f;

    if (SubmitChunkGenJob(
            chunk, cx, cz, WorldTerrainMode(), preload)) return true;

    double startedMs = ChunkNowMs();
    GenerateChunkTerrain(chunk, cx, cz, WorldTerrainMode());
    double elapsedMs = ChunkNowMs() - startedMs;
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    ChunkIndexInsert(chunk);
    WorldNotifyChunkLoaded(chunk);
    pthread_mutex_lock(&genMutex);
    streamingStats.generationCompleted++;
    streamingStats.generationCpuMs += elapsedMs;
    pthread_mutex_unlock(&genMutex);
    MarkChunkAndHorizontalNeighborsDirty(cx, cz);
    return true;
}

static int StreamingRegionUnionCount(int radius, int offsetX, int offsetZ)
{
    int side = radius * 2 + 1;
    int overlapX = side - abs(offsetX);
    int overlapZ = side - abs(offsetZ);
    if (overlapX < 0) overlapX = 0;
    if (overlapZ < 0) overlapZ = 0;
    return side * side * 2 - overlapX * overlapZ;
}

static void ReduceStreamingOffsetToCapacity(
    int radius, int *offsetX, int *offsetZ)
{
    while (StreamingRegionUnionCount(radius, *offsetX, *offsetZ) >
           MAX_ACTIVE_CHUNKS) {
        int x = abs(*offsetX);
        int z = abs(*offsetZ);
        if (x >= z && x > 0) *offsetX += *offsetX > 0 ? -1 : 1;
        if (z >= x && z > 0) *offsetZ += *offsetZ > 0 ? -1 : 1;
    }
}

static ChunkStreamingFocus StreamingFocusForRequest(
    const ChunkStreamingRequest *request)
{
    ChunkStreamingFocus focus = { 0 };
    if (!request || !ChunkStreamingPositionIsValid(request->position)) {
        return focus;
    }
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(request->position.x),
                      (int)floorf(request->position.z),
                      &focus.currentCx, &focus.currentCz,
                      &localX, &localZ);
    focus.predictedCx = focus.currentCx;
    focus.predictedCz = focus.currentCz;
    focus.renderDistance = request->effectiveRenderDistance;
    if (focus.renderDistance < MIN_RENDER_DISTANCE_CHUNKS) {
        focus.renderDistance = MIN_RENDER_DISTANCE_CHUNKS;
    }
    if (focus.renderDistance > MAX_RENDER_DISTANCE_CHUNKS) {
        focus.renderDistance = MAX_RENDER_DISTANCE_CHUNKS;
    }

    int maxPreload = request->maxPreloadChunks;
    if (maxPreload < 0) maxPreload = 0;
    if (maxPreload > CHUNK_PRELOAD_SHIP_MAX_DISTANCE) {
        maxPreload = CHUNK_PRELOAD_SHIP_MAX_DISTANCE;
    }
    float velocityX = request->velocity.x;
    float velocityZ = request->velocity.z;
    float speed = sqrtf(velocityX * velocityX + velocityZ * velocityZ);
    pthread_mutex_lock(&genMutex);
    bool workersStarted = genWorkerThreadsStarted != 0u;
    pthread_mutex_unlock(&genMutex);
    if (!workersStarted || maxPreload == 0 ||
        !isfinite(velocityX) || !isfinite(velocityZ) || !isfinite(speed) ||
        speed < CHUNK_PRELOAD_MIN_SPEED) {
        return focus;
    }

    float projectedX = request->position.x +
        velocityX * CHUNK_PRELOAD_LOOKAHEAD_SECONDS;
    float projectedZ = request->position.z +
        velocityZ * CHUNK_PRELOAD_LOOKAHEAD_SECONDS;
    if (!isfinite(projectedX) || !isfinite(projectedZ) ||
        !ChunkStreamingCoordinateIsValid(projectedX) ||
        !ChunkStreamingCoordinateIsValid(projectedZ)) {
        return focus;
    }

    int projectedCx = 0;
    int projectedCz = 0;
    WorldToChunkLocal((int)floorf(projectedX), (int)floorf(projectedZ),
                      &projectedCx, &projectedCz, &localX, &localZ);
    int offsetX = projectedCx - focus.currentCx;
    int offsetZ = projectedCz - focus.currentCz;
    if (WorldIsSurfaceActive()) {
        float originX = (float)WorldSurfaceMapOriginX();
        float originZ = (float)WorldSurfaceMapOriginZ();
        float half = (float)CHUNK_SIZE * 0.5f;
        SurfaceMapOffset mapOffset = SurfaceShortestMapOffset(
            originX + (float)focus.currentCx * (float)CHUNK_SIZE + half,
            originZ + (float)focus.currentCz * (float)CHUNK_SIZE + half,
            originX + (float)projectedCx * (float)CHUNK_SIZE + half,
            originZ + (float)projectedCz * (float)CHUNK_SIZE + half);
        offsetX = (int)lroundf(mapOffset.x / (float)CHUNK_SIZE);
        offsetZ = (int)lroundf(mapOffset.z / (float)CHUNK_SIZE);
    }
    if (offsetX < -maxPreload) offsetX = -maxPreload;
    if (offsetX > maxPreload) offsetX = maxPreload;
    if (offsetZ < -maxPreload) offsetZ = -maxPreload;
    if (offsetZ > maxPreload) offsetZ = maxPreload;
    ReduceStreamingOffsetToCapacity(
        focus.renderDistance, &offsetX, &offsetZ);
    focus.predictedCx += offsetX;
    focus.predictedCz += offsetZ;
    int side = focus.renderDistance * 2 + 1;
    focus.preloadTargetCount = StreamingRegionUnionCount(
        focus.renderDistance, offsetX, offsetZ) - side * side;
    return focus;
}

static bool ChunkInStreamingRegion(
    const Chunk *chunk, int centerCx, int centerCz, int radius)
{
    return chunk &&
        ChunkGridDistanceFrom(chunk, centerCx, centerCz) <= radius;
}

static bool MeshJobTargetMatchesChunk(const MeshJob *job, const Chunk *chunk)
{
    return job && chunk && chunk->loaded && chunk->cx == job->cx &&
           chunk->cz == job->cz &&
           chunk->generation == job->chunkGeneration &&
           chunk->spherical == job->spherical &&
           (!job->spherical ||
            SurfaceChunkKeyEqual(chunk->surfaceKey, job->surfaceKey));
}

static bool ChunkGenJobTargetMatchesChunk(
    const ChunkGenJob *job, const Chunk *chunk)
{
    if (!job || !chunk || job->cx != chunk->cx || job->cz != chunk->cz ||
        job->chunkGeneration != chunk->generation ||
        job->spherical != chunk->spherical ||
        (job->spherical &&
         !SurfaceChunkKeyEqual(chunk->surfaceKey, job->surfaceKey))) {
        return false;
    }
    if (job->scope == CHUNK_GEN_SCOPE_COLUMN) {
        return chunk->generating && !chunk->loaded;
    }
    if (job->scope == CHUNK_GEN_SCOPE_SECTION) return chunk->loaded;
    return false;
}

static void ReclassifyStreamingWork(const ChunkStreamingFocus *focus)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        Chunk *chunk = &chunks[i];
        if (!chunk->loaded && !chunk->generating) continue;
        bool current = ChunkInStreamingRegion(
            chunk, focus->currentCx, focus->currentCz,
            focus->renderDistance);
        bool predicted = ChunkInStreamingRegion(
            chunk, focus->predictedCx, focus->predictedCz,
            focus->renderDistance);
        if (chunk->loaded && !current && !predicted) {
            MarkChunkAndHorizontalNeighborsDirty(chunk->cx, chunk->cz);
            ChunkIndexRemove(chunk);
            ChunkClearBlockStorage(chunk);
            chunk->loaded = false;
            chunk->preloaded = false;
        } else if (current || predicted) {
            chunk->preloaded = !current;
        } else if (chunk->generating) {
            chunk->generation++;
            if (chunk->generation == 0u) chunk->generation = 1u;
            chunk->generating = false;
            chunk->preloaded = false;
        }
    }

    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob *job = &chunkGenJobs[i];
        if (!job->inUse || job->slotIndex < 0 ||
            job->slotIndex >= MAX_ACTIVE_CHUNKS) {
            continue;
        }
        Chunk *chunk = &chunks[job->slotIndex];
        if (!ChunkGenJobTargetMatchesChunk(job, chunk) && !job->running) {
            FreeChunkGenJobResult(job);
            *job = (ChunkGenJob){ 0 };
            streamingStats.generationCanceled++;
            continue;
        }
        bool current = ChunkInStreamingRegion(
            chunk, focus->currentCx, focus->currentCz,
            focus->renderDistance);
        bool predicted = ChunkInStreamingRegion(
            chunk, focus->predictedCx, focus->predictedCz,
            focus->renderDistance);
        if (!current && !predicted && !job->running) {
            FreeChunkGenJobResult(job);
            *job = (ChunkGenJob){ 0 };
            streamingStats.generationCanceled++;
        } else if ((current || predicted) && !job->running) {
            job->preload = !current;
        }
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        MeshJob *job = &meshJobs[i];
        if (!job->inUse || job->slotIndex < 0 ||
            job->slotIndex >= MAX_ACTIVE_CHUNKS) {
            continue;
        }
        Chunk *chunk = &chunks[job->slotIndex];
        if (!MeshJobTargetMatchesChunk(job, chunk)) {
            if (!job->running) {
                FreeMeshData(&job->mesh);
                FreeMeshData(&job->waterMesh);
                FreeMeshData(&job->floraMesh);
                FreeMeshData(&job->lodFloraMesh);
                free(job->floraInstances);
                *job = (MeshJob){ 0 };
                streamingStats.meshCanceled++;
            }
            continue;
        }
        bool current = ChunkInStreamingRegion(
            chunk, focus->currentCx, focus->currentCz,
            focus->renderDistance);
        bool predicted = ChunkInStreamingRegion(
            chunk, focus->predictedCx, focus->predictedCz,
            focus->renderDistance);
        if (job->preload && !current && !predicted && !job->running) {
            FreeMeshData(&job->mesh);
            FreeMeshData(&job->waterMesh);
            FreeMeshData(&job->floraMesh);
            FreeMeshData(&job->lodFloraMesh);
            free(job->floraInstances);
            *job = (MeshJob){ 0 };
            streamingStats.meshCanceled++;
        } else if ((current || predicted) && !job->running) {
            job->preload = !current;
        }
    }
    UpdateQueuePeaksLocked();
    pthread_mutex_unlock(&genMutex);
}

static bool CoordinatesInCurrentRegion(
    int cx, int cz, const ChunkStreamingFocus *focus)
{
    Chunk candidate = {
        .cx = cx,
        .cz = cz,
        .spherical = WorldIsSurfaceActive()
    };
    return ChunkGridDistanceFrom(
               &candidate, focus->currentCx, focus->currentCz) <=
           focus->renderDistance;
}

int ScheduleNearbyTerrainSections(
    const ChunkStreamingRequest *request,
    const ChunkStreamingFocus *focus)
{
    if (!request || !focus || !ChunkStreamingPositionIsValid(request->position) ||
        WorldCurrentSurfaceContext().kind != WORLD_SURFACE_HOME) return 0;
    int playerY = (int)floorf(request->position.y);
    if (!InHeight(playerY)) return 0;
    int playerSectionY = SurfaceSectionYFromBlockY(playerY);
    static const int verticalOffsets[3] = { 0, -1, 1 };
    int centers[2][2] = {
        { focus->currentCx, focus->currentCz },
        { focus->predictedCx, focus->predictedCz }
    };

    int submissions = 0;
    for (int pass = 0; pass < 2; pass++) {
        bool preload = pass == 1;
        if (preload && focus->preloadTargetCount == 0) continue;
        for (int distance = 0; distance <= focus->renderDistance; distance++) {
            for (int vertical = 0; vertical < 3; vertical++) {
                int sectionY = playerSectionY + verticalOffsets[vertical];
                if (!SurfaceSectionInBounds(sectionY)) continue;
                for (int dz = -distance; dz <= distance; dz++) {
                    for (int dx = -distance; dx <= distance; dx++) {
                        if (abs(dx) != distance && abs(dz) != distance) {
                            continue;
                        }
                        int cx = centers[pass][0] + dx;
                        int cz = centers[pass][1] + dz;
                        if (preload && CoordinatesInCurrentRegion(
                                cx, cz, focus)) {
                            continue;
                        }
                        Chunk *chunk = FindChunk(cx, cz);
                        if (RequestLoadedChunkTerrainSection(
                                chunk, sectionY, preload)) {
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
    }
    return submissions;
}

typedef struct MissingChunkCandidate {
    int cx;
    int cz;
    bool preload;
} MissingChunkCandidate;

static void CollectMissingChunkCandidates(
    MissingChunkCandidate candidates[MAX_ACTIVE_CHUNKS], int *count,
    const ChunkStreamingFocus *focus, bool preload)
{
    int centerCx = preload ? focus->predictedCx : focus->currentCx;
    int centerCz = preload ? focus->predictedCz : focus->currentCz;
    for (int distance = 0; distance <= focus->renderDistance; distance++) {
        for (int dz = -distance; dz <= distance; dz++) {
            for (int dx = -distance; dx <= distance; dx++) {
                if (abs(dx) != distance && abs(dz) != distance) continue;
                int cx = centerCx + dx;
                int cz = centerCz + dz;
                if (preload && CoordinatesInCurrentRegion(cx, cz, focus)) {
                    continue;
                }
                Chunk *chunk = FindChunk(cx, cz);
                if (chunk) {
                    if (!preload) chunk->preloaded = false;
                    continue;
                }
                if (FindPendingGenJob(cx, cz, !preload)) continue;
                if (*count >= MAX_ACTIVE_CHUNKS) return;
                candidates[*count] = (MissingChunkCandidate){
                    .cx = cx,
                    .cz = cz,
                    .preload = preload
                };
                (*count)++;
            }
        }
    }
}

void UpdateChunks(const ChunkStreamingRequest *request)
{
    if (!request || !ChunkStreamingPositionIsValid(request->position)) return;
    ChunkStreamingFocus focus = StreamingFocusForRequest(request);
    chunkStreamingFocus = focus;
    ReclassifyStreamingWork(&focus);

    int playerY = (int)floorf(request->position.y);
    if (InHeight(playerY)) {
        int playerSectionY = SurfaceSectionYFromBlockY(playerY);
        CancelDistantNegativeSectionJobs(playerSectionY);
        PruneDistantNegativeTerrainSections(playerSectionY);
    }

    MissingChunkCandidate missing[MAX_ACTIVE_CHUNKS];
    int missingCount = 0;
    CollectMissingChunkCandidates(missing, &missingCount, &focus, false);
    CollectMissingChunkCandidates(missing, &missingCount, &focus, true);

    int submissions = 0;
    int preloadSubmissions = 0;
    for (int i = 0; i < missingCount &&
         submissions < CHUNK_GEN_SUBMISSIONS_PER_FRAME; i++) {
        if (missing[i].preload &&
            preloadSubmissions >= CHUNK_PRELOAD_SUBMISSIONS_PER_FRAME) {
            continue;
        }
        if (EnsureChunk(
                missing[i].cx, missing[i].cz, missing[i].preload)) {
            submissions++;
            if (missing[i].preload) preloadSubmissions++;
        }
    }
    ScheduleNearbyTerrainSections(request, &focus);
    ChunksUpdateLodTargets(request->position, WorldIsSurfaceActive());
}

#ifdef CHUNKS_TESTING
int ChunksTestWorkerCountForCpu(int onlineCpus, int requestedWorkers)
{
    return ChunkWorkerCountForCpu(onlineCpus, requestedWorkers);
}

void ChunksTestReleaseScheduler(void)
{
    pthread_mutex_lock(&genMutex);
    genWorkerThreadsConfigured = 0u;
    genWorkerThreadsStarted = 0u;
    genWorkerThreadsActive = 0u;
    pthread_mutex_unlock(&genMutex);
}

void ChunksTestFailNextColumnAllocation(void)
{
    failNextColumnAllocation = true;
}

bool ChunksTestEnsureChunk(int cx, int cz)
{
    return EnsureChunk(cx, cz, false);
}

bool ChunksTestFindPendingGenerationJob(int cx, int cz)
{
    return FindPendingGenJob(cx, cz, false);
}

void ChunksTestSeedGenerationJob(int jobIndex, int cx, int cz,
                                 int sectionY, bool done)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    if (chunkGenJobs[jobIndex].inUse) {
        ChunkTaskQueueRemove(&generationTaskQueue, jobIndex,
                             chunkGenJobs[jobIndex].queueSequence);
        ChunkTaskQueueRemove(&generationDoneQueue, jobIndex,
                             chunkGenJobs[jobIndex].queueSequence);
    }
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
    if (done) {
        QueueGenerationDoneLocked(&chunkGenJobs[jobIndex]);
    } else {
        chunkGenJobs[jobIndex].queued = ChunkTaskQueuePush(
            &generationTaskQueue, ChunkGenQueueLane(&chunkGenJobs[jobIndex]),
            jobIndex, chunkGenJobs[jobIndex].queueSequence);
        if (chunkGenJobs[jobIndex].queued) {
            ChunkDiagnosticsRecordReasonLocked("generation_pending");
        } else {
            ChunkDiagnosticsRecordReasonLocked("generation_queue_full");
        }
    }
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

bool ChunksTestGenerationJobPreload(int jobIndex)
{
    assert(jobIndex >= 0 && jobIndex < MAX_CHUNK_GEN_JOBS);
    pthread_mutex_lock(&genMutex);
    bool preload = chunkGenJobs[jobIndex].inUse &&
                   chunkGenJobs[jobIndex].preload;
    pthread_mutex_unlock(&genMutex);
    return preload;
}

#endif
