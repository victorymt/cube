#ifndef VOXELCRAFT_GAME_SAVE_H
#define VOXELCRAFT_GAME_SAVE_H

#include "gameplay/player_types.h"

void GameSaveMap(const Player *player);
void GameLoadMap(Player *player);

#ifdef GAME_SAVE_TESTING
#include "world/world_environment.h"
#include "world/save_format.h"

#include <stdio.h>
const char *GameSaveTestLoadVolcanoState(FILE *file, WorldSaveFormat format);

bool GameSaveTestLoadSphericalTrailer(
    FILE *file, WorldDimension dimension, Player *player,
    BlockEdit *edits, uint32_t *dimensions, int editCount,
    SurfaceAddress **outEditAddresses, SurfaceMapCell **outEditMapCells);
bool GameSaveTestWriteRadiationRuntimeState(FILE *file);
const char *GameSaveTestLoadRadiationRuntimeState(
    FILE *file, WorldSaveFormat format);
bool GameSaveTestWriteHydrologyRuntimeState(FILE *file);
const char *GameSaveTestLoadHydrologyRuntimeState(
    FILE *file, WorldSaveFormat format);
#endif

#endif
