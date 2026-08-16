#ifndef VOXELCRAFT_FAUNA_MOTION_H
#define VOXELCRAFT_FAUNA_MOTION_H

#include <stdbool.h>

#define FAUNA_MOTION_CANDIDATE_COUNT 5

typedef enum FaunaLocomotionArchetype {
    FAUNA_LOCOMOTION_QUADRUPED = 0,
    FAUNA_LOCOMOTION_BIPED,
    FAUNA_LOCOMOTION_HEXAPOD,
    FAUNA_LOCOMOTION_SERPENTINE,
    FAUNA_LOCOMOTION_FLOATING,
    FAUNA_LOCOMOTION_COLONY
} FaunaLocomotionArchetype;

typedef struct FaunaMotionProfileInput {
    FaunaLocomotionArchetype archetype;
    float baseSpeed;
    float sprintMultiplier;
    float organismScale;
    float gravityScale;
    float windStrength;
} FaunaMotionProfileInput;

typedef struct FaunaMotionProfile {
    float cruiseSpeed;
    float sprintSpeed;
    float acceleration;
    float deceleration;
    float turnRate;
    float stepHeight;
    float maxDrop;
    float bodyRadius;
    float hoverClearance;
    float windCoupling;
    bool airborne;
    bool stationary;
    bool canTraverseLiquid;
} FaunaMotionProfile;

typedef struct FaunaTerrainCandidate {
    float yaw;
    float heightDelta;
    bool blocked;
    bool liquid;
    bool lava;
    bool unsupported;
} FaunaTerrainCandidate;

typedef struct FaunaMotionInput {
    FaunaMotionProfile profile;
    FaunaTerrainCandidate candidates[FAUNA_MOTION_CANDIDATE_COUNT];
    float currentYaw;
    float targetYaw;
    float currentSpeed;
    float movementScale;
    float deltaTime;
    bool moving;
    bool sprinting;
} FaunaMotionInput;

typedef struct FaunaMotionStep {
    float yaw;
    float speed;
    int candidateIndex;
    bool pathAvailable;
} FaunaMotionStep;

FaunaMotionProfile FaunaMotionProfileDerive(
    const FaunaMotionProfileInput *input);
bool FaunaMotionCandidateUsable(const FaunaMotionProfile *profile,
                                const FaunaTerrainCandidate *candidate);
FaunaMotionStep FaunaMotionAdvance(const FaunaMotionInput *input);
float FaunaMotionAngleDelta(float fromYaw, float toYaw);

#endif
