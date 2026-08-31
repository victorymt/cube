#include "world/ecology_hooks.h"

#include <math.h>

static WorldEcologyHooks ecologyHooks;

static float WorldEcologyUnit(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

void WorldInstallEcologyHooks(const WorldEcologyHooks *hooks)
{
    ecologyHooks = hooks ? *hooks : (WorldEcologyHooks){ 0 };
}

bool WorldEcologyFloraCapacityAt(int x, int z, float *outCapacity)
{
    if (!outCapacity) return false;
    *outCapacity = 0.0f;
    if (!ecologyHooks.floraCapacityAt) return false;

    float capacity = 0.0f;
    if (!ecologyHooks.floraCapacityAt(
            x, z, &capacity, ecologyHooks.userData) ||
        !isfinite(capacity)) {
        return false;
    }
    *outCapacity = WorldEcologyUnit(capacity);
    return true;
}

bool WorldEcologyFloraVisualAt(
    int x, int z, float daylight, WorldFloraVisualSample *outSample)
{
    if (!outSample) return false;
    *outSample = (WorldFloraVisualSample){
        .activity = 1.0f,
        .capacity = 1.0f,
        .growthScale = 1.0f,
        .visualPresence = 1.0f
    };
    if (!ecologyHooks.floraVisualAt || !isfinite(daylight)) return false;

    WorldFloraVisualSample sample = { 0 };
    if (!ecologyHooks.floraVisualAt(
            x, z, daylight, &sample, ecologyHooks.userData) ||
        !isfinite(sample.activity) || !isfinite(sample.capacity) ||
        !isfinite(sample.growthScale) ||
        !isfinite(sample.visualPresence)) {
        return false;
    }
    *outSample = (WorldFloraVisualSample){
        .activity = WorldEcologyUnit(sample.activity),
        .capacity = WorldEcologyUnit(sample.capacity),
        .growthScale = WorldEcologyUnit(sample.growthScale),
        .visualPresence = WorldEcologyUnit(sample.visualPresence)
    };
    return true;
}

void WorldEcologyDecorateChunk(Chunk *chunk, int chunkX, int chunkZ)
{
    if (!chunk || !ecologyHooks.decorateChunk) return;
    ecologyHooks.decorateChunk(
        chunk, chunkX, chunkZ, ecologyHooks.userData);
}
