#ifndef VOXELCRAFT_SHIP_H
#define VOXELCRAFT_SHIP_H

#include "types.h"

#define SHIP_MAX_SPEED 30.0f

bool ShipTryEnter(int x, int y, int z, Player *player);
bool ShipIsDriving(void);
void ShipUpdate(Player *player, float dt);
void ShipExit(Player *player);

#endif
