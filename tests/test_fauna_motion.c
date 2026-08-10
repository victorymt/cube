#include "fauna_motion.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static float TestUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static FaunaMotionProfile TestProfile(FaunaLocomotionArchetype archetype)
{
    FaunaMotionProfileInput input = {
        .archetype = archetype,
        .baseSpeed = 1.0f,
        .sprintMultiplier = 1.6f,
        .organismScale = 1.0f,
        .gravityScale = 1.0f,
        .windStrength = 0.2f
    };
    return FaunaMotionProfileDerive(&input);
}

static FaunaMotionInput OpenMotionInput(FaunaMotionProfile profile)
{
    FaunaMotionInput input = {
        .profile = profile,
        .currentYaw = 0.0f,
        .targetYaw = 1.0f,
        .currentSpeed = 0.0f,
        .movementScale = 1.0f,
        .deltaTime = 0.1f,
        .moving = true
    };
    for (int index = 0; index < FAUNA_MOTION_CANDIDATE_COUNT; index++) {
        input.candidates[index].yaw = input.targetYaw + (float)index * 0.3f;
    }
    return input;
}

static void AssertProfileValid(FaunaMotionProfile profile)
{
#define ASSERT_NONNEGATIVE(field) do { \
    assert(isfinite(profile.field)); \
    assert(profile.field >= 0.0f); \
} while (0)
    ASSERT_NONNEGATIVE(cruiseSpeed);
    ASSERT_NONNEGATIVE(sprintSpeed);
    ASSERT_NONNEGATIVE(acceleration);
    ASSERT_NONNEGATIVE(deceleration);
    ASSERT_NONNEGATIVE(turnRate);
    ASSERT_NONNEGATIVE(stepHeight);
    ASSERT_NONNEGATIVE(maxDrop);
    ASSERT_NONNEGATIVE(bodyRadius);
    ASSERT_NONNEGATIVE(hoverClearance);
    ASSERT_NONNEGATIVE(windCoupling);
#undef ASSERT_NONNEGATIVE
    assert(profile.sprintSpeed >= profile.cruiseSpeed);
    assert(profile.bodyRadius >= 0.18f && profile.bodyRadius <= 0.58f);
}

static void TestMorphologyProfiles(void)
{
    FaunaMotionProfile missing = FaunaMotionProfileDerive(NULL);
    FaunaMotionProfile hexapod = TestProfile(FAUNA_LOCOMOTION_HEXAPOD);
    FaunaMotionProfile serpentine = TestProfile(FAUNA_LOCOMOTION_SERPENTINE);
    FaunaMotionProfile floating = TestProfile(FAUNA_LOCOMOTION_FLOATING);
    FaunaMotionProfile colony = TestProfile(FAUNA_LOCOMOTION_COLONY);

    AssertProfileValid(missing);
    AssertProfileValid(hexapod);
    AssertProfileValid(serpentine);
    AssertProfileValid(floating);
    AssertProfileValid(colony);
    assert(hexapod.stepHeight > serpentine.stepHeight);
    assert(missing.stationary && missing.cruiseSpeed == 0.0f);
    assert(serpentine.turnRate > hexapod.turnRate);
    assert(floating.airborne && floating.hoverClearance > 0.0f);
    assert(floating.windCoupling > hexapod.windCoupling);
    assert(colony.stationary && colony.cruiseSpeed == 0.0f);

    FaunaMotionProfileInput lowGravityInput = {
        .archetype = FAUNA_LOCOMOTION_QUADRUPED,
        .baseSpeed = 1.0f,
        .sprintMultiplier = 1.5f,
        .organismScale = 1.0f,
        .gravityScale = 0.35f
    };
    FaunaMotionProfileInput highGravityInput = lowGravityInput;
    highGravityInput.gravityScale = 2.4f;
    FaunaMotionProfile lowGravity = FaunaMotionProfileDerive(
        &lowGravityInput);
    FaunaMotionProfile highGravity = FaunaMotionProfileDerive(
        &highGravityInput);
    assert(lowGravity.stepHeight > highGravity.stepHeight);
    assert(lowGravity.maxDrop > highGravity.maxDrop);
}

static void TestTerrainChoice(void)
{
    FaunaMotionProfile profile = TestProfile(FAUNA_LOCOMOTION_QUADRUPED);
    FaunaMotionInput input = OpenMotionInput(profile);
    input.candidates[0].blocked = true;
    input.candidates[1].yaw = 0.55f;
    input.candidates[2].yaw = 1.45f;
    input.candidates[3].lava = true;
    input.candidates[4].unsupported = true;

    FaunaMotionStep step = FaunaMotionAdvance(&input);
    assert(step.pathAvailable);
    assert(step.candidateIndex == 1);
    assert(step.speed > 0.0f);
    assert(fabsf(FaunaMotionAngleDelta(input.currentYaw, step.yaw)) <=
           profile.turnRate * input.deltaTime + 0.000001f);

    for (int index = 0; index < FAUNA_MOTION_CANDIDATE_COUNT; index++) {
        input.candidates[index].blocked = true;
    }
    step = FaunaMotionAdvance(&input);
    assert(!step.pathAvailable);
    assert(step.candidateIndex == -1);
    assert(step.speed == 0.0f);

    FaunaMotionProfile floating = TestProfile(FAUNA_LOCOMOTION_FLOATING);
    input = OpenMotionInput(floating);
    input.candidates[0].unsupported = true;
    input.candidates[0].liquid = true;
    step = FaunaMotionAdvance(&input);
    assert(step.pathAvailable && step.candidateIndex == 0);
}

static void TestAccelerationAndStopping(void)
{
    FaunaMotionProfile profile = TestProfile(FAUNA_LOCOMOTION_BIPED);
    FaunaMotionInput input = OpenMotionInput(profile);
    FaunaMotionStep accelerating = FaunaMotionAdvance(&input);
    assert(accelerating.speed > 0.0f);
    assert(accelerating.speed <= profile.acceleration * input.deltaTime +
           0.000001f);

    input.currentYaw = accelerating.yaw;
    input.currentSpeed = profile.sprintSpeed;
    input.moving = false;
    FaunaMotionStep stopping = FaunaMotionAdvance(&input);
    assert(stopping.pathAvailable);
    assert(stopping.speed < input.currentSpeed);
    assert(input.currentSpeed - stopping.speed <=
           profile.deceleration * input.deltaTime + 0.000001f);

    input.profile = TestProfile(FAUNA_LOCOMOTION_COLONY);
    input.moving = true;
    FaunaMotionStep stationary = FaunaMotionAdvance(&input);
    assert(stationary.speed == 0.0f);
    assert(!stationary.pathAvailable);
}

static void TestRandomizedMotionProperties(void)
{
    uint32_t state = 0x91e10da5u;
    for (int sample = 0; sample < 20000; sample++) {
        FaunaMotionProfileInput profileInput = {
            .archetype = (FaunaLocomotionArchetype)(
                (uint32_t)(TestUnit(&state) * 6.0f) % 6u),
            .baseSpeed = TestUnit(&state) * 5.0f,
            .sprintMultiplier = 1.0f + TestUnit(&state) * 1.5f,
            .organismScale = 0.1f + TestUnit(&state) * 3.5f,
            .gravityScale = 0.05f + TestUnit(&state) * 3.5f,
            .windStrength = TestUnit(&state)
        };
        FaunaMotionProfile profile = FaunaMotionProfileDerive(&profileInput);
        AssertProfileValid(profile);
        FaunaMotionInput input = OpenMotionInput(profile);
        input.currentYaw = TestUnit(&state) * 12.0f - 6.0f;
        input.targetYaw = TestUnit(&state) * 12.0f - 6.0f;
        input.currentSpeed = TestUnit(&state) * profile.sprintSpeed;
        input.movementScale = TestUnit(&state);
        input.deltaTime = TestUnit(&state) * 0.25f;
        input.moving = TestUnit(&state) > 0.15f;
        input.sprinting = TestUnit(&state) > 0.72f;
        for (int index = 0; index < FAUNA_MOTION_CANDIDATE_COUNT; index++) {
            FaunaTerrainCandidate *candidate = &input.candidates[index];
            candidate->yaw = input.targetYaw +
                (TestUnit(&state) - 0.5f) * 2.4f;
            candidate->heightDelta =
                (TestUnit(&state) - 0.5f) * 4.0f;
            candidate->blocked = TestUnit(&state) > 0.90f;
            candidate->liquid = TestUnit(&state) > 0.88f;
            candidate->lava = TestUnit(&state) > 0.96f;
            candidate->unsupported = TestUnit(&state) > 0.90f;
        }

        FaunaMotionStep first = FaunaMotionAdvance(&input);
        FaunaMotionStep second = FaunaMotionAdvance(&input);
        assert(first.yaw == second.yaw);
        assert(first.speed == second.speed);
        assert(first.candidateIndex == second.candidateIndex);
        assert(first.pathAvailable == second.pathAvailable);
        assert(isfinite(first.yaw));
        assert(isfinite(first.speed));
        assert(first.speed >= 0.0f &&
               first.speed <= profile.sprintSpeed + 0.000001f);
        assert(fabsf(FaunaMotionAngleDelta(input.currentYaw, first.yaw)) <=
               profile.turnRate * input.deltaTime + 0.000001f);
        assert(first.candidateIndex >= -1 &&
               first.candidateIndex < FAUNA_MOTION_CANDIDATE_COUNT);
        if (first.candidateIndex >= 0) {
            assert(FaunaMotionCandidateUsable(
                &profile, &input.candidates[first.candidateIndex]));
        }
        if (profile.stationary) assert(first.speed == 0.0f);
    }
}

int main(void)
{
    TestMorphologyProfiles();
    TestTerrainChoice();
    TestAccelerationAndStopping();
    TestRandomizedMotionProperties();
    puts("fauna motion tests passed");
    return 0;
}
