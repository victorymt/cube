#ifndef VOXELCRAFT_GAME_DEBUG_BLOCK_H
#define VOXELCRAFT_GAME_DEBUG_BLOCK_H

#include "core/debug_control.h"
#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

bool GameDebugBlockDispatch(struct GameRuntime *game,
                            DebugControlCommand command);
bool GameDebugBlockDslResolve(const struct GameRuntime *game,
                              const char *name, DebugDslValue *outValue);

#endif
