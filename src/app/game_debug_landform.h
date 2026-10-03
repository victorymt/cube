#ifndef VOXELCRAFT_GAME_DEBUG_LANDFORM_H
#define VOXELCRAFT_GAME_DEBUG_LANDFORM_H
#include "core/debug_control.h"
#include "core/debug_dsl.h"
struct GameRuntime;
bool GameDebugLandformDispatch(struct GameRuntime *game, DebugControlCommand command);
bool GameDebugLandformResolve(struct GameRuntime *game, const char *name, DebugDslValue *out);
#endif
