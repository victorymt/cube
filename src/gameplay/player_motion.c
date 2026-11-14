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

static const ActorLocomotionProfile playerLocomotionProfile = {
    .walkSpeed = WALK_SPEED,
    .sprintSpeed = SPRINT_SPEED,
    .jumpSpeed = JUMP_SPEED,
    .minGravityScale = 0.45f,
    .maxGravityScale = 1.75f,
    .maxFallSpeed = 35.0f
};

const ActorCollider *PlayerMotionCollider(void)
{
    return &playerCollider;
}

const ActorLocomotionProfile *PlayerLocomotionProfile(void)
{
    return &playerLocomotionProfile;
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
