#ifndef VOXELCRAFT_GAME_BIOLOGY_H
#define VOXELCRAFT_GAME_BIOLOGY_H

#include "app/game_runtime.h"
#include "ecology/entity.h"

void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info,
                            bool locked);
void GameInstallWorldEcologyHooks(void);
int BiologyAtlasNextSlot(int current, int direction);
void DrawBiologyAtlas(GameRuntime *game);

#endif
