#include "subsurface.h"

#include <assert.h>
#include <stdio.h>

static void TestDeterminismAndSurfaceProtection(void)
{
    SubsurfaceParams params = {
        .seed = 0x564f5843u,
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    SubsurfaceSample first = SubsurfaceSampleAt(&params, 137, 41, -293, 152);
    SubsurfaceSample repeat = SubsurfaceSampleAt(&params, 137, 41, -293, 152);
    assert(first.tunnel == repeat.tunnel);
    assert(first.chamber == repeat.chamber);
    assert(first.shaft == repeat.shaft);
    assert(first.aquifer == repeat.aquifer);
    assert(first.openness == repeat.openness);
    assert(first.cave == repeat.cave);
    assert(first.flooded == repeat.flooded);

    for (int y = 0; y < params.minY; y++) {
        assert(!SubsurfaceSampleAt(&params, 137, y, -293, 152).cave);
    }
    for (int y = 148; y <= 152; y++) {
        assert(!SubsurfaceSampleAt(&params, 137, y, -293, 152).cave);
    }
}

static void TestEarthScaleUnderground(void)
{
    SubsurfaceParams params = {
        .seed = 0x564f5843u,
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    const int surfaceHeight = 176;
    int samples = 0;
    int caves = 0;
    int deepCaves = 0;
    int highCaves = 0;
    int flooded = 0;
    int chambers = 0;
    int shafts = 0;
    int continuous = 0;
    int maxVerticalRun = 0;

    for (int z = -96; z <= 96; z += 2) {
        for (int x = -96; x <= 96; x += 2) {
            int verticalRun = 0;
            for (int y = params.minY; y < surfaceHeight - params.surfaceClearance;
                 y += 2) {
                SubsurfaceSample sample = SubsurfaceSampleAt(
                    &params, x, y, z, surfaceHeight);
                samples++;
                if (!sample.cave) {
                    verticalRun = 0;
                    continue;
                }
                caves++;
                verticalRun++;
                if (verticalRun > maxVerticalRun) maxVerticalRun = verticalRun;
                if (y <= 48) deepCaves++;
                if (y >= 112) highCaves++;
                if (sample.flooded) flooded++;
                if (sample.chamber >= 0.52f) chambers++;
                if (sample.shaft >= 0.60f) shafts++;
                if (SubsurfaceSampleAt(&params, x + 1, y, z,
                                       surfaceHeight).cave ||
                    SubsurfaceSampleAt(&params, x, y, z + 1,
                                       surfaceHeight).cave) {
                    continuous++;
                }
            }
        }
    }

    printf("subsurface samples=%d caves=%d deep=%d high=%d flooded=%d "
           "chambers=%d shafts=%d max_vertical=%d\n",
           samples, caves, deepCaves, highCaves, flooded, chambers,
           shafts, maxVerticalRun * 2);
    fflush(stdout);

    assert(caves > samples / 100);
    assert(caves < samples / 4);
    assert(deepCaves > 0);
    assert(highCaves > 0);
    assert(flooded > 0);
    assert(chambers > 0);
    assert(shafts > 0);
    assert(continuous > caves * 3 / 4);
    assert(maxVerticalRun >= 8);

}

static void TestPlanetVariation(void)
{
    SubsurfaceParams dry = {
        .seed = 0x6d617273u,
        .activity = 1.08f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 30,
        .aquiferChance = 0.04f
    };
    SubsurfaceParams wet = dry;
    wet.seed = 0x6575726fu;
    wet.aquiferChance = 0.88f;
    int dryFlooded = 0;
    int wetFlooded = 0;
    int dryCaves = 0;
    int wetCaves = 0;
    for (int z = -64; z <= 64; z += 2) {
        for (int x = -64; x <= 64; x += 2) {
            for (int y = 2; y < 112; y += 2) {
                SubsurfaceSample drySample = SubsurfaceSampleAt(
                    &dry, x, y, z, 128);
                SubsurfaceSample wetSample = SubsurfaceSampleAt(
                    &wet, x, y, z, 128);
                if (drySample.cave) dryCaves++;
                if (wetSample.cave) wetCaves++;
                if (drySample.flooded) dryFlooded++;
                if (wetSample.flooded) wetFlooded++;
            }
        }
    }
    assert(dryCaves > 0 && wetCaves > 0);
    assert(wetFlooded > dryFlooded * 2);
}

int main(void)
{
    TestDeterminismAndSurfaceProtection();
    TestEarthScaleUnderground();
    TestPlanetVariation();
    puts("subsurface tests passed");
    return 0;
}
