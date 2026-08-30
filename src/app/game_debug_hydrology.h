#ifndef VOXELCRAFT_GAME_DEBUG_HYDROLOGY_H
#define VOXELCRAFT_GAME_DEBUG_HYDROLOGY_H

#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

bool GameDebugHydrologyDslResolve(const struct GameRuntime *game,
                                  const char *name,
                                  DebugDslValue *outValue);

#endif
