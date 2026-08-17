#ifndef VOXELCRAFT_SPACE_RUNTIME_H
#define VOXELCRAFT_SPACE_RUNTIME_H

#include <stdbool.h>

struct Player;

void SpaceInit(void);
void SpaceShutdown(void);
void SpaceReset(void);
void SpaceAdvanceTime(float gameTimeDelta);
bool SpaceRebasePlayer(struct Player *player);
void SpaceResetOrigin(void);

#endif
