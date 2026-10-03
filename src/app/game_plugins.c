#include "app/game_plugins.h"

#include "app/game_runtime.h"
#include "core/game_notice.h"
#include "core/plugin.h"
#include "world/chunks.h"
#include "world/hydrology_runtime.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <stdio.h>

static bool GamePluginIsPlaying(void *userData)
{
    const GameRuntime *game = userData;
    return game && game->screen == SCREEN_PLAYING;
}

static bool GamePluginIsPaused(void *userData)
{
    const GameRuntime *game = userData;
    return game && game->paused;
}

static uint64_t GamePluginFrameIndex(void *userData)
{
    const GameRuntime *game = userData;
    return game ? game->debugFrame : 0u;
}

static uint32_t GamePluginWorldSeed(void *userData)
{
    (void)userData;
    return WorldGetSeed();
}

static void GamePluginPostNotice(void *userData, const char *message)
{
    (void)userData;
    GameNoticePost(message);
}

static bool GamePluginWorldSnapshot(
    void *userData, VoxelcraftPluginWorldSnapshot *outSnapshot)
{
    (void)userData;
    if (!outSnapshot || outSnapshot->struct_size <
                            sizeof(VoxelcraftPluginWorldSnapshot)) {
        return false;
    }
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    outSnapshot->surface_active = context.active;
    outSnapshot->dimension = (VoxelcraftPluginWorldDimension)
        WorldCurrentDimension();
    outSnapshot->surface_kind = (VoxelcraftPluginWorldSurfaceKind)context.kind;
    outSnapshot->surface_id = context.bodyId;
    outSnapshot->seed = context.seed;
    outSnapshot->map_origin_x = context.originX;
    outSnapshot->map_origin_z = context.originZ;
    return true;
}

static bool GamePluginStreamSnapshot(
    void *userData, VoxelcraftPluginStreamSnapshot *outSnapshot)
{
    (void)userData;
    if (!outSnapshot || outSnapshot->struct_size <
                            sizeof(VoxelcraftPluginStreamSnapshot)) {
        return false;
    }
    ChunkDiagnostics diagnostics = { 0 };
    ChunksCaptureDiagnostics(&diagnostics);
    ChunkStreamingStats stats = diagnostics.streaming;
    ChunkPreloadInfo preload = diagnostics.preload;
    outSnapshot->surface_active = WorldIsSurfaceActive();
    outSnapshot->render_distance = ChunksRenderDistance();
    outSnapshot->loaded_chunks = diagnostics.activeChunks;
    outSnapshot->pending_generation_jobs = diagnostics.pendingGenerationJobs;
    outSnapshot->pending_mesh_jobs = diagnostics.pendingMeshJobs;
    outSnapshot->worker_threads_configured =
        diagnostics.workerThreadsConfigured;
    outSnapshot->worker_threads_started = diagnostics.workerThreadsStarted;
    outSnapshot->worker_threads_active = diagnostics.workerThreadsActive;
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
    return true;
}

static bool GamePluginHydrologySnapshot(
    void *userData, VoxelcraftPluginHydrologySnapshot *outSnapshot)
{
    (void)userData;
    if (!outSnapshot || outSnapshot->struct_size <
                            sizeof(VoxelcraftPluginHydrologySnapshot)) {
        return false;
    }
    HydrologyRuntimeSnapshot snapshot = { 0 };
    if (!HydrologyRuntimeGlobalSnapshot(&snapshot)) return false;
    outSnapshot->active = snapshot.active;
    outSnapshot->surface_id = snapshot.location.surfaceId;
    outSnapshot->region_x = snapshot.location.regionX;
    outSnapshot->region_z = snapshot.location.regionZ;
    outSnapshot->step_count = snapshot.stepCount;
    outSnapshot->simulation_time_seconds = snapshot.simulationTimeSeconds;
    outSnapshot->accumulator_seconds = snapshot.accumulatorSeconds;
    outSnapshot->total_water_volume_m3 = snapshot.totalWaterVolumeM3;
    outSnapshot->total_sediment_mass_kg = snapshot.totalSedimentMassKg;
    return true;
}

static bool GamePluginHydrologyFeedbackAt(
    void *userData, uint32_t surfaceId, int32_t mapX, int32_t mapZ,
    VoxelcraftPluginHydrologyFeedback *outFeedback)
{
    (void)userData;
    if (!outFeedback || outFeedback->struct_size <
                            sizeof(VoxelcraftPluginHydrologyFeedback)) {
        return false;
    }
    HydrologyRuntimeFeedback feedback = { 0 };
    if (!HydrologyRuntimeGlobalFeedbackAt(
            surfaceId, mapX, mapZ, &feedback)) {
        return false;
    }
    outFeedback->active = feedback.active;
    outFeedback->surface_water_m = feedback.surfaceWaterM;
    outFeedback->soil_moisture = feedback.soilMoisture;
    outFeedback->groundwater_m = feedback.groundwaterM;
    outFeedback->runoff_m3_per_s = feedback.runoffM3PerS;
    outFeedback->evaporation_m_per_s = feedback.evaporationMPerS;
    outFeedback->sediment_load_kg_m2 = feedback.sedimentLoadKgM2;
    return true;
}

bool GamePluginsArgumentsValid(const GameRuntime *game)
{
    if (!game || !game->pluginDirectoryInvalid) return true;
    fprintf(stderr,
            "Invalid or repeated --plugin-dir option; expected a "
            "non-empty directory path shorter than %u bytes.\n",
            VOXELCRAFT_PLUGIN_MAX_PATH);
    return false;
}

bool GamePluginsStart(GameRuntime *game)
{
    if (!game) return false;
    if (game->pluginDirectory[0] != '\0' &&
        !VoxelcraftPluginHostLoadDirectory(&game->plugins,
                                           game->pluginDirectory)) {
        goto fail;
    }
    if (VoxelcraftPluginHostCount(&game->plugins) > 0u) {
        game->pluginGameService = (VoxelcraftPluginGameService){
            .abi_version = VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginGameService),
            .user_data = game,
            .is_playing = GamePluginIsPlaying,
            .is_paused = GamePluginIsPaused,
            .frame_index = GamePluginFrameIndex,
            .world_seed = GamePluginWorldSeed,
            .post_notice = GamePluginPostNotice};
        game->pluginWorldService = (VoxelcraftPluginWorldService){
            .abi_version = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginWorldService),
            .user_data = game,
            .snapshot = GamePluginWorldSnapshot};
        game->pluginStreamService = (VoxelcraftPluginStreamService){
            .abi_version = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginStreamService),
            .user_data = game,
            .snapshot = GamePluginStreamSnapshot};
        game->pluginHydrologyService = (VoxelcraftPluginHydrologyService){
            .abi_version = VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginHydrologyService),
            .user_data = game,
            .snapshot = GamePluginHydrologySnapshot,
            .feedback_at = GamePluginHydrologyFeedbackAt};
        const VoxelcraftPluginService gameService = {
            .abi_version = VOXELCRAFT_PLUGIN_GAME_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginService),
            .id = VOXELCRAFT_PLUGIN_GAME_SERVICE_ID,
            .interface = &game->pluginGameService};
        const VoxelcraftPluginService worldService = {
            .abi_version = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginService),
            .id = VOXELCRAFT_PLUGIN_WORLD_SERVICE_ID,
            .interface = &game->pluginWorldService};
        const VoxelcraftPluginService streamService = {
            .abi_version = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginService),
            .id = VOXELCRAFT_PLUGIN_STREAM_SERVICE_ID,
            .interface = &game->pluginStreamService};
        const VoxelcraftPluginService hydrologyService = {
            .abi_version = VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ABI_VERSION,
            .struct_size = sizeof(VoxelcraftPluginService),
            .id = VOXELCRAFT_PLUGIN_HYDROLOGY_SERVICE_ID,
            .interface = &game->pluginHydrologyService};
        if (!VoxelcraftPluginHostProvideBuiltin(&game->plugins, "host",
                                                &gameService) ||
            !VoxelcraftPluginHostProvideBuiltin(&game->plugins, "host",
                                                &worldService) ||
            !VoxelcraftPluginHostProvideBuiltin(&game->plugins, "host",
                                                &streamService) ||
            !VoxelcraftPluginHostProvideBuiltin(&game->plugins, "host",
                                                &hydrologyService) ||
            !VoxelcraftPluginHostActivate(&game->plugins, game)) {
            goto fail;
        }
    }
    return true;

fail:
    fprintf(stderr, "Failed to load plugins from '%s': %s\n",
            game->pluginDirectory,
            VoxelcraftPluginHostLastError(&game->plugins));
    game->processExitCode = 2;
    VoxelcraftPluginHostShutdown(&game->plugins);
    return false;
}

void GamePluginsReset(GameRuntime *game)
{
    if (game) VoxelcraftPluginHostReset(&game->plugins);
}

void GamePluginsUpdate(GameRuntime *game, float dt)
{
    if (!game) return;
    VoxelcraftPluginFrame frame = {
        .frame_index = game->debugFrame,
        .dt = dt,
        .debug_mode = game->debugControlEnabled
    };
    VoxelcraftPluginHostUpdate(&game->plugins, &frame);
}

void GamePluginsRender(GameRuntime *game, float dt)
{
    if (!game) return;
    VoxelcraftPluginFrame frame = {
        .frame_index = game->debugFrame,
        .dt = dt,
        .debug_mode = game->debugControlEnabled
    };
    VoxelcraftPluginHostRender(&game->plugins, &frame);
}

void GamePluginsShutdown(GameRuntime *game)
{
    if (game) VoxelcraftPluginHostShutdown(&game->plugins);
}

size_t GamePluginsCount(const GameRuntime *game)
{
    return game ? VoxelcraftPluginHostCount(&game->plugins) : 0u;
}
