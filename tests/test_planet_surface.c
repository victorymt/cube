#include "space/planet_surface.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double simulationTime = 0.0;

double SpaceSimulationTime(void)
{
    return simulationTime;
}

double SpaceElapsedSimulationTime(void)
{
    return simulationTime;
}

double SpacePeriodicSimulationTime(double elapsedTime)
{
    return elapsedTime;
}

static PlanetProfile TestProfile(SolarBodyStyle style)
{
    PlanetProfile profile = { 0 };
    profile.seed = 0x5a17c3u;
    profile.style = style;
    profile.atmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
    profile.receivedIrradiance = 1.0;
    profile.radiativeTempK = 255.0f;
    profile.equilibriumTempK = 288.0f;
    profile.surfacePressureAtm = 1.0f;
    profile.atmosphereDensity = 0.78f;
    profile.oceanCoverage = 0.46f;
    profile.iceCoverage = style == SOLAR_STYLE_ICE ? 0.62f : 0.08f;
    profile.cloudCoverage = 0.52f;
    profile.albedo = 0.30f;
    profile.greenhouseEffect = 0.84f;
    profile.axialTilt = 0.41f;
    profile.seasonPhase = 0.0f;
    profile.yearLength = 100.0f;
    profile.prevailingWindAngle = 0.73f;
    profile.windStrength = 0.46f;
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
    ASSERT_SAMPLE_FIELD(erosion);
    ASSERT_SAMPLE_FIELD(ridge);
    ASSERT_SAMPLE_FIELD(peak);
    ASSERT_SAMPLE_FIELD(trench);
    ASSERT_SAMPLE_FIELD(climate);
    ASSERT_SAMPLE_FIELD(detail);
    ASSERT_SAMPLE_FIELD(temperature);
    ASSERT_SAMPLE_FIELD(meanTemperature);
    ASSERT_SAMPLE_FIELD(seasonalAmplitude);
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
    AssertUnitInterval(sample.erosion);
    AssertUnitInterval(sample.ridge);
    AssertUnitInterval(sample.peak);
    AssertUnitInterval(sample.trench);
    AssertUnitInterval(sample.climate);
    AssertUnitInterval(sample.detail);
    assert(isfinite(sample.temperature));
    assert(isfinite(sample.meanTemperature));
    assert(isfinite(sample.seasonalAmplitude));
    assert(sample.seasonalAmplitude >= 0.0f);
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

static void TestLargeTimeSeasonalPeriodicity(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    double referenceTime = (double)profile.yearLength * 0.25;
    double largeTime = (double)profile.yearLength * 10000000000.0 +
                       referenceTime;
    PlanetSurfaceSample reference = PlanetSampleGlobalSurfaceAtTime(
        profile.seed, &profile, 0.42f, 0.61f, referenceTime);
    PlanetSurfaceSample repeated = PlanetSampleGlobalSurfaceAtTime(
        profile.seed, &profile, 0.42f, 0.61f, largeTime);
    PlanetSurfaceSample invalid = PlanetSampleGlobalSurfaceAtTime(
        profile.seed, &profile, 0.42f, 0.61f, INFINITY);

    AssertSamplesNear(reference, repeated, 0.0001f);
    AssertValidSample(invalid);
}

static void TestBaselineIgnoresSimulationTime(void)
{
    PlanetProfile profile = TestProfile(SOLAR_STYLE_TEMPERATE);
    simulationTime = 0.0;
    PlanetSurfaceSample first = PlanetSampleGlobalSurfaceBaseline(
        profile.seed, &profile, 0.42f, 0.61f);
    simulationTime = profile.yearLength * 0.25;
    PlanetSurfaceSample second = PlanetSampleGlobalSurfaceBaseline(
        profile.seed, &profile, 0.42f, 0.61f);
    AssertSamplesNear(first, second, 0.0f);
    AssertNear(first.temperature, first.meanTemperature, 0.0f);

    PlanetSurfaceSample explicitSeason = PlanetSampleGlobalSurfaceAtTime(
        profile.seed, &profile, 0.42f, 0.61f, profile.yearLength * 0.25);
    assert(explicitSeason.temperature > first.temperature + 10.0f);
    AssertNear(explicitSeason.meanTemperature, first.meanTemperature, 0.0f);
}

static void TestTiltChangesSeasonalAmplitude(void)
{
    PlanetProfile flat = TestProfile(SOLAR_STYLE_TEMPERATE);
    flat.axialTilt = 0.0f;
    PlanetProfile tilted = flat;
    tilted.axialTilt = 0.56f;
    simulationTime = tilted.yearLength * 0.25;
    PlanetSurfaceSample flatSample = PlanetSampleGlobalSurface(
        flat.seed, &flat, 0.42f, 0.61f);
    PlanetSurfaceSample tiltedSample = PlanetSampleGlobalSurface(
        tilted.seed, &tilted, 0.42f, 0.61f);
    assert(tiltedSample.seasonalAmplitude >
           flatSample.seasonalAmplitude + 8.0f);
    assert(tiltedSample.temperature > flatSample.temperature + 8.0f);
}

static void TestEccentricityChangesSeasonalTemperature(void)
{
    PlanetProfile circular = TestProfile(SOLAR_STYLE_TEMPERATE);
    circular.axialTilt = 0.0f;
    circular.orbitalEccentricity = 0.0f;
    circular.orbitalTemperatureAmplitudeK = 0.0f;
    PlanetProfile eccentric = circular;
    eccentric.orbitalEccentricity = 0.32f;
    eccentric.orbitalTemperatureAmplitudeK = 18.0f;

    PlanetSurfaceSample nearPeriapsis = PlanetSampleGlobalSurfaceAtTime(
        eccentric.seed, &eccentric, 0.42f, 0.0f, 0.0);
    PlanetSurfaceSample nearApoapsis = PlanetSampleGlobalSurfaceAtTime(
        eccentric.seed, &eccentric, 0.42f, 0.0f,
        eccentric.yearLength * 0.5);
    PlanetSurfaceSample circularReference = PlanetSampleGlobalSurfaceAtTime(
        circular.seed, &circular, 0.42f, 0.0f, 0.0);

    assert(fabsf(nearPeriapsis.temperature - nearApoapsis.temperature) > 6.0f);
    assert(fabsf(circularReference.temperature - circularReference.meanTemperature) <
           0.0001f);
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

static PlanetProfile CanonicalSurfaceProfile(uint32_t bodyId,
                                             SolarBodyStyle style)
{
    PlanetProfile profile = TestProfile(style);
    profile.canonicalBodyId = bodyId;
    if (bodyId == 2u) {
        profile.equilibriumTempK = 737.0f;
        profile.oceanCoverage = 0.0f;
        profile.iceCoverage = 0.0f;
        profile.cloudCoverage = 0.98f;
        profile.volcanicActivity = 0.86f;
        profile.impactRate = 0.10f;
    } else if (bodyId == 3u) {
        profile.equilibriumTempK = 288.0f;
        profile.oceanCoverage = 0.71f;
        profile.iceCoverage = 0.03f;
        profile.cloudCoverage = 0.60f;
    } else if (bodyId == 4u) {
        profile.equilibriumTempK = 210.0f;
        profile.oceanCoverage = 0.0f;
        profile.iceCoverage = 0.12f;
        profile.cloudCoverage = 0.12f;
        profile.impactRate = 0.74f;
    }
    return profile;
}

static void TestCanonicalSurfaceCharacter(void)
{
    PlanetProfile venus = CanonicalSurfaceProfile(2u, SOLAR_STYLE_LAVA);
    for (int index = 0; index < 96; index++) {
        float longitude = -PI + 2.0f * PI * (float)index / 96.0f;
        float latitude = -1.45f + 2.90f * (float)(index % 19) / 18.0f;
        PlanetSurfaceSample sample = PlanetSampleGlobalSurfaceBaseline(
            venus.seed, &venus, longitude, latitude);
        assert(sample.biome != PLANET_BIOME_LAVA_SEA);
    }

    PlanetProfile earth = CanonicalSurfaceProfile(3u, SOLAR_STYLE_TEMPERATE);
    int oceanSamples = 0;
    int totalSamples = 0;
    for (int latitudeIndex = 0; latitudeIndex < 40; latitudeIndex++) {
        float latitude = -1.48f + 2.96f * (float)latitudeIndex / 39.0f;
        for (int longitudeIndex = 0; longitudeIndex < 80; longitudeIndex++) {
            float longitude = -PI + 2.0f * PI * (float)longitudeIndex / 80.0f;
            PlanetSurfaceSample sample = PlanetSampleGlobalSurfaceBaseline(
                earth.seed, &earth, longitude, latitude);
            oceanSamples += sample.biome == PLANET_BIOME_OCEAN ||
                            sample.biome == PLANET_BIOME_COAST;
            totalSamples++;
        }
    }
    float oceanFraction = (float)oceanSamples / (float)totalSamples;
    assert(oceanFraction > 0.58f && oceanFraction < 0.82f);

    PlanetProfile mars = CanonicalSurfaceProfile(4u, SOLAR_STYLE_DESERT);
    PlanetSurfaceSample northPole = PlanetSampleGlobalSurfaceBaseline(
        mars.seed, &mars, 0.3f, 1.50f);
    PlanetSurfaceSample southPole = PlanetSampleGlobalSurfaceBaseline(
        mars.seed, &mars, -1.1f, -1.50f);
    assert(northPole.biome == PLANET_BIOME_ICE_SHEET);
    assert(southPole.biome == PLANET_BIOME_ICE_SHEET);
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
    TestLargeTimeSeasonalPeriodicity();
    TestBaselineIgnoresSimulationTime();
    TestTiltChangesSeasonalAmplitude();
    TestEccentricityChangesSeasonalTemperature();
    TestStyleBiomeDomains();
    TestCanonicalSurfaceCharacter();
    TestBiomeNames();
    puts("planet_surface tests passed");
    return 0;
}
