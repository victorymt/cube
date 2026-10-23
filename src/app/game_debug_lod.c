#include "app/game_debug_lod.h"

#include "world/chunks.h"
#include "world/chunks_cube.h"

#include <string.h>

static bool LodDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool LodDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

/* Cube coverage is reported alongside the LOD counters because both describe
 * what the streamer is doing this frame, and the DSL groups them under
 * `stream.`. */
static bool GameDebugCubeDslResolve(const char *name, DebugDslValue *outValue)
{
    if (strncmp(name, "stream.cube.", 12u) != 0) return false;
    if (strcmp(name, "stream.cube.active") == 0) {
        return LodDslBool(outValue, ChunksCubeCoverageIsActive());
    }
    CubeCoverageStatus status;
    bool have = ChunksLastCubeCoverage(&status);
    if (strcmp(name, "stream.cube.ready") == 0) {
        return LodDslNumber(outValue, have ? status.ready : 0u);
    }
    if (strcmp(name, "stream.cube.requested") == 0) {
        return LodDslNumber(outValue, have ? status.requested : 0u);
    }
    if (strcmp(name, "stream.cube.admitted") == 0) {
        return LodDslNumber(outValue, have ? status.admitted : 0u);
    }
    if (strcmp(name, "stream.cube.missing") == 0) {
        return LodDslNumber(outValue, have ? status.missing : 0u);
    }
    return false;
}

bool GameDebugLodDslResolveSnapshot(const ChunkDiagnostics *diagnostics,
                                    const char *name,
                                    DebugDslValue *outValue)
{
    if (!name || !outValue) return false;
    if (GameDebugCubeDslResolve(name, outValue)) return true;
    if (!diagnostics || strncmp(name, "stream.lod_", 11u) != 0) {
        return false;
    }

    const ChunkLodStats *stats = &diagnostics->lod;
    if (strcmp(name, "stream.lod_coarse_allowed") == 0) {
        return LodDslBool(outValue, stats->coarseAllowed);
    }
    if (strcmp(name, "stream.lod_target_changes") == 0) {
        return LodDslNumber(outValue, stats->targetChanges);
    }
#define LOD_DSL_NUMBER(fieldName, fieldValue) \
    if (strcmp(name, fieldName) == 0) { \
        return LodDslNumber(outValue, (fieldValue)); \
    }
    LOD_DSL_NUMBER("stream.lod_capture_id", diagnostics->identity.captureId)
    LOD_DSL_NUMBER("stream.lod_frame_id", diagnostics->identity.frameId)
    LOD_DSL_NUMBER("stream.lod_simulation_tick", diagnostics->identity.simulationTick)
    LOD_DSL_NUMBER("stream.lod_world_generation", diagnostics->identity.worldGeneration)
    LOD_DSL_NUMBER("stream.lod_target_exact",
                   stats->targetChunks[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_target_half",
                   stats->targetChunks[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_target_quarter",
                   stats->targetChunks[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_active_exact",
                   stats->activeChunks[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_active_half",
                   stats->activeChunks[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_active_quarter",
                   stats->activeChunks[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_ready_exact_sections",
                   stats->readySections[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_ready_half_sections",
                   stats->readySections[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_ready_quarter_sections",
                   stats->readySections[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_proxy_flora_half",
                   stats->proxyFloraSections[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_proxy_flora_quarter",
                   stats->proxyFloraSections[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_jobs_exact",
                   stats->pendingJobs[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_jobs_half",
                   stats->pendingJobs[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_jobs_quarter",
                   stats->pendingJobs[CHUNK_LOD_QUARTER])
#undef LOD_DSL_NUMBER
    return false;
}

bool GameDebugLodDslResolve(const char *name, DebugDslValue *outValue)
{
    if (!name || !outValue || strncmp(name, "stream.lod_", 11u) != 0)
        return false;
    ChunkDiagnostics diagnostics = { 0 };
    ChunksCaptureDetailedDiagnostics(&diagnostics);
    return GameDebugLodDslResolveSnapshot(&diagnostics, name, outValue);
}
