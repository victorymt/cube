#include "world/landform_runtime.h"
#include "world/terrain_generation.h"
#include "world/world.h"
#include <math.h>
#include <string.h>

// Main-thread publication only, after workers drain. Two immutable atlases;
// seed, version and the fixed Homeworld baseline revision form the cache key.
static struct { LandformAtlas *atlas; uint32_t seed, version; } cache[2];
static const LandformAtlas *active;
static uint32_t activeSeed;
static unsigned replacement;

static LandformBaseline HomeBaseline(Vector3 direction, void *user)
{
    uint32_t seed = *(const uint32_t *)user;
    int x = (int)floorf(atan2f(direction.z, direction.x) * SURFACE_RADIUS_BLOCKS);
    int z = (int)floorf(asinf(fmaxf(-1, fminf(1, direction.y))) * SURFACE_RADIUS_BLOCKS);
    SurfaceTerrainSample sample = { 0 };
    float elevation = TerrainHomeBaselineElevation(seed, x, z, &sample);
    // A fixed climatology proxy, never the live hydrology/weather feedback.
    float latitude = fabsf(direction.y);
    float rain = 0.65f - 0.56f * expf(-powf((latitude - .45f) / .19f, 2));
    return (LandformBaseline){ elevation, rain, .25f + .6f * sample.ridge };
}

bool LandformRuntimePrepare(uint32_t seed, uint32_t version, bool enabled)
{
    active = NULL;
    if (!enabled || version < TERRAIN_GENERATION_CURRENT) return true;
    for (int i = 0; i < 2; i++) {
        if (cache[i].atlas && cache[i].seed == seed && cache[i].version == version) {
            active = cache[i].atlas; activeSeed = seed; return true;
        }
    }
    LandformAtlas *next = LandformBuild(HOME_SEA_LEVEL, HomeBaseline, &seed);
    if (!next) return false;
    unsigned slot = replacement++ % 2;
    LandformDestroy(cache[slot].atlas);
    cache[slot].atlas = next;
    cache[slot].seed = seed;
    cache[slot].version = version;
    active = next; activeSeed = seed;
    return true;
}

const LandformAtlas *LandformRuntimeAtlas(void)
{
    if (TerrainGenerationVersion() < TERRAIN_GENERATION_CURRENT ||
        WorldTerrainMode() == TERRAIN_FLAT || WorldGetSeed() != activeSeed) return NULL;
    return active;
}

void LandformRuntimeReset(void)
{
    active = NULL;
    for (int i = 0; i < 2; i++) LandformDestroy(cache[i].atlas);
    memset(cache, 0, sizeof(cache));
    replacement = 0;
}

void LandformApplyTerrainSample(int x, int z, SurfaceTerrainSample *sample)
{
    if (!sample) return;
    const LandformAtlas *a = LandformRuntimeAtlas();
    if (!a) return;
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)x, (float)z);
    sample->landform = LandformSampleAt(a, SurfaceDirectionFromMapCoordinates(
        (float)cell.x + .5f, (float)cell.z + .5f), sample->elevation);
    sample->elevation = sample->landform.elevation;
}
