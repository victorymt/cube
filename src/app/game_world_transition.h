#ifndef VOXELCRAFT_GAME_WORLD_TRANSITION_H
#define VOXELCRAFT_GAME_WORLD_TRANSITION_H

#include "gameplay/player_types.h"

#include <stdbool.h>

bool GameWorldTransitionBeginDescent(Player *player, bool homeWorldTarget,
                                     Vector3 *outLandingPosition);
bool GameWorldTransitionTryLaunch(Player *player);

#endif
