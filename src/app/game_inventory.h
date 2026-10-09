#ifndef VOXELCRAFT_GAME_INVENTORY_H
#define VOXELCRAFT_GAME_INVENTORY_H
#include <stdbool.h>
#include "core/debug_dsl.h"
typedef struct GameRuntime GameRuntime;
bool GameInventorySetOpen(GameRuntime *game, bool open);
bool GameInventoryDebug(GameRuntime *game, const char *command);
bool GameInventoryResolve(GameRuntime *game, const char *name, DebugDslValue *out);
#endif
