#ifndef VOXELCRAFT_CAMERA_WORLD_H
#define VOXELCRAFT_CAMERA_WORLD_H

#include "gameplay/camera.h"

#include <stdbool.h>

CameraWorldOps CameraWorldOpsDefault(void);

/* Compatibility query used by diagnostics and camera regression tests. */
bool PlayerCameraPositionInsideSolid(Vector3 position);
float RaycastCameraOcclusion(Vector3 origin, Vector3 direction,
                             float maxDistance);

#endif
