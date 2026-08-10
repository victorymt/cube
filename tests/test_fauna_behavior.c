#include "fauna_behavior.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static float TestUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static FaunaBehaviorDirection Direction(float yaw, float improvement)
{
    return (FaunaBehaviorDirection){
        .yaw = yaw,
        .improvement = improvement,
        .shouldSeek = improvement >= 0.06f
    };
}

static void AssertNeedsValid(FaunaNeeds needs)
{
    assert(isfinite(needs.energy) && needs.energy >= 0.0f && needs.energy <= 1.0f);
    assert(isfinite(needs.hydration) && needs.hydration >= 0.0f && needs.hydration <= 1.0f);
    assert(isfinite(needs.fatigue) && needs.fatigue >= 0.0f && needs.fatigue <= 1.0f);
    assert(isfinite(needs.stress) && needs.stress >= 0.0f && needs.stress <= 1.0f);
}

static void TestNeedsCausality(void)
{
    FaunaNeeds initial = FaunaNeedsDefault();
    FaunaNeedInput travel = {
        .activityRatio = 0.8f,
        .movementRatio = 1.0f,
        .foodAvailability = 0.1f,
        .waterAvailability = 0.1f,
        .shelterAvailability = 0.1f,
        .stormPressure = 0.8f,
        .temperatureStress = 0.7f,
        .moving = true,
        .threatened = true
    };
    FaunaNeeds stressed = FaunaNeedsAdvance(&initial, &travel, 30.0f);
    AssertNeedsValid(stressed);
    assert(stressed.energy < initial.energy);
    assert(stressed.hydration < initial.hydration);
    assert(stressed.fatigue > initial.fatigue);
    assert(stressed.stress > initial.stress);

    FaunaNeedInput recovery = travel;
    recovery.moving = false;
    recovery.threatened = false;
    recovery.feeding = true;
    recovery.drinking = true;
    recovery.resting = true;
    recovery.foodAvailability = 1.0f;
    recovery.waterAvailability = 1.0f;
    recovery.shelterAvailability = 1.0f;
    recovery.stormPressure = 0.0f;
    recovery.temperatureStress = 0.0f;
    FaunaNeeds recovered = FaunaNeedsAdvance(&stressed, &recovery, 30.0f);
    AssertNeedsValid(recovered);
    assert(recovered.energy > stressed.energy);
    assert(recovered.hydration > stressed.hydration);
    assert(recovered.fatigue < stressed.fatigue);
    assert(recovered.stress < stressed.stress);
}

static FaunaBehaviorInput BaseBehaviorInput(void)
{
    FaunaBehaviorInput input = {
        .needs = FaunaNeedsDefault(),
        .environment = {
            .activityRatio = 0.8f,
            .foodAvailability = 0.8f,
            .waterAvailability = 0.8f,
            .shelterAvailability = 0.8f
        },
        .food = Direction(0.0f, 0.3f),
        .water = Direction(1.57f, 0.3f),
        .shelter = Direction(-1.57f, 0.3f),
        .habitat = Direction(3.14f, 0.2f),
        .foodDependence = 0.9f,
        .waterDependence = 0.9f,
        .baseThinkInterval = 3.0f,
        .fleeYaw = 2.0f,
        .wanderYaw = 1.0f,
        .baseWanderDuration = 2.0f,
        .wanderRoll = 99u
    };
    return input;
}

static void TestBehaviorUtility(void)
{
    FaunaBehaviorInput input = BaseBehaviorInput();
    input.needs.hydration = 0.05f;
    FaunaBehaviorDecision drink = FaunaBehaviorEvaluate(&input);
    assert(drink.action == FAUNA_ACTION_SEEK_WATER);
    assert(drink.yaw == input.water.yaw);
    assert(FaunaBehaviorActionMoves(drink.action));

    input = BaseBehaviorInput();
    input.needs.energy = 0.05f;
    FaunaBehaviorDecision forage = FaunaBehaviorEvaluate(&input);
    assert(forage.action == FAUNA_ACTION_SEEK_FOOD);

    input = BaseBehaviorInput();
    input.needs.fatigue = 0.70f;
    input.environment.stormPressure = 0.9f;
    input.environment.shelterAvailability = 0.05f;
    input.shelter = Direction(-1.57f, 0.8f);
    FaunaBehaviorDecision shelter = FaunaBehaviorEvaluate(&input);
    assert(shelter.action == FAUNA_ACTION_SEEK_SHELTER);

    input = BaseBehaviorInput();
    input.environment.threatened = true;
    FaunaBehaviorDecision flee = FaunaBehaviorEvaluate(&input);
    assert(flee.action == FAUNA_ACTION_FLEE);
    assert(flee.yaw == input.fleeYaw);
    assert(flee.movementFloor == 0.28f);

    input = BaseBehaviorInput();
    input.dormant = true;
    FaunaBehaviorDecision idle = FaunaBehaviorEvaluate(&input);
    assert(idle.action == FAUNA_ACTION_IDLE || idle.action == FAUNA_ACTION_REST);
}

static void TestBehaviorDeterminismAndProperties(void)
{
    uint32_t state = 0x6a31e2d7u;
    for (int sample = 0; sample < 20000; sample++) {
        FaunaBehaviorInput input = BaseBehaviorInput();
        input.needs.energy = TestUnit(&state);
        input.needs.hydration = TestUnit(&state);
        input.needs.fatigue = TestUnit(&state);
        input.needs.stress = TestUnit(&state);
        input.environment.activityRatio = TestUnit(&state);
        input.environment.foodAvailability = TestUnit(&state);
        input.environment.waterAvailability = TestUnit(&state);
        input.environment.shelterAvailability = TestUnit(&state);
        input.environment.stormPressure = TestUnit(&state);
        input.environment.temperatureStress = TestUnit(&state);
        input.environment.threatened = TestUnit(&state) > 0.90f;
        input.colony = TestUnit(&state) > 0.92f;
        input.dormant = TestUnit(&state) > 0.94f;
        input.currentAction = (FaunaBehaviorAction)(
            (unsigned)(TestUnit(&state) * 10.0f) % 10u);
        input.wanderRoll = (unsigned)(TestUnit(&state) * 100.0f);
        input.food = Direction(TestUnit(&state) * 6.28f, TestUnit(&state));
        input.water = Direction(TestUnit(&state) * 6.28f, TestUnit(&state));
        input.shelter = Direction(TestUnit(&state) * 6.28f, TestUnit(&state));
        input.habitat = Direction(TestUnit(&state) * 6.28f, TestUnit(&state));

        FaunaBehaviorDecision first = FaunaBehaviorEvaluate(&input);
        FaunaBehaviorDecision second = FaunaBehaviorEvaluate(&input);
        assert(first.action == second.action);
        assert(first.yaw == second.yaw);
        assert(first.moveDuration == second.moveDuration);
        assert(first.thinkInterval == second.thinkInterval);
        assert(first.movementFloor == second.movementFloor);
        assert(first.utility == second.utility);
        assert(FaunaBehaviorActionValid(first.action));
        assert(isfinite(first.yaw));
        assert(isfinite(first.moveDuration) && first.moveDuration >= 0.0f);
        assert(isfinite(first.thinkInterval) && first.thinkInterval >= 0.0f);
        assert(isfinite(first.movementFloor) &&
               first.movementFloor >= 0.0f && first.movementFloor <= 1.0f);
        assert(isfinite(first.utility) && first.utility >= 0.0f &&
               first.utility <= 1.2f);
        if (input.environment.threatened) assert(first.action == FAUNA_ACTION_FLEE);
        if (input.colony && input.dormant &&
            !input.environment.threatened) {
            assert(first.action == FAUNA_ACTION_REST);
        }
    }
}

int main(void)
{
    TestNeedsCausality();
    TestBehaviorUtility();
    TestBehaviorDeterminismAndProperties();
    puts("fauna behavior tests passed");
    return 0;
}
