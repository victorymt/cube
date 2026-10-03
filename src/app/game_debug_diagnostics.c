#include "app/game_debug_diagnostics.h"

#include "app/game_runtime.h"
#include "world/world_surface_snapshot.h"

#include <string.h>

const ChunkDiagnostics *GameDebugChunkDiagnostics(
    GameRuntime *game, bool includeLod)
{
    if (!game) return NULL;
    ChunkDiagnostics *diagnostics = includeLod
        ? &game->debugLodDiagnostics : &game->debugChunkDiagnostics;
    bool *valid = includeLod
        ? &game->debugLodDiagnosticsValid : &game->debugChunkDiagnosticsValid;
    uint64_t *frame = includeLod
        ? &game->debugLodDiagnosticsFrame : &game->debugChunkDiagnosticsFrame;
    if (!*valid || *frame != game->debugFrame ||
        diagnostics->identity.worldGeneration != WorldCurrentGeneration()) {
        DiagnosticsCaptureIdentity identity =
            GameRuntimeCaptureDiagnosticsIdentity(game);
        if (includeLod) ChunksCaptureDetailedDiagnosticsAt(diagnostics, &identity);
        else ChunksCaptureDiagnosticsAt(diagnostics, &identity);
        *frame = game->debugFrame;
        *valid = true;
    }
    return diagnostics;
}

void GameDebugInvalidateDiagnostics(GameRuntime *game)
{
    if (!game) return;
    game->debugChunkDiagnosticsValid = false;
    game->debugLodDiagnosticsValid = false;
}

static bool DiagnosticsDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool DiagnosticsDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool DiagnosticsDslString(DebugDslValue *outValue, const char *value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_STRING,
        .as.string = value ? value : ""
    };
    return true;
}

static bool DiagnosticsDslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_VEC3,
        .as.vec3 = { value.x, value.y, value.z }
    };
    return true;
}


bool GameDebugDiagnosticsDslResolve(GameRuntime *game, const char *name,
                                    DebugDslValue *outValue)
{
    if (!game || !name || !outValue || strncmp(name, "stream.chunks.", 14u) != 0)
        return false;
    const ChunkDiagnostics *chunkDiagnostics = GameDebugChunkDiagnostics(game, false);
    if (chunkDiagnostics && strncmp(name, "stream.chunks.", 14u) == 0) {
        const char *field = name + 14u;
        if (!strcmp(field, "status")) {
            return DiagnosticsDslString(
                outValue, ChunkDiagnosticsStatusName(
                    chunkDiagnostics->status));
        }
        if (!strcmp(field, "capture_sequence")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->captureSequence);
        }
        if (!strcmp(field, "capture_id")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->identity.captureId);
        }
        if (!strcmp(field, "frame_id")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->identity.frameId);
        }
        if (!strcmp(field, "simulation_tick")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->identity.simulationTick);
        }
        if (!strcmp(field, "world_generation")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->identity.worldGeneration);
        }
        if (!strcmp(field, "blocked_reason")) {
            return DiagnosticsDslString(outValue,
                                      chunkDiagnostics->blockedReason);
        }
        if (!strcmp(field, "blocked_reason_sequence")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->blockedReasonSequence);
        }
        if (!strcmp(field, "last_event_reason")) {
            return DiagnosticsDslString(outValue, chunkDiagnostics->lastEvent.reason);
        }
        if (!strcmp(field, "last_event_sequence")) {
            return DiagnosticsDslNumber(outValue, (double)chunkDiagnostics->lastEvent.sequence);
        }
        if (!strcmp(field, "last_event_time_ms")) {
            return DiagnosticsDslNumber(outValue, chunkDiagnostics->lastEvent.timeMs);
        }
        if (!strcmp(field, "last_event_has_location")) {
            return DiagnosticsDslBool(outValue, chunkDiagnostics->lastEvent.hasLocation);
        }
        if (!strcmp(field, "last_event_has_section")) {
            return DiagnosticsDslBool(outValue, chunkDiagnostics->lastEvent.hasSection);
        }
        if (!strcmp(field, "last_event_location")) {
            return DiagnosticsDslVec3(outValue, (Vector3){
                chunkDiagnostics->lastEvent.chunkX,
                chunkDiagnostics->lastEvent.sectionY,
                chunkDiagnostics->lastEvent.chunkZ });
        }
        if (!strcmp(field, "completed_generation_jobs")) {
            return DiagnosticsDslNumber(outValue, chunkDiagnostics->completedGenerationJobs);
        }
        if (!strcmp(field, "completed_mesh_jobs")) {
            return DiagnosticsDslNumber(outValue, chunkDiagnostics->completedMeshJobs);
        }
        if (!strcmp(field, "lod_consistency")) {
            return DiagnosticsDslString(
                outValue, ChunkDiagnosticsLodConsistencyName(
                    chunkDiagnostics->lodConsistency));
        }
        if (!strcmp(field, "active_chunks")) {
            return DiagnosticsDslNumber(outValue,
                                      chunkDiagnostics->activeChunks);
        }
        if (!strcmp(field, "pending_generation_jobs")) {
            return DiagnosticsDslNumber(
                outValue, chunkDiagnostics->pendingGenerationJobs);
        }
        if (!strcmp(field, "pending_mesh_jobs")) {
            return DiagnosticsDslNumber(
                outValue, chunkDiagnostics->pendingMeshJobs);
        }
        if (!strcmp(field, "focus_chunk_loaded")) {
            return DiagnosticsDslBool(outValue,
                                    chunkDiagnostics->focusChunkLoaded);
        }
        if (!strcmp(field, "generation_submitted")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->streaming.generationSubmitted);
        }
        if (!strcmp(field, "generation_completed")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->streaming.generationCompleted);
        }
        if (!strcmp(field, "mesh_submitted")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->streaming.meshSubmitted);
        }
        if (!strcmp(field, "mesh_completed")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->streaming.meshCompleted);
        }
        if (!strcmp(field, "upload_budget_deferrals")) {
            return DiagnosticsDslNumber(
                outValue, (double)chunkDiagnostics->streaming.uploadBudgetDeferrals);
        }
        return false;
    }
    return false;
}
