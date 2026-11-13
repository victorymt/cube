#ifndef VOXELCRAFT_CAMERA_DISTANCE_H
#define VOXELCRAFT_CAMERA_DISTANCE_H

/* Camera rendering and terrain streaming consume the same height response
 * today, but they are separate policies: the former follows the resolved
 * camera pose while the latter follows the controlled actor's eye height. */
int CameraRenderDistanceForHeight(float cameraHeight);
int CameraStreamingDistanceForHeight(float actorEyeHeight);

#endif
