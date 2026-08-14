#include "weather.h"

#include "audio.h"
#include "particles.h"
#include "space.h"
#include "terrain.h"
#include "world.h"
#include "world_environment.h"

#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define WEATHER_AREA_RADIUS 26.0f
#define WEATHER_TOP_OFFSET 16.0f
#define WEATHER_MANUAL_SECONDS 45.0f
#define WEATHER_SAMPLE_CACHE_SIZE 128u
#define WEATHER_MAX_UPDATE_DT 0.25f
#define WEATHER_MAX_RAIN_EMISSIONS 24u
#define WEATHER_MAX_SNOW_EMISSIONS 12u

#if defined(__GNUC__) || defined(__clang__)
#define WEATHER_THREAD_LOCAL __thread
#else
#define WEATHER_THREAD_LOCAL
#endif

typedef struct WeatherSampleCacheEntry {
    bool valid;
    WeatherFieldInput input;
    WeatherFieldSample sample;
} WeatherSampleCacheEntry;

static Weather current = WEATHER_CLEAR;
static WeatherFieldSample fieldSample = { 0 };
static float manualTimer = 0.0f;
static Weather manualWeather = WEATHER_CLEAR;
static float rainEmissionAccumulator = 0.0f;
static float snowEmissionAccumulator = 0.0f;
static bool rainAudioActive = false;
static uint32_t particleRandomState = 0x91e10da5u;
static float particleWindAngle = 0.0f;
static float particleScale = 1.0f;
static bool particlesSheltered = false;
static WEATHER_THREAD_LOCAL WeatherSampleCacheEntry
    weatherSampleCache[WEATHER_SAMPLE_CACHE_SIZE] = { 0 };

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

static uint32_t WeatherCacheMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static unsigned WeatherSampleCacheIndex(const WeatherFieldInput *input)
{
    uint64_t timeBits;
    uint32_t worldXBits;
    uint32_t worldZBits;
    memcpy(&timeBits, &input->simulationTime, sizeof(timeBits));
    memcpy(&worldXBits, &input->worldX, sizeof(worldXBits));
    memcpy(&worldZBits, &input->worldZ, sizeof(worldZBits));
    uint32_t hash = input->seed ^ (uint32_t)timeBits ^
                    (uint32_t)(timeBits >> 32);
    hash ^= worldXBits * 0x9e3779b9u;
    hash ^= worldZBits * 0x85ebca6bu;
    return WeatherCacheMix(hash) & (WEATHER_SAMPLE_CACHE_SIZE - 1u);
}

static bool WeatherSampleCacheMatches(const WeatherSampleCacheEntry *cached,
                                      const WeatherFieldInput *input)
{
    return cached->valid &&
           cached->input.seed == input->seed &&
           cached->input.simulationTime == input->simulationTime &&
           cached->input.worldX == input->worldX &&
           cached->input.worldZ == input->worldZ &&
           cached->input.temperatureK == input->temperatureK &&
           cached->input.moisture == input->moisture &&
           cached->input.cloudPotential == input->cloudPotential &&
           cached->input.windStrength == input->windStrength &&
           cached->input.prevailingWindAngle == input->prevailingWindAngle;
}

static bool WeatherInputAt(Vector3 playerPosition, double simulationTime,
                           WeatherFieldInput *outInput)
{
    WeatherFieldInput input = { 0 };
    if (!outInput || !isfinite(simulationTime) || simulationTime < 0.0 ||
        !isfinite(playerPosition.x) || !isfinite(playerPosition.z)) {
        return false;
    }

    double cellX = floor((double)playerPosition.x);
    double cellZ = floor((double)playerPosition.z);
    if (cellX < (double)INT_MIN || cellX > (double)INT_MAX ||
        cellZ < (double)INT_MIN || cellZ > (double)INT_MAX) {
        return false;
    }
    int x = (int)cellX;
    int z = (int)cellZ;
    input.simulationTime = simulationTime;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *planet = PlanetWorldProfile();
        if (!planet) return false;
        PlanetSurfaceSample surface = PlanetSurfaceAtTime(
            x, z, input.simulationTime);
        PlanetLightState light = { 0 };
        int height = PlanetTerrainHeight(x, z);
        input.seed = PlanetWorldSeed();
        input.worldX = (float)PlanetWorldOriginX() + playerPosition.x;
        input.worldZ = (float)PlanetWorldOriginZ() + playerPosition.z;
        float daylight = 0.5f;
        float eclipse = 0.0f;
        if (PlanetWorldLightStateAtTime(
                playerPosition, simulationTime, &light)) {
            daylight = light.daylight;
            eclipse = light.eclipse;
        }
        float daylightDeltaK = (daylight - 0.5f) *
            (2.0f + surface.seasonalAmplitude * 0.08f) *
            (0.28f + (1.0f - planet->atmosphereDensity) * 0.72f);
        input.temperatureK = surface.temperature + daylightDeltaK -
            fmaxf((float)height - 12.0f, 0.0f) * 0.68f;
        input.moisture = WeatherClamp(
            surface.moisture * (1.0f - surface.iceCoverage * 0.42f) +
            planet->seasonalHumidityBias * (0.18f - daylight * 0.12f));
        input.cloudPotential = WeatherClamp(
            planet->cloudCoverage * (0.92f + eclipse * 0.10f) +
            (1.0f - daylight) * planet->seasonalHumidityBias * 0.08f);
        input.windStrength = planet->windStrength;
        input.prevailingWindAngle = planet->prevailingWindAngle;
        *outInput = input;
        return true;
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
    *outInput = input;
    return true;
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

WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z)
{
    return WeatherFieldSampleAtWorldTime(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

WeatherFieldSample WeatherFieldSampleAtWorldTime(
    int x, int z, double simulationTime)
{
    if (manualTimer > 0.0f) return WeatherManualSample(manualWeather);
    if (!isfinite(simulationTime) || simulationTime < 0.0) {
        return (WeatherFieldSample){ 0 };
    }

    WeatherFieldInput input;
    if (!WeatherInputAt((Vector3){
            (float)x + 0.5f, 0.0f, (float)z + 0.5f
        }, simulationTime, &input)) {
        return (WeatherFieldSample){ 0 };
    }
    WeatherSampleCacheEntry *cached =
        &weatherSampleCache[WeatherSampleCacheIndex(&input)];
    if (WeatherSampleCacheMatches(cached, &input)) {
        return cached->sample;
    }

    WeatherFieldSample sample = WeatherFieldSampleAt(&input);
    *cached = (WeatherSampleCacheEntry){
        .valid = true,
        .input = input,
        .sample = sample
    };
    return sample;
}

float WeatherWindAngleAtWorld(int x, int z)
{
    return WeatherWindAngleAtWorldTime(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

float WeatherWindAngleAtWorldTime(int x, int z, double simulationTime)
{
    if (!isfinite(simulationTime) || simulationTime < 0.0) return 0.0f;
    WeatherFieldInput input;
    if (!WeatherInputAt((Vector3){
            (float)x + 0.5f, 0.0f, (float)z + 0.5f
        }, simulationTime, &input)) {
        return 0.0f;
    }
    return input.prevailingWindAngle;
}

WeatherVisualState WeatherVisualStateAtWorld(
    Vector3 position, double simulationTime, float daylight)
{
    WeatherVisualState clear = { .visibility = 1.0f };
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z) || !isfinite(simulationTime) ||
        simulationTime < 0.0 || !isfinite(daylight)) {
        return clear;
    }

    double cellX = floor((double)position.x);
    double cellZ = floor((double)position.z);
    if (cellX < (double)INT_MIN || cellX > (double)INT_MAX ||
        cellZ < (double)INT_MIN || cellZ > (double)INT_MAX) {
        return clear;
    }

    float atmosphereDensity = 0.0f;
    bool atmosphereActive = false;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        if (profile && profile->atmosphereType != PLANET_ATMOSPHERE_NONE) {
            atmosphereDensity = WeatherClamp(profile->atmosphereDensity) *
                (1.0f - PlanetWorldAtmosphereFade(position));
            atmosphereActive = atmosphereDensity > 0.01f;
        }
    } else if (HomeWorldSurfaceIsActive()) {
        atmosphereDensity = 0.62f * (1.0f - HomeWorldSpaceFade(position));
        atmosphereActive = atmosphereDensity > 0.01f;
    }
    if (!atmosphereActive) return clear;

    int x = (int)cellX;
    int z = (int)cellZ;
    WeatherVisualInput input = {
        .weather = WeatherFieldSampleAtWorldTime(x, z, simulationTime),
        .atmosphereDensity = atmosphereDensity,
        .daylight = daylight,
        .windAngle = WeatherWindAngleAtWorldTime(x, z, simulationTime),
        .atmosphereActive = true
    };
    return WeatherVisualStateEvaluate(&input);
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
    memset(weatherSampleCache, 0, sizeof(weatherSampleCache));
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
    return WeatherFieldSkyFactor(fieldSample);
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

static bool WeatherPositionIsFinite(Vector3 position)
{
    return isfinite(position.x) && isfinite(position.y) &&
           isfinite(position.z);
}

static unsigned WeatherAdvanceEmissionAccumulator(
    float *accumulator, float intensity, float rate, float dt,
    unsigned emissionLimit)
{
    if (!isfinite(*accumulator) || *accumulator < 0.0f) {
        *accumulator = 0.0f;
    }
    if (!isfinite(intensity) || intensity <= 0.0f) return 0u;

    *accumulator += WeatherClamp(intensity) * rate * dt;
    if (!isfinite(*accumulator) || *accumulator < 0.0f) {
        *accumulator = 0.0f;
        return 0u;
    }

    unsigned emissions = 0u;
    while (*accumulator >= 1.0f && emissions < emissionLimit) {
        *accumulator -= 1.0f;
        emissions++;
    }
    if (*accumulator >= 1.0f) {
        *accumulator = fmodf(*accumulator, 1.0f);
    }
    return emissions;
}

void WeatherUpdate(float dt, Vector3 playerPosition)
{
    if (!isfinite(dt) || dt <= 0.0f) return;
    float updateDt = fminf(dt, WEATHER_MAX_UPDATE_DT);

    if (manualTimer > 0.0f) {
        manualTimer = fmaxf(manualTimer - updateDt, 0.0f);
        fieldSample = WeatherManualSample(manualWeather);
    } else {
        WeatherFieldInput input;
        if (WeatherInputAt(
                playerPosition,
                SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
                &input)) {
            particleWindAngle = isfinite(input.prevailingWindAngle) ?
                input.prevailingWindAngle : 0.0f;
            fieldSample = WeatherFieldSampleAt(&input);
        } else {
            particleWindAngle = 0.0f;
            fieldSample = (WeatherFieldSample){ 0 };
        }
    }
    WeatherUpdateTypeAndAudio();

    bool canEmit = WeatherPositionIsFinite(playerPosition) && !particlesSheltered;
    unsigned rainCount = WeatherAdvanceEmissionAccumulator(
        &rainEmissionAccumulator, fieldSample.rain, 92.0f * particleScale, updateDt,
        canEmit ? WEATHER_MAX_RAIN_EMISSIONS : 0u);
    unsigned snowCount = WeatherAdvanceEmissionAccumulator(
        &snowEmissionAccumulator, fieldSample.snow, 42.0f * particleScale, updateDt,
        canEmit ? WEATHER_MAX_SNOW_EMISSIONS : 0u);
    for (unsigned index = 0; index < rainCount; index++) {
        EmitRain(playerPosition);
    }
    for (unsigned index = 0; index < snowCount; index++) {
        EmitSnow(playerPosition);
    }
}

void WeatherSetParticleScale(float scale)
{
    if (!isfinite(scale)) scale = 1.0f;
    if (scale < 0.20f) scale = 0.20f;
    if (scale > 1.50f) scale = 1.50f;
    particleScale = scale;
}

void WeatherSetSheltered(bool sheltered)
{
    particlesSheltered = sheltered;
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
