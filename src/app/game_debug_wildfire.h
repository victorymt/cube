#ifndef VOXELCRAFT_GAME_DEBUG_WILDFIRE_H
#define VOXELCRAFT_GAME_DEBUG_WILDFIRE_H

#include "core/debug_control.h"

#include <stdbool.h>

struct GameRuntime;

bool GameDebugDispatchWildfireCommand(
    struct GameRuntime *game, DebugControlCommand command);

#endif
