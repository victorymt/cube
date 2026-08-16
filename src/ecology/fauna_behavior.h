#ifndef VOXELCRAFT_FAUNA_BEHAVIOR_H
#define VOXELCRAFT_FAUNA_BEHAVIOR_H

#include <stdbool.h>

typedef enum FaunaBehaviorAction {
    FAUNA_ACTION_IDLE = 0,
    FAUNA_ACTION_WANDER,
    FAUNA_ACTION_SEEK_HABITAT,
    FAUNA_ACTION_FORAGE,
    FAUNA_ACTION_SEEK_FOOD,
    FAUNA_ACTION_DRINK,
    FAUNA_ACTION_SEEK_WATER,
    FAUNA_ACTION_REST,
    FAUNA_ACTION_SEEK_SHELTER,
    FAUNA_ACTION_FLEE,
    FAUNA_ACTION_HUNT,
    FAUNA_ACTION_SCAVENGE,
    FAUNA_ACTION_MATE,
    FAUNA_ACTION_NEST
} FaunaBehaviorAction;

typedef struct FaunaNeeds {
    float energy;
    float hydration;
    float fatigue;
    float stress;
} FaunaNeeds;

typedef struct FaunaNeedInput {
    float activityRatio;
    float movementRatio;
    float foodAvailability;
    float waterAvailability;
    float shelterAvailability;
    float stormPressure;
    float temperatureStress;
    bool moving;
    bool threatened;
    bool feeding;
    bool drinking;
    bool resting;
} FaunaNeedInput;

typedef struct FaunaBehaviorDirection {
    float yaw;
    float improvement;
    bool shouldSeek;
} FaunaBehaviorDirection;

typedef struct FaunaBehaviorInput {
    FaunaNeeds needs;
    FaunaNeedInput environment;
    FaunaBehaviorDirection food;
    FaunaBehaviorDirection water;
    FaunaBehaviorDirection shelter;
    FaunaBehaviorDirection habitat;
    float foodDependence;
    float waterDependence;
    float baseThinkInterval;
    float fleeYaw;
    float wanderYaw;
    float baseWanderDuration;
    unsigned wanderRoll;
    FaunaBehaviorAction currentAction;
    bool colony;
    bool dormant;
} FaunaBehaviorInput;

typedef struct FaunaBehaviorDecision {
    FaunaBehaviorAction action;
    float yaw;
    float moveDuration;
    float thinkInterval;
    float movementFloor;
    float utility;
} FaunaBehaviorDecision;

FaunaNeeds FaunaNeedsDefault(void);
FaunaNeeds FaunaNeedsAdvance(const FaunaNeeds *current,
                             const FaunaNeedInput *input,
                             float deltaTime);
FaunaBehaviorDecision FaunaBehaviorEvaluate(
    const FaunaBehaviorInput *input);
bool FaunaBehaviorActionMoves(FaunaBehaviorAction action);
bool FaunaBehaviorActionValid(FaunaBehaviorAction action);

#endif
