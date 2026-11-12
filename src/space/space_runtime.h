#ifndef VOXELCRAFT_SPACE_RUNTIME_H
#define VOXELCRAFT_SPACE_RUNTIME_H

#include "world/actor_body.h"

#include <stdbool.h>

void SpaceInit(void);
void SpaceShutdown(void);
void SpaceReset(void);
void SpaceAdvanceTime(float gameTimeDelta);
bool SpaceRebaseActor(ActorBody *actor);
void SpaceResetOrigin(void);

#endif
