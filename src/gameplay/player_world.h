#ifndef VOXELCRAFT_PLAYER_WORLD_H
#define VOXELCRAFT_PLAYER_WORLD_H

#include "gameplay/player_sensors.h"
#include "world/actor_motion.h"
#include "world/environment_query.h"

#include "raylib.h"

#include <stdbool.h>

typedef WorldGravitySample PlayerGravitySample;

/* The world-facing half of player motion. Implementations may wrap either the
 * cube or legacy surface backend, but this contract exposes neither topology
 * nor chunk identity to player policy. */
typedef struct PlayerWorldOps {
    void *user;
    bool (*move)(void *user, ActorBody *body,
                 const ActorMotionRequest *request);
    bool (*overlaps)(void *user, Vector3 position, bool allowUnready);
    PlayerWaterState (*sampleWater)(void *user, Vector3 position,
                                    double simulationTime);
    bool (*sampleGravity)(void *user, Vector3 position, double simulationTime,
                          PlayerGravitySample *out);
} PlayerWorldOps;

PlayerWorldOps PlayerWorldOpsDefault(void);

#endif
