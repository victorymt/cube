#ifndef VOXELCRAFT_GAME_DEBUG_WEATHER_H
#define VOXELCRAFT_GAME_DEBUG_WEATHER_H

#include "app/game_debug.h"
#include "core/debug_control.h"
#include "core/debug_dsl.h"

#include <stdbool.h>

struct GameRuntime;

GameDebugDispatchResult GameDebugDispatchWeatherCommand(
    struct GameRuntime *game, DebugControlCommand command);

bool GameDebugWeatherDslResolve(const struct GameRuntime *game,
                                const char *name,
                                DebugDslValue *outValue);

#endif
