#ifndef VOXELCRAFT_ACTOR_LOCOMOTION_H
#define VOXELCRAFT_ACTOR_LOCOMOTION_H

#include "gameplay/player_world.h"

#include "world/actor_body.h"
#include "world/environment_query.h"

#include <stdbool.h>

/* Intent is relative to the actor's current yaw. Positive forward moves along
 * the actor's forward axis; positive strafe follows the existing screen-right
 * basis. Jump is an already-staged edge, not a held-key poll. */
typedef struct ActorLocomotionIntent {
    float forward;
    float strafe;
    bool sprint;
    bool jump;
} ActorLocomotionIntent;

/* Capabilities for ordinary ground locomotion. Current position and velocity
 * remain exclusively in ActorBody. */
typedef struct ActorLocomotionProfile {
    float walkSpeed;
    float sprintSpeed;
    float jumpSpeed;
    float minGravityScale;
    float maxGravityScale;
    float maxFallSpeed;
} ActorLocomotionProfile;

typedef struct ActorLocomotionResult {
    ActorBody body;
    bool moved;
    bool jumped;
} ActorLocomotionResult;

/* Compute the intent-derived candidate body without applying collision. */
bool ActorGroundLocomotionCompute(
    const ActorBody *body,
    const ActorLocomotionIntent *intent,
    const ActorLocomotionProfile *profile,
    const WorldEnvironmentSample *environment,
    float dt,
    ActorBody *outBody,
    bool *outJumped);

/* Resolve one fixed-tick ordinary-ground step. Geometry remains in world
 * ActorMotion; this function only computes intent-derived velocity and asks the
 * supplied world ops for the constrained body. It never receives a Player. */
bool ActorGroundLocomotionStep(
    const ActorBody *body,
    const ActorLocomotionIntent *intent,
    const ActorLocomotionProfile *profile,
    const WorldEnvironmentSample *environment,
    const PlayerWorldOps *world,
    float dt,
    ActorLocomotionResult *out);

#endif
