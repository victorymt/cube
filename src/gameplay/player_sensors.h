#ifndef VOXELCRAFT_PLAYER_SENSORS_H
#define VOXELCRAFT_PLAYER_SENSORS_H

#include "world/environment_query.h"

#include "raylib.h"

typedef WorldWaterSample PlayerWaterState;

PlayerWaterState PlayerWaterStateAt(Vector3 position);

#endif
