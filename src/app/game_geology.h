#ifndef VOXELCRAFT_GAME_GEOLOGY_H
#define VOXELCRAFT_GAME_GEOLOGY_H
#include "core/debug_control.h"
#include "core/debug_dsl.h"
struct GameRuntime;
struct GameFrameView;
void GameGeologyUpdate(struct GameRuntime *game, double seconds);
void GameGeologyDraw(const struct GameRuntime *game, const struct GameFrameView *frame);
bool GameDebugGeologyDispatch(struct GameRuntime *game, DebugControlCommand command);
bool GameDebugGeologyResolve(struct GameRuntime *game, const char *name, DebugDslValue *out);
#endif
