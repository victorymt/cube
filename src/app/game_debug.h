#ifndef VOXELCRAFT_GAME_DEBUG_H
#define VOXELCRAFT_GAME_DEBUG_H

#include <stdbool.h>

struct GameRuntime;

bool GameDispatchDebugCommand(struct GameRuntime *game);
bool GameDebugTraceStart(struct GameRuntime *game);
void GameDebugTraceFrame(struct GameRuntime *game, float dt);
void GameDebugTraceEvent(struct GameRuntime *game, const char *reason);
void GameDebugTraceStop(struct GameRuntime *game);

#endif
