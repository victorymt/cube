#include "weather.h"

#include "audio.h"
#include "particles.h"
#include "space.h"
#include "terrain.h"
#include "world.h"
#include "world_environment.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#define WEATHER_AREA_RADIUS 26.0f
#define WEATHER_TOP_OFFSET 16.0f
#define WEATHER_MANUAL_SECONDS 45.0f

static Weather current = WEATHER_CLEAR;
static WeatherFieldSample fieldSample = { 0 };
static float manualTimer = 0.0f;
static Weather manualWeather = WEATHER_CLEAR;
static float rainEmissionAccumulator = 0.0f;
static float snowEmissionAccumulator = 0.0f;
static bool rainAudioActive = false;
static uint32_t particleRandomState = 0x91e10da5u;
static float particleWindAngle = 0.0f;

static float WeatherClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float WeatherParticleRandom(void)
{
    particleRandomState ^= particleRandomState << 13;
    particleRandomState ^= particleRandomState >> 17;
    particleRandomState ^= particleRandomState << 5;
    return (float)(particleRandomState & 0x00ffffffu) / 16777215.0f;
}

static WeatherFieldInput WeatherInputAt(Vector3 playerPosition)
{
    WeatherFieldInput input = { 0 };
    int x = (int)floorf(playerPosition.x);
    int z = (int)floorf(playerPosition.z);
    input.simulationTime = SpaceSimulationTime();
    if (PlanetWorldIsActive()) {
        const PlanetProfile *planet = PlanetWorldProfile();
        PlanetSurfaceSample surface = PlanetSurfaceAtTime(
            x, z, input.simulationTime);
        int height = PlanetTerrainHeight(x, z);
        input.seed = PlanetWorldSeed();
        input.worldX = (float)PlanetWorldOriginX() + playerPosition.x;
        input.worldZ = (float)PlanetWorldOriginZ() + playerPosition.z;
        input.temperatureK = surface.temperature -
            fmaxf((float)height - 12.0f, 0.0f) * 0.68f;
        input.moisture = WeatherClamp(
            surface.moisture * (1.0f - surface.iceCoverage * 0.42f));
        input.cloudPotential = planet->cloudCoverage;
        input.windStrength = planet->windStrength;
        input.prevailingWindAngle = planet->prevailingWindAngle;
        return input;
    }

    Biome biome = BiomeAt(x, z);
    int height = WorldSurfaceHeightAt(x, z);
    input.seed = WorldGetSeed();
    input.worldX = playerPosition.x;
    input.worldZ = playerPosition.z;
    input.temperatureK = 288.0f;
    input.moisture = 0.55f;
    input.cloudPotential = 0.46f;
    input.windStrength = 0.38f;
    switch (biome) {
    case BIOME_FOREST:
        input.temperatureK = 285.0f;
        input.moisture = 0.78f;
        input.cloudPotential = 0.58f;
        break;
    case BIOME_DESERT:
        input.temperatureK = 306.0f;
        input.moisture = 0.10f;
        input.cloudPotential = 0.16f;
        input.windStrength = 0.52f;
        break;
    case BIOME_SNOW:
        input.temperatureK = 263.0f;
        input.moisture = 0.48f;
        input.cloudPotential = 0.52f;
        break;
    case BIOME_MOUNTAIN:
        input.temperatureK = 275.0f;
        input.moisture = 0.42f;
        input.cloudPotential = 0.50f;
        input.windStrength = 0.72f;
        break;
    case BIOME_PLAINS:
    default:
        break;
    }
    input.temperatureK -= fmaxf((float)height - 12.0f, 0.0f) * 0.72f;
    input.prevailingWindAngle =
        (float)(input.seed & 0xffffu) / 65535.0f * 2.0f * PI;
    return input;
}

static WeatherFieldSample WeatherManualSample(Weather weather)
{
    WeatherFieldSample sample = { 0 };
    if (weather == WEATHER_RAIN) {
        sample.cloudCover = 0.90f;
        sample.precipitation = 0.86f;
        sample.rain = 0.86f;
        sample.storm = 0.24f;
        sample.wind = 0.58f;
    } else if (weather == WEATHER_SNOW) {
        sample.cloudCover = 0.88f;
        sample.precipitation = 0.72f;
        sample.snow = 0.72f;
        sample.storm = 0.12f;
        sample.wind = 0.46f;
    } else {
        sample.cloudCover = 0.08f;
        sample.wind = 0.18f;
    }
    return sample;
}

static void WeatherUpdateTypeAndAudio(void)
{
    if (fieldSample.precipitation < 0.08f) current = WEATHER_CLEAR;
    else if (fieldSample.snow > fieldSample.rain) current = WEATHER_SNOW;
    else current = WEATHER_RAIN;

    bool shouldPlayRain = fieldSample.rain >
        (rainAudioActive ? 0.035f : 0.075f);
    if (shouldPlayRain != rainAudioActive) {
        rainAudioActive = shouldPlayRain;
        AudioSetRain(rainAudioActive);
    }
}

void WeatherInit(void)
{
    current = WEATHER_CLEAR;
    fieldSample = (WeatherFieldSample){ 0 };
    manualTimer = 0.0f;
    manualWeather = WEATHER_CLEAR;
    rainEmissionAccumulator = 0.0f;
    snowEmissionAccumulator = 0.0f;
    rainAudioActive = false;
    particleRandomState = 0x91e10da5u;
    particleWindAngle = 0.0f;
    AudioSetRain(false);
}

const char *WeatherName(void)
{
    if (fieldSample.storm > 0.48f) {
        return current == WEATHER_SNOW ? "Snow storm" : "Rain storm";
    }
    if (current == WEATHER_RAIN) return "Rain";
    if (current == WEATHER_SNOW) return "Snow";
    return fieldSample.cloudCover > 0.68f ? "Overcast" : "Clear";
}

Weather WeatherGetCurrent(void)
{
    return current;
}

float WeatherSkyFactor(void)
{
    return WeatherClamp(fieldSample.cloudCover * 0.55f +
                        fieldSample.precipitation * 0.25f +
                        fieldSample.storm * 0.20f);
}

float WeatherCloudCover(void)
{
    return fieldSample.cloudCover;
}

float WeatherPrecipitationRate(void)
{
    return fieldSample.precipitation;
}

float WeatherStormIntensity(void)
{
    return fieldSample.storm;
}

float WeatherWindIntensity(void)
{
    return fieldSample.wind;
}

void WeatherCycle(void)
{
    manualWeather = (Weather)(((int)current + 1) % 3);
    manualTimer = WEATHER_MANUAL_SECONDS;
    fieldSample = WeatherManualSample(manualWeather);
    WeatherUpdateTypeAndAudio();
}

static void EmitRain(Vector3 playerPosition)
{
    float x = playerPosition.x +
        (WeatherParticleRandom() - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float z = playerPosition.z +
        (WeatherParticleRandom() - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float y = playerPosition.y + WEATHER_TOP_OFFSET +
        WeatherParticleRandom() * 8.0f;
    float drift = fieldSample.wind * 2.8f;
    ParticlesEmitOne((Vector3){ x, y, z },
                     (Vector3){ cosf(particleWindAngle) * drift, -20.0f,
                                sinf(particleWindAngle) * drift },
                     (Color){ 168, 190, 215, 220 },
                     (Vector3){ 0.03f, 0.5f, 0.03f }, 1.4f, 0.0f);
}

static void EmitSnow(Vector3 playerPosition)
{
    float x = playerPosition.x +
        (WeatherParticleRandom() - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float z = playerPosition.z +
        (WeatherParticleRandom() - 0.5f) * 2.0f * WEATHER_AREA_RADIUS;
    float y = playerPosition.y + WEATHER_TOP_OFFSET +
        WeatherParticleRandom() * 8.0f;
    float driftScale = 0.4f + fieldSample.wind * 2.4f;
    float driftX = (WeatherParticleRandom() - 0.5f) * driftScale +
                   cosf(particleWindAngle) * fieldSample.wind * 1.8f;
    float driftZ = (WeatherParticleRandom() - 0.5f) * driftScale +
                   sinf(particleWindAngle) * fieldSample.wind * 1.8f;
    ParticlesEmitOne((Vector3){ x, y, z }, (Vector3){ driftX, -2.2f, driftZ },
                     (Color){ 235, 242, 248, 235 },
                     (Vector3){ 0.07f, 0.07f, 0.07f }, 3.0f, 0.2f);
}

void WeatherUpdate(float dt, Vector3 playerPosition)
{
    if (manualTimer > 0.0f) {
        manualTimer = fmaxf(manualTimer - dt, 0.0f);
        fieldSample = WeatherManualSample(manualWeather);
    } else {
        WeatherFieldInput input = WeatherInputAt(playerPosition);
        particleWindAngle = input.prevailingWindAngle;
        fieldSample = WeatherFieldSampleAt(&input);
    }
    WeatherUpdateTypeAndAudio();

    rainEmissionAccumulator += fieldSample.rain * 92.0f * dt;
    snowEmissionAccumulator += fieldSample.snow * 42.0f * dt;
    while (rainEmissionAccumulator >= 1.0f) {
        rainEmissionAccumulator -= 1.0f;
        EmitRain(playerPosition);
    }
    while (snowEmissionAccumulator >= 1.0f) {
        snowEmissionAccumulator -= 1.0f;
        EmitSnow(playerPosition);
    }
}

void WeatherSuspend(void)
{
    rainEmissionAccumulator = 0.0f;
    snowEmissionAccumulator = 0.0f;
    if (rainAudioActive) {
        rainAudioActive = false;
        AudioSetRain(false);
    }
}
