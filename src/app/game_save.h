#ifndef VOXELCRAFT_GAME_SAVE_H
#define VOXELCRAFT_GAME_SAVE_H

#include "gameplay/player_types.h"

void GameSaveMap(const Player *player);
void GameLoadMap(Player *player);

#endif
