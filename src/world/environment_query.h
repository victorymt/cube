#ifndef VOXELCRAFT_ENVIRONMENT_QUERY_H
#define VOXELCRAFT_ENVIRONMENT_QUERY_H

#include "world/world_rules.h"

#include "raylib.h"

#include <stdbool.h>

typedef struct WorldWaterSample {
    bool feetSubmerged;
    bool bodySubmerged;
    bool eyesSubmerged;
    float surfaceY;
    float eyeDepth;
    Vector3 flowVelocity;
} WorldWaterSample;

typedef struct WorldGravitySample {
    bool inSpace;
    float gravityScale;
    bool hasPlanetGravity;
    Vector3 direction;
    float surfaceDistance;
    float surfaceGravityScale;
    Vector3 acceleration;
    unsigned ruleCount;
} WorldGravitySample;

typedef struct WorldEnvironmentSample {
    Vector3 position;
    double simulationTime;
    WorldWaterSample water;
    WorldGravitySample gravity;
} WorldEnvironmentSample;

/* Queries use the active local frame until the explicit FrameId migration.
 * simulationTime is explicit even where the current backend is time-invariant. */
WorldWaterSample WorldWaterSampleAt(Vector3 position, float eyeHeight,
                                    float bodyHeight);
WorldGravitySample WorldGravitySampleAt(Vector3 position,
                                        double simulationTime);
WorldEnvironmentSample WorldEnvironmentSampleAt(Vector3 position,
                                                float eyeHeight,
                                                float bodyHeight,
                                                double simulationTime);
WorldEnvironmentSample WorldEnvironmentSampleAtWithRules(
    Vector3 position, float eyeHeight, float bodyHeight,
    double simulationTime, uint32_t bodyId, uint64_t regionId,
    const WorldRuleSet *rules);

#endif
