#ifndef VOXELCRAFT_GAME_GEOLOGY_H
#define VOXELCRAFT_GAME_GEOLOGY_H
#include "core/debug_control.h"
#include "core/debug_dsl.h"
#include "world/world_types.h"
struct GameRuntime;
struct GameFrameView;
void GameGeologyUpdate(struct GameRuntime *game, double seconds);
void GameGeologyDraw(const struct GameRuntime *game, const struct GameFrameView *frame);
BlockType GameGeologyBlockAt(int x, int y, int z);
float GameGeologyCollisionHeightAt(int x, int y, int z);
float GameGeologyLiquidHeightAt(int x, int y, int z);
bool GameGeologyCanSetBlock(int x, int y, int z, BlockType next);
bool GameDebugGeologyDispatch(struct GameRuntime *game, DebugControlCommand command);
bool GameDebugGeologyResolve(struct GameRuntime *game, const char *name, DebugDslValue *out);
#endif
