#include "world/planet_landform_strategy.h"
#include "world/terrain.h"
#include "raymath.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static PlanetProfile WaterProfile(void)
{
    return (PlanetProfile){ .seed = 1234, .style = SOLAR_STYLE_TEMPERATE,
        .atmosphereType = PLANET_ATMOSPHERE_BREATHABLE, .hasSolidSurface = true,
        .physicalRadiusKm = 5000, .surfaceGravity = 7, .rotationRate = 180,
        .equilibriumTempK = 288, .radiativeTempK = 255, .surfacePressureAtm = 1,
        .atmosphereDensity = 1, .receivedIrradiance = 1, .oceanCoverage = .6f,
        .iceCoverage = .1f, .terrainRoughness = .8f, .cloudCoverage = .4f,
        .yearLength = 365, .ageGyr = 4 };
}

static void AssertBaseline(PlanetLandformStrategy *s, Vector3 d)
{
    LandformBaseline b = PlanetLandformBaseline(d, s);
    int x = (int)floorf(atan2f(d.z, d.x) * SURFACE_RADIUS_BLOCKS);
    int z = (int)floorf(asinf(Clamp(d.y, -1, 1)) * SURFACE_RADIUS_BLOCKS);
    PlanetSurfaceSample sample;
    assert(b.elevation == TerrainPlanetBaselineElevation(s->seed, &s->profile, x, z, &sample));
    float rain = Clamp(sample.moisture * Clamp((sample.meanTemperature - 263.15f) / 20, 0, 1) *
                       (1 - sample.iceCoverage), 0, 1);
    assert(b.runoff == rain);
    assert(b.hardness == Clamp(.25f + .6f * sample.ridge, 0, 1));
    LandformBaseline repeat = PlanetLandformBaseline(d, s);
    assert(b.elevation == repeat.elevation && b.runoff == repeat.runoff && b.hardness == repeat.hardness);
}

int main(void)
{
    PlanetProfile p = WaterProfile();
    PlanetLandformStrategy water = WaterWorldLandformStrategy(1234, &p);
    assert(water.id == LANDFORM_STRATEGY_WATER_WORLD && water.seaLevel == 80);
    assert(water.capabilities == (LANDFORM_HAS_DRAINAGE | LANDFORM_HAS_LIQUID_WATER | LANDFORM_HAS_SEDIMENT));
    assert(water.physics.gravity == 7 && water.physics.radiusMeters == 5000000);
    assert(fabs(water.physics.angularVelocity - acos(-1)/86400) < 1e-15);
    assert(fabs(water.physics.radiusMeters / water.physics.metersPerBlock - SURFACE_RADIUS_BLOCKS) < 1e-9);
    uint64_t hash = PlanetLandformStrategyFingerprint(&water);
    PlanetLandformStrategy copy = water;
    assert(hash == PlanetLandformStrategyFingerprint(&copy));
    copy.seaLevel += 1; assert(hash != PlanetLandformStrategyFingerprint(&copy));
    copy = water; copy.profile.rotationRate += 1; assert(hash != PlanetLandformStrategyFingerprint(&copy));
    copy = water; copy.profile.oceanCoverage += .1f; assert(hash != PlanetLandformStrategyFingerprint(&copy));
    copy = water; copy.seed++; assert(hash != PlanetLandformStrategyFingerprint(&copy));
    // Every cube face, including both sides of all its edges and polar cells.
    for (uint32_t id = 0; id < LANDFORM_CELL_COUNT; id += 127) {
        AssertBaseline(&water, LandformCellDirection(id));
        for (int edge = 0; edge < 4; edge++)
            AssertBaseline(&water, LandformCellDirection(LandformNeighbor(id, edge)));
    }
    AssertBaseline(&water, (Vector3){0, 1, 0});
    AssertBaseline(&water, (Vector3){0, -1, 0});
    // The two longitude representations of the date line denote one column.
    for (int z = -4096; z <= 4096; z += 128) {
        int left = TerrainPlanetBaselineElevation(1234, &p, -8192, z, NULL);
        int right = TerrainPlanetBaselineElevation(1234, &p, 8192, z, NULL);
        assert(left == right);
    }
    LandformBuildInput input = PlanetLandformBuildInput(&water);
    LandformAtlas *a = LandformBuildWithInput(&input), *b = LandformBuildWithInput(&input);
    assert(a && b && a->rivers && a->seaLevel == water.seaLevel);
    assert(!memcmp(a->cells, b->cells, sizeof(*a->cells) * LANDFORM_CELL_COUNT));
    assert(a->strategyId == LANDFORM_STRATEGY_WATER_WORLD);
    printf("water strategy: %u rivers, %u lake cells\n", a->rivers, a->lakes);
    LandformDestroy(a); LandformDestroy(b);

    PlanetLandformStrategy home = HomeworldLandformStrategy(1234);
    assert(home.id == LANDFORM_STRATEGY_HOMEWORLD && home.seaLevel == HOME_SEA_LEVEL);
    for (uint32_t id = 0; id < LANDFORM_CELL_COUNT; id += 127) {
        Vector3 d = LandformCellDirection(id);
        int x = (int)floorf(atan2f(d.z, d.x) * SURFACE_RADIUS_BLOCKS);
        int z = (int)floorf(asinf(Clamp(d.y, -1, 1)) * SURFACE_RADIUS_BLOCKS);
        SurfaceTerrainSample sample = {0};
        float height = TerrainHomeBaselineElevation(1234, x, z, &sample);
        LandformBaseline base = PlanetLandformBaseline(d, &home);
        assert(base.elevation == height && base.hardness == .25f + .6f * sample.ridge);
        assert(base.runoff == .65f - .56f * expf(-powf((fabsf(d.y) - .45f) / .19f, 2)));
    }
    for (unsigned i = 0; i < 9; i++) {
        p = WaterProfile();
        switch (i) {
        case 0: p.style = SOLAR_STYLE_DESERT; break;
        case 1: p.style = SOLAR_STYLE_LAVA; break;
        case 2: p.style = SOLAR_STYLE_ICE; break;
        case 3: p.style = SOLAR_STYLE_GAS; break;
        case 4: p.hasSolidSurface = false; break;
        case 5: p.oceanCoverage = 0; break;
        case 6: p.iceCoverage = 1; break;
        case 7: p.surfaceGravity = NAN; break;
        case 8: p.equilibriumTempK = 200; break;
        }
        PlanetLandformStrategy none = WaterWorldLandformStrategy(1234, &p);
        assert(none.capabilities == LANDFORM_CAPABILITY_NONE);
        input = PlanetLandformBuildInput(&none);
        assert(!LandformBuildWithInput(&input));
    }
    p = WaterProfile(); p.rotationRate = 0;
    water = WaterWorldLandformStrategy(1234, &p);
    assert(water.capabilities && water.physics.angularVelocity == 0);
    assert(WaterWorldLandformStrategy(1, NULL).capabilities == 0);
    puts("planet landform strategy tests passed");
}
