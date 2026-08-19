#ifndef VOXELCRAFT_GAME_DEBUG_GATE_H
#define VOXELCRAFT_GAME_DEBUG_GATE_H

#include "core/debug_control.h"

struct GameRuntime;

const char *GameDebugDslCommandBlocked(
    const struct GameRuntime *game, DebugControlCommand command);

#endif
