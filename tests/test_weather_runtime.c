#include "audio.h"
#include "particles.h"
#include "space.h"
#include "terrain.h"
#include "weather.h"
#include "world.h"
#include "world_environment.h"

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

const PlanetProfile *PlanetWorldProfile(void)
{
    static PlanetProfile profile = {
        .cloudCoverage = 0.52f,
        .windStrength = 0.44f,
        .prevailingWindAngle = 0.75f
    };
    return &profile;
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
    WeatherInit();
    audioChanges = 0;
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
    TestInvalidPositionsAvoidSamplingAndEmission();
    TestInvalidSimulationTimeReturnsClearWeather();
    TestExtremeWorldCellsReturnSafeDefaults();
    TestNormalRainAndSnowStillEmit();
    TestInitRestoresCleanState();
    puts("weather runtime tests passed");
    return 0;
}
