#include "app/game_debug_lod.h"

#include "world/chunks.h"

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

bool GameDebugLodDslResolve(const char *name, DebugDslValue *outValue)
{
    if (!name || !outValue || strncmp(name, "stream.lod_", 11u) != 0) {
        return false;
    }

    ChunkLodStats stats = ChunksGetLodStats();
    if (strcmp(name, "stream.lod_coarse_allowed") == 0) {
        return LodDslBool(outValue, stats.coarseAllowed);
    }
    if (strcmp(name, "stream.lod_target_changes") == 0) {
        return LodDslNumber(outValue, stats.targetChanges);
    }
#define LOD_DSL_NUMBER(fieldName, fieldValue) \
    if (strcmp(name, fieldName) == 0) { \
        return LodDslNumber(outValue, (fieldValue)); \
    }
    LOD_DSL_NUMBER("stream.lod_target_exact",
                   stats.targetChunks[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_target_half",
                   stats.targetChunks[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_target_quarter",
                   stats.targetChunks[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_active_exact",
                   stats.activeChunks[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_active_half",
                   stats.activeChunks[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_active_quarter",
                   stats.activeChunks[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_ready_exact_sections",
                   stats.readySections[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_ready_half_sections",
                   stats.readySections[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_ready_quarter_sections",
                   stats.readySections[CHUNK_LOD_QUARTER])
    LOD_DSL_NUMBER("stream.lod_jobs_exact",
                   stats.pendingJobs[CHUNK_LOD_EXACT])
    LOD_DSL_NUMBER("stream.lod_jobs_half",
                   stats.pendingJobs[CHUNK_LOD_HALF])
    LOD_DSL_NUMBER("stream.lod_jobs_quarter",
                   stats.pendingJobs[CHUNK_LOD_QUARTER])
#undef LOD_DSL_NUMBER
    return false;
}
