#ifndef VOXELCRAFT_PLAYER_MOTION_SIMULATION_H
#define VOXELCRAFT_PLAYER_MOTION_SIMULATION_H

#include "gameplay/actor_locomotion.h"
#include "gameplay/player_world.h"

#include "world/actor_body.h"
#include "world/environment_query.h"

#include <stdbool.h>

typedef enum PlayerMotionMode {
    PLAYER_MOTION_GROUND = 0,
    PLAYER_MOTION_WATER,
    PLAYER_MOTION_SPACE,
    PLAYER_MOTION_FLOATING
} PlayerMotionMode;

typedef struct PlayerMotionIntent {
    float forward;
    float strafe;
    float vertical;
    bool sprint;
    bool jump;
    bool toggleFloating;
    Vector2 lookDelta;
} PlayerMotionIntent;

typedef struct PlayerWaterMotionProfile {
    float walkSpeed;
    float sprintSpeed;
    float verticalSpeed;
    float maxVerticalSpeed;
    float horizontalResponse;
    float verticalResponse;
} PlayerWaterMotionProfile;

typedef struct PlayerSpaceMotionProfile {
    float verticalInputAcceleration;
    float verticalDamping;
    float minVerticalSpeed;
    float maxVerticalSpeed;
} PlayerSpaceMotionProfile;

typedef struct PlayerMotionProfile {
    ActorLocomotionProfile ground;
    PlayerWaterMotionProfile water;
    PlayerSpaceMotionProfile space;
    float floatingVerticalSpeed;
} PlayerMotionProfile;

typedef struct PlayerMotionResult {
    ActorBody body;
    PlayerMotionMode mode;
    bool moved;
    bool jumped;
    bool feetInWater;
} PlayerMotionResult;

bool PlayerMotionModeSelect(const ActorBody *body,
                            const WorldEnvironmentSample *environment,
                            PlayerMotionMode *outMode);

/* Run one fixed-tick Player locomotion simulation. The pipeline samples the
 * environment, selects one mode, computes a candidate body, resolves geometry
 * once, then samples the moved environment for contact-only state. It never
 * writes the Player owner. */
bool PlayerMotionStep(
    const ActorBody *body,
    const PlayerMotionIntent *intent,
    const PlayerMotionProfile *profile,
    const PlayerWorldOps *world,
    double simulationTime,
    float dt,
    PlayerMotionResult *out);

#endif
