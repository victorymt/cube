#ifndef VOXELCRAFT_GAME_SAVE_H
#define VOXELCRAFT_GAME_SAVE_H

#include "gameplay/player_types.h"

void GameSaveMap(const Player *player);
void GameLoadMap(Player *player);

#ifdef GAME_SAVE_TESTING
#include "world/world_environment.h"

#include <stdio.h>

bool GameSaveTestLoadSphericalTrailer(
    FILE *file, WorldDimension dimension, Player *player,
    BlockEdit *edits, uint32_t *dimensions, int editCount,
    SurfaceAddress **outEditAddresses, SurfaceMapCell **outEditMapCells);
#endif

#endif
