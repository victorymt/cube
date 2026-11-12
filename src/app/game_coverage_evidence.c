#include "app/game_coverage_evidence.h"

#include "app/game_runtime.h"
#include "gameplay/player_motion.h"
#include "world/chunks_cube.h"
#include "world/world_environment.h"

#include <string.h>

static bool CoverageBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool CoverageNumber(DebugDslValue *outValue, uint64_t value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

bool GameCoverageEvidenceCapture(const struct GameRuntime *game,
                                 GameCoverageEvidence *out)
{
    if (!out) return false;
    *out = (GameCoverageEvidence){ 0 };
    if (!game) return false;
    out->active = ChunksCubeCoverageIsActive();
    CubeCoverageStatus status = { 0 };
    out->available = out->active && ChunksLastCubeCoverage(&status);
    if (!out->available) return true;
    CubeCoverageObservation observation = { 0 };
    (void)ChunksCubeCoverageObservation(&observation);
    bool localTerrainReady = game->screen == SCREEN_PLAYING &&
        WorldIsSurfaceActive() &&
        PlayerMissingSurfaceChunkCount(game->player.position) == 0;
    out->coverage = SurfaceCoverageEvidenceBuild(
        status.requested, status.admitted, status.ready, status.pending,
        status.missing, status.submitted, status.released, status.saturated,
        status.duplicates, status.stale,
        ChunksCubeTargetLodReadyCount(),
        ChunksCubeTargetLodMissingSections(), observation.sequence,
        localTerrainReady);
    return true;
}

bool GameCoverageEvidenceDslResolve(const char *name,
                                    const GameCoverageEvidence *evidence,
                                    DebugDslValue *outValue)
{
    if (!name || !evidence || !outValue ||
        strncmp(name, "stream.cube.", 12u) != 0) return false;
    const SurfaceCoverageEvidence *coverage = &evidence->coverage;
    const char *field = name + 12u;
    if (strcmp(field, "active") == 0)
        return CoverageBool(outValue, evidence->active);
    if (strcmp(field, "available") == 0)
        return CoverageBool(outValue, evidence->available);
    if (strcmp(field, "observation_sequence") == 0)
        return CoverageNumber(outValue, coverage->observationSequence);
    if (strcmp(field, "saturated") == 0)
        return CoverageBool(outValue, coverage->saturated);
    if (strcmp(field, "local_terrain_ready") == 0)
        return CoverageBool(outValue, coverage->localTerrainReady);
    if (strcmp(field, "visual_ready") == 0)
        return CoverageBool(outValue, coverage->visualReady);
    if (strcmp(field, "requested") == 0)
        return CoverageNumber(outValue, coverage->requested);
    if (strcmp(field, "admitted") == 0)
        return CoverageNumber(outValue, coverage->admitted);
    if (strcmp(field, "ready") == 0)
        return CoverageNumber(outValue, coverage->ready);
    if (strcmp(field, "pending") == 0)
        return CoverageNumber(outValue, coverage->pending);
    if (strcmp(field, "missing") == 0)
        return CoverageNumber(outValue, coverage->missing);
    if (strcmp(field, "submitted") == 0)
        return CoverageNumber(outValue, coverage->submitted);
    if (strcmp(field, "released") == 0)
        return CoverageNumber(outValue, coverage->released);
    if (strcmp(field, "duplicates") == 0)
        return CoverageNumber(outValue, coverage->duplicates);
    if (strcmp(field, "stale") == 0)
        return CoverageNumber(outValue, coverage->stale);
    if (strcmp(field, "target_lod_ready") == 0)
        return CoverageNumber(outValue, coverage->targetLodReady);
    if (strcmp(field, "target_lod_missing_sections") == 0)
        return CoverageNumber(outValue, coverage->targetLodMissingSections);
    return false;
}
