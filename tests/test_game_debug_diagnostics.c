#include "app/game_debug_diagnostics.h"
#include "app/game_runtime.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <stdio.h>

static uint64_t generation = 1u;
static unsigned baseCaptures;
static unsigned detailedCaptures;

uint64_t WorldCurrentGeneration(void) { return generation; }

void ChunksCaptureDiagnosticsAt(ChunkDiagnostics *out,
                                const DiagnosticsCaptureIdentity *identity)
{
    baseCaptures++;
    *out = (ChunkDiagnostics){ .status = CHUNK_DIAGNOSTICS_VALID,
        .identity = *identity, .pendingGenerationJobs = 2 };
}

void ChunksCaptureDetailedDiagnosticsAt(ChunkDiagnostics *out,
                                        const DiagnosticsCaptureIdentity *identity)
{
    detailedCaptures++;
    *out = (ChunkDiagnostics){ .status = CHUNK_DIAGNOSTICS_VALID,
        .identity = *identity, .pendingGenerationJobs = 3,
        .lodConsistency = CHUNK_DIAGNOSTICS_LOD_CONSISTENT };
}

int main(void)
{
    static GameRuntime game;
    game.debugFrame = 12u;
    game.simulationTick = 34u;
    const ChunkDiagnostics *base = GameDebugChunkDiagnostics(&game, false);
    assert(baseCaptures == 1u && detailedCaptures == 0u);
    uint64_t baseId = base->identity.captureId;
    assert(base->identity.frameId == 12u && base->identity.simulationTick == 34u);
    assert(base->lodConsistency == CHUNK_DIAGNOSTICS_LOD_NOT_CAPTURED);
    assert(GameDebugChunkDiagnostics(&game, false)->identity.captureId == baseId);
    assert(baseCaptures == 1u && detailedCaptures == 0u);

    const ChunkDiagnostics *detail = GameDebugChunkDiagnostics(&game, true);
    uint64_t detailId = detail->identity.captureId;
    assert(detailId > baseId && detailedCaptures == 1u);
    assert(detail->lodConsistency == CHUNK_DIAGNOSTICS_LOD_CONSISTENT);
    assert(GameDebugChunkDiagnostics(&game, true)->identity.captureId == detailId);
    assert(GameDebugChunkDiagnostics(&game, false)->identity.captureId == baseId);
    assert(base->pendingGenerationJobs == 2 && detail->pendingGenerationJobs == 3);
    assert(baseCaptures == 1u && detailedCaptures == 1u);

    /* A command invalidates both caches even without advancing the frame. */
    GameDebugInvalidateDiagnostics(&game);
    assert(GameDebugChunkDiagnostics(&game, false)->identity.captureId > detailId);
    assert(GameDebugChunkDiagnostics(&game, true)->identity.captureId > detailId);
    assert(baseCaptures == 2u && detailedCaptures == 2u);
    assert(base->identity.frameId == 12u && detail->identity.frameId == 12u);

    game.debugFrame++;
    GameDebugChunkDiagnostics(&game, false);
    GameDebugChunkDiagnostics(&game, true);
    assert(baseCaptures == 3u && detailedCaptures == 3u);
    assert(base->identity.frameId == 13u && detail->identity.frameId == 13u);

    /* Context changes must invalidate caches independently of frame timing. */
    generation++;
    GameDebugChunkDiagnostics(&game, false);
    GameDebugChunkDiagnostics(&game, true);
    assert(baseCaptures == 4u && detailedCaptures == 4u);
    assert(base->identity.worldGeneration == 2u);
    assert(detail->identity.worldGeneration == 2u);
    puts("game debug diagnostics tests passed");
    return 0;
}
