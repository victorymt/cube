#ifndef VOXELCRAFT_SHIP_VISUAL_INTERNAL_H
#define VOXELCRAFT_SHIP_VISUAL_INTERNAL_H

#include "gameplay/player_types.h"
#include "gameplay/ship.h"

void ShipVisualUpdateMainExhaust(const Player *player, float dt,
                                 ShipDriveMode mode, float demand,
                                 float atmosphereDensity);
void ShipVisualSetDebugExhaust(const Player *player, float demand,
                               float atmosphereDensity);

#endif
