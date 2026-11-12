#ifndef VOXELCRAFT_PLAYER_WORLD_H
#define VOXELCRAFT_PLAYER_WORLD_H

#include "gameplay/player_sensors.h"

#include "raylib.h"

#include <stdbool.h>

typedef struct PlayerBodyState {
    Vector3 position;
    Vector3 velocity;
    bool onGround;
    bool floating;
} PlayerBodyState;

typedef struct PlayerMoveRequest {
    Vector3 delta;
    bool allowUnready;
    bool canStepUp;
} PlayerMoveRequest;

typedef struct PlayerGravitySample {
    bool inSpace;
    float gravityScale;
    bool hasPlanetGravity;
    Vector3 direction;
    float surfaceDistance;
    float surfaceGravityScale;
} PlayerGravitySample;

/* The world-facing half of player motion. Implementations may wrap either the
 * cube or legacy surface backend, but this contract exposes neither topology
 * nor chunk identity to player policy. */
typedef struct PlayerWorldOps {
    void *user;
    bool (*move)(void *user, PlayerBodyState *body,
                 const PlayerMoveRequest *request);
    bool (*overlaps)(void *user, Vector3 position, bool allowUnready);
    PlayerWaterState (*sampleWater)(void *user, Vector3 position);
    bool (*sampleGravity)(void *user, Vector3 position,
                          PlayerGravitySample *out);
} PlayerWorldOps;

PlayerWorldOps PlayerWorldOpsDefault(void);

#endif
