#ifndef VOXELCRAFT_GAME_PLUGINS_H
#define VOXELCRAFT_GAME_PLUGINS_H

#include <stdbool.h>
#include <stddef.h>

struct GameRuntime;

bool GamePluginsArgumentsValid(const struct GameRuntime *game);
bool GamePluginsStart(struct GameRuntime *game);
void GamePluginsReset(struct GameRuntime *game);
void GamePluginsUpdate(struct GameRuntime *game, float dt);
void GamePluginsRender(struct GameRuntime *game, float dt);
void GamePluginsShutdown(struct GameRuntime *game);
size_t GamePluginsCount(const struct GameRuntime *game);

#endif
