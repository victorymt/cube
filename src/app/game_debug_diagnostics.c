#include "app/game_debug_diagnostics.h"

#include "app/game_runtime.h"
#include "world/world_surface_snapshot.h"

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
