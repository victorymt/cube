#ifndef VOXELCRAFT_PLAYER_H
#define VOXELCRAFT_PLAYER_H

#include "types.h"

bool IsSolidBlockAt(int x, int y, int z);
bool PlayerOverlapsWorld(Vector3 position);
void MovePlayer(Player *player, Vector3 delta);
Vector3 ForwardFromAngles(float yaw, float pitch);
Vector3 FlatForward(float yaw);
Vector3 RightFromYaw(float yaw);
float CameraHeightFactor(float eyeHeight);
float CameraFovForHeight(float eyeHeight);
int EffectiveRenderDistanceForHeight(float eyeHeight);
void UpdatePlayerCamera(Camera3D *camera, const Player *player, float dt, bool thirdPerson);
void UpdatePlayer(Player *player, float dt);

#endif
