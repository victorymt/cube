#ifndef VOXELCRAFT_PLAYER_H
#define VOXELCRAFT_PLAYER_H

#include "gameplay/player_input.h"
#include "gameplay/player_types.h"
#include "gameplay/player_world.h"

void PlayerResetRuntimeState(Player *player);
void UpdatePlayerWithWorld(Player *player, float dt, const PlayerInput *input,
                           const PlayerWorldOps *world);

#endif
