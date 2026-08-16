#include "ecology/fauna_behavior.h"

#include <math.h>

static float FaunaBehaviorClamp(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float FaunaBehaviorUnit(float value)
{
    return FaunaBehaviorClamp(value, 0.0f, 1.0f);
}

FaunaNeeds FaunaNeedsDefault(void)
{
    return (FaunaNeeds){
        .energy = 0.82f,
        .hydration = 0.78f,
        .fatigue = 0.12f,
        .stress = 0.0f
    };
}

FaunaNeeds FaunaNeedsAdvance(const FaunaNeeds *current,
                             const FaunaNeedInput *input,
                             float deltaTime)
{
    FaunaNeeds result = current ? *current : FaunaNeedsDefault();
    if (!input) return result;

    result.energy = FaunaBehaviorUnit(result.energy);
    result.hydration = FaunaBehaviorUnit(result.hydration);
    result.fatigue = FaunaBehaviorUnit(result.fatigue);
    result.stress = FaunaBehaviorUnit(result.stress);
    float dt = FaunaBehaviorClamp(deltaTime, 0.0f, 10.0f);
    if (dt <= 0.0f) return result;

    float movement = FaunaBehaviorUnit(input->movementRatio);
    float food = FaunaBehaviorUnit(input->foodAvailability);
    float water = FaunaBehaviorUnit(input->waterAvailability);
    float shelter = FaunaBehaviorUnit(input->shelterAvailability);
    float storm = FaunaBehaviorUnit(input->stormPressure);
    float temperature = FaunaBehaviorUnit(input->temperatureStress);

    float energyRate = 0.0012f + movement * 0.0048f +
                       (input->threatened ? 0.0020f : 0.0f);
    float hydrationRate = 0.0015f + movement * 0.0024f +
                          temperature * 0.0030f + storm * 0.0020f;
    result.energy -= energyRate * dt;
    result.hydration -= hydrationRate * dt;
    if (input->feeding) result.energy += (0.006f + food * 0.012f) * dt;
    if (input->drinking) result.hydration += (0.010f + water * 0.016f) * dt;
    if (!input->feeding && food > 0.82f && !input->moving) {
        result.energy += food * 0.0015f * dt;
    }
    if (!input->drinking && water > 0.85f && !input->moving) {
        result.hydration += water * 0.0010f * dt;
    }

    float fatigueRate = 0.0010f + movement * 0.0070f + storm * 0.0030f;
    result.fatigue += fatigueRate * dt;
    if (input->resting) result.fatigue -= 0.024f *
        (0.55f + shelter * 0.45f) * dt;

    float stressRate = (input->threatened ? 0.11f : 0.0f) +
                       storm * 0.026f + temperature * 0.020f;
    float recoveryRate = (input->resting ? 0.020f : 0.0f) +
                         shelter * 0.008f;
    result.stress += (stressRate - recoveryRate) * dt;
    result.energy = FaunaBehaviorUnit(result.energy);
    result.hydration = FaunaBehaviorUnit(result.hydration);
    result.fatigue = FaunaBehaviorUnit(result.fatigue);
    result.stress = FaunaBehaviorUnit(result.stress);
    return result;
}

static float FaunaBehaviorDirectionUtility(FaunaBehaviorDirection direction,
                                            float urgency)
{
    if (!direction.shouldSeek || !isfinite(direction.improvement)) return 0.0f;
    return urgency * FaunaBehaviorUnit(direction.improvement);
}

static void FaunaBehaviorSelect(FaunaBehaviorDecision *result,
                                FaunaBehaviorAction action, float utility,
                                float yaw, float duration, float floor)
{
    if (!result || utility <= result->utility) return;
    result->action = action;
    result->utility = utility;
    result->yaw = isfinite(yaw) ? yaw : 0.0f;
    result->moveDuration = fmaxf(isfinite(duration) ? duration : 0.0f, 0.0f);
    result->movementFloor = FaunaBehaviorClamp(floor, 0.0f, 1.0f);
}

FaunaBehaviorDecision FaunaBehaviorEvaluate(
    const FaunaBehaviorInput *input)
{
    FaunaBehaviorDecision result = {
        .action = FAUNA_ACTION_IDLE,
        .thinkInterval = 2.0f,
        .utility = 0.0f
    };
    if (!input) return result;

    float activity = FaunaBehaviorUnit(input->environment.activityRatio);
    float baseThink = fmaxf(isfinite(input->baseThinkInterval)
        ? input->baseThinkInterval : 0.0f, 0.0f);
    result.thinkInterval = baseThink * (1.0f + (1.0f - activity) * 1.5f);
    if (result.thinkInterval <= 0.0f) result.thinkInterval = 0.25f;

    if (input->environment.threatened) {
        result.action = FAUNA_ACTION_FLEE;
        result.yaw = isfinite(input->fleeYaw) ? input->fleeYaw : 0.0f;
        result.moveDuration = 0.8f;
        result.movementFloor = 0.28f;
        result.utility = 1.0f;
        return result;
    }
    if (input->colony && input->dormant) {
        result.action = FAUNA_ACTION_REST;
        result.moveDuration = 0.75f;
        result.utility = 0.72f;
        return result;
    }

    float energy = FaunaBehaviorUnit(input->needs.energy);
    float hydration = FaunaBehaviorUnit(input->needs.hydration);
    float fatigue = FaunaBehaviorUnit(input->needs.fatigue);
    float storm = FaunaBehaviorUnit(input->environment.stormPressure);
    float shelter = FaunaBehaviorUnit(input->environment.shelterAvailability);
    float food = FaunaBehaviorUnit(input->environment.foodAvailability);
    float water = FaunaBehaviorUnit(input->environment.waterAvailability);
    float hungerUrgency = (1.0f - energy) *
        (0.45f + FaunaBehaviorUnit(input->foodDependence) * 0.55f);
    float thirstUrgency = (1.0f - hydration) *
        (0.45f + FaunaBehaviorUnit(input->waterDependence) * 0.55f);
    float restUrgency = fatigue * (0.35f + storm * 0.65f);
    float shelterUrgency = storm * (1.0f - shelter);

    float foodUtility = hungerUrgency * (0.30f + food * 0.70f);
    float waterUtility = thirstUrgency * (0.30f + water * 0.70f);
    float restUtility = restUrgency;
    float shelterUtility = shelterUrgency * 0.95f;
    float habitatUtility = (1.0f - activity) *
        FaunaBehaviorDirectionUtility(input->habitat, 0.80f);
    if (input->currentAction == FAUNA_ACTION_FORAGE ||
        input->currentAction == FAUNA_ACTION_SEEK_FOOD) foodUtility += 0.08f;
    if (input->currentAction == FAUNA_ACTION_DRINK ||
        input->currentAction == FAUNA_ACTION_SEEK_WATER) waterUtility += 0.08f;
    if (input->currentAction == FAUNA_ACTION_REST ||
        input->currentAction == FAUNA_ACTION_SEEK_SHELTER) restUtility += 0.08f;

    bool foodTarget = input->food.shouldSeek && foodUtility > 0.22f;
    bool waterTarget = input->water.shouldSeek && waterUtility > 0.22f;
    bool shelterTarget = input->shelter.shouldSeek && shelterUtility > 0.22f;
    if (!input->colony && waterTarget) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_SEEK_WATER,
                            waterUtility, input->water.yaw,
                            1.1f + input->water.improvement * 1.4f, 0.24f);
    }
    if (!input->colony && foodTarget) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_SEEK_FOOD,
                            foodUtility, input->food.yaw,
                            1.0f + input->food.improvement * 1.5f, 0.22f);
    }
    if (!input->colony && shelterTarget) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_SEEK_SHELTER,
                            shelterUtility, input->shelter.yaw,
                            1.0f + input->shelter.improvement * 1.2f, 0.20f);
    }
    if (waterUtility > 0.18f && water >= 0.20f) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_DRINK,
                            waterUtility * 0.92f, 0.0f, 0.9f, 0.0f);
    }
    if (foodUtility > 0.18f && food >= 0.20f) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_FORAGE,
                            foodUtility * 0.90f, 0.0f, 0.9f, 0.0f);
    }
    FaunaBehaviorSelect(&result, FAUNA_ACTION_REST,
                        restUtility, 0.0f, 0.8f, 0.0f);
    if (input->dormant) return result;
    if (result.action != FAUNA_ACTION_IDLE) return result;

    if (!input->colony && habitatUtility > 0.16f) {
        FaunaBehaviorSelect(&result, FAUNA_ACTION_SEEK_HABITAT,
                            habitatUtility, input->habitat.yaw,
                            1.1f + input->habitat.improvement * 1.4f, 0.22f);
    }
    unsigned wanderThreshold = 12u + (unsigned)(activity * 42.0f);
    if (!input->colony && input->wanderRoll < wanderThreshold) {
        float wanderUtility = 0.10f + activity * 0.18f;
        FaunaBehaviorSelect(&result, FAUNA_ACTION_WANDER,
                            wanderUtility, input->wanderYaw,
                            fmaxf(isfinite(input->baseWanderDuration)
                                ? input->baseWanderDuration : 0.0f, 0.0f) *
                                (0.45f + activity * 0.55f), 0.0f);
    }
    return result;
}

bool FaunaBehaviorActionMoves(FaunaBehaviorAction action)
{
    return action == FAUNA_ACTION_WANDER ||
           action == FAUNA_ACTION_SEEK_HABITAT ||
           action == FAUNA_ACTION_SEEK_FOOD ||
           action == FAUNA_ACTION_SEEK_WATER ||
           action == FAUNA_ACTION_SEEK_SHELTER ||
           action == FAUNA_ACTION_FLEE ||
           action == FAUNA_ACTION_HUNT ||
           action == FAUNA_ACTION_SCAVENGE ||
           action == FAUNA_ACTION_MATE;
}

bool FaunaBehaviorActionValid(FaunaBehaviorAction action)
{
    return action >= FAUNA_ACTION_IDLE && action <= FAUNA_ACTION_NEST;
}
