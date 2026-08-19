#ifndef VOXELCRAFT_GAME_DEBUG_TOPOLOGY_H
#define VOXELCRAFT_GAME_DEBUG_TOPOLOGY_H

#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

void GameDebugTopologyReply(struct GameRuntime *game);
bool GameDebugTopologyDslResolve(const struct GameRuntime *game,
                                 const char *name,
                                 DebugDslValue *outValue);

#endif
