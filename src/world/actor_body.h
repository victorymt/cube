#ifndef VOXELCRAFT_ACTOR_BODY_H
#define VOXELCRAFT_ACTOR_BODY_H

#include "raylib.h"

#include <stdbool.h>

/* Topology-neutral kinematic body shared by read-only consumers and the ship
 * runtime. It intentionally carries no Player-only timers or inventory state. */
typedef struct ActorBody {
    Vector3 position;
    Vector3 velocity;
    float yaw;
    float pitch;
    bool onGround;
    bool floating;
} ActorBody;

#endif
