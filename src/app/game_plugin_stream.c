#include "app/game_plugin_stream.h"

#include "app/game_runtime.h"
#include "world/chunks.h"
#include "world/world_environment.h"

static void FillStreamCounters(VoxelcraftPluginStreamSnapshot *outSnapshot,
                                const ChunkDiagnostics *diagnostics)
{
    ChunkStreamingStats stats = diagnostics->streaming;
    ChunkPreloadInfo preload = diagnostics->preload;
    outSnapshot->surface_active = WorldIsSurfaceActive();
    outSnapshot->render_distance = diagnostics->renderDistance;
    outSnapshot->loaded_chunks = diagnostics->activeChunks;
    outSnapshot->pending_generation_jobs = diagnostics->pendingGenerationJobs;
    outSnapshot->pending_mesh_jobs = diagnostics->pendingMeshJobs;
    outSnapshot->worker_threads_configured =
        diagnostics->workerThreadsConfigured;
    outSnapshot->worker_threads_started = diagnostics->workerThreadsStarted;
    outSnapshot->worker_threads_active = diagnostics->workerThreadsActive;
    outSnapshot->generation_submitted = stats.generationSubmitted;
    outSnapshot->generation_completed = stats.generationCompleted;
    outSnapshot->mesh_submitted = stats.meshSubmitted;
    outSnapshot->mesh_completed = stats.meshCompleted;
    outSnapshot->generation_queue_peak = stats.generationQueuePeak;
    outSnapshot->mesh_queue_peak = stats.meshQueuePeak;
    outSnapshot->preload_active = preload.active;
    outSnapshot->preload_offset_x = preload.offsetX;
    outSnapshot->preload_offset_z = preload.offsetZ;
    outSnapshot->preload_target_chunks = preload.targetChunks;
    outSnapshot->preload_loaded_chunks = preload.loadedChunks;
    outSnapshot->preload_pending_generation_jobs =
        preload.pendingGenerationJobs;
    outSnapshot->preload_pending_mesh_jobs = preload.pendingMeshJobs;
}

bool GamePluginStreamSnapshot(void *userData,
                              VoxelcraftPluginStreamSnapshot *outSnapshot)
{
    if (!userData || !outSnapshot || outSnapshot->struct_size < sizeof(*outSnapshot))
        return false;
    DiagnosticsCaptureIdentity identity = GameRuntimeCaptureDiagnosticsIdentity(userData);
    ChunkDiagnostics diagnostics = { 0 };
    ChunksCaptureDiagnosticsAt(&diagnostics, &identity);
    if (diagnostics.status != CHUNK_DIAGNOSTICS_VALID) return false;
    FillStreamCounters(outSnapshot, &diagnostics);
    return true;
}

bool GamePluginStreamSnapshotV2(void *userData,
                                VoxelcraftPluginStreamSnapshotV2 *outSnapshot)
{
    if (!userData || !outSnapshot || outSnapshot->struct_size < sizeof(*outSnapshot))
        return false;
    DiagnosticsCaptureIdentity identity = GameRuntimeCaptureDiagnosticsIdentity(userData);
    ChunkDiagnostics diagnostics = { 0 };
    ChunksCaptureDiagnosticsAt(&diagnostics, &identity);
    if (diagnostics.status != CHUNK_DIAGNOSTICS_VALID) return false;
    outSnapshot->capture_id = diagnostics.identity.captureId;
    outSnapshot->frame_id = diagnostics.identity.frameId;
    outSnapshot->simulation_tick = diagnostics.identity.simulationTick;
    outSnapshot->world_generation = diagnostics.identity.worldGeneration;
    outSnapshot->capture_sequence = diagnostics.captureSequence;
    outSnapshot->counters.struct_size = sizeof(outSnapshot->counters);
    FillStreamCounters(&outSnapshot->counters, &diagnostics);
    return true;
}
