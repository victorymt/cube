#include "presentation/audio.h"
#include "presentation/particles.h"
#include "space/space.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static double simulationTime = 120.0;
static bool planetWorldActive = false;
static int particleEmissions = 0;
static int biomeSamples = 0;
static int planetSurfaceSamples = 0;
static int audioChanges = 0;
static bool rainAudioEnabled = false;
static float lightDaylight = 0.5f;
static float lightEclipse = 0.0f;
static bool homeWorldSurfaceActive = true;
static PlanetAtmosphereType planetAtmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
static float planetAtmosphereDensity = 0.62f;

void AudioSetRain(bool enabled)
{
    audioChanges++;
    rainAudioEnabled = enabled;
}

void ParticlesEmitOne(Vector3 position, Vector3 velocity, Color color,
                      Vector3 size, float life, float gravity)
{
    (void)velocity;
    (void)color;
    (void)size;
    (void)life;
    (void)gravity;
    assert(isfinite(position.x));
    assert(isfinite(position.y));
    assert(isfinite(position.z));
    particleEmissions++;
}

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

bool PlanetWorldIsActive(void)
{
    return planetWorldActive;
}

bool HomeWorldSurfaceIsActive(void)
{
    return homeWorldSurfaceActive && !planetWorldActive;
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

const PlanetProfile *PlanetWorldProfile(void)
{
    static PlanetProfile profile = {
        .cloudCoverage = 0.52f,
        .seasonalHumidityBias = 0.24f,
        .windStrength = 0.44f,
        .prevailingWindAngle = 0.75f
    };
    profile.atmosphereType = planetAtmosphereType;
    profile.atmosphereDensity = planetAtmosphereDensity;
    return &profile;
}

bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out)
{
    (void)surfacePosition;
    if (!out) return false;
    *out = (PlanetLightState){
        .daylight = lightDaylight,
        .eclipse = lightEclipse,
        .sourceCount = 1,
        .totalIntensity = 1.0f
    };
    return true;
}

bool PlanetWorldLightStateAtTime(Vector3 surfacePosition,
                                 double sampleTime,
                                 PlanetLightState *out)
{
    (void)sampleTime;
    return PlanetWorldLightStateAt(surfacePosition, out);
}

uint32_t PlanetWorldSeed(void)
{
    return 0x2938a4d1u;
}

int PlanetWorldOriginX(void)
{
    return 0;
}

int PlanetWorldOriginZ(void)
{
    return 0;
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

uint32_t WorldGetSeed(void)
{
    return 0x68b32f19u;
}

static void ResetRuntime(void)
{
    simulationTime = 120.0;
    planetWorldActive = false;
    particleEmissions = 0;
    biomeSamples = 0;
    planetSurfaceSamples = 0;
    audioChanges = 0;
    rainAudioEnabled = false;
    lightDaylight = 0.5f;
    lightEclipse = 0.0f;
    homeWorldSurfaceActive = true;
    planetAtmosphereType = PLANET_ATMOSPHERE_BREATHABLE;
    planetAtmosphereDensity = 0.62f;
    WeatherInit();
    audioChanges = 0;
}

static void TestPlanetLightFeedbackChangesWeather(void)
{
    ResetRuntime();
    planetWorldActive = true;
    lightDaylight = 1.0f;
    WeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    float sunlitPrecipitation = WeatherPrecipitationRate();
    float sunlitClouds = WeatherCloudCover();

    ResetRuntime();
    planetWorldActive = true;
    lightDaylight = 0.0f;
    lightEclipse = 1.0f;
    WeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    float eclipsedPrecipitation = WeatherPrecipitationRate();
    float eclipsedClouds = WeatherCloudCover();

    assert(isfinite(sunlitPrecipitation));
    assert(isfinite(eclipsedPrecipitation));
    assert(isfinite(sunlitClouds));
    assert(isfinite(eclipsedClouds));
    assert(fabsf(eclipsedPrecipitation - sunlitPrecipitation) > 0.0001f ||
           fabsf(eclipsedClouds - sunlitClouds) > 0.0001f);
}

static void TestInvalidDeltaTimeIsIgnored(void)
{
    const float invalidDeltas[] = { NAN, INFINITY, -INFINITY, -1.0f, 0.0f };
    for (unsigned index = 0;
         index < sizeof(invalidDeltas) / sizeof(invalidDeltas[0]); index++) {
        ResetRuntime();
        WeatherCycle();
        Weather weatherBefore = WeatherGetCurrent();
        float precipitationBefore = WeatherPrecipitationRate();
        int audioBefore = audioChanges;
        particleEmissions = 0;

        WeatherUpdate(invalidDeltas[index], (Vector3){ 4.0f, 20.0f, -8.0f });

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
    WeatherCycle();
    particleEmissions = 0;

    WeatherUpdate(1000000.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions > 0);
    assert(particleEmissions <= 24);

    particleEmissions = 0;
    WeatherUpdate(1.0f / 60.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions <= 2);
}

static void TestLongRunStaysFiniteAndBounded(void)
{
    ResetRuntime();
    WeatherCycle();

    for (int frame = 0; frame < 20000; frame++) {
        particleEmissions = 0;
        Vector3 position = {
            (float)((frame * 17) % 257) - 128.0f,
            20.0f + (float)(frame % 5),
            (float)((frame * 31) % 257) - 128.0f
        };
        WeatherUpdate(1.0f / 60.0f, position);
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
            WeatherUpdate(0.25f, (Vector3){ NAN, 20.0f, 0.0f });
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
        WeatherUpdate(0.25f, invalidPositions[index]);
        assert(WeatherGetCurrent() == WEATHER_CLEAR);
        assert(WeatherPrecipitationRate() == 0.0f);
        assert(particleEmissions == 0);
        assert(biomeSamples == 0);
        assert(planetSurfaceSamples == 0);
    }

    ResetRuntime();
    WeatherCycle();
    particleEmissions = 0;
    WeatherUpdate(0.25f, (Vector3){ 1.0f, NAN, 2.0f });
    assert(particleEmissions == 0);
    WeatherUpdate(1.0f / 60.0f, (Vector3){ 1.0f, 20.0f, 2.0f });
    assert(particleEmissions <= 2);
}

static void TestInvalidSimulationTimeReturnsClearWeather(void)
{
    ResetRuntime();
    simulationTime = NAN;
    WeatherUpdate(0.25f, (Vector3){ 1.0f, 20.0f, 2.0f });
    assert(WeatherGetCurrent() == WEATHER_CLEAR);
    assert(WeatherPrecipitationRate() == 0.0f);
    assert(particleEmissions == 0);
    assert(biomeSamples == 0);
    assert(planetSurfaceSamples == 0);
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

static void TestVisualStateSafeFallbacks(void)
{
    ResetRuntime();
    WeatherVisualState invalid = WeatherVisualStateAtWorld(
        (Vector3){ NAN, 20.0f, 0.0f }, simulationTime, 0.5f);
    assert(!invalid.active);
    assert(invalid.visibility == 1.0f);

    planetWorldActive = true;
    planetAtmosphereType = PLANET_ATMOSPHERE_NONE;
    planetAtmosphereDensity = 0.0f;
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
    WeatherCycle();
    particleEmissions = 0;
    WeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(WeatherGetCurrent() == WEATHER_RAIN);
    assert(rainAudioEnabled);
    assert(particleEmissions > 0);

    WeatherCycle();
    particleEmissions = 0;
    WeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(WeatherGetCurrent() == WEATHER_SNOW);
    assert(!rainAudioEnabled);
    assert(particleEmissions > 0);
}

static void TestInitRestoresCleanState(void)
{
    ResetRuntime();
    WeatherCycle();
    WeatherUpdate(0.25f, (Vector3){ 4.0f, 20.0f, -8.0f });
    particleEmissions = 0;

    WeatherInit();
    assert(WeatherGetCurrent() == WEATHER_CLEAR);
    assert(WeatherPrecipitationRate() == 0.0f);
    assert(!rainAudioEnabled);
    WeatherUpdate(1.0f / 60.0f, (Vector3){ 4.0f, 20.0f, -8.0f });
    assert(particleEmissions == 0);
}

int main(void)
{
    TestInvalidDeltaTimeIsIgnored();
    TestHugeDeltaTimeHasBoundedEmission();
    TestLongRunStaysFiniteAndBounded();
    TestInvalidPositionsAvoidSamplingAndEmission();
    TestInvalidSimulationTimeReturnsClearWeather();
    TestExtremeWorldCellsReturnSafeDefaults();
    TestVisualStateUsesWeatherFieldWithoutMutation();
    TestVisualStateSafeFallbacks();
    TestNormalRainAndSnowStillEmit();
    TestInitRestoresCleanState();
    TestPlanetLightFeedbackChangesWeather();
    puts("weather runtime tests passed");
    return 0;
}
