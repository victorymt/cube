#ifndef VOXELCRAFT_SHIP_GROUND_EFFECTS_H
#define VOXELCRAFT_SHIP_GROUND_EFFECTS_H

#include "world/actor_body.h"

#include <stdbool.h>

void ShipGroundEffectsReset(void);
void ShipGroundEffectsEmit(const ActorBody *player, float dt,
                           float requestedIntensity, bool burst);

#endif
