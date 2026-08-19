#ifndef VOXELCRAFT_GAME_DEBUG_FLORA_H
#define VOXELCRAFT_GAME_DEBUG_FLORA_H

#include "core/debug_control.h"
#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

bool GameDebugFloraDispatch(struct GameRuntime *game,
                            DebugControlCommand command);
bool GameDebugFloraDslResolve(const struct GameRuntime *game,
                              const char *name, DebugDslValue *outValue);

#endif
