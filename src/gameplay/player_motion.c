#include "gameplay/player_motion.h"

#include "core/config.h"
#include "gameplay/player_state.h"
#include "gameplay/ship.h"

#include "raymath.h"

#include <math.h>

static const ActorCollider playerCollider = {
    .radius = PLAYER_RADIUS,
    .height = PLAYER_HEIGHT,
    .stepHeight = 0.5f
};

#define WATER_WALK_SPEED 3.0f
#define WATER_SPRINT_SPEED 4.2f
#define WATER_VERTICAL_SPEED 3.0f
#define WATER_MAX_VERTICAL_SPEED 3.5f
#define WATER_HORIZONTAL_RESPONSE 6.0f
#define WATER_VERTICAL_RESPONSE 5.0f
#define SPACE_VERTICAL_INPUT_ACCELERATION 12.0f
#define SPACE_VERTICAL_DAMPING 2.0f
#define SPACE_MIN_VERTICAL_SPEED (-10.0f)
#define SPACE_MAX_VERTICAL_SPEED 10.0f

static const PlayerMotionProfile playerMotionProfile = {
    .ground = {
        .walkSpeed = WALK_SPEED,
        .sprintSpeed = SPRINT_SPEED,
        .jumpSpeed = JUMP_SPEED,
        .minGravityScale = 0.45f,
        .maxGravityScale = 1.75f,
        .maxFallSpeed = 35.0f
    },
    .water = {
        .walkSpeed = WATER_WALK_SPEED,
        .sprintSpeed = WATER_SPRINT_SPEED,
        .verticalSpeed = WATER_VERTICAL_SPEED,
        .maxVerticalSpeed = WATER_MAX_VERTICAL_SPEED,
        .horizontalResponse = WATER_HORIZONTAL_RESPONSE,
        .verticalResponse = WATER_VERTICAL_RESPONSE
    },
    .space = {
        .verticalInputAcceleration = SPACE_VERTICAL_INPUT_ACCELERATION,
        .verticalDamping = SPACE_VERTICAL_DAMPING,
        .minVerticalSpeed = SPACE_MIN_VERTICAL_SPEED,
        .maxVerticalSpeed = SPACE_MAX_VERTICAL_SPEED
    },
    .floatingVerticalSpeed = FLOAT_VERTICAL_SPEED
};

const ActorCollider *PlayerMotionCollider(void)
{
    return &playerCollider;
}

const ActorLocomotionProfile *PlayerLocomotionProfile(void)
{
    return &playerMotionProfile.ground;
}

const PlayerMotionProfile *PlayerMotionProfileDefault(void)
{
    return &playerMotionProfile;
}

bool PlayerOverlapsWorldWithOptions(Vector3 position, bool allowUnready)
{
    return WorldActorOverlaps(PlayerMotionCollider(), position, allowUnready);
}

bool PlayerOverlapsWorld(Vector3 position)
{
    return PlayerOverlapsWorldWithOptions(position, ShipIsDriving());
}

bool PlayerFindLandingSpot(Vector3 start, int minY, int maxY, Vector3 *out)
{
    return WorldActorFindLandingSpot(PlayerMotionCollider(), start, minY, maxY,
                                     ShipIsDriving(), out);
}

bool PlayerSurfacePoseFromLocal(Vector3 position, SurfacePose *outPose)
{
    return WorldActorPoseFromLocal(position, outPose);
}

bool PlayerLocalFromSurfacePose(const SurfacePose *pose, Vector3 *outPosition)
{
    return WorldActorLocalFromPose(pose, outPosition);
}

int PlayerMissingSurfaceChunkCount(Vector3 position)
{
    return WorldActorMissingSurfaceChunkCount(PlayerMotionCollider(), position);
}

void PlayerMoveWithOptions(Player *player, Vector3 delta, bool allowUnready,
                           bool canStepUp)
{
    if (!player) return;
    ActorBody body = {
        .position = player->position,
        .velocity = player->velocity,
        .yaw = player->yaw,
        .pitch = player->pitch,
        .onGround = player->onGround,
        .floating = player->floating
    };
    ActorMotionRequest request = {
        .delta = delta,
        .allowUnready = allowUnready,
        .canStepUp = canStepUp
    };
    ActorMotionResult result;
    if (!WorldActorResolveMotion(&body, PlayerMotionCollider(), &request,
                                 &result)) {
        return;
    }
    PlayerApplyMotionResult(player, &result);
}

void MovePlayer(Player *player, Vector3 delta)
{
    bool canStepUp = player && player->onGround && !player->floating;
    PlayerMoveWithOptions(player, delta, ShipIsDriving(), canStepUp);
}

Vector3 ForwardFromAngles(float yaw, float pitch)
{
    return Vector3Normalize((Vector3){
        sinf(yaw) * cosf(pitch),
        sinf(pitch),
        cosf(yaw) * cosf(pitch)
    });
}

Vector3 FlatForward(float yaw)
{
    return Vector3Normalize((Vector3){ sinf(yaw), 0.0f, cosf(yaw) });
}

Vector3 RightFromYaw(float yaw)
{
    return Vector3Normalize((Vector3){ -cosf(yaw), 0.0f, sinf(yaw) });
}
