#include "app/game_debug_trace.h"

#include "app/game_internal.h"
#include "app/game_runtime.h"
#include "gameplay/player.h"
#include "world/chunks.h"
#include "world/fluid.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define TRACE_BUFFER_SIZE 8192
#define TRACE_INTERVAL_SECONDS 0.1

typedef struct TraceBuffer {
    char data[TRACE_BUFFER_SIZE];
    size_t length;
    bool failed;
} TraceBuffer;

static double TraceClockMs(clockid_t clockId)
{
    struct timespec value = { 0 };
    if (clock_gettime(clockId, &value) != 0) return 0.0;
    return (double)value.tv_sec * 1000.0 +
           (double)value.tv_nsec / 1000000.0;
}

double GameDebugTraceMainCpuNowMs(void)
{
    return TraceClockMs(CLOCK_THREAD_CPUTIME_ID);
}

static int64_t TraceUnixMs(void)
{
    return (int64_t)llround(TraceClockMs(CLOCK_REALTIME));
}

static double TraceElapsedRealMs(const GameRuntime *game)
{
    if (!game || game->debugTrace.startedMonotonicMs <= 0.0) return 0.0;
    double elapsed = TraceClockMs(CLOCK_MONOTONIC) -
                     game->debugTrace.startedMonotonicMs;
    return elapsed > 0.0 ? elapsed : 0.0;
}

static double TraceElapsedCpuMs(const GameRuntime *game)
{
    if (!game || game->debugTrace.startedCpuMs <= 0.0) return 0.0;
    double elapsed = TraceClockMs(CLOCK_PROCESS_CPUTIME_ID) -
                     game->debugTrace.startedCpuMs;
    return elapsed > 0.0 ? elapsed : 0.0;
}

static double TraceElapsedMainCpuMs(const GameRuntime *game)
{
    if (!game || game->debugTrace.startedMainCpuMs <= 0.0) return 0.0;
    double elapsed = TraceClockMs(CLOCK_THREAD_CPUTIME_ID) -
                     game->debugTrace.startedMainCpuMs;
    return elapsed > 0.0 ? elapsed : 0.0;
}

static void TraceAppendFormat(TraceBuffer *buffer, const char *format, ...)
{
    if (!buffer || buffer->failed || !format) return;
    size_t remaining = sizeof(buffer->data) - buffer->length;
    va_list arguments;
    va_start(arguments, format);
    int written = vsnprintf(buffer->data + buffer->length, remaining,
                            format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= remaining) {
        buffer->failed = true;
        return;
    }
    buffer->length += (size_t)written;
}

static void TraceAppendJsonString(TraceBuffer *buffer, const char *value)
{
    static const char hex[] = "0123456789abcdef";
    TraceAppendFormat(buffer, "\"");
    const unsigned char *cursor = (const unsigned char *)(value ? value : "");
    while (!buffer->failed && *cursor != '\0') {
        unsigned char character = *cursor++;
        const char *escape = NULL;
        switch (character) {
        case '\"': escape = "\\\""; break;
        case '\\': escape = "\\\\"; break;
        case '\b': escape = "\\b"; break;
        case '\f': escape = "\\f"; break;
        case '\n': escape = "\\n"; break;
        case '\r': escape = "\\r"; break;
        case '\t': escape = "\\t"; break;
        default: break;
        }
        if (escape) {
            TraceAppendFormat(buffer, "%s", escape);
        } else if (character < 0x20u) {
            TraceAppendFormat(buffer, "\\u00%c%c",
                              hex[character >> 4], hex[character & 0x0fu]);
        } else {
            TraceAppendFormat(buffer, "%c", character);
        }
    }
    TraceAppendFormat(buffer, "\"");
}

static void TraceFail(GameRuntime *game, const char *operation)
{
    if (!game) return;
    GameDebugTraceState *trace = &game->debugTrace;
    int errorNumber = errno != 0 ? errno : EIO;
    if (!trace->ioErrorReported) {
        fprintf(stderr, "Debug trace %s failed for %s: %s\n",
                operation ? operation : "I/O", game->debugTracePath,
                strerror(errorNumber));
        trace->ioErrorReported = true;
    }
    if (trace->file) fclose(trace->file);
    trace->file = NULL;
    game->debugTraceEnabled = false;
}

static bool TraceWrite(GameRuntime *game, TraceBuffer *buffer)
{
    if (!game || !buffer || buffer->failed || !game->debugTrace.file) {
        if (game && buffer && buffer->failed) {
            errno = EOVERFLOW;
            TraceFail(game, "record serialization");
        }
        return false;
    }
    if (fwrite(buffer->data, 1u, buffer->length,
               game->debugTrace.file) != buffer->length ||
        fflush(game->debugTrace.file) != 0) {
        TraceFail(game, "write");
        return false;
    }
    return true;
}

static int ModelVertexCount(const Model *model, bool present)
{
    if (!present || !model || model->meshCount <= 0 || !model->meshes) {
        return 0;
    }
    int vertices = 0;
    for (int index = 0; index < model->meshCount; index++) {
        vertices += model->meshes[index].vertexCount;
    }
    return vertices;
}

static int SectionVertexCount(const ChunkSection *section)
{
    if (!section) return 0;
    return ModelVertexCount(&section->model, section->hasModel) +
           ModelVertexCount(&section->waterModel, section->hasWaterModel) +
           ModelVertexCount(&section->floraModel, section->hasFloraModel);
}

bool GameDebugTraceSetPath(char *destination, size_t destinationSize,
                           const char *source)
{
    if (!destination || destinationSize == 0u || !source ||
        source[0] == '\0') return false;
    size_t length = strlen(source);
    if (length >= destinationSize) return false;
    memcpy(destination, source, length + 1u);
    return true;
}

static bool ResolveDefaultPath(GameRuntime *game)
{
    if (!game) return false;
    if (game->debugTracePath[0] != '\0') return true;
    if (mkdir("debug-traces", 0755) != 0 && errno != EEXIST) {
        return GameDebugTraceSetPath(
            game->debugTracePath, sizeof(game->debugTracePath),
            "voxelcraft_debug_trace.jsonl");
    }
    time_t now = time(NULL);
    struct tm local = { 0 };
    localtime_r(&now, &local);
    char stamp[32] = { 0 };
    strftime(stamp, sizeof(stamp), "%Y%m%d_%H%M%S", &local);
    int written = snprintf(
        game->debugTracePath, sizeof(game->debugTracePath),
        "debug-traces/voxelcraft_%s.jsonl", stamp);
    return written > 0 && (size_t)written < sizeof(game->debugTracePath);
}

static double AdvanceDeadline(double deadline, double elapsed)
{
    do {
        deadline += TRACE_INTERVAL_SECONDS;
    } while (deadline <= elapsed);
    return deadline;
}

static bool WriteStart(GameRuntime *game)
{
    TraceBuffer buffer = { 0 };
    TraceAppendFormat(
        &buffer,
        "{\"version\":1,\"type\":\"start\",\"timestamp_unix_ms\":%lld,"
        "\"elapsed_real_ms\":0.000,\"elapsed_cpu_ms\":0.000,"
        "\"elapsed_main_cpu_ms\":0.000,\"path\":",
        (long long)game->debugTrace.startedUnixMs);
    TraceAppendJsonString(&buffer, game->debugTracePath);
    TraceAppendFormat(&buffer, "}\n");
    return TraceWrite(game, &buffer);
}

static bool WriteEvent(GameRuntime *game, const char *reason)
{
    TraceBuffer buffer = { 0 };
    TraceAppendFormat(
        &buffer,
        "{\"version\":1,\"type\":\"event\",\"reason\":");
    TraceAppendJsonString(&buffer, reason ? reason : "event");
    TraceAppendFormat(
        &buffer,
        ",\"time\":%.6f,\"timestamp_unix_ms\":%lld,"
        "\"elapsed_real_ms\":%.3f,\"elapsed_cpu_ms\":%.3f,"
        "\"elapsed_main_cpu_ms\":%.3f,"
        "\"frame\":%llu}\n",
        game->debugTrace.elapsed,
        (long long)TraceUnixMs(), TraceElapsedRealMs(game),
        TraceElapsedCpuMs(game), TraceElapsedMainCpuMs(game),
        (unsigned long long)game->debugTrace.frame);
    return TraceWrite(game, &buffer);
}

static bool WriteSample(GameRuntime *game, const GameFrameView *frame,
                        const char *reason)
{
    int playerX = (int)floorf(game->player.position.x);
    int playerY = (int)floorf(game->player.position.y);
    int playerZ = (int)floorf(game->player.position.z);
    int focusCx = 0;
    int focusCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal(playerX, playerZ, &focusCx, &focusCz,
                      &localX, &localZ);
    int focusSectionY = SurfaceSectionYFromBlockY(playerY);
    const Chunk *focusChunk = FindChunk(focusCx, focusCz);
    const ChunkSection *focusSection = focusChunk
        ? ChunkGetSectionConst(focusChunk, focusSectionY) : NULL;

    HitResult targeted = frame->hit;
    int targetCx = 0;
    int targetCz = 0;
    int targetLx = 0;
    int targetLz = 0;
    const Chunk *targetChunk = NULL;
    const ChunkSection *targetSection = NULL;
    BlockType targetBlock = BLOCK_AIR;
    if (targeted.hit) {
        targetBlock = GetBlockAt(targeted.x, targeted.y, targeted.z);
        WorldToChunkLocal(targeted.x, targeted.z, &targetCx, &targetCz,
                          &targetLx, &targetLz);
        targetChunk = FindChunk(targetCx, targetCz);
        targetSection = targetChunk ? ChunkGetSectionConst(
            targetChunk, SurfaceSectionYFromBlockY(targeted.y)) : NULL;
    }

    FluidSample fluid = FluidSampleAt(game->player.position);
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    int focusVertices = SectionVertexCount(focusSection);
    int targetVertices = SectionVertexCount(targetSection);
    int missingSurfaceChunks =
        PlayerMissingSurfaceChunkCount(game->player.position);
    bool invisibleTarget = targeted.hit && targetVertices == 0;
    const PlayerWaterState *water = &frame->playerWater;
    const BathymetrySample *bathymetry = &frame->bathymetry;
    ChunkSectionPipelineInfo focusPipeline = { 0 };
    ChunksGetSectionPipelineInfo(focusCx, focusSectionY, focusCz,
                                 &focusPipeline);
    int targetSectionY = targeted.hit
        ? SurfaceSectionYFromBlockY(targeted.y) : focusSectionY;
    ChunkSectionPipelineInfo targetPipeline = { 0 };
    if (targeted.hit) {
        ChunksGetSectionPipelineInfo(targetCx, targetSectionY, targetCz,
                                     &targetPipeline);
    }
    int64_t timestampUnixMs = TraceUnixMs();
    double elapsedRealMs = TraceElapsedRealMs(game);
    double elapsedCpuMs = TraceElapsedCpuMs(game);
    double elapsedMainCpuMs = TraceElapsedMainCpuMs(game);

    TraceBuffer buffer = { 0 };
    TraceAppendFormat(&buffer,
        "{\"version\":1,\"type\":\"sample\",\"reason\":");
    TraceAppendJsonString(&buffer, reason ? reason : "periodic");
    TraceAppendFormat(
        &buffer,
        ",\"time\":%.6f,\"timestamp_unix_ms\":%lld,"
        "\"elapsed_real_ms\":%.3f,\"elapsed_cpu_ms\":%.3f,"
        "\"elapsed_main_cpu_ms\":%.3f,"
        "\"frame\":%llu,\"screen\":%d,"
        "\"seed\":%u,\"dimension\":",
        game->debugTrace.elapsed,
        (long long)timestampUnixMs, elapsedRealMs, elapsedCpuMs,
        elapsedMainCpuMs,
        (unsigned long long)game->debugTrace.frame, (int)game->screen,
        WorldGetSeed());
    TraceAppendJsonString(&buffer, WorldDimensionName(WorldCurrentDimension()));
    TraceAppendFormat(
        &buffer,
        ",\"timing\":{\"update_main_cpu_ms\":%.3f,"
        "\"render_main_cpu_ms\":%.3f,"
        "\"simulation_main_cpu_ms\":%.3f,"
        "\"streaming_main_cpu_ms\":%.3f,"
        "\"interaction_main_cpu_ms\":%.3f,"
        "\"environment_main_cpu_ms\":%.3f,"
        "\"astronomy_main_cpu_ms\":%.3f,"
        "\"ecology_main_cpu_ms\":%.3f,"
        "\"sky_main_cpu_ms\":%.3f,"
        "\"water_main_cpu_ms\":%.3f,"
        "\"environment_sample_main_cpu_ms\":%.3f,"
        "\"environment_present_main_cpu_ms\":%.3f,"
        "\"flora_visuals_main_cpu_ms\":%.3f,"
        "\"entities_main_cpu_ms\":%.3f},"
        "\"player\":{\"position\":[%.6f,%.6f,%.6f],"
        "\"velocity\":[%.6f,%.6f,%.6f],\"yaw\":%.6f,"
        "\"pitch\":%.6f,\"floating\":%d,\"on_ground\":%d},"
        "\"camera\":{\"position\":[%.6f,%.6f,%.6f],"
        "\"target\":[%.6f,%.6f,%.6f]},"
        "\"focus\":{\"chunk\":[%d,%d],\"section\":%d,"
        "\"chunk_loaded\":%d,\"surface_ready\":%d,"
        "\"player_missing_surface_chunks\":%d,"
        "\"section_materialized\":%d,\"dirty\":%d,"
        "\"stamp\":%u,\"vertices\":%d},"
        "\"stream\":{\"active_chunks\":%d,\"pending_gen\":%d,"
        "\"pending_mesh\":%d,\"gen_submitted\":%llu,"
        "\"gen_completed\":%llu,\"gen_canceled\":%llu,"
        "\"mesh_submitted\":%llu,\"mesh_completed\":%llu,"
        "\"mesh_canceled\":%llu,\"mesh_uploaded\":%llu,"
        "\"upload_deferrals\":%llu,"
        "\"generation_cpu_ms\":%.3f,\"mesh_cpu_ms\":%.3f,"
        "\"upload_cpu_ms\":%.3f,\"max_upload_cpu_ms\":%.3f},"
        "\"environment\":{\"water\":[%d,%d,%d],"
        "\"water_depth\":%.6f,\"water_surface\":%.6f,"
        "\"fluid_volume\":%u,\"fluid_surface\":%.6f,"
        "\"bathymetry\":",
        frame->debugUpdateMainCpuMs, frame->debugRenderMainCpuMs,
        frame->debugSimulationMainCpuMs, frame->debugStreamingMainCpuMs,
        frame->debugInteractionMainCpuMs, frame->debugEnvironmentMainCpuMs,
        frame->debugAstronomyMainCpuMs, frame->debugEcologyMainCpuMs,
        frame->debugSkyMainCpuMs, frame->debugWaterMainCpuMs,
        frame->debugEnvironmentSampleMainCpuMs,
        frame->debugEnvironmentPresentMainCpuMs,
        frame->debugFloraVisualsMainCpuMs, frame->debugEntitiesMainCpuMs,
        game->player.position.x, game->player.position.y,
        game->player.position.z, game->player.velocity.x,
        game->player.velocity.y, game->player.velocity.z,
        game->player.yaw, game->player.pitch,
        game->player.floating ? 1 : 0, game->player.onGround ? 1 : 0,
        game->camera.position.x, game->camera.position.y,
        game->camera.position.z, game->camera.target.x,
        game->camera.target.y, game->camera.target.z,
        focusCx, focusCz, focusSectionY,
        focusChunk && focusChunk->loaded ? 1 : 0,
        missingSurfaceChunks == 0 ? 1 : 0, missingSurfaceChunks,
        focusSection ? 1 : 0,
        focusSection && focusSection->dirty ? 1 : 0,
        focusSection ? focusSection->dirtyStamp : 0u, focusVertices,
        GetActiveChunkCount(), GetPendingGenJobCount(),
        GetPendingMeshJobCount(),
        (unsigned long long)stats.generationSubmitted,
        (unsigned long long)stats.generationCompleted,
        (unsigned long long)stats.generationCanceled,
        (unsigned long long)stats.meshSubmitted,
        (unsigned long long)stats.meshCompleted,
        (unsigned long long)stats.meshCanceled,
        (unsigned long long)stats.uploadedMeshes,
        (unsigned long long)stats.uploadBudgetDeferrals,
        stats.generationCpuMs, stats.meshCpuMs, stats.uploadCpuMs,
        stats.maxUploadCpuMs,
        water->feetSubmerged ? 1 : 0, water->bodySubmerged ? 1 : 0,
        water->eyesSubmerged ? 1 : 0, water->eyeDepth, water->surfaceY,
        (unsigned)fluid.volume, fluid.surfaceY);
    TraceAppendJsonString(&buffer, BathymetryZoneName(bathymetry->zone));
    TraceAppendFormat(
        &buffer,
        ",\"seabed\":%d,\"water_column\":%d},"
        "\"target\":{\"hit\":%d,\"position\":[%d,%d,%d],"
        "\"normal\":[%d,%d,%d],\"block\":%d,"
        "\"section_materialized\":%d,\"dirty\":%d,"
        "\"vertices\":%d,\"invisible\":%d},"
        "\"pipeline\":{\"focus_stage\":",
        bathymetry->seabedY, bathymetry->waterDepth,
        targeted.hit ? 1 : 0, targeted.x, targeted.y, targeted.z,
        targeted.nx, targeted.ny, targeted.nz, (int)targetBlock,
        targetSection ? 1 : 0,
        targetSection && targetSection->dirty ? 1 : 0,
        targetVertices, invisibleTarget ? 1 : 0);
    TraceAppendJsonString(
        &buffer, ChunkPipelineStageName(focusPipeline.stage));
    TraceAppendFormat(
        &buffer,
        ",\"focus_stage_age_ms\":%.3f,"
        "\"focus_stamps\":[%u,%u],"
        "\"focus_vertices\":[%d,%d,%d],\"target_stage\":",
        focusPipeline.stageAgeMs, focusPipeline.snapshotStamp,
        focusPipeline.currentStamp, focusPipeline.solidVertices,
        focusPipeline.waterVertices, focusPipeline.floraVertices);
    TraceAppendJsonString(
        &buffer, targeted.hit
            ? ChunkPipelineStageName(targetPipeline.stage) : "none");
    TraceAppendFormat(
        &buffer,
        ",\"target_stage_age_ms\":%.3f,"
        "\"target_stamps\":[%u,%u],"
        "\"target_vertices\":[%d,%d,%d]}}\n",
        targetPipeline.stageAgeMs, targetPipeline.snapshotStamp,
        targetPipeline.currentStamp, targetPipeline.solidVertices,
        targetPipeline.waterVertices, targetPipeline.floraVertices);
    return TraceWrite(game, &buffer);
}

bool GameDebugTraceStart(GameRuntime *game)
{
    if (!game || !game->debugTraceEnabled) return true;
    if (game->debugTracePathInvalid || !ResolveDefaultPath(game)) {
        fprintf(stderr, "Invalid debug trace path\n");
        game->debugTraceEnabled = false;
        return false;
    }
    FILE *file = fopen(game->debugTracePath, "wb");
    if (!file) {
        fprintf(stderr, "Failed to open debug trace %s: %s\n",
                game->debugTracePath, strerror(errno));
        game->debugTraceEnabled = false;
        return false;
    }
    game->debugTrace = (GameDebugTraceState){
        .file = file,
        .nextSample = 0.0,
        .startedMonotonicMs = TraceClockMs(CLOCK_MONOTONIC),
        .startedCpuMs = TraceClockMs(CLOCK_PROCESS_CPUTIME_ID),
        .startedMainCpuMs = TraceClockMs(CLOCK_THREAD_CPUTIME_ID),
        .startedUnixMs = TraceUnixMs()
    };
    if (setvbuf(file, NULL, _IOLBF, 0) != 0) {
        TraceFail(game, "buffer setup");
        return false;
    }
    fprintf(stderr, "DEBUG_TRACE path=%s\n", game->debugTracePath);
    return WriteStart(game);
}

void GameDebugTraceFrame(GameRuntime *game, const GameFrameView *frame)
{
    if (!game || !frame || !game->debugTrace.file ||
        game->screen != SCREEN_PLAYING) return;
    GameDebugTraceState *trace = &game->debugTrace;
    trace->frame++;
    if (isfinite(frame->dt) && frame->dt > 0.0f) {
        trace->elapsed += frame->dt;
    }

    int focusCx = 0;
    int focusCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(game->player.position.x),
                      (int)floorf(game->player.position.z),
                      &focusCx, &focusCz, &localX, &localZ);
    int focusSectionY = SurfaceSectionYFromBlockY(
        (int)floorf(game->player.position.y));
    bool focusChanged = !trace->haveFocus || focusCx != trace->lastCx ||
                        focusCz != trace->lastCz ||
                        focusSectionY != trace->lastSectionY;

    const ChunkSection *targetSection = NULL;
    if (frame->hit.hit) {
        int targetCx = 0;
        int targetCz = 0;
        WorldToChunkLocal(frame->hit.x, frame->hit.z, &targetCx, &targetCz,
                          &localX, &localZ);
        const Chunk *targetChunk = FindChunk(targetCx, targetCz);
        targetSection = targetChunk ? ChunkGetSectionConst(
            targetChunk, SurfaceSectionYFromBlockY(frame->hit.y)) : NULL;
    }
    bool invisibleTarget = frame->hit.hit &&
        SectionVertexCount(targetSection) == 0;
    bool invisibleChanged =
        invisibleTarget != trace->lastInvisibleTarget;
    bool waterChanged =
        frame->playerWater.feetSubmerged != trace->lastFeetSubmerged ||
        frame->playerWater.bodySubmerged != trace->lastBodySubmerged ||
        frame->playerWater.eyesSubmerged != trace->lastEyesSubmerged;
    bool periodic = trace->elapsed >= trace->nextSample;
    if (periodic || focusChanged || invisibleChanged || waterChanged) {
        const char *reason = focusChanged ? "focus_change" :
            (invisibleChanged ? "target_visibility_change" :
             (waterChanged ? "water_state_change" : "periodic"));
        WriteSample(game, frame, reason);
        if (periodic && trace->file) {
            trace->nextSample = AdvanceDeadline(
                trace->nextSample, trace->elapsed);
        }
    }

    trace->haveFocus = true;
    trace->lastCx = focusCx;
    trace->lastCz = focusCz;
    trace->lastSectionY = focusSectionY;
    trace->lastInvisibleTarget = invisibleTarget;
    trace->lastFeetSubmerged = frame->playerWater.feetSubmerged;
    trace->lastBodySubmerged = frame->playerWater.bodySubmerged;
    trace->lastEyesSubmerged = frame->playerWater.eyesSubmerged;
}

void GameDebugTraceEvent(GameRuntime *game, const char *reason)
{
    if (!game || !game->debugTrace.file ||
        game->screen != SCREEN_PLAYING) return;
    WriteEvent(game, reason ? reason : "event");
}

void GameDebugTraceStop(GameRuntime *game)
{
    if (!game || !game->debugTrace.file) return;
    TraceBuffer buffer = { 0 };
    TraceAppendFormat(
        &buffer,
        "{\"version\":1,\"type\":\"stop\",\"time\":%.6f,"
        "\"timestamp_unix_ms\":%lld,\"elapsed_real_ms\":%.3f,"
        "\"elapsed_cpu_ms\":%.3f,\"elapsed_main_cpu_ms\":%.3f,"
        "\"frame\":%llu}\n",
        game->debugTrace.elapsed,
        (long long)TraceUnixMs(), TraceElapsedRealMs(game),
        TraceElapsedCpuMs(game), TraceElapsedMainCpuMs(game),
        (unsigned long long)game->debugTrace.frame);
    if (!TraceWrite(game, &buffer)) return;
    FILE *file = game->debugTrace.file;
    game->debugTrace.file = NULL;
    if (fclose(file) != 0 && !game->debugTrace.ioErrorReported) {
        fprintf(stderr, "Debug trace close failed for %s: %s\n",
                game->debugTracePath, strerror(errno));
        game->debugTrace.ioErrorReported = true;
    }
}

#ifdef GAME_DEBUG_TRACE_TESTING
bool GameDebugTraceEscapeForTest(const char *value, char *destination,
                                 size_t destinationSize)
{
    if (!destination || destinationSize == 0u) return false;
    TraceBuffer buffer = { 0 };
    TraceAppendJsonString(&buffer, value);
    if (buffer.failed || buffer.length >= destinationSize) return false;
    memcpy(destination, buffer.data, buffer.length);
    destination[buffer.length] = '\0';
    return true;
}

double GameDebugTraceAdvanceDeadlineForTest(double deadline, double elapsed)
{
    return AdvanceDeadline(deadline, elapsed);
}

bool GameDebugTraceWriteEventForTest(GameRuntime *game, const char *reason)
{
    return WriteEvent(game, reason);
}
#endif
