#include "gameplay/player_motion_simulation.h"

#include "core/config.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>

static bool PlayerMotionVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool PlayerMotionProfileIsValid(const PlayerMotionProfile *profile)
{
    return profile &&
        isfinite(profile->ground.walkSpeed) && profile->ground.walkSpeed >= 0.0f &&
        isfinite(profile->ground.sprintSpeed) && profile->ground.sprintSpeed >= 0.0f &&
        isfinite(profile->ground.jumpSpeed) && profile->ground.jumpSpeed >= 0.0f &&
        isfinite(profile->ground.minGravityScale) &&
        isfinite(profile->ground.maxGravityScale) &&
        profile->ground.minGravityScale > 0.0f &&
        profile->ground.maxGravityScale >= profile->ground.minGravityScale &&
        isfinite(profile->ground.maxFallSpeed) &&
        profile->ground.maxFallSpeed > 0.0f &&
        isfinite(profile->water.walkSpeed) && profile->water.walkSpeed >= 0.0f &&
        isfinite(profile->water.sprintSpeed) && profile->water.sprintSpeed >= 0.0f &&
        isfinite(profile->water.verticalSpeed) &&
        profile->water.verticalSpeed >= 0.0f &&
        isfinite(profile->water.maxVerticalSpeed) &&
        profile->water.maxVerticalSpeed >= profile->water.verticalSpeed &&
        isfinite(profile->water.horizontalResponse) &&
        profile->water.horizontalResponse > 0.0f &&
        isfinite(profile->water.verticalResponse) &&
        profile->water.verticalResponse > 0.0f &&
        isfinite(profile->space.verticalInputAcceleration) &&
        profile->space.verticalInputAcceleration >= 0.0f &&
        isfinite(profile->space.verticalDamping) &&
        profile->space.verticalDamping >= 0.0f &&
        isfinite(profile->space.minVerticalSpeed) &&
        isfinite(profile->space.maxVerticalSpeed) &&
        profile->space.minVerticalSpeed <= profile->space.maxVerticalSpeed &&
        isfinite(profile->floatingVerticalSpeed) &&
        profile->floatingVerticalSpeed >= 0.0f;
}

static float PlayerMotionApproach(float current, float target, float dt,
                                  float response)
{
    if (!isfinite(current)) current = 0.0f;
    float amount = 1.0f - expf(-fmaxf(dt, 0.0f) * response);
    return current + (target - current) * amount;
}

static void PlayerMotionApplyLook(ActorBody *body, Vector2 lookDelta)
{
    body->yaw -= lookDelta.x * MOUSE_SENSITIVITY;
    body->pitch -= lookDelta.y * MOUSE_SENSITIVITY;
    body->pitch = Clamp(body->pitch, -1.45f, 1.45f);
}

static Vector3 PlayerMotionWish(const ActorBody *body,
                                const PlayerMotionIntent *intent)
{
    Vector3 forward = {
        sinf(body->yaw), 0.0f, cosf(body->yaw)
    };
    Vector3 right = {
        -cosf(body->yaw), 0.0f, sinf(body->yaw)
    };
    Vector3 wish = Vector3Add(
        Vector3Scale(forward, Clamp(intent->forward, -1.0f, 1.0f)),
        Vector3Scale(right, Clamp(intent->strafe, -1.0f, 1.0f)));
    if (Vector3LengthSqr(wish) > 0.0f) wish = Vector3Normalize(wish);
    return wish;
}

bool PlayerMotionModeSelect(const ActorBody *body,
                            const WorldEnvironmentSample *environment,
                            PlayerMotionMode *outMode)
{
    if (!body || !environment || !outMode ||
        !isfinite(body->position.x) || !isfinite(body->position.y) ||
        !isfinite(body->position.z) ||
        !PlayerMotionVectorIsFinite(environment->water.flowVelocity)) {
        return false;
    }
    bool swimming = environment->water.bodySubmerged ||
                    environment->water.eyesSubmerged;
    if (environment->gravity.inSpace) *outMode = PLAYER_MOTION_SPACE;
    else if (body->floating) *outMode = PLAYER_MOTION_FLOATING;
    else if (swimming) *outMode = PLAYER_MOTION_WATER;
    else *outMode = PLAYER_MOTION_GROUND;
    return true;
}

static bool PlayerMotionSimulateWater(
    ActorBody *body, const PlayerMotionIntent *intent,
    const PlayerWaterMotionProfile *profile,
    const WorldEnvironmentSample *environment, float dt)
{
    if (!PlayerMotionVectorIsFinite(environment->water.flowVelocity))
        return false;
    Vector3 wish = PlayerMotionWish(body, intent);
    float speed = intent->sprint ? profile->sprintSpeed : profile->walkSpeed;
    float targetX = wish.x * speed + environment->water.flowVelocity.x;
    float targetZ = wish.z * speed + environment->water.flowVelocity.z;
    body->velocity.x = PlayerMotionApproach(
        body->velocity.x, targetX, dt, profile->horizontalResponse);
    body->velocity.z = PlayerMotionApproach(
        body->velocity.z, targetZ, dt, profile->horizontalResponse);

    float verticalInput = Clamp(intent->vertical, -1.0f, 1.0f);
    float targetVertical = verticalInput * profile->verticalSpeed;
    if (fabsf(verticalInput) < 0.001f) {
        targetVertical = environment->water.eyesSubmerged ? 0.35f : -0.60f;
    }
    targetVertical += environment->water.flowVelocity.y;
    body->velocity.y = PlayerMotionApproach(
        body->velocity.y, targetVertical, dt, profile->verticalResponse);
    body->velocity.y = Clamp(
        body->velocity.y, -profile->maxVerticalSpeed,
        profile->maxVerticalSpeed);
    if (verticalInput > 0.0f) body->onGround = false;
    return true;
}

static void PlayerMotionSimulateSpace(
    ActorBody *body, const PlayerMotionIntent *intent,
    const PlayerMotionProfile *profile,
    const WorldEnvironmentSample *environment, float dt)
{
    Vector3 wish = PlayerMotionWish(body, intent);
    float speed = intent->sprint
        ? profile->ground.sprintSpeed : profile->ground.walkSpeed;
    body->velocity.x = wish.x * speed;
    body->velocity.z = wish.z * speed;
    body->velocity = Vector3Add(
        body->velocity,
        Vector3Scale(environment->gravity.acceleration, dt));
    body->velocity.y -= body->velocity.y *
        profile->space.verticalDamping * dt;
    body->velocity.y += Clamp(intent->vertical, -1.0f, 1.0f) *
        profile->space.verticalInputAcceleration * dt;
    body->velocity.y = Clamp(
        body->velocity.y, profile->space.minVerticalSpeed,
        profile->space.maxVerticalSpeed);
}

static void PlayerMotionSimulateFloating(
    ActorBody *body, const PlayerMotionIntent *intent,
    const PlayerMotionProfile *profile)
{
    Vector3 wish = PlayerMotionWish(body, intent);
    float speed = intent->sprint
        ? profile->ground.sprintSpeed : profile->ground.walkSpeed;
    body->velocity.x = wish.x * speed;
    body->velocity.z = wish.z * speed;
    body->velocity.y = Clamp(intent->vertical, -1.0f, 1.0f) *
        profile->floatingVerticalSpeed;
}

bool PlayerMotionStep(
    const ActorBody *body,
    const PlayerMotionIntent *intent,
    const PlayerMotionProfile *profile,
    const PlayerWorldOps *world,
    double simulationTime,
    float dt,
    PlayerMotionResult *out)
{
    ActorBody bodySnapshot = { 0 };
    bool haveBody = body != NULL;
    if (haveBody) bodySnapshot = *body;
    if (out) *out = (PlayerMotionResult){ 0 };
    if (!haveBody) return false;
    body = &bodySnapshot;
    if (!intent || !PlayerMotionProfileIsValid(profile) ||
        !world || !world->move || !world->sampleEnvironment || !out ||
        !isfinite(simulationTime) || !isfinite(dt) || dt <= 0.0f ||
        !PlayerMotionVectorIsFinite(body->position) ||
        !PlayerMotionVectorIsFinite(body->velocity) ||
        !isfinite(body->yaw) || !isfinite(body->pitch) ||
        !isfinite(intent->forward) || !isfinite(intent->strafe) ||
        !isfinite(intent->vertical) || !isfinite(intent->lookDelta.x) ||
        !isfinite(intent->lookDelta.y)) {
        return false;
    }

    WorldEnvironmentSample environment = {
        .gravity = { .gravityScale = 1.0f }
    };
    if (!world->sampleEnvironment(
            world->user, body->position, simulationTime, &environment)) {
        return false;
    }

    ActorBody working = *body;
    if (intent->toggleFloating) {
        working.floating = !working.floating;
        working.velocity.y = 0.0f;
        working.onGround = false;
    }
    PlayerMotionApplyLook(&working, intent->lookDelta);

    PlayerMotionMode mode = PLAYER_MOTION_GROUND;
    if (!PlayerMotionModeSelect(&working, &environment, &mode)) return false;

    bool jumped = false;
    if (mode == PLAYER_MOTION_GROUND) {
        const ActorLocomotionIntent groundIntent = {
            .forward = intent->forward,
            .strafe = intent->strafe,
            .sprint = intent->sprint,
            .jump = intent->jump
        };
        if (!ActorGroundLocomotionCompute(
                &working, &groundIntent, &profile->ground, &environment, dt,
                &working, &jumped)) {
            return false;
        }
    } else if (mode == PLAYER_MOTION_WATER) {
        if (!PlayerMotionSimulateWater(
                &working, intent, &profile->water, &environment, dt)) {
            return false;
        }
    } else if (mode == PLAYER_MOTION_SPACE) {
        PlayerMotionSimulateSpace(&working, intent, profile, &environment, dt);
    } else {
        PlayerMotionSimulateFloating(&working, intent, profile);
    }

    const ActorMotionRequest request = {
        .delta = Vector3Scale(working.velocity, dt),
        .canStepUp = working.onGround && !working.floating
    };
    if (!world->move(world->user, &working, &request)) return false;

    WorldEnvironmentSample movedEnvironment = environment;
    (void)world->sampleEnvironment(
        world->user, working.position, simulationTime, &movedEnvironment);
    *out = (PlayerMotionResult){
        .body = working,
        .mode = mode,
        .moved = Vector3DistanceSqr(body->position, working.position) > 0.0f,
        .jumped = jumped,
        .feetInWater = movedEnvironment.water.feetSubmerged
    };
    return true;
}
