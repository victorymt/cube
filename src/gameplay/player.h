#ifndef VOXELCRAFT_PLAYER_H
#define VOXELCRAFT_PLAYER_H

#include "gameplay/player_input.h"
#include "gameplay/player_types.h"

void PlayerResetRuntimeState(Player *player);
void UpdatePlayerWithInput(Player *player, float dt, const PlayerInput *input);

#endif
