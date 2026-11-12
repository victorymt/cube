#ifndef VOXELCRAFT_SHIP_VISUAL_INTERNAL_H
#define VOXELCRAFT_SHIP_VISUAL_INTERNAL_H

#include "world/actor_body.h"
#include "gameplay/ship.h"

void ShipVisualUpdateMainExhaust(const ActorBody *player, float dt,
                                 ShipDriveMode mode, float demand,
                                 float atmosphereDensity);
void ShipVisualSetDebugExhaust(const ActorBody *player, float demand,
                               float atmosphereDensity);

#endif
