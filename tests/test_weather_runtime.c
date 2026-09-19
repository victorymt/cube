#include "core/game_effects.h"
#include "presentation/weather_presentation.h"
#include "world/terrain.h"
#include "world/surface_topology.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static double simulationTime = 120.0;
static int particleEmissions = 0;
static int biomeSamples = 0;
static int planetSurfaceSamples = 0;
static int audioChanges = 0;
static bool rainAudioEnabled = false;
static float lightDaylight = 0.5f;
static float lightEclipse = 0.0f;
static PlanetAtmosphereType planetAtmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
static float planetAtmosphereDensity = 0.62f;

static PlanetProfile TestPlanetProfile(void)
{
    return (PlanetProfile){
        .seed = 0x2938a4d1u,
        .style = SOLAR_STYLE_TEMPERATE,
        .atmosphereType = planetAtmosphereType,
        .equilibriumTempK = 288.0f,
        .surfacePressureAtm = 1.0f,
        .atmosphereDensity = planetAtmosphereDensity,
        .oceanCoverage = 0.54f,
        .iceCoverage = 0.08f,
        .cloudCoverage = 0.52f,
        .seasonalHumidityBias = 0.24f,
        .windStrength = 0.44f,
        .prevailingWindAngle = 0.75f,
        .hasSolidSurface = true
    };
}

static void InstallHomeSurface(void)
{
    const WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .seed = 0x68b32f19u
        }
    };
    WorldInstallSurfaceSnapshot(&snapshot);
}

static void InstallPlanetSurface(void)
{
    const WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_PLANET,
            .bodyId = 7u,
            .seed = 0x2938a4d1u
        },
        .planetProfile = TestPlanetProfile()
    };
    WorldInstallSurfaceSnapshot(&snapshot);
}

// weather_presentation still consumes the narrow space-facing presentation
// queries; keep its focused-test adapters backed by the installed snapshot.
bool HomeWorldSurfaceIsActive(void)
{
    return WorldCurrentSurfaceSnapshot()->context.kind == WORLD_SURFACE_HOME;
}

bool PlanetWorldIsActive(void)
{
    return WorldCurrentSurfaceSnapshot()->context.kind == WORLD_SURFACE_PLANET;
}

const PlanetProfile *PlanetWorldProfile(void)
{
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    return snapshot->context.kind == WORLD_SURFACE_PLANET
        ? &snapshot->planetProfile : NULL;
}

static void CaptureGameEffects(void)
{
    GameEffect effect;
    while (GameEffectsPoll(&effect)) {
        if (effect.type == GAME_EFFECT_AUDIO) {
            assert(effect.data.audio.cue == GAME_AUDIO_SET_RAIN);
            audioChanges++;
            rainAudioEnabled = effect.data.audio.enabled;
            continue;
        }
        assert(effect.type == GAME_EFFECT_PARTICLE_ONE);
        assert(isfinite(effect.data.particleOne.position.x));
        assert(isfinite(effect.data.particleOne.position.y));
        assert(isfinite(effect.data.particleOne.position.z));
        particleEmissions++;
    }
}

static void TestWeatherInit(void)
{
    WeatherInit();
    CaptureGameEffects();
}

static void TestWeatherCycle(void)
{
    WeatherCycle();
    CaptureGameEffects();
}

static void TestWeatherUpdate(float dt, Vector3 position)
{
    WeatherUpdate(dt, position, simulationTime);
    CaptureGameEffects();
}

bool WorldNetherIsActive(void)
{
    return false;
}

float HomeWorldSpaceFade(Vector3 position)
{
    (void)position;
    return 0.0f;
}

float PlanetWorldAtmosphereFade(Vector3 position)
{
    (void)position;
    return 0.0f;
}

static bool TestSurfaceLightAt(Vector3 position, double sampleTime,
                               WeatherSurfaceLightSample *out,
                               void *userData)
{
    (void)position;
    (void)sampleTime;
    (void)userData;
    if (!out) return false;
    *out = (WeatherSurfaceLightSample){
        .daylight = lightDaylight,
        .eclipse = lightEclipse
    };
    return true;
}

PlanetSurfaceSample PlanetSurfaceAtTime(int x, int z,
                                       double sampleTime)
{
    (void)x;
    (void)z;
    (void)sampleTime;
    planetSurfaceSamples++;
    return (PlanetSurfaceSample){
        .temperature = 288.0f,
        .moisture = 0.65f
    };
}

int PlanetTerrainHeight(int x, int z)
{
    (void)x;
    (void)z;
    return 10;
}

void PlanetSurfaceLatLonAt(int x, int z, float *longitude, float *latitude)
{
    (void)x;
    (void)z;
    if (longitude) *longitude = 0.25f;
    if (latitude) *latitude = 0.52f;
}

void HomeSurfaceLatLonAt(int x, int z, float *longitude, float *latitude)
{
    (void)x;
    (void)z;
    if (longitude) *longitude = -0.15f;
    if (latitude) *latitude = 0.42f;
}

Biome BiomeAt(int x, int z)
{
    (void)x;
    (void)z;
    biomeSamples++;
    return BIOME_PLAINS;
}

int WorldSurfaceHeightAt(int x, int z)
{
    (void)x;
    (void)z;
    return 10;
}

int WorldSurfaceMapOriginX(void)
{
    return WorldCurrentSurfaceSnapshot()->context.originX;
}

int WorldSurfaceMapOriginZ(void)
{
    return WorldCurrentSurfaceSnapshot()->context.originZ;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return WorldCurrentSurfaceSnapshot()->context.bodyId;
}

uint32_t WorldGetSeed(void)
{
    return WorldCurrentSurfaceSnapshot()->context.seed;
}

static void ResetRuntime(void)
{
    simulationTime = 120.0;
    particleEmissions = 0;
    biomeSamples = 0;
    planetSurfaceSamples = 0;
    audioChanges = 0;
    rainAudioEnabled = false;
    lightDaylight = 0.5f;
    lightEclipse = 0.0f;
    planetAtmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
    planetAtmosphereDensity = 0.62f;
    InstallHomeSurface();
    GameEffectsReset();
    TestWeatherInit();
    WeatherSetSurfaceLightProvider(TestSurfaceLightAt, NULL);
    audioChanges = 0;
}

static void TestPlanetLightFeedbackChangesWeather(void)
{
    ResetRuntime();
    InstallPlanetSurface();
    lightDaylight = 1.0f;
    TestWeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    float sunlitPrecipitation = WeatherPrecipitationRate();
    float sunlitClouds = WeatherCloudCover();

    ResetRuntime();
    InstallPlanetSurface();
    lightDaylight = 0.0f;
    lightEclipse = 1.0f;
    TestWeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    float eclipsedPrecipitation = WeatherPrecipitationRate();
    float eclipsedClouds = WeatherCloudCover();

    assert(isfinite(sunlitPrecipitation));
    assert(isfinite(eclipsedPrecipitation));
    assert(isfinite(sunlitClouds));
    assert(isfinite(eclipsedClouds));
    assert(fabsf(eclipsedPrecipitation - sunlitPrecipitation) > 0.0001f ||
           fabsf(eclipsedClouds - sunlitClouds) > 0.0001f);
}

static bool HistoricalLightAt(Vector3 position, double sampleTime,
                               WeatherSurfaceLightSample *out, void *context)
{
    (void)position;
    (void)context;
    *out = (WeatherSurfaceLightSample){
        .daylight = sampleTime < 100.0 ? 0.2f : 0.9f
    };
    return true;
}

static int historicalFeedbackCalls;
static bool CurrentHydrologyAt(int x, int z, double sampleTime,
                               LocalClimateHydrologyFeedback *out,
                               void *context)
{
    (void)x; (void)z; (void)sampleTime; (void)out; (void)context;
    historicalFeedbackCalls++;
    return false;
}

static void TestHistoricalWeatherIgnoresCurrentOverrides(void)
{
    for (int planet = 0; planet < 2; planet++) {
        ResetRuntime();
        if (planet) InstallPlanetSurface();
        WeatherSetSurfaceLightProvider(HistoricalLightAt, NULL);
        WeatherSetHydrologyFeedbackProvider(CurrentHydrologyAt, NULL);
        historicalFeedbackCalls = 0;
        float daylight;
        WeatherFieldSample before = WeatherFieldSampleHistoricalAtWorldTime(
            4, -8, 80.0, 0.5f, &daylight);
        assert(daylight == 0.2f);
        WeatherSetDaylight(1.0f);
        TestWeatherCycle();
        assert(WeatherForcePhenomenon(
            WEATHER_PHENOMENON_BLIZZARD, 1.0f, 60u));
        assert(WeatherForceCloudGenus(WEATHER_CLOUD_GENUS_CIRRUS, 1.0f, 60u));
        WeatherFieldSample after = WeatherFieldSampleHistoricalAtWorldTime(
            4, -8, 80.0, 0.0f, &daylight);
        assert(daylight == 0.2f);
        assert(before.temperatureK == after.temperatureK);
        assert(before.cloudCover == after.cloudCover);
        assert(before.precipitation == after.precipitation);
        assert(before.windAngle == after.windAngle);
        assert(before.phenomena == after.phenomena);
        assert(historicalFeedbackCalls == 0);
        assert(WeatherForcedFramesRemaining() == 60u);
        WeatherFieldSample later = WeatherFieldSampleHistoricalAtWorldTime(
            4, -8, 160.0, 0.0f, &daylight);
        assert(daylight == 0.9f);
        assert(later.temperatureK != before.temperatureK);
        WeatherClearForced();
        WeatherLocalClimateAtWorldTime(4, -8, 160.0, &(LocalClimateState){0});
        assert(historicalFeedbackCalls == 1);
        WeatherClearHydrologyFeedbackProvider();
    }
}

static void TestInvalidDeltaTimeIsIgnored(void)
{
    const float invalidDeltas[] = { NAN, INFINITY, -INFINITY, -1.0f, 0.0f };
    for (unsigned index = 0;
         index < sizeof(invalidDeltas) / sizeof(invalidDeltas[0]); index++) {
        ResetRuntime();
        TestWeatherCycle();
        Weather weatherBefore = WeatherGetCurrent();
        float precipitationBefore = WeatherPrecipitationRate();
        int audioBefore = audioChanges;
        particleEmissions = 0;

        TestWeatherUpdate(invalidDeltas[index], (Vector3){ 4.0f, 20.0f, -8.0f });

        assert(WeatherGetCurrent() == weatherBefore);
        assert(WeatherPrecipitationRate() == precipitationBefore);
        assert(audioChanges == audioBefore);
        assert(particleEmissions == 0);
        assert(biomeSamples == 0);
    }
}

static void TestHugeDeltaTimeHasBoundedEmission(void)
{
    ResetRuntime();
    TestWeatherCycle();
    particleEmissions = 0;

    TestWeatherUpdate(1000000.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions > 0);
    assert(particleEmissions <= 24);

    particleEmissions = 0;
    TestWeatherUpdate(1.0f / 60.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions <= 2);
}

static void TestLongRunStaysFiniteAndBounded(void)
{
    ResetRuntime();
    TestWeatherCycle();

    for (int frame = 0; frame < 20000; frame++) {
        particleEmissions = 0;
        Vector3 position = {
            (float)((frame * 17) % 257) - 128.0f,
            20.0f + (float)(frame % 5),
            (float)((frame * 31) % 257) - 128.0f
        };
        TestWeatherUpdate(1.0f / 60.0f, position);
        assert(particleEmissions <= 2);
        assert(isfinite(WeatherSkyFactor()));
        assert(isfinite(WeatherCloudCover()));
        assert(isfinite(WeatherPrecipitationRate()));
        assert(isfinite(WeatherStormIntensity()));
        assert(isfinite(WeatherWindIntensity()));
        assert(WeatherSkyFactor() >= 0.0f && WeatherSkyFactor() <= 1.0f);
        assert(WeatherCloudCover() >= 0.0f && WeatherCloudCover() <= 1.0f);
        assert(WeatherPrecipitationRate() >= 0.0f &&
               WeatherPrecipitationRate() <= 1.0f);
        assert(WeatherStormIntensity() >= 0.0f &&
               WeatherStormIntensity() <= 1.0f);
        assert(WeatherWindIntensity() >= 0.0f &&
               WeatherWindIntensity() <= 1.0f);

        if ((frame % 997) == 0) {
            particleEmissions = 0;
            TestWeatherUpdate(0.25f, (Vector3){ NAN, 20.0f, 0.0f });
            assert(particleEmissions == 0);
            assert(isfinite(WeatherPrecipitationRate()));
            assert(WeatherPrecipitationRate() >= 0.0f &&
                   WeatherPrecipitationRate() <= 1.0f);
        }
    }
}

static void TestInvalidPositionsAvoidSamplingAndEmission(void)
{
    const Vector3 invalidPositions[] = {
        { NAN, 20.0f, 0.0f },
        { INFINITY, 20.0f, 0.0f },
        { 0.0f, 20.0f, -INFINITY },
        { 0x1p31f, 20.0f, 0.0f },
        { 0.0f, 20.0f, 0x1p31f }
    };
    for (unsigned index = 0;
         index < sizeof(invalidPositions) / sizeof(invalidPositions[0]);
         index++) {
        ResetRuntime();
        TestWeatherUpdate(0.25f, invalidPositions[index]);
        assert(WeatherGetCurrent() == WEATHER_CLEAR);
        assert(WeatherPrecipitationRate() == 0.0f);
        assert(particleEmissions == 0);
        assert(biomeSamples == 0);
        assert(planetSurfaceSamples == 0);
    }

    ResetRuntime();
    TestWeatherCycle();
    particleEmissions = 0;
    TestWeatherUpdate(0.25f, (Vector3){ 1.0f, NAN, 2.0f });
    assert(particleEmissions == 0);
    TestWeatherUpdate(1.0f / 60.0f, (Vector3){ 1.0f, 20.0f, 2.0f });
    assert(particleEmissions <= 2);
}

static void TestInvalidSimulationTimeReturnsClearWeather(void)
{
    ResetRuntime();
    simulationTime = NAN;
    TestWeatherUpdate(0.25f, (Vector3){ 1.0f, 20.0f, 2.0f });
    assert(WeatherGetCurrent() == WEATHER_CLEAR);
    assert(WeatherPrecipitationRate() == 0.0f);
    assert(particleEmissions == 0);
    assert(biomeSamples == 0);
    assert(planetSurfaceSamples == 0);
}

static void TestImplicitSamplingUsesLastUpdateTime(void)
{
    ResetRuntime();
    simulationTime = 321.25;
    TestWeatherUpdate(0.25f, (Vector3){ 1.0f, 20.0f, 2.0f });

    WeatherFieldSample implicit = WeatherFieldSampleAtWorld(17, -23);
    WeatherFieldSample explicit = WeatherFieldSampleAtWorldTime(
        17, -23, simulationTime);
    float implicitWind = WeatherWindAngleAtWorld(17, -23);
    float explicitWind = WeatherWindAngleAtWorldTime(
        17, -23, simulationTime);
    assert(memcmp(&implicit, &explicit, sizeof(implicit)) == 0);
    assert(implicitWind == explicitWind);

    simulationTime += 97.0;
    WeatherFieldSample unchanged = WeatherFieldSampleAtWorld(17, -23);
    assert(memcmp(&implicit, &unchanged, sizeof(implicit)) == 0);
    assert(WeatherWindAngleAtWorld(17, -23) == implicitWind);
}

static void TestExtremeWorldCellsReturnSafeDefaults(void)
{
    ResetRuntime();
    WeatherFieldSample sample = WeatherFieldSampleAtWorldTime(
        INT_MAX, INT_MIN, simulationTime);
    assert(sample.cloudCover == 0.0f);
    assert(sample.precipitation == 0.0f);
    assert(WeatherWindAngleAtWorldTime(INT_MAX, INT_MIN, simulationTime) ==
           0.0f);
    assert(biomeSamples == 0);
    assert(planetSurfaceSamples == 0);
}

static void TestWeatherCanonicalAliases(void)
{
    ResetRuntime();
    const int half = SURFACE_EQUATOR_BLOCKS / 2;
    const int pole = SURFACE_POLE_TO_POLE_BLOCKS / 2;
    WeatherFieldSample base = WeatherFieldSampleAtWorldTime(
        917, pole + 73, simulationTime);
    WeatherFieldSample northAlias = WeatherFieldSampleAtWorldTime(
        917 + half, pole - 74, simulationTime);
    WeatherFieldSample southBase = WeatherFieldSampleAtWorldTime(
        -731, -pole - 91, simulationTime);
    WeatherFieldSample southAlias = WeatherFieldSampleAtWorldTime(
        -731 + half, -pole + 90, simulationTime);
    WeatherFieldSample wrapped = WeatherFieldSampleAtWorldTime(
        917 + SURFACE_EQUATOR_BLOCKS, pole + 73, simulationTime);
    assert(memcmp(&base, &northAlias, sizeof(base)) == 0);
    assert(memcmp(&southBase, &southAlias, sizeof(southBase)) == 0);
    assert(memcmp(&base, &wrapped, sizeof(base)) == 0);
}

static void TestVisualStateUsesWeatherFieldWithoutMutation(void)
{
    ResetRuntime();
    Weather weatherBefore = WeatherGetCurrent();
    WeatherVisualState first = WeatherVisualStateAtWorld(
        (Vector3){ 18.0f, 20.0f, -42.0f }, simulationTime, 0.72f);
    WeatherVisualState replay = WeatherVisualStateAtWorld(
        (Vector3){ 18.0f, 20.0f, -42.0f }, simulationTime, 0.72f);
    assert(first.active);
    assert(first.active == replay.active);
    assert(first.atmosphereDensity == replay.atmosphereDensity);
    assert(first.daylight == replay.daylight);
    assert(first.cloudCover == replay.cloudCover);
    assert(first.cloudBaseHeight == replay.cloudBaseHeight);
    assert(first.cloudThickness == replay.cloudThickness);
    assert(first.cloudOpacity == replay.cloudOpacity);
    assert(first.fogDensity == replay.fogDensity);
    assert(first.visibility == replay.visibility);
    assert(first.precipitationVeil == replay.precipitationVeil);
    assert(first.stormDarkening == replay.stormDarkening);
    assert(first.windDrift == replay.windDrift);
    assert(first.windAngle == replay.windAngle);
    assert(first.snowFraction == replay.snowFraction);
    assert(WeatherGetCurrent() == weatherBefore);
    assert(first.visibility >= 0.0f && first.visibility <= 1.0f);
    assert(first.fogDensity >= 0.0f && first.fogDensity <= 1.0f);

    float minimumClouds = 1.0f;
    float maximumClouds = 0.0f;
    for (int index = 0; index < 80; index++) {
        WeatherVisualState sample = WeatherVisualStateAtWorld(
            (Vector3){ (float)(index * 311 - 9000), 20.0f,
                       (float)(index * index * 13 - 4000) },
            simulationTime + (double)index * 7.0, 0.72f);
        assert(sample.active);
        minimumClouds = fminf(minimumClouds, sample.cloudCover);
        maximumClouds = fmaxf(maximumClouds, sample.cloudCover);
    }
    assert(maximumClouds - minimumClouds > 0.12f);
}

static void TestVisualStateIsContinuousAcrossCellBoundaries(void)
{
    ResetRuntime();
    const float epsilon = 0.001f;
    WeatherFieldSample fieldLeft;
    WeatherFieldSample fieldRight;
    WeatherFieldSample fieldBelow;
    WeatherFieldSample fieldAbove;
    assert(WeatherFieldSampleInterpolatedAtWorldTime(
        18.0f - epsilon, -42.25f, simulationTime, &fieldLeft));
    assert(WeatherFieldSampleInterpolatedAtWorldTime(
        18.0f + epsilon, -42.25f, simulationTime, &fieldRight));
    assert(WeatherFieldSampleInterpolatedAtWorldTime(
        18.25f, -42.0f - epsilon, simulationTime, &fieldBelow));
    assert(WeatherFieldSampleInterpolatedAtWorldTime(
        18.25f, -42.0f + epsilon, simulationTime, &fieldAbove));
    assert(fabsf(fieldRight.cloudCover - fieldLeft.cloudCover) < 0.0001f);
    assert(fabsf(sinf(fieldRight.windAngle - fieldLeft.windAngle)) <
           0.0001f);
    assert(fabsf(fieldAbove.cloudCover - fieldBelow.cloudCover) < 0.0001f);
    assert(fabsf(sinf(fieldAbove.windAngle - fieldBelow.windAngle)) <
           0.0001f);

    WeatherVisualState left = WeatherVisualStateAtWorld(
        (Vector3){ 18.0f - epsilon, 20.0f, -42.25f },
        simulationTime, 0.72f);
    WeatherVisualState right = WeatherVisualStateAtWorld(
        (Vector3){ 18.0f + epsilon, 20.0f, -42.25f },
        simulationTime, 0.72f);
    WeatherVisualState below = WeatherVisualStateAtWorld(
        (Vector3){ 18.25f, 20.0f, -42.0f - epsilon },
        simulationTime, 0.72f);
    WeatherVisualState above = WeatherVisualStateAtWorld(
        (Vector3){ 18.25f, 20.0f, -42.0f + epsilon },
        simulationTime, 0.72f);
    assert(left.active && right.active && below.active && above.active);
    assert(fabsf(right.cloudCover - left.cloudCover) < 0.0001f);
    assert(fabsf(right.cloudBaseHeight - left.cloudBaseHeight) < 0.001f);
    assert(fabsf(sinf(right.windAngle - left.windAngle)) < 0.0001f);
    assert(fabsf(above.cloudCover - below.cloudCover) < 0.0001f);
    assert(fabsf(sinf(above.windAngle - below.windAngle)) < 0.0001f);
}

static void TestVisualStateSafeFallbacks(void)
{
    ResetRuntime();
    WeatherFieldSample invalidField = { .cloudCover = 1.0f };
    assert(!WeatherFieldSampleInterpolatedAtWorldTime(
        NAN, 0.0f, simulationTime, &invalidField));
    assert(invalidField.cloudCover == 0.0f);
    assert(invalidField.visibility == 1.0f);
    assert(!WeatherFieldSampleInterpolatedAtWorldTime(
        0.0f, 0.0f, simulationTime, NULL));

    WeatherVisualState invalid = WeatherVisualStateAtWorld(
        (Vector3){ NAN, 20.0f, 0.0f }, simulationTime, 0.5f);
    assert(!invalid.active);
    assert(invalid.visibility == 1.0f);

    planetAtmosphereType = PLANET_ATMOSPHERE_NONE;
    planetAtmosphereDensity = 0.0f;
    InstallPlanetSurface();
    WeatherVisualState airless = WeatherVisualStateAtWorld(
        (Vector3){ 4.0f, 20.0f, -8.0f }, simulationTime, 0.5f);
    assert(!airless.active);
    assert(airless.visibility == 1.0f);
    assert(airless.cloudOpacity == 0.0f);
    assert(planetSurfaceSamples == 0);
}

static void TestNormalRainAndSnowStillEmit(void)
{
    ResetRuntime();
    TestWeatherCycle();
    particleEmissions = 0;
    TestWeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(WeatherGetCurrent() == WEATHER_RAIN);
    assert(rainAudioEnabled);
    assert(particleEmissions > 0);

    TestWeatherCycle();
    particleEmissions = 0;
    TestWeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(WeatherGetCurrent() == WEATHER_SNOW);
    assert(!rainAudioEnabled);
    assert(particleEmissions > 0);
}

static void TestForcedPhenomena(void)
{
    ResetRuntime();
    for (int value = 0; value < WEATHER_PHENOMENON_COUNT; value++) {
        WeatherPhenomenon phenomenon = (WeatherPhenomenon)value;
        assert(WeatherForcePhenomenon(phenomenon, 0.8f, 2u));
        assert(WeatherForcedFramesRemaining() == 2u);

        WeatherFieldSample sample = WeatherCurrentSample();
        assert(sample.dominantPhenomenon == phenomenon);
        assert(WeatherSampleHasPhenomenon(sample, phenomenon));
        assert(isfinite(sample.temperatureK));
        assert(isfinite(sample.pressureAtm));
        assert(isfinite(sample.relativeHumidity));
        assert(sample.relativeHumidity >= 0.0f &&
               sample.relativeHumidity <= 1.0f);
        assert(sample.dewPointK <= sample.temperatureK);
        assert(sample.wetBulbK <= sample.temperatureK);
        assert(isfinite(sample.wind));
        assert(isfinite(sample.gust));
        assert(sample.cloudCover >= 0.0f && sample.cloudCover <= 1.0f);
        assert(sample.precipitation >= 0.0f && sample.precipitation <= 1.0f);
        assert(sample.wind >= 0.0f && sample.wind <= 1.0f);
        assert(sample.gust >= sample.wind && sample.gust <= 1.0f);
        assert(sample.visibility >= 0.0f && sample.visibility <= 1.0f);

        WeatherFieldSample spatial = WeatherFieldSampleAtWorldTime(
            4, -8, simulationTime);
        assert(spatial.dominantPhenomenon == phenomenon);
        TestWeatherUpdate(1.0f / 60.0f,
                          (Vector3){ 4.0f, 20.0f, -8.0f });
        assert(WeatherForcedFramesRemaining() == 1u);
        assert(WeatherCurrentSample().dominantPhenomenon == phenomenon);
        TestWeatherUpdate(1.0f / 60.0f,
                          (Vector3){ 4.0f, 20.0f, -8.0f });
        assert(WeatherForcedFramesRemaining() == 0u);
        assert(WeatherCurrentSample().dominantPhenomenon == phenomenon);
        TestWeatherUpdate(1.0f / 60.0f,
                          (Vector3){ 4.0f, 20.0f, -8.0f });
    }

    assert(!WeatherForcePhenomenon(
        WEATHER_PHENOMENON_COUNT, 0.5f, 10u));
    assert(!WeatherForcePhenomenon(WEATHER_PHENOMENON_RAINBOW, NAN, 10u));
    assert(!WeatherForcePhenomenon(WEATHER_PHENOMENON_RAINBOW, -0.1f, 10u));
    assert(!WeatherForcePhenomenon(WEATHER_PHENOMENON_RAINBOW, 1.1f, 10u));
    assert(!WeatherForcePhenomenon(WEATHER_PHENOMENON_RAINBOW, 0.5f, 0u));

    assert(WeatherForcePhenomenon(
        WEATHER_PHENOMENON_THUNDERSTORM, 1.0f, 60u));
    WeatherClearForced();
    assert(WeatherForcedFramesRemaining() == 0u);
}

static void TestForcedCloudGenera(void)
{
    ResetRuntime();
    assert(WeatherForcePhenomenon(
        WEATHER_PHENOMENON_CLOUDY, 0.8f, 1000u));
    for (int value = WEATHER_CLOUD_GENUS_CIRRUS;
         value < WEATHER_CLOUD_GENUS_COUNT; value++) {
        WeatherCloudGenus genus = (WeatherCloudGenus)value;
        assert(WeatherForceCloudGenus(genus, 0.76f, 2u));
        assert(WeatherForcedCloudFramesRemaining() == 2u);
        WeatherFieldSample sample = WeatherCurrentSample();
        assert(sample.dominantCloudGenus == genus);
        assert(WeatherSampleHasCloudGenus(sample, genus));
        assert(fabsf(sample.cloudGenusCoverage[genus] - 0.76f) < 0.00001f);
        WeatherFieldSample spatial = WeatherFieldSampleAtWorldTime(
            4, -8, simulationTime);
        assert(spatial.dominantCloudGenus == genus);

        TestWeatherUpdate(1.0f / 60.0f,
                          (Vector3){ 4.0f, 20.0f, -8.0f });
        assert(WeatherForcedCloudFramesRemaining() == 1u);
        assert(WeatherCurrentSample().dominantCloudGenus == genus);
        TestWeatherUpdate(1.0f / 60.0f,
                          (Vector3){ 4.0f, 20.0f, -8.0f });
        assert(WeatherForcedCloudFramesRemaining() == 0u);
        assert(WeatherCurrentSample().dominantCloudGenus == genus);
    }
    assert(!WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_NONE, 0.5f, 10u));
    assert(!WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_COUNT, 0.5f, 10u));
    assert(!WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_CUMULUS, NAN, 10u));
    assert(!WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_CUMULUS, 1.1f, 10u));
    assert(!WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_CUMULUS, 0.5f, 0u));

    assert(WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_CUMULUS, 0.8f, 60u));
    WeatherClearForcedCloud();
    assert(WeatherForcedCloudFramesRemaining() == 0u);
    assert(WeatherForceCloudGenus(
        WEATHER_CLOUD_GENUS_CIRRUS, 0.4f, 60u));
    WeatherClearForced();
    assert(WeatherForcedCloudFramesRemaining() == 0u);
    assert(WeatherForcedFramesRemaining() == 0u);
}

static void TestInitRestoresCleanState(void)
{
    ResetRuntime();
    TestWeatherCycle();
    TestWeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    particleEmissions = 0;

    TestWeatherInit();
    assert(WeatherGetCurrent() == WEATHER_CLEAR);
    assert(WeatherPrecipitationRate() == 0.0f);
    assert(!rainAudioEnabled);
    TestWeatherUpdate(1.0f / 60.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions == 0);
}

int main(void)
{
    TestInvalidDeltaTimeIsIgnored();
    TestHugeDeltaTimeHasBoundedEmission();
    TestLongRunStaysFiniteAndBounded();
    TestInvalidPositionsAvoidSamplingAndEmission();
    TestInvalidSimulationTimeReturnsClearWeather();
    TestImplicitSamplingUsesLastUpdateTime();
    TestExtremeWorldCellsReturnSafeDefaults();
    TestWeatherCanonicalAliases();
    TestVisualStateUsesWeatherFieldWithoutMutation();
    TestVisualStateIsContinuousAcrossCellBoundaries();
    TestVisualStateSafeFallbacks();
    TestNormalRainAndSnowStillEmit();
    TestForcedPhenomena();
    TestForcedCloudGenera();
    TestInitRestoresCleanState();
    TestPlanetLightFeedbackChangesWeather();
    TestHistoricalWeatherIgnoresCurrentOverrides();
    puts("weather runtime tests passed");
    return 0;
}
