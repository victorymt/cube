#ifndef VOXELCRAFT_PLAYER_INPUT_H
#define VOXELCRAFT_PLAYER_INPUT_H

#include "raylib.h"

#include <stdbool.h>

typedef struct PlayerInput {
    float forward;
    float strafe;
    float vertical;
    bool sprint;
    bool jumpPressed;
    bool toggleFloating;
    Vector2 lookDelta;
} PlayerInput;

#endif
