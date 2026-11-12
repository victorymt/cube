#ifndef VOXELCRAFT_PLAYER_CAMERA_H
#define VOXELCRAFT_PLAYER_CAMERA_H

#include "gameplay/player_types.h"

#include "raylib.h"

#include <stdbool.h>

float CameraHeightFactor(float eyeHeight);
float CameraFovForHeight(float eyeHeight);
int EffectiveRenderDistanceForHeight(float eyeHeight);
bool PlayerCameraPositionInsideSolid(Vector3 position);
Vector3 PlayerResolveCameraPosition(Vector3 pivot, Vector3 desired);
void UpdatePlayerCamera(Camera3D *camera, const Player *player, float dt,
                        bool thirdPerson);

#endif
