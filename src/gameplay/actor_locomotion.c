#include "gameplay/actor_locomotion.h"

#include "raymath.h"

#include <math.h>
#include <stddef.h>

static bool ActorLocomotionVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool ActorLocomotionProfileIsValid(
    const ActorLocomotionProfile *profile)
{
    return profile &&
        isfinite(profile->walkSpeed) && profile->walkSpeed >= 0.0f &&
        isfinite(profile->sprintSpeed) && profile->sprintSpeed >= 0.0f &&
        isfinite(profile->jumpSpeed) && profile->jumpSpeed >= 0.0f &&
        isfinite(profile->minGravityScale) &&
        isfinite(profile->maxGravityScale) &&
        profile->minGravityScale > 0.0f &&
        profile->maxGravityScale >= profile->minGravityScale &&
        isfinite(profile->maxFallSpeed) && profile->maxFallSpeed > 0.0f;
}

bool ActorGroundLocomotionCompute(
    const ActorBody *body,
    const ActorLocomotionIntent *intent,
    const ActorLocomotionProfile *profile,
    const WorldEnvironmentSample *environment,
    float dt,
    ActorBody *outBody,
    bool *outJumped)
{
    ActorBody bodySnapshot = { 0 };
    bool haveBody = body != NULL;
    if (haveBody) bodySnapshot = *body;
    if (outBody) *outBody = (ActorBody){ 0 };
    if (outJumped) *outJumped = false;
    if (!haveBody) return false;
    body = &bodySnapshot;
    if (!intent || !ActorLocomotionProfileIsValid(profile) ||
        !environment || !outBody ||
        !isfinite(dt) || dt <= 0.0f || body->floating ||
        !ActorLocomotionVectorIsFinite(body->position) ||
        !ActorLocomotionVectorIsFinite(body->velocity) ||
        !isfinite(body->yaw) || !isfinite(intent->forward) ||
        !isfinite(intent->strafe) ||
        !ActorLocomotionVectorIsFinite(environment->gravity.acceleration) ||
        !isfinite(environment->gravity.gravityScale)) {
        return false;
    }

    float forward = Clamp(intent->forward, -1.0f, 1.0f);
    float strafe = Clamp(intent->strafe, -1.0f, 1.0f);
    Vector3 wishForward = {
        sinf(body->yaw), 0.0f, cosf(body->yaw)
    };
    Vector3 wishRight = {
        -cosf(body->yaw), 0.0f, sinf(body->yaw)
    };
    Vector3 wish = Vector3Add(Vector3Scale(wishForward, forward),
                              Vector3Scale(wishRight, strafe));
    if (Vector3LengthSqr(wish) > 0.0f) wish = Vector3Normalize(wish);

    ActorBody working = *body;
    float speed = intent->sprint ? profile->sprintSpeed : profile->walkSpeed;
    working.velocity.x = wish.x * speed;
    working.velocity.z = wish.z * speed;

    float gravityScale = Clamp(environment->gravity.gravityScale,
                               profile->minGravityScale,
                               profile->maxGravityScale);
    bool jumped = false;
    if (intent->jump && working.onGround) {
        working.velocity.y = profile->jumpSpeed * sqrtf(gravityScale);
        working.onGround = false;
        jumped = true;
    }
    working.velocity = Vector3Add(
        working.velocity,
        Vector3Scale(environment->gravity.acceleration, dt));
    if (working.velocity.y < -profile->maxFallSpeed)
        working.velocity.y = -profile->maxFallSpeed;

    *outBody = working;
    if (outJumped) *outJumped = jumped;
    return true;
}

bool ActorGroundLocomotionStep(
    const ActorBody *body,
    const ActorLocomotionIntent *intent,
    const ActorLocomotionProfile *profile,
    const WorldEnvironmentSample *environment,
    const PlayerWorldOps *world,
    float dt,
    ActorLocomotionResult *out)
{
    ActorBody bodySnapshot = { 0 };
    bool haveBody = body != NULL;
    if (haveBody) bodySnapshot = *body;
    if (out) *out = (ActorLocomotionResult){ 0 };
    if (!haveBody || !world || !world->move || !out) return false;
    body = &bodySnapshot;

    ActorBody working = { 0 };
    bool jumped = false;
    if (!ActorGroundLocomotionCompute(
            body, intent, profile, environment, dt, &working, &jumped)) {
        return false;
    }
    const ActorMotionRequest request = {
        .delta = Vector3Scale(working.velocity, dt),
        .canStepUp = working.onGround && !working.floating
    };
    if (!world->move(world->user, &working, &request)) return false;

    *out = (ActorLocomotionResult){
        .body = working,
        .moved = Vector3DistanceSqr(body->position, working.position) > 0.0f,
        .jumped = jumped
    };
    return true;
}
