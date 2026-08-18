#ifndef VOXELCRAFT_GAME_DEBUG_H
#define VOXELCRAFT_GAME_DEBUG_H

#include <stdbool.h>

struct GameRuntime;

bool GameDebugScriptLoad(struct GameRuntime *game);
void GameDebugScriptStop(struct GameRuntime *game);
bool GameDispatchDebugCommand(struct GameRuntime *game);
#endif
