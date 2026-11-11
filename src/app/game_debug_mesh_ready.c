#include "app/game_internal.h"

#include "world/chunks.h"

void GameUpdateDebugLeftMeshReady(GameRuntime *game)
{
    if (!game || !game->debugLeftMeshPending) return;
    bool ready = false;
    if (game->debugLeftHasAddress) {
        ready = ChunksBlockMeshReadyAtAddress(game->debugLeftAddress);
    } else {
        ready = ChunksBlockMeshReadyAtMapPosition(
            game->debugLeftTargetX, game->debugLeftTargetY,
            game->debugLeftTargetZ);
    }
    if (ready) {
        game->debugLeftMeshPending = false;
        game->debugLeftMeshFrame = game->debugFrame;
        GameDebugTraceEvent(game, "mouse_left_mesh_ready");
    }
}
