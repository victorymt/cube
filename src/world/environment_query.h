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
    /* gravityScale is the relative field strength used by jump response;
     * acceleration is the resulting game-space acceleration after Earth-scale
     * or planetary atmospheric falloff and rule evaluation. */
    float gravityScale;
    bool hasPlanetGravity;
    Vector3 direction;
    float surfaceDistance;
    float surfaceGravityScale;
    Vector3 acceleration;
    unsigned ruleCount;
} WorldGravitySample;

typedef struct WorldTornadoSample {
    bool active;
    Vector3 acceleration;
    float exposure;
    float localWindMps;
    float horizontalDistance;
} WorldTornadoSample;

typedef struct WorldEnvironmentSample {
    Vector3 position;
    double simulationTime;
    WorldWaterSample water;
    WorldGravitySample gravity;
    WorldTornadoSample tornado;
    unsigned waterRuleCount;
} WorldEnvironmentSample;

/* Queries use the active local frame until the explicit FrameId migration.
 * simulationTime is explicit even where the current backend is time-invariant. */
WorldWaterSample WorldWaterSampleAt(Vector3 position, float eyeHeight,
                                    float bodyHeight);
WorldGravitySample WorldGravitySampleAt(Vector3 position,
                                        double simulationTime);
/* Sample the tornado force field at the supplied point. Exposure/force are
 * independent of Player shelter, water resistance and velocity clamps. */
WorldTornadoSample WorldTornadoSampleAt(Vector3 samplePosition);
WorldEnvironmentSample WorldEnvironmentSampleAt(Vector3 position,
                                                float eyeHeight,
                                                float bodyHeight,
                                                double simulationTime);
/* bodyId/regionId identify the consumer for scoped rules. A missing rule set
 * is equivalent to an empty one; water and gravity remain one coherent sample. */
WorldEnvironmentSample WorldEnvironmentSampleAtWithRules(
    Vector3 position, float eyeHeight, float bodyHeight,
    double simulationTime, uint32_t bodyId, uint64_t regionId,
    const WorldRuleSet *rules);

#endif
