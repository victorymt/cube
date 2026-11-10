#ifndef VOXELCRAFT_GAME_DEBUG_RENDER_H
#define VOXELCRAFT_GAME_DEBUG_RENDER_H

#include "core/debug_control.h"

#include <stdbool.h>

struct GameRuntime;
struct DebugDslValue;

bool GameDebugRenderDispatch(struct GameRuntime *game,
                             DebugControlCommand command);
bool GameDebugRenderDslResolve(struct GameRuntime *game, const char *name,
                               struct DebugDslValue *outValue);

#endif
