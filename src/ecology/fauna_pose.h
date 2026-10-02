#ifndef VOXELCRAFT_FAUNA_POSE_H
#define VOXELCRAFT_FAUNA_POSE_H

#include "ecology/fauna_behavior.h"

/* Simulation-owned animation state. All phases are cycles in [0,1).
 * Keep the versioned entity disk layout in sync when adding fields/actions. */
typedef struct FaunaPoseState {
    float gaitPhase;
    float actionPhase;
    float movement;
    float turn;
    float engagement;
    float weights[FAUNA_ACTION_COUNT];
} FaunaPoseState;

typedef struct FaunaPoseInput {
    FaunaBehaviorAction action;
    float distance;
    float speed;
    float scale;
    float turnRate;
    bool engaged;
    bool corpse;
} FaunaPoseInput;

typedef struct FaunaBodyPose {
    float bodyHeight;
    float bodyPitch;
    float bodyRoll;
    float headHeight;
    float headPitch;
    float headYaw;
    float headForward;
    float crouch;
    float display;
    float strike;
    float tailYaw;
} FaunaBodyPose;

typedef struct FaunaFootPose {
    float along;
    float lift;
    bool planted;
} FaunaFootPose;

FaunaPoseState FaunaPoseAdvance(FaunaPoseState state,
                                FaunaPoseInput input, float dt);
FaunaPoseState FaunaPoseInterpolate(FaunaPoseState from,
                                    FaunaPoseState to, float alpha);
bool FaunaPoseStateValid(const FaunaPoseState *state);
float FaunaPoseStride(float scale);
FaunaBodyPose FaunaPoseEvaluate(const FaunaPoseState *state, float phase,
                                float fatigue, float stress, bool airborne);
/* leg/pair are anatomical indices, never module-array indices. */
FaunaFootPose FaunaPoseFoot(const FaunaPoseState *state, float scale,
                            unsigned pair, unsigned side, unsigned pairs);
/* Contact occupies only the forward part of a 0.8-second strike cycle. */
float FaunaPoseContactSeconds(float previousPhase, float dt);

#endif
