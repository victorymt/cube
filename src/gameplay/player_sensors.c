#include "gameplay/player_sensors.h"

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

PlayerWaterState PlayerWaterStateAt(Vector3 position)
{
    PlayerWaterState state = { .surfaceY = position.y + EYE_HEIGHT };
    float feetSurface = state.surfaceY;
    float bodySurface = state.surfaceY;
    float eyeSurface = state.surfaceY;
    Vector3 feetFlow = Vector3Zero();
    Vector3 bodyFlow = Vector3Zero();
    Vector3 eyeFlow = Vector3Zero();
    state.feetSubmerged = LiquidAt(position, 0.30f, &feetSurface, &feetFlow);
    state.bodySubmerged = LiquidAt(position, PLAYER_HEIGHT * 0.52f,
                                   &bodySurface, &bodyFlow);
    state.eyesSubmerged = LiquidAt(position, EYE_HEIGHT, &eyeSurface, &eyeFlow);
    state.flowVelocity = state.bodySubmerged ? bodyFlow :
                         (state.feetSubmerged ? feetFlow : eyeFlow);
    if (!state.eyesSubmerged) {
        state.surfaceY = state.bodySubmerged ? bodySurface : feetSurface;
        return state;
    }

    int x = (int)floorf(position.x);
    int z = (int)floorf(position.z);
    int y = (int)floorf(position.y + EYE_HEIGHT);
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
                    (position.y + EYE_HEIGHT));
                return state;
            }
        }
        surfaceBlockY = sampleY + 1;
    }
    float proceduralSurface = 0.0f;
    if (surfaceBlockY == y + WATER_SURFACE_SCAN_LIMIT &&
        WorldProceduralWaterSurfaceAt(x, z, &proceduralSurface) &&
        proceduralSurface > position.y + EYE_HEIGHT) {
        state.surfaceY = proceduralSurface;
        state.eyeDepth = proceduralSurface - (position.y + EYE_HEIGHT);
        return state;
    }
    state.surfaceY = (float)surfaceBlockY;
    state.eyeDepth = fmaxf(0.0f, state.surfaceY -
                                  (position.y + EYE_HEIGHT));
    return state;
}
