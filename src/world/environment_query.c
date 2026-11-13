#include "world/environment_query.h"

#include "space/space_query.h"
#include "world/block_edits.h"
#include "world/block_types.h"
#include "world/fluid.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <math.h>
#include <stdint.h>

#define WATER_SURFACE_SCAN_LIMIT 512

static bool LiquidAt(Vector3 position, float height, float *outSurfaceY,
                     Vector3 *outFlow)
{
    Vector3 point = { position.x, position.y + height, position.z };
    int x = (int)floorf(point.x);
    int y = (int)floorf(point.y);
    int z = (int)floorf(point.z);
    BlockType block = GetBlockAt(x, y, z);
    if (WorldIsSurfaceActive()) {
        FluidSample sample = FluidSampleAt(point);
        if (sample.volume > 0u && point.y < sample.surfaceY) {
            if (outSurfaceY) *outSurfaceY = sample.surfaceY;
            if (outFlow) *outFlow = sample.velocity;
            return true;
        }
    }
    if (block == BLOCK_LAVA) {
        float surfaceY = (float)y + BlockLiquidHeightAt(x, y, z);
        if (outSurfaceY) *outSurfaceY = surfaceY;
        if (outFlow) *outFlow = Vector3Zero();
        return point.y < surfaceY;
    }
    if (outFlow) *outFlow = Vector3Zero();
    return false;
}

WorldWaterSample WorldWaterSampleAt(Vector3 position, float eyeHeight,
                                    float bodyHeight)
{
    WorldWaterSample state = { .surfaceY = position.y + eyeHeight };
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z) || !isfinite(eyeHeight) ||
        !isfinite(bodyHeight) || eyeHeight <= 0.0f || bodyHeight <= 0.0f) {
        return state;
    }
    float feetSurface = state.surfaceY;
    float bodySurface = state.surfaceY;
    float eyeSurface = state.surfaceY;
    Vector3 feetFlow = Vector3Zero();
    Vector3 bodyFlow = Vector3Zero();
    Vector3 eyeFlow = Vector3Zero();
    state.feetSubmerged = LiquidAt(position, 0.30f, &feetSurface, &feetFlow);
    state.bodySubmerged = LiquidAt(position, bodyHeight * 0.52f,
                                   &bodySurface, &bodyFlow);
    state.eyesSubmerged = LiquidAt(position, eyeHeight, &eyeSurface, &eyeFlow);
    state.flowVelocity = state.bodySubmerged ? bodyFlow :
                         (state.feetSubmerged ? feetFlow : eyeFlow);
    if (!state.eyesSubmerged) {
        state.surfaceY = state.bodySubmerged ? bodySurface : feetSurface;
        return state;
    }

    int x = (int)floorf(position.x);
    int z = (int)floorf(position.z);
    int y = (int)floorf(position.y + eyeHeight);
    int surfaceBlockY = y;
    for (int offset = 0; offset < WATER_SURFACE_SCAN_LIMIT; offset++) {
        int sampleY = y + offset;
        if (sampleY < y) break;
        BlockType block = GetBlockAt(x, sampleY, z);
        uint8_t volume = WorldIsSurfaceActive()
            ? FluidGetVolumeAt(x, sampleY, z) : 0u;
        bool liquid = IsLiquidBlock(block) || volume > 0u;
        if (!liquid) {
            surfaceBlockY = sampleY;
            break;
        }
        if (WorldIsSurfaceActive() && volume > 0u) {
            if (volume < FLUID_CAPACITY) {
                state.surfaceY = (float)sampleY +
                    (float)volume / (float)FLUID_CAPACITY;
                state.eyeDepth = fmaxf(0.0f, state.surfaceY -
                    (position.y + eyeHeight));
                return state;
            }
        }
        surfaceBlockY = sampleY + 1;
    }
    float proceduralSurface = 0.0f;
    if (surfaceBlockY == y + WATER_SURFACE_SCAN_LIMIT &&
        WorldProceduralWaterSurfaceAt(x, z, &proceduralSurface) &&
        proceduralSurface > position.y + eyeHeight) {
        state.surfaceY = proceduralSurface;
        state.eyeDepth = proceduralSurface - (position.y + eyeHeight);
        return state;
    }
    state.surfaceY = (float)surfaceBlockY;
    state.eyeDepth = fmaxf(0.0f, state.surfaceY -
                                  (position.y + eyeHeight));
    return state;
}

WorldGravitySample WorldGravitySampleAt(Vector3 position,
                                        double simulationTime)
{
    (void)simulationTime;
    WorldGravitySample sample = {
        .inSpace = WorldIsSpaceActive(),
        .gravityScale = WorldGravityScale()
    };
    if (sample.inSpace) {
        sample.hasPlanetGravity = PlanetSurfaceAt(
            position, &sample.direction, &sample.surfaceDistance,
            &sample.surfaceGravityScale);
        if (sample.hasPlanetGravity) {
            sample.acceleration = Vector3Scale(
                sample.direction, sample.surfaceGravityScale);
        }
    } else {
        sample.acceleration = (Vector3){ 0.0f, -sample.gravityScale, 0.0f };
    }
    return sample;
}

WorldEnvironmentSample WorldEnvironmentSampleAt(Vector3 position,
                                                float eyeHeight,
                                                float bodyHeight,
                                                double simulationTime)
{
    return WorldEnvironmentSampleAtWithRules(
        position, eyeHeight, bodyHeight, simulationTime, 0u, 0u, NULL);
}

WorldEnvironmentSample WorldEnvironmentSampleAtWithRules(
    Vector3 position, float eyeHeight, float bodyHeight,
    double simulationTime, uint32_t bodyId, uint64_t regionId,
    const WorldRuleSet *rules)
{
    WorldEnvironmentSample sample = {
        .position = position,
        .simulationTime = simulationTime,
        .water = WorldWaterSampleAt(position, eyeHeight, bodyHeight),
        .gravity = WorldGravitySampleAt(position, simulationTime)
    };
    if (rules) {
        unsigned indices[WORLD_RULE_MAX_RULES];
        unsigned count = 0u;
        if (WorldRuleSetResolve(rules, WORLD_RULE_STAGE_GRAVITY, bodyId,
                                regionId, indices, WORLD_RULE_MAX_RULES,
                                &count) && count > 0u) {
            (void)WorldRuleSetApplyVector(
                rules, WORLD_RULE_STAGE_GRAVITY, bodyId, regionId,
                sample.gravity.acceleration, &sample.gravity.acceleration);
            sample.gravity.ruleCount = count;
        }
        count = 0u;
        if (WorldRuleSetResolve(rules, WORLD_RULE_STAGE_WATER, bodyId,
                                regionId, indices, WORLD_RULE_MAX_RULES,
                                &count) && count > 0u) {
            (void)WorldRuleSetApplyVector(
                rules, WORLD_RULE_STAGE_WATER, bodyId, regionId,
                sample.water.flowVelocity, &sample.water.flowVelocity);
            sample.waterRuleCount = count;
        }
    }
    return sample;
}
