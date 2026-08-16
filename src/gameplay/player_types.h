#ifndef VOXELCRAFT_PLAYER_TYPES_H
#define VOXELCRAFT_PLAYER_TYPES_H

#include "core/config.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct Player {
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float pitch;
    bool onGround;
    bool floating;
    bool wasInWater;
    float stepTimer;
} Player;

typedef struct HitResult {
    bool hit;
    int x;
    int y;
    int z;
    int nx;
    int ny;
    int nz;
} HitResult;

#endif
