#include "world/chunks_internal.h"

#include <stdio.h>

static ChunkDiagnosticsEvent chunkDiagnosticsLastEvent = { .reason = "none" };

void ChunkDiagnosticsRecordReasonLocked(const char *reason)
{
    uint64_t sequence = chunkDiagnosticsLastEvent.sequence + 1u;
    chunkDiagnosticsLastEvent = (ChunkDiagnosticsEvent){
        .sequence = sequence ? sequence : 1u,
        .timeMs = ChunkNowMs()
    };
    snprintf(chunkDiagnosticsLastEvent.reason,
             sizeof(chunkDiagnosticsLastEvent.reason), "%s",
             reason ? reason : "unknown");
}

void ChunkDiagnosticsRecordReasonAtLocked(const char *reason, int cx,
                                          int sectionY, int cz,
                                          bool hasSection)
{
    ChunkDiagnosticsRecordReasonLocked(reason);
    chunkDiagnosticsLastEvent.hasLocation = true;
    chunkDiagnosticsLastEvent.chunkX = cx;
    chunkDiagnosticsLastEvent.chunkZ = cz;
    chunkDiagnosticsLastEvent.sectionY = sectionY;
    chunkDiagnosticsLastEvent.hasSection = hasSection;
}

void ChunkDiagnosticsRecordReasonAt(const char *reason, int cx,
                                    int sectionY, int cz, bool hasSection)
{
    pthread_mutex_lock(&genMutex);
    ChunkDiagnosticsRecordReasonAtLocked(reason, cx, sectionY, cz, hasSection);
    pthread_mutex_unlock(&genMutex);
}

void ChunkDiagnosticsRecordReason(const char *reason)
{
    pthread_mutex_lock(&genMutex);
    ChunkDiagnosticsRecordReasonLocked(reason);
    pthread_mutex_unlock(&genMutex);
}

void ChunkDiagnosticsResetReasonLocked(void)
{
    chunkDiagnosticsLastEvent = (ChunkDiagnosticsEvent){ .reason = "none" };
}

const char *ChunkDiagnosticsStatusName(ChunkDiagnosticsStatus status)
{
    switch (status) {
    case CHUNK_DIAGNOSTICS_VALID: return "valid";
    case CHUNK_DIAGNOSTICS_UNAVAILABLE:
    default: return "unavailable";
    }
}

const char *ChunkDiagnosticsLodConsistencyName(
    ChunkDiagnosticsLodConsistency consistency)
{
    switch (consistency) {
    case CHUNK_DIAGNOSTICS_LOD_CONSISTENT: return "consistent";
    case CHUNK_DIAGNOSTICS_LOD_INCONSISTENT: return "inconsistent";
    case CHUNK_DIAGNOSTICS_LOD_NOT_CAPTURED:
    default: return "not_captured";
    }
}

static void CaptureChunkDiagnostics(ChunkDiagnostics *outDiagnostics,
                                    bool includeLod,
                                    const DiagnosticsCaptureIdentity *identity)
{
    if (!outDiagnostics) return;
    *outDiagnostics = (ChunkDiagnostics){
        .status = CHUNK_DIAGNOSTICS_UNAVAILABLE
    };

    pthread_mutex_lock(&genMutex);
    outDiagnostics->status = CHUNK_DIAGNOSTICS_VALID;
    outDiagnostics->captureSequence = ++chunkDiagnosticsCaptureSequence;
    outDiagnostics->identity = identity
        ? *identity
        : (DiagnosticsCaptureIdentity){
            .captureId = outDiagnostics->captureSequence
        };
    outDiagnostics->lastEvent = chunkDiagnosticsLastEvent;
    outDiagnostics->blockedReasonSequence = chunkDiagnosticsLastEvent.sequence;
    outDiagnostics->streaming = streamingStats;
    outDiagnostics->renderDistance = renderDistanceChunks;
    outDiagnostics->focusChunkX = chunkStreamingFocus.currentCx;
    outDiagnostics->focusChunkZ = chunkStreamingFocus.currentCz;
    outDiagnostics->predictedChunkX = chunkStreamingFocus.predictedCx;
    outDiagnostics->predictedChunkZ = chunkStreamingFocus.predictedCz;
    outDiagnostics->workerThreadsConfigured = genWorkerThreadsConfigured;
    outDiagnostics->workerThreadsStarted = genWorkerThreadsStarted;
    outDiagnostics->workerThreadsActive = genWorkerThreadsActive;

    uint64_t pendingSnapshotBytes = 0u;
    for (int index = 0; index < MAX_CHUNK_GEN_JOBS; index++) {
        const ChunkGenJob *job = &chunkGenJobs[index];
        if (job->inUse && !job->done) outDiagnostics->pendingGenerationJobs++;
        if (job->inUse && job->done) outDiagnostics->completedGenerationJobs++;
        if (job->inUse && job->preload) {
            outDiagnostics->preload.pendingGenerationJobs++;
        }
    }
    for (int index = 0; index < MAX_MESH_JOBS; index++) {
        const MeshJob *job = &meshJobs[index];
        if (job->inUse && !job->done) outDiagnostics->pendingMeshJobs++;
        if (job->inUse && job->done) outDiagnostics->completedMeshJobs++;
        if (job->inUse && job->preload) {
            outDiagnostics->preload.pendingMeshJobs++;
        }
        if (job->inUse) {
            pendingSnapshotBytes += sizeof(job->blocks) +
                                    sizeof(job->waterVolumes) +
                                    sizeof(job->boundary);
        }
    }
    outDiagnostics->streaming.pendingMeshSnapshotBytes = pendingSnapshotBytes;

    /* Prefer work that can drain queues immediately. Historical budget or
     * failure events must never masquerade as a current blockage. */
    const char *reason = "none";
    if (outDiagnostics->completedMeshJobs > 0) reason = "mesh_upload_pending";
    else if (outDiagnostics->completedGenerationJobs > 0)
        reason = "generation_apply_pending";
    else if (outDiagnostics->pendingGenerationJobs > 0 &&
             genWorkerThreadsStarted == 0u)
        reason = "generation_workers_unavailable";
    else if (outDiagnostics->pendingMeshJobs > 0 && genWorkerThreadsStarted == 0u)
        reason = "mesh_workers_unavailable";
    else if (outDiagnostics->pendingGenerationJobs == MAX_CHUNK_GEN_JOBS)
        reason = "generation_queue_full";
    else if (outDiagnostics->pendingMeshJobs == MAX_MESH_JOBS)
        reason = "mesh_queue_full";
    else if (outDiagnostics->pendingGenerationJobs > 0)
        reason = "generation_pending";
    else if (outDiagnostics->pendingMeshJobs > 0) reason = "mesh_pending";
    snprintf(outDiagnostics->blockedReason,
             sizeof(outDiagnostics->blockedReason), "%s", reason);

    outDiagnostics->preload.active =
        chunkStreamingFocus.preloadTargetCount > 0;
    outDiagnostics->preload.offsetX = chunkStreamingFocus.predictedCx -
                                      chunkStreamingFocus.currentCx;
    outDiagnostics->preload.offsetZ = chunkStreamingFocus.predictedCz -
                                      chunkStreamingFocus.currentCz;
    outDiagnostics->preload.targetChunks =
        chunkStreamingFocus.preloadTargetCount;

    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        const Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        outDiagnostics->activeChunks++;
        if (chunk->preloaded) outDiagnostics->preload.loadedChunks++;
        if (chunk->cx == chunkStreamingFocus.currentCx &&
            chunk->cz == chunkStreamingFocus.currentCz) {
            outDiagnostics->focusChunkLoaded = true;
        }
    }
    /* LOD state is an optional detailed view.  Capture it while the scheduler
     * lock is held so the base fields and LOD counters share one observation
     * point. */
    if (includeLod) {
        outDiagnostics->lod = ChunksGetLodStatsLocked();
        outDiagnostics->lodConsistency = CHUNK_DIAGNOSTICS_LOD_CONSISTENT;
    }
    pthread_mutex_unlock(&genMutex);
}

void ChunksCaptureDiagnostics(ChunkDiagnostics *outDiagnostics)
{
    ChunksCaptureDiagnosticsAt(outDiagnostics, NULL);
}

void ChunksCaptureDetailedDiagnostics(ChunkDiagnostics *outDiagnostics)
{
    ChunksCaptureDetailedDiagnosticsAt(outDiagnostics, NULL);
}

void ChunksCaptureDiagnosticsAt(
    ChunkDiagnostics *outDiagnostics,
    const DiagnosticsCaptureIdentity *identity)
{
    CaptureChunkDiagnostics(outDiagnostics, false, identity);
}

void ChunksCaptureDetailedDiagnosticsAt(
    ChunkDiagnostics *outDiagnostics,
    const DiagnosticsCaptureIdentity *identity)
{
    CaptureChunkDiagnostics(outDiagnostics, true, identity);
}

ChunkPreloadInfo ChunksGetPreloadInfo(void)
{
    ChunkPreloadInfo info = {
        .active = chunkStreamingFocus.preloadTargetCount > 0,
        .offsetX = chunkStreamingFocus.predictedCx -
                   chunkStreamingFocus.currentCx,
        .offsetZ = chunkStreamingFocus.predictedCz -
                   chunkStreamingFocus.currentCz,
        .targetChunks = chunkStreamingFocus.preloadTargetCount
    };
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded && chunks[i].preloaded) info.loadedChunks++;
    }
    pthread_mutex_lock(&genMutex);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && chunkGenJobs[i].preload) {
            info.pendingGenerationJobs++;
        }
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && meshJobs[i].preload) {
            info.pendingMeshJobs++;
        }
    }
    pthread_mutex_unlock(&genMutex);
    return info;
}

static int PipelineModelVertexCount(const Model *model, bool present)
{
    if (!present || !model || !model->meshes || model->meshCount <= 0) {
        return 0;
    }
    int vertices = 0;
    for (int index = 0; index < model->meshCount; index++) {
        vertices += model->meshes[index].vertexCount;
    }
    return vertices;
}

static double PipelineStageAge(double now, double since)
{
    return since > 0.0 && now >= since ? now - since : 0.0;
}

const char *ChunkPipelineStageName(ChunkPipelineStage stage)
{
    switch (stage) {
    case CHUNK_PIPELINE_MISSING_CHUNK: return "missing_chunk";
    case CHUNK_PIPELINE_GENERATION_WAIT: return "generation_wait";
    case CHUNK_PIPELINE_GENERATION_QUEUED: return "generation_queued";
    case CHUNK_PIPELINE_GENERATION_RUNNING: return "generation_running";
    case CHUNK_PIPELINE_GENERATION_DONE: return "generation_done_wait_apply";
    case CHUNK_PIPELINE_IMPLICIT: return "implicit";
    case CHUNK_PIPELINE_DIRTY_WAIT: return "dirty_wait_submit";
    case CHUNK_PIPELINE_MESH_QUEUED: return "mesh_queued";
    case CHUNK_PIPELINE_MESH_RUNNING: return "mesh_running";
    case CHUNK_PIPELINE_MESH_DONE: return "mesh_done_wait_upload";
    case CHUNK_PIPELINE_READY: return "ready";
    default: return "unknown";
    }
}

bool ChunksGetSectionPipelineInfo(int cx, int sectionY, int cz,
                                  ChunkSectionPipelineInfo *outInfo)
{
    if (!outInfo || !SurfaceSectionInBounds(sectionY)) return false;
    *outInfo = (ChunkSectionPipelineInfo){
        .stage = CHUNK_PIPELINE_MISSING_CHUNK
    };
    Chunk *chunk = FindChunk(cx, cz);
    const ChunkSection *section = chunk
        ? ChunkGetSectionConst(chunk, sectionY) : NULL;
    if (chunk) {
        outInfo->chunkLoaded = chunk->loaded;
        outInfo->resolved = ChunkTerrainSectionIsResolved(chunk, sectionY);
    }
    if (section) {
        outInfo->materialized = true;
        outInfo->dirty = section->dirty;
        outInfo->currentStamp = section->dirtyStamp;
        outInfo->solidVertices = PipelineModelVertexCount(
            &section->model, section->hasModel);
        outInfo->waterVertices = PipelineModelVertexCount(
            &section->waterModel, section->hasWaterModel);
        outInfo->floraVertices = PipelineModelVertexCount(
            &section->floraModel, section->hasFloraModel);
    }

    double now = ChunkNowMs();
    const ChunkGenJob *generation = NULL;
    const MeshJob *mesh = NULL;
    pthread_mutex_lock(&genMutex);
    for (int index = 0; index < MAX_CHUNK_GEN_JOBS; index++) {
        const ChunkGenJob *candidate = &chunkGenJobs[index];
        if (!candidate->inUse || candidate->cx != cx ||
            candidate->cz != cz ||
            (candidate->scope == CHUNK_GEN_SCOPE_SECTION &&
             candidate->sectionY != sectionY)) continue;
        generation = candidate;
        break;
    }
    for (int index = 0; index < MAX_MESH_JOBS; index++) {
        const MeshJob *candidate = &meshJobs[index];
        if (!candidate->inUse || candidate->cx != cx ||
            candidate->cz != cz || candidate->sectionY != sectionY ||
            (chunk && candidate->chunkGeneration != chunk->generation)) {
            continue;
        }
        mesh = candidate;
        break;
    }

    if (generation) {
        if (generation->done) {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_DONE;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->completedAtMs);
        } else if (generation->running) {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_RUNNING;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->startedAtMs);
        } else {
            outInfo->stage = CHUNK_PIPELINE_GENERATION_QUEUED;
            outInfo->stageAgeMs = PipelineStageAge(
                now, generation->submittedAtMs);
        }
    } else if (!chunk || !chunk->loaded) {
        outInfo->stage = CHUNK_PIPELINE_MISSING_CHUNK;
    } else if (mesh) {
        outInfo->snapshotStamp = mesh->sectionStamp;
        if (mesh->done) {
            outInfo->stage = CHUNK_PIPELINE_MESH_DONE;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->completedAtMs);
        } else if (mesh->running) {
            outInfo->stage = CHUNK_PIPELINE_MESH_RUNNING;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->startedAtMs);
        } else {
            outInfo->stage = CHUNK_PIPELINE_MESH_QUEUED;
            outInfo->stageAgeMs = PipelineStageAge(
                now, mesh->submittedAtMs);
        }
    } else if (!section) {
        outInfo->stage = outInfo->resolved
            ? CHUNK_PIPELINE_IMPLICIT : CHUNK_PIPELINE_GENERATION_WAIT;
    } else if (section->dirty) {
        outInfo->stage = CHUNK_PIPELINE_DIRTY_WAIT;
        outInfo->stageAgeMs = PipelineStageAge(now, section->dirtySinceMs);
    } else {
        outInfo->stage = CHUNK_PIPELINE_READY;
    }
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool ChunksGetWaterRenderDebugInfo(Vector3 position,
                                   ChunkWaterRenderDebugInfo *outInfo)
{
    if (!outInfo || !ChunkStreamingPositionIsValid(position)) {
        return false;
    }

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal((int)floorf(position.x), (int)floorf(position.z),
                      &cx, &cz, &lx, &lz);
    int blockY = (int)floorf(position.y);
    int sectionY = InHeight(blockY)
        ? SurfaceSectionYFromBlockY(blockY) : SURFACE_SECTION_MIN_Y - 1;
    *outInfo = (ChunkWaterRenderDebugInfo){
        .cx = cx,
        .cz = cz,
        .sectionY = sectionY
    };

    if (FindHorizontalChunkNeighbor(cx, cz, -1, 0)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_WEST;
    }
    if (FindHorizontalChunkNeighbor(cx, cz, 1, 0)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_EAST;
    }
    if (FindHorizontalChunkNeighbor(cx, cz, 0, -1)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_NORTH;
    }
    if (FindHorizontalChunkNeighbor(cx, cz, 0, 1)) {
        outInfo->neighborLoadedMask |= CHUNK_WATER_NEIGHBOR_SOUTH;
    }

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return false;
    outInfo->chunkLoaded = true;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        const ChunkSection *section = chunk->sections[sectionIndex];
        const Model *waterModel = ChunksSectionWaterModel(chunk, section);
        if (!waterModel || !waterModel->meshes) continue;
        for (int meshIndex = 0; meshIndex < waterModel->meshCount;
             meshIndex++) {
            int triangles = waterModel->meshes[meshIndex].triangleCount;
            outInfo->triangleCount += triangles;
            if (section->sectionY == sectionY) {
                outInfo->sectionTriangleCount += triangles;
            }
        }
    }
    return true;
}

RenderResourceSnapshot ChunksGetRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = { 0 };
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        const Chunk *chunk = &chunks[i];
        if (!chunk->loaded) continue;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            const ChunkSection *section = chunk->sections[sectionIndex];
            if (section->hasModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->model,
                                               RENDER_RESOURCE_SOLID);
            }
            if (section->hasLodModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->lodModel,
                                               RENDER_RESOURCE_SOLID);
            }
            if (section->hasFloraModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->floraModel,
                                               RENDER_RESOURCE_FLORA);
            }
            if (section->hasLodFloraModel) {
                RenderResourceSnapshotAddModel(
                    &snapshot, &section->lodFloraModel,
                    RENDER_RESOURCE_FLORA);
            }
            if (section->hasWaterModel) {
                RenderResourceSnapshotAddModel(&snapshot, &section->waterModel,
                                               RENDER_RESOURCE_TRANSPARENT);
            }
            if (section->hasLodWaterModel) {
                RenderResourceSnapshotAddModel(
                    &snapshot, &section->lodWaterModel,
                    RENDER_RESOURCE_TRANSPARENT);
            }
        }
    }
    pthread_mutex_lock(&genMutex);
    UpdateQueuePeaksLocked();
    snapshot.pendingMeshSnapshotBytes = streamingStats.pendingMeshSnapshotBytes;
    snapshot.workerThreadsConfigured = genWorkerThreadsConfigured;
    snapshot.workerThreadsStarted = genWorkerThreadsStarted;
    snapshot.workerThreadsActive = genWorkerThreadsActive;
    pthread_mutex_unlock(&genMutex);
    return snapshot;
}
