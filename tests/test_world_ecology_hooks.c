#include "world/ecology_hooks.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct HookFixture {
    int capacityCalls;
    int visualCalls;
    int decoratorCalls;
} HookFixture;

static bool SampleCapacity(
    int x, int z, float *outCapacity, void *userData)
{
    HookFixture *fixture = userData;
    assert(x == 7);
    assert(z == -3);
    fixture->capacityCalls++;
    *outCapacity = 1.4f;
    return true;
}

static bool SampleVisual(
    int x, int z, float daylight, WorldFloraVisualSample *outSample,
    void *userData)
{
    HookFixture *fixture = userData;
    assert(x == 7);
    assert(z == -3);
    assert(daylight == 0.6f);
    fixture->visualCalls++;
    *outSample = (WorldFloraVisualSample){
        .activity = -0.2f,
        .capacity = 0.5f,
        .growthScale = 1.7f,
        .visualPresence = 0.25f
    };
    return true;
}

static void DecorateChunk(
    Chunk *chunk, int chunkX, int chunkZ, void *userData)
{
    HookFixture *fixture = userData;
    assert(chunk);
    assert(chunkX == 2);
    assert(chunkZ == -5);
    fixture->decoratorCalls++;
    chunk->cx = chunkX;
    chunk->cz = chunkZ;
}

int main(void)
{
    WorldInstallEcologyHooks(NULL);
    float capacity = 4.0f;
    assert(!WorldEcologyFloraCapacityAt(7, -3, &capacity));
    assert(capacity == 0.0f);

    WorldFloraVisualSample fallback = { 0 };
    assert(!WorldEcologyFloraVisualAt(7, -3, 0.6f, &fallback));
    assert(fallback.activity == 1.0f);
    assert(fallback.capacity == 1.0f);
    assert(fallback.growthScale == 1.0f);
    assert(fallback.visualPresence == 1.0f);

    HookFixture fixture = { 0 };
    const WorldEcologyHooks hooks = {
        .floraCapacityAt = SampleCapacity,
        .floraVisualAt = SampleVisual,
        .decorateChunk = DecorateChunk,
        .userData = &fixture
    };
    WorldInstallEcologyHooks(&hooks);
    assert(WorldEcologyFloraCapacityAt(7, -3, &capacity));
    assert(capacity == 1.0f);

    WorldFloraVisualSample visual;
    assert(WorldEcologyFloraVisualAt(7, -3, 0.6f, &visual));
    assert(visual.activity == 0.0f);
    assert(visual.capacity == 0.5f);
    assert(visual.growthScale == 1.0f);
    assert(visual.visualPresence == 0.25f);

    Chunk chunk = { 0 };
    WorldEcologyDecorateChunk(&chunk, 2, -5);
    assert(chunk.cx == 2 && chunk.cz == -5);
    assert(fixture.capacityCalls == 1);
    assert(fixture.visualCalls == 1);
    assert(fixture.decoratorCalls == 1);

    assert(!WorldEcologyFloraVisualAt(7, -3, NAN, &visual));
    assert(visual.growthScale == 1.0f);
    WorldInstallEcologyHooks(NULL);
    puts("world ecology hook tests passed");
    return 0;
}
