#ifndef VOXELCRAFT_PLAYER_SENSORS_H
#define VOXELCRAFT_PLAYER_SENSORS_H

#include "raylib.h"

#include <stdbool.h>

typedef struct PlayerWaterState {
    bool feetSubmerged;
    bool bodySubmerged;
    bool eyesSubmerged;
    float surfaceY;
    float eyeDepth;
    Vector3 flowVelocity;
} PlayerWaterState;

PlayerWaterState PlayerWaterStateAt(Vector3 position);

#endif
