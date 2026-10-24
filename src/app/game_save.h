#ifndef VOXELCRAFT_GAME_SAVE_H
#define VOXELCRAFT_GAME_SAVE_H

#include "gameplay/player_types.h"

#include <stdbool.h>
#include <stdio.h>

void GameSaveMap(const Player *player);
// False leaves the caller's clocks/input state intact; see the posted notice.
bool GameLoadMap(Player *player);

#ifdef GAME_SAVE_TESTING
/* Returns true when the active policy writes the cube edit section. */
bool GameSaveTestPolicyUsesCubeGrid(void);
bool GameSaveTestWriteCubeEdits(FILE *file);
#include "world/world_environment.h"
#include "world/save_format.h"

#include <stdio.h>
#include "world/block_edit_types.h"
const char *GameSaveTestLoadVolcanoState(FILE *file, WorldSaveFormat format);
const char *GameSaveTestLoadLavaState(FILE *file, WorldSaveFormat format);
const char *GameSaveTestLoadAshState(FILE *file, WorldSaveFormat format);
const char *GameSaveTestLoadInventoryRemainders(FILE *file, WorldSaveFormat format);

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
