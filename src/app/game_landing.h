#ifndef VOXELCRAFT_GAME_LANDING_H
#define VOXELCRAFT_GAME_LANDING_H

#include "app/game_runtime.h"

bool LandingTransitionBegin(LandingTransition *transition, Player *player);
bool LandingTransitionUpdate(LandingTransition *transition, Player *player,
                             float dt);
void DrawLandingTransitionOverlay(const LandingTransition *transition);

#endif
