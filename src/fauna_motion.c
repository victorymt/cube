#include "fauna_motion.h"

#include <math.h>
#include <stddef.h>

#define FAUNA_PI 3.14159265358979323846f
#define FAUNA_TAU (2.0f * FAUNA_PI)

static float FaunaMotionClamp(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float FaunaMotionMoveToward(float current, float target,
                                   float maximumDelta)
{
    if (current < target) return fminf(current + maximumDelta, target);
    return fmaxf(current - maximumDelta, target);
}

float FaunaMotionAngleDelta(float fromYaw, float toYaw)
{
    if (!isfinite(fromYaw) || !isfinite(toYaw)) return 0.0f;
    float delta = fmodf(toYaw - fromYaw, FAUNA_TAU);
    if (delta > FAUNA_PI) delta -= FAUNA_TAU;
    if (delta < -FAUNA_PI) delta += FAUNA_TAU;
    return delta;
}

FaunaMotionProfile FaunaMotionProfileDerive(
    const FaunaMotionProfileInput *input)
{
    FaunaMotionProfile result = { 0 };
    if (!input) {
        result.stationary = true;
        result.bodyRadius = 0.18f;
        return result;
    }

    FaunaLocomotionArchetype archetype = input->archetype;
    if (archetype < FAUNA_LOCOMOTION_QUADRUPED ||
        archetype > FAUNA_LOCOMOTION_COLONY) {
        archetype = FAUNA_LOCOMOTION_QUADRUPED;
    }
    float scale = FaunaMotionClamp(input->organismScale, 0.20f, 3.0f);
    float gravity = FaunaMotionClamp(input->gravityScale, 0.15f, 3.0f);
    float wind = FaunaMotionClamp(input->windStrength, 0.0f, 1.0f);
    float baseSpeed = FaunaMotionClamp(input->baseSpeed, 0.0f, 8.0f);
    float sprintMultiplier = FaunaMotionClamp(
        input->sprintMultiplier, 1.0f, 2.5f);

    float acceleration = 3.2f;
    float turnRate = 2.5f;
    float stepHeight = 1.0f;
    float maxDrop = 1.45f;
    float baseRadius = 0.28f;
    float windCoupling = 0.06f;

    switch (archetype) {
    case FAUNA_LOCOMOTION_BIPED:
        acceleration = 3.5f;
        turnRate = 2.9f;
        stepHeight = 1.0f;
        maxDrop = 1.30f;
        baseRadius = 0.25f;
        break;
    case FAUNA_LOCOMOTION_HEXAPOD:
        acceleration = 2.9f;
        turnRate = 2.35f;
        stepHeight = 1.25f;
        maxDrop = 1.75f;
        baseRadius = 0.30f;
        break;
    case FAUNA_LOCOMOTION_SERPENTINE:
        acceleration = 2.2f;
        turnRate = 3.35f;
        stepHeight = 0.48f;
        maxDrop = 0.85f;
        baseRadius = 0.22f;
        break;
    case FAUNA_LOCOMOTION_FLOATING:
        acceleration = 1.65f;
        turnRate = 1.75f;
        stepHeight = 0.0f;
        maxDrop = 0.0f;
        baseRadius = 0.24f;
        windCoupling = 0.82f;
        result.airborne = true;
        break;
    case FAUNA_LOCOMOTION_COLONY:
        result.stationary = true;
        baseSpeed = 0.0f;
        acceleration = 0.0f;
        turnRate = 0.0f;
        stepHeight = 0.0f;
        maxDrop = 0.0f;
        baseRadius = 0.34f;
        windCoupling = 0.0f;
        break;
    case FAUNA_LOCOMOTION_QUADRUPED:
    default:
        break;
    }

    float gravityRoot = sqrtf(gravity);
    float scaleRoot = sqrtf(scale);
    float windSlowdown = result.airborne ? wind * 0.20f : wind * 0.05f;
    result.cruiseSpeed = baseSpeed * (1.0f - windSlowdown);
    result.sprintSpeed = result.cruiseSpeed * sprintMultiplier;
    result.acceleration = acceleration * gravityRoot / scaleRoot;
    result.deceleration = result.acceleration * 1.35f;
    result.turnRate = turnRate / scaleRoot;
    result.stepHeight = FaunaMotionClamp(
        stepHeight / gravityRoot, 0.0f, 1.35f);
    result.maxDrop = FaunaMotionClamp(
        maxDrop / gravityRoot, 0.0f, 2.0f);
    result.bodyRadius = FaunaMotionClamp(
        baseRadius * scaleRoot, 0.18f, 0.58f);
    result.hoverClearance = result.airborne
        ? FaunaMotionClamp(2.2f + scale * 1.15f, 2.4f, 5.8f) : 0.0f;
    result.windCoupling = windCoupling;
    result.canTraverseLiquid = false;

    if (result.stationary) {
        result.cruiseSpeed = 0.0f;
        result.sprintSpeed = 0.0f;
        result.acceleration = 0.0f;
        result.deceleration = 0.0f;
        result.turnRate = 0.0f;
    }
    return result;
}

bool FaunaMotionCandidateUsable(const FaunaMotionProfile *profile,
                                const FaunaTerrainCandidate *candidate)
{
    if (!profile || !candidate || !isfinite(candidate->yaw) ||
        !isfinite(candidate->heightDelta) || candidate->blocked ||
        candidate->lava) {
        return false;
    }
    if (profile->airborne) return true;
    if (profile->stationary || candidate->unsupported ||
        (candidate->liquid && !profile->canTraverseLiquid)) {
        return false;
    }
    return candidate->heightDelta <= profile->stepHeight + 0.0001f &&
           -candidate->heightDelta <= profile->maxDrop + 0.0001f;
}

FaunaMotionStep FaunaMotionAdvance(const FaunaMotionInput *input)
{
    FaunaMotionStep result = { .candidateIndex = -1 };
    if (!input) return result;

    const FaunaMotionProfile *profile = &input->profile;
    float currentYaw = isfinite(input->currentYaw) ? input->currentYaw : 0.0f;
    float targetYaw = isfinite(input->targetYaw)
        ? input->targetYaw : currentYaw;
    float deltaTime = FaunaMotionClamp(input->deltaTime, 0.0f, 1.0f);
    float currentSpeed = FaunaMotionClamp(
        input->currentSpeed, 0.0f, profile->sprintSpeed);

    result.yaw = currentYaw;
    if (profile->stationary) return result;

    float selectedYaw = targetYaw;
    if (input->moving) {
        float bestScore = INFINITY;
        for (int index = 0; index < FAUNA_MOTION_CANDIDATE_COUNT; index++) {
            const FaunaTerrainCandidate *candidate = &input->candidates[index];
            if (!FaunaMotionCandidateUsable(profile, candidate)) continue;
            float score = fabsf(FaunaMotionAngleDelta(targetYaw,
                                                       candidate->yaw));
            if (!profile->airborne) score += fabsf(candidate->heightDelta) * 0.08f;
            if (score < bestScore) {
                bestScore = score;
                selectedYaw = candidate->yaw;
                result.candidateIndex = index;
            }
        }
        result.pathAvailable = result.candidateIndex >= 0;
    } else {
        result.pathAvailable = true;
    }

    float maximumTurn = profile->turnRate * deltaTime;
    float turn = FaunaMotionAngleDelta(currentYaw, selectedYaw);
    turn = FaunaMotionClamp(turn, -maximumTurn, maximumTurn);
    result.yaw = currentYaw + turn;

    float targetSpeed = 0.0f;
    if (input->moving && result.pathAvailable) {
        float movementScale = FaunaMotionClamp(
            input->movementScale, 0.0f, 1.0f);
        targetSpeed = profile->cruiseSpeed * movementScale;
        if (input->sprinting) {
            targetSpeed = profile->sprintSpeed *
                fmaxf(movementScale, 0.28f);
        }
        targetSpeed = fminf(targetSpeed, profile->sprintSpeed);
    }
    float rate = targetSpeed > currentSpeed
        ? profile->acceleration : profile->deceleration;
    result.speed = FaunaMotionMoveToward(
        currentSpeed, targetSpeed, rate * deltaTime);
    return result;
}
