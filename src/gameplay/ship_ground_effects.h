#ifndef VOXELCRAFT_SHIP_GROUND_EFFECTS_H
#define VOXELCRAFT_SHIP_GROUND_EFFECTS_H

#include "gameplay/player_types.h"

#include <stdbool.h>

void ShipGroundEffectsReset(void);
void ShipGroundEffectsEmit(const Player *player, float dt,
                           float requestedIntensity, bool burst);

#endif
