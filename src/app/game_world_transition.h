#ifndef VOXELCRAFT_GAME_WORLD_TRANSITION_H
#define VOXELCRAFT_GAME_WORLD_TRANSITION_H

#include "gameplay/player_types.h"
#include "space/planet_profile.h"

#include <stdbool.h>

bool GameWorldTransitionBeginDescent(Player *player, bool homeWorldTarget,
                                     Vector3 *outLandingPosition);
bool GameWorldTransitionTryLaunch(Player *player);
bool GameWorldTransitionDebugSurface(Player *player, bool homeWorld,
                                     SolarBodyStyle style, uint32_t seed);

#endif
