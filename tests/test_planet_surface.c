#include "planet_surface.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double simulationTime = 0.0;

double SpaceSimulationTime(void)
{
    return simulationTime;
}

static PlanetProfile TestProfile(SolarBodyStyle style)
{
    PlanetProfile profile = { 0 };
    profile.seed = 0x5a17c3u;
    profile.style = style;
    profile.atmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
    profile.equilibriumTempK = 288.0f;
    profile.oceanCoverage = 0.46f;
    profile.albedo = 0.30f;
    profile.greenhouseEffect = 0.18f;
    profile.axialTilt = 0.41f;
    profile.seasonPhase = 0.0f;
    profile.yearLength = 100.0f;
    profile.prevailingWindAngle = 0.73f;
    profile.volcanicActivity = 0.62f;
    profile.impactRate = 0.74f;
    profile.hasSolidSurface = style != SOLAR_STYLE_GAS;
    return profile;
}

static void AssertNear(float left, float right, float tolerance)
{
    assert(fabsf(left - right) <= tolerance);
}

static void AssertSamplesNear(PlanetSurfaceSample left, PlanetSurfaceSample right,
                              float tolerance)
{
#define ASSERT_SAMPLE_FIELD(field) AssertNear(left.field, right.field, tolerance)
    ASSERT_SAMPLE_FIELD(continentalness);
    ASSERT_SAMPLE_FIELD(regionalness);
    ASSERT_SAMPLE_FIELD(climate);
    ASSERT_SAMPLE_FIELD(detail);
    ASSERT_SAMPLE_FIELD(temperature);
    ASSERT_SAMPLE_FIELD(moisture);
    ASSERT_SAMPLE_FIELD(iceCoverage);
    ASSERT_SAMPLE_FIELD(impactDepth);
    ASSERT_SAMPLE_FIELD(impactRim);
    ASSERT_SAMPLE_FIELD(ejecta);
    ASSERT_SAMPLE_FIELD(volcanicActivity);
    ASSERT_SAMPLE_FIELD(volcanicCone);
    ASSERT_SAMPLE_FIELD(caldera);
    ASSERT_SAMPLE_FIELD(lavaFlow);
    ASSERT_SAMPLE_FIELD(duneBand);
    ASSERT_SAMPLE_FIELD(glacierFlow);
    ASSERT_SAMPLE_FIELD(glacierCracks);
#undef ASSERT_SAMPLE_FIELD
    assert(left.biome == right.biome);
}

static void AssertUnitInterval(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f);
    assert(value <= 1.0f);
}

static void AssertValidSample(PlanetSurfaceSample sample)
{
    AssertUnitInterval(sample.continentalness);
    AssertUnitInterval(sample.regionalness);
    AssertUnitInterval(sample.climate);
    AssertUnitInterval(sample.detail);
    assert(isfinite(sample.temperature));
    AssertUnitInterval(sample.moisture);
    AssertUnitInterval(sample.iceCoverage);
    AssertUnitInterval(sample.impactDepth);
    AssertUnitInterval(sample.impactRim);
    AssertUnitInterval(sample.ejecta);
    AssertUnitInterval(sample.volcanicActivity);
    AssertUnitInterval(sample.volcanicCone);
    AssertUnitInterval(sample.caldera);
    AssertUnitInterval(sample.lavaFlow);
    AssertUnitInterval(sample.duneBand);
    AssertUnitInterval(sample.glacierFlow);
    AssertUnitInterval(sample.glacierCracks);
    assert(sample.biome >= 0 && sample.biome < PLANET_BIOME_COUNT);
}

static void TestDeterministicSampling(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    simulationTime = 17.25;
    PlanetSurfaceSample first = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                          1.14f, -0.37f);
    PlanetSurfaceSample second = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                           1.14f, -0.37f);
    AssertSamplesNear(first, second, 0.0f);
    AssertValidSample(first);
}

static void TestLongitudeSeam(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    simulationTime = 0.0;
    PlanetSurfaceSample west = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                         -PI, 0.28f);
    PlanetSurfaceSample east = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                         PI, 0.28f);
    AssertSamplesNear(west, east, 0.0001f);
}

static void TestSeasonalTemperature(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    simulationTime = 0.0;
    PlanetSurfaceSample northernWinter = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                                    0.42f, 0.61f);
    PlanetSurfaceSample equatorStart = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                                 0.42f, 0.0f);

    simulationTime = profile.yearLength * 0.25;
    PlanetSurfaceSample northernSummer = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                                    0.42f, 0.61f);
    PlanetSurfaceSample southernSeason = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                                    0.42f, -0.61f);
    PlanetSurfaceSample equatorQuarter = PlanetSampleGlobalSurface(profile.seed, &profile,
                                                                   0.42f, 0.0f);

    assert(northernSummer.temperature > northernWinter.temperature + 10.0f);
    assert(southernSeason.temperature < northernWinter.temperature - 10.0f);
    AssertNear(equatorStart.temperature, equatorQuarter.temperature, 0.0001f);
}

static bool BiomeMatchesStyle(SolarBodyStyle style, PlanetBiome biome)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:
        return biome == PLANET_BIOME_LAVA_SEA ||
               biome == PLANET_BIOME_VOLCANIC_RIDGE ||
               biome == PLANET_BIOME_BASALT_PLAINS;
    case SOLAR_STYLE_ICE:
        return biome == PLANET_BIOME_GLACIER || biome == PLANET_BIOME_ICE_SHEET;
    case SOLAR_STYLE_DESERT:
        return biome == PLANET_BIOME_OASIS || biome == PLANET_BIOME_BADLANDS ||
               biome == PLANET_BIOME_DUNES;
    case SOLAR_STYLE_CRATER:
        return biome == PLANET_BIOME_GLACIER || biome == PLANET_BIOME_ICE_SHEET ||
               biome == PLANET_BIOME_IMPACT_BASIN ||
               biome == PLANET_BIOME_CRATER_HIGHLANDS;
    case SOLAR_STYLE_TEMPERATE:
        return biome == PLANET_BIOME_OCEAN || biome == PLANET_BIOME_COAST ||
               biome == PLANET_BIOME_ICE_SHEET || biome == PLANET_BIOME_ALPINE ||
               biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_PLAINS;
    case SOLAR_STYLE_GAS:
        return biome == PLANET_BIOME_STORM_BANDS;
    default:
        return false;
    }
}

static void TestStyleBiomeDomains(void)
{
    simulationTime = 41.0;
    for (int style = SOLAR_STYLE_LAVA; style <= SOLAR_STYLE_TEMPERATE; style++) {
        PlanetProfile profile = TestProfile((SolarBodyStyle)style);
        for (int sampleIndex = 0; sampleIndex < 12; sampleIndex++) {
            float longitude = -PI + (2.0f * PI * (float)sampleIndex / 12.0f);
            float latitude = -1.1f + 2.2f * (float)(sampleIndex % 5) / 4.0f;
            PlanetSurfaceSample sample = PlanetSampleGlobalSurface(
                profile.seed + (uint32_t)sampleIndex, &profile, longitude, latitude);
            AssertValidSample(sample);
            assert(BiomeMatchesStyle(profile.style, sample.biome));
        }
    }
}

static void TestBiomeNames(void)
{
    for (int biome = 0; biome < PLANET_BIOME_COUNT; biome++) {
        assert(strcmp(PlanetBiomeName((PlanetBiome)biome), "Unknown terrain") != 0);
    }
    assert(strcmp(PlanetBiomeName(PLANET_BIOME_COUNT), "Unknown terrain") == 0);
}

int main(void)
{
    TestDeterministicSampling();
    TestLongitudeSeam();
    TestSeasonalTemperature();
    TestStyleBiomeDomains();
    TestBiomeNames();
    puts("planet_surface tests passed");
    return 0;
}
