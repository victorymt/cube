#ifndef VOXELCRAFT_GAME_DEBUG_WEATHER_H
#define VOXELCRAFT_GAME_DEBUG_WEATHER_H

#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

bool GameDebugWeatherDslResolve(const struct GameRuntime *game,
                                const char *name,
                                DebugDslValue *outValue);

#endif
