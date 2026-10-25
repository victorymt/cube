#ifndef VOXELCRAFT_GAME_DEBUG_H
#define VOXELCRAFT_GAME_DEBUG_H

#include <stdbool.h>

struct GameRuntime;

/* Command dispatch is shared across the per-domain debug files: each one
 * handles the commands it owns and reports UNHANDLED so the chain continues. */
typedef enum GameDebugDispatchResult {
    GAME_DEBUG_DISPATCH_UNHANDLED = 0,
    GAME_DEBUG_DISPATCH_HANDLED,
    GAME_DEBUG_DISPATCH_START,
    GAME_DEBUG_DISPATCH_ERROR
} GameDebugDispatchResult;

void GameDebugMarkCommandError(struct GameRuntime *game, const char *reason);

bool GameDebugScriptLoad(struct GameRuntime *game);
void GameDebugScriptStop(struct GameRuntime *game);
bool GameDispatchDebugCommand(struct GameRuntime *game);
#endif
