#include "world/weather.h"

#include "core/game_effects.h"
#include "space/space_state.h"
#include "world/local_climate.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

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
static bool forcedActive = false;
static WeatherPhenomenon forcedPhenomenon = WEATHER_PHENOMENON_CLEAR;
static float forcedIntensity = 0.0f;
static unsigned forcedFramesRemaining = 0u;
static bool forcedCloudActive = false;
static WeatherCloudGenus forcedCloudGenus = WEATHER_CLOUD_GENUS_NONE;
static float forcedCloudCoverage = 0.0f;
static unsigned forcedCloudFramesRemaining = 0u;
static float rainEmissionAccumulator = 0.0f;
static float snowEmissionAccumulator = 0.0f;
static bool rainAudioActive = false;
static uint32_t particleRandomState = 0x91e10da5u;
static float particleWindAngle = 0.0f;
static float particleScale = 1.0f;
static bool particlesSheltered = false;
static float weatherDaylight = 0.5f;
static WEATHER_THREAD_LOCAL WeatherSampleCacheEntry
    weatherSampleCache[WEATHER_SAMPLE_CACHE_SIZE] = { 0 };

static float WeatherClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float WeatherForcedDewPoint(float temperatureK, float humidity)
{
    float temperatureC = fminf(fmaxf(temperatureK - 273.15f, -80.0f), 60.0f);
    float safeHumidity = fminf(fmaxf(humidity, 0.001f), 1.0f);
    const float a = 17.625f;
    const float b = 243.04f;
    float gamma = logf(safeHumidity) +
                  a * temperatureC / (b + temperatureC);
    float dewPointC = b * gamma / (a - gamma);
    return fminf(fmaxf(dewPointC + 273.15f, 150.0f), temperatureK);
}

static float WeatherForcedWetBulb(float temperatureK, float humidity)
{
    float temperatureC = fminf(fmaxf(temperatureK - 273.15f, -80.0f), 60.0f);
    float relativePercent = WeatherClamp(humidity) * 100.0f;
    float wetBulbC = temperatureC *
        atanf(0.151977f * sqrtf(relativePercent + 8.313659f)) +
        atanf(temperatureC + relativePercent) -
        atanf(relativePercent - 1.676331f) +
        0.00391838f * powf(relativePercent, 1.5f) *
        atanf(0.023101f * relativePercent) - 4.686035f;
    if (!isfinite(wetBulbC)) wetBulbC = temperatureC;
    return fminf(fmaxf(wetBulbC + 273.15f, 150.0f), temperatureK);
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
           cached->input.prevailingWindAngle == input->prevailingWindAngle &&
           cached->input.surfacePressureAtm == input->surfacePressureAtm &&
           cached->input.atmosphereDensity == input->atmosphereDensity &&
           cached->input.relativeHumidity == input->relativeHumidity &&
           cached->input.dewPointK == input->dewPointK &&
           cached->input.wetBulbK == input->wetBulbK &&
           cached->input.instability == input->instability &&
           cached->input.orographicLift == input->orographicLift &&
           cached->input.aridity == input->aridity &&
           cached->input.dustAvailability == input->dustAvailability &&
           cached->input.latitude == input->latitude &&
           cached->input.magneticLatitude == input->magneticLatitude &&
           cached->input.magneticFieldStrength == input->magneticFieldStrength &&
           cached->input.daylight == input->daylight &&
           cached->input.solarElevation == input->solarElevation &&
           cached->input.atmosphereActive == input->atmosphereActive &&
           cached->input.supportsWaterCycle == input->supportsWaterCycle;
}

static bool WeatherInputAt(Vector3 playerPosition, double simulationTime,
                           WeatherFieldInput *outInput,
                           LocalClimateState *outClimate)
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
    LocalClimateInput climateInput = { 0 };
    LocalClimateState climate = { 0 };
    if (PlanetWorldIsActive()) {
        const PlanetProfile *planet = PlanetWorldProfile();
        if (!planet) return false;
        PlanetSurfaceSample surface = PlanetSurfaceAtTime(
            x, z, input.simulationTime);
        PlanetLightState light = { 0 };
        int height = PlanetTerrainHeight(x, z);
        float longitude = 0.0f;
        float latitude = 0.0f;
        PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
        (void)longitude;
        input.seed = PlanetWorldSeed();
        input.worldX = playerPosition.x;
        input.worldZ = playerPosition.z;
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
        float moisture = WeatherClamp(
            surface.moisture * (1.0f - surface.iceCoverage * 0.42f) +
            planet->seasonalHumidityBias * (0.18f - daylight * 0.12f));
        float cloudPotential = WeatherClamp(
            planet->cloudCoverage * (0.92f + eclipse * 0.10f) +
            (1.0f - daylight) * planet->seasonalHumidityBias * 0.08f);
        float meanTemperature = surface.meanTemperature > 0.0f ?
            surface.meanTemperature : surface.temperature;
        climateInput = (LocalClimateInput){
            .meanTemperatureK = meanTemperature,
            .seasonalTemperatureDeltaK = surface.temperature - meanTemperature,
            .daylightTemperatureDeltaK = daylightDeltaK,
            .surfacePressureAtm = planet->surfacePressureAtm,
            .atmosphereDensity = planet->atmosphereDensity,
            .moisture = moisture,
            .cloudPotential = cloudPotential,
            .windStrength = planet->windStrength,
            .latitude = latitude,
            .elevation = fmaxf((float)height - 12.0f, 0.0f),
            .waterCoverage = planet->oceanCoverage,
            .iceCoverage = fmaxf(surface.iceCoverage, planet->iceCoverage),
            .seasonalAmplitudeK = surface.seasonalAmplitude > 0.0f ?
                surface.seasonalAmplitude : planet->seasonalTemperatureAmplitudeK,
            .hasAtmosphere = planet->atmosphereType != PLANET_ATMOSPHERE_NONE,
            .supportsWaterCycle = planet->oceanCoverage > 0.01f ||
                planet->atmosphereType == PLANET_ATMOSPHERE_BREATHABLE
        };
        if (!LocalClimateEvaluate(&climateInput, &climate)) return false;
        input.temperatureK = climate.temperatureK;
        input.moisture = moisture;
        input.cloudPotential = climate.cloudPotential;
        input.windStrength = climate.windStrength;
        input.prevailingWindAngle = planet->prevailingWindAngle;
        input.dustAvailability = WeatherClamp(
            (1.0f - moisture) *
            (planet->style == SOLAR_STYLE_DESERT ||
             planet->style == SOLAR_STYLE_CRATER ? 1.0f : 0.58f));
        input.magneticFieldStrength = WeatherClamp(
            0.22f + fabsf(planet->rotationRate) * 0.04f +
            planet->atmosphereDensity * 0.18f);
    } else {
        Biome biome = BiomeAt(x, z);
        int height = WorldSurfaceHeightAt(x, z);
        float longitude = 0.0f;
        float latitude = 0.0f;
        HomeSurfaceLatLonAt(x, z, &longitude, &latitude);
        (void)longitude;
        float meanTemperature = 288.0f;
        float moisture = 0.55f;
        float cloudPotential = 0.46f;
        float windStrength = 0.38f;
        switch (biome) {
        case BIOME_FOREST:
            meanTemperature = 285.0f;
            moisture = 0.78f;
            cloudPotential = 0.58f;
            break;
        case BIOME_DESERT:
            meanTemperature = 306.0f;
            moisture = 0.10f;
            cloudPotential = 0.16f;
            windStrength = 0.52f;
            break;
        case BIOME_SNOW:
            meanTemperature = 263.0f;
            moisture = 0.48f;
            cloudPotential = 0.52f;
            break;
        case BIOME_MOUNTAIN:
            meanTemperature = 275.0f;
            moisture = 0.42f;
            cloudPotential = 0.50f;
            windStrength = 0.72f;
            break;
        case BIOME_SWAMP:
            meanTemperature = 286.0f;
            moisture = 0.94f;
            cloudPotential = 0.70f;
            windStrength = 0.24f;
            break;
        case BIOME_PLAINS:
        default:
            break;
        }
        climateInput = (LocalClimateInput){
            .meanTemperatureK = meanTemperature,
            .daylightTemperatureDeltaK = (weatherDaylight - 0.5f) * 5.0f,
            .surfacePressureAtm = 1.0f,
            .atmosphereDensity = 0.72f,
            .moisture = moisture,
            .cloudPotential = cloudPotential,
            .windStrength = windStrength,
            .latitude = latitude,
            .elevation = fmaxf((float)height - 12.0f, 0.0f),
            .waterCoverage = biome == BIOME_SWAMP ? 0.72f :
                (height <= HOME_SEA_LEVEL ? 0.85f : 0.34f),
            .iceCoverage = biome == BIOME_SNOW ? 0.68f : 0.04f,
            .seasonalAmplitudeK = 8.0f + fabsf(latitude) * 12.0f,
            .hasAtmosphere = true,
            .supportsWaterCycle = true
        };
        if (!LocalClimateEvaluate(&climateInput, &climate)) return false;
        input.seed = WorldGetSeed();
        input.worldX = playerPosition.x;
        input.worldZ = playerPosition.z;
        input.temperatureK = climate.temperatureK;
        input.moisture = moisture;
        input.cloudPotential = climate.cloudPotential;
        input.windStrength = climate.windStrength;
        input.prevailingWindAngle =
            (float)(input.seed & 0xffffu) / 65535.0f * 2.0f * PI;
        input.dustAvailability = WeatherClamp(
            (1.0f - moisture) * (biome == BIOME_DESERT ? 1.0f : 0.52f));
        input.magneticFieldStrength = 0.72f;
    }

    input.surfacePressureAtm = climate.pressureAtm;
    input.atmosphereDensity = climateInput.atmosphereDensity;
    input.relativeHumidity = climate.relativeHumidity;
    input.dewPointK = climate.dewPointK;
    input.wetBulbK = climate.wetBulbK;
    input.instability = climate.instability;
    input.orographicLift = climate.orographicLift;
    input.aridity = climate.aridity;
    input.latitude = climateInput.latitude;
    input.magneticLatitude = climateInput.latitude;
    input.daylight = weatherDaylight;
    input.solarElevation = WeatherClamp(weatherDaylight);
    input.atmosphereActive = climate.atmosphereActive;
    input.supportsWaterCycle = climate.waterCycleActive;
    *outInput = input;
    if (outClimate) *outClimate = climate;
    return true;
}

static WeatherFieldSample WeatherPhenomenonSample(
    WeatherPhenomenon phenomenon, float intensity)
{
    float strength = WeatherClamp(intensity);
    WeatherFieldSample sample = {
        .phenomena = WEATHER_PHENOMENON_FLAG(phenomenon),
        .dominantPhenomenon = phenomenon,
        .temperatureK = 288.15f,
        .pressureAtm = 1.0f,
        .relativeHumidity = 0.42f + strength * 0.12f,
        .dewPointK = 277.0f + strength * 4.0f,
        .wetBulbK = 282.0f + strength * 2.0f,
        .cloudCover = 0.06f,
        .cloudBaseHeight = 18.0f,
        .wind = 0.12f + strength * 0.06f,
        .gust = 0.16f + strength * 0.08f,
        .windAngle = 0.7853982f,
        .visibility = 1.0f
    };

    switch (phenomenon) {
    case WEATHER_PHENOMENON_CLEAR:
        break;
    case WEATHER_PHENOMENON_CLOUDY:
        sample.cloudCover = 0.55f + strength * 0.40f;
        sample.relativeHumidity = 0.58f + strength * 0.28f;
        break;
    case WEATHER_PHENOMENON_FOG:
        sample.cloudCover = 0.42f + strength * 0.30f;
        sample.fog = 0.35f + strength * 0.65f;
        sample.relativeHumidity = 0.90f + strength * 0.10f;
        sample.dewPointK = sample.temperatureK - (1.0f - strength) * 1.8f;
        sample.visibility = 1.0f - strength * 0.92f;
        break;
    case WEATHER_PHENOMENON_FROST:
        sample.temperatureAnomalyK = -4.0f - strength * 8.0f;
        sample.temperatureK += sample.temperatureAnomalyK;
        sample.dewPointK = sample.temperatureK - 0.8f;
        sample.wetBulbK = sample.temperatureK - 0.4f;
        sample.relativeHumidity = 0.82f + strength * 0.16f;
        sample.frost = strength;
        break;
    case WEATHER_PHENOMENON_DRIZZLE:
        sample.cloudCover = 0.68f + strength * 0.24f;
        sample.precipitation = strength * 0.55f;
        sample.drizzle = strength;
        sample.rain = strength * 0.42f;
        sample.relativeHumidity = 0.82f + strength * 0.16f;
        sample.visibility = 1.0f - strength * 0.24f;
        break;
    case WEATHER_PHENOMENON_SHOWERS:
        sample.cloudCover = 0.62f + strength * 0.30f;
        sample.precipitation = strength * 0.78f;
        sample.rain = strength * 0.82f;
        sample.storm = strength * 0.16f;
        sample.gust = 0.30f + strength * 0.42f;
        sample.visibility = 1.0f - strength * 0.38f;
        break;
    case WEATHER_PHENOMENON_HEAVY_RAIN:
        sample.cloudCover = 0.82f + strength * 0.18f;
        sample.precipitation = strength;
        sample.rain = strength;
        sample.storm = strength * 0.38f;
        sample.wind = 0.30f + strength * 0.42f;
        sample.gust = 0.42f + strength * 0.48f;
        sample.visibility = 1.0f - strength * 0.70f;
        break;
    case WEATHER_PHENOMENON_THUNDERSTORM:
        sample.cloudCover = 0.86f + strength * 0.14f;
        sample.precipitation = strength * 0.92f;
        sample.rain = strength * 0.92f;
        sample.instability = 0.55f + strength * 0.45f;
        sample.storm = strength;
        sample.lightning = strength * 0.78f;
        sample.wind = 0.40f + strength * 0.50f;
        sample.gust = 0.55f + strength * 0.45f;
        sample.pressureAnomaly = -0.035f * strength;
        sample.pressureAtm += sample.pressureAnomaly;
        sample.visibility = 1.0f - strength * 0.74f;
        break;
    case WEATHER_PHENOMENON_LIGHTNING:
        sample.cloudCover = 0.78f + strength * 0.20f;
        sample.precipitation = strength * 0.38f;
        sample.rain = strength * 0.38f;
        sample.instability = 0.62f + strength * 0.38f;
        sample.storm = strength * 0.82f;
        sample.lightning = strength;
        sample.wind = 0.34f + strength * 0.38f;
        sample.gust = 0.48f + strength * 0.46f;
        sample.visibility = 1.0f - strength * 0.40f;
        break;
    case WEATHER_PHENOMENON_SLEET:
        sample.temperatureK = 273.65f;
        sample.temperatureAnomalyK = -5.0f;
        sample.cloudCover = 0.76f + strength * 0.22f;
        sample.precipitation = strength * 0.82f;
        sample.sleet = strength;
        sample.rain = strength * 0.20f;
        sample.snow = strength * 0.30f;
        sample.wind = 0.30f + strength * 0.38f;
        sample.gust = 0.42f + strength * 0.42f;
        sample.visibility = 1.0f - strength * 0.58f;
        break;
    case WEATHER_PHENOMENON_FREEZING_RAIN:
        sample.temperatureK = 271.65f;
        sample.temperatureAnomalyK = -7.0f;
        sample.cloudCover = 0.80f + strength * 0.18f;
        sample.precipitation = strength * 0.84f;
        sample.freezingRain = strength;
        sample.rain = strength * 0.36f;
        sample.frost = strength * 0.25f;
        sample.visibility = 1.0f - strength * 0.52f;
        break;
    case WEATHER_PHENOMENON_HAIL:
        sample.temperatureK = 277.15f;
        sample.temperatureAnomalyK = -4.0f;
        sample.cloudCover = 0.82f + strength * 0.18f;
        sample.precipitation = strength * 0.80f;
        sample.hail = strength;
        sample.rain = strength * 0.34f;
        sample.instability = 0.58f + strength * 0.40f;
        sample.storm = strength * 0.72f;
        sample.wind = 0.38f + strength * 0.44f;
        sample.gust = 0.54f + strength * 0.46f;
        sample.visibility = 1.0f - strength * 0.62f;
        break;
    case WEATHER_PHENOMENON_SNOW:
        sample.temperatureK = 267.15f;
        sample.temperatureAnomalyK = -12.0f;
        sample.cloudCover = 0.74f + strength * 0.24f;
        sample.precipitation = strength * 0.78f;
        sample.snow = strength;
        sample.wind = 0.22f + strength * 0.32f;
        sample.gust = 0.32f + strength * 0.36f;
        sample.visibility = 1.0f - strength * 0.56f;
        break;
    case WEATHER_PHENOMENON_BLIZZARD:
        sample.temperatureK = 258.15f;
        sample.temperatureAnomalyK = -21.0f;
        sample.cloudCover = 0.88f + strength * 0.12f;
        sample.precipitation = strength;
        sample.snow = strength;
        sample.storm = strength * 0.72f;
        sample.wind = 0.62f + strength * 0.38f;
        sample.gust = 0.75f + strength * 0.25f;
        sample.coldSnap = strength * 0.72f;
        sample.visibility = 1.0f - strength * 0.92f;
        break;
    case WEATHER_PHENOMENON_STRONG_WIND:
        sample.cloudCover = 0.20f + strength * 0.34f;
        sample.wind = 0.55f + strength * 0.45f;
        sample.gust = 0.68f + strength * 0.32f;
        sample.pressureAnomaly = -0.018f * strength;
        sample.pressureAtm += sample.pressureAnomaly;
        sample.visibility = 1.0f - strength * 0.12f;
        break;
    case WEATHER_PHENOMENON_DUST_STORM:
        sample.cloudCover = 0.20f + strength * 0.30f;
        sample.dust = strength;
        sample.wind = 0.60f + strength * 0.40f;
        sample.gust = 0.74f + strength * 0.26f;
        sample.relativeHumidity = 0.12f;
        sample.visibility = 1.0f - strength * 0.94f;
        break;
    case WEATHER_PHENOMENON_HEAT_WAVE:
        sample.temperatureAnomalyK = 5.0f + strength * 11.0f;
        sample.temperatureK += sample.temperatureAnomalyK;
        sample.heatWave = strength;
        sample.relativeHumidity = 0.24f;
        sample.cloudCover = 0.04f + strength * 0.10f;
        sample.pressureAnomaly = 0.020f * strength;
        sample.pressureAtm += sample.pressureAnomaly;
        sample.visibility = 0.94f;
        break;
    case WEATHER_PHENOMENON_COLD_SNAP:
        sample.temperatureAnomalyK = -8.0f - strength * 16.0f;
        sample.temperatureK += sample.temperatureAnomalyK;
        sample.coldSnap = strength;
        sample.frost = strength * 0.58f;
        sample.pressureAnomaly = 0.024f * strength;
        sample.pressureAtm += sample.pressureAnomaly;
        sample.visibility = 0.96f;
        break;
    case WEATHER_PHENOMENON_RAINBOW:
        sample.cloudCover = 0.28f + strength * 0.24f;
        sample.precipitation = strength * 0.18f;
        sample.rain = strength * 0.16f;
        sample.rainbow = strength;
        sample.visibility = 0.92f;
        break;
    case WEATHER_PHENOMENON_AURORA:
        sample.aurora = strength;
        sample.cloudCover = 0.02f + (1.0f - strength) * 0.12f;
        sample.visibility = 1.0f;
        break;
    case WEATHER_PHENOMENON_COUNT:
        break;
    }
    if (sample.precipitation > 0.0f) {
        sample.relativeHumidity = fmaxf(
            sample.relativeHumidity, 0.72f + sample.precipitation * 0.25f);
    }
    sample.relativeHumidity = WeatherClamp(sample.relativeHumidity);
    sample.dewPointK = WeatherForcedDewPoint(
        sample.temperatureK, sample.relativeHumidity);
    sample.wetBulbK = WeatherForcedWetBulb(
        sample.temperatureK, sample.relativeHumidity);
    sample.cloudCover = WeatherClamp(sample.cloudCover);
    sample.precipitation = WeatherClamp(sample.precipitation);
    sample.wind = WeatherClamp(sample.wind);
    sample.gust = WeatherClamp(fmaxf(sample.gust, sample.wind));
    sample.visibility = WeatherClamp(sample.visibility);
    WeatherCloudGenus genus = WEATHER_CLOUD_GENUS_NONE;
    switch (phenomenon) {
    case WEATHER_PHENOMENON_CLOUDY:
        genus = WEATHER_CLOUD_GENUS_STRATOCUMULUS;
        break;
    case WEATHER_PHENOMENON_FOG:
    case WEATHER_PHENOMENON_DRIZZLE:
        genus = WEATHER_CLOUD_GENUS_STRATUS;
        break;
    case WEATHER_PHENOMENON_SHOWERS:
    case WEATHER_PHENOMENON_RAINBOW:
        genus = WEATHER_CLOUD_GENUS_CUMULUS;
        break;
    case WEATHER_PHENOMENON_HEAVY_RAIN:
    case WEATHER_PHENOMENON_SLEET:
    case WEATHER_PHENOMENON_FREEZING_RAIN:
    case WEATHER_PHENOMENON_SNOW:
    case WEATHER_PHENOMENON_BLIZZARD:
        genus = WEATHER_CLOUD_GENUS_NIMBOSTRATUS;
        break;
    case WEATHER_PHENOMENON_THUNDERSTORM:
    case WEATHER_PHENOMENON_LIGHTNING:
    case WEATHER_PHENOMENON_HAIL:
        genus = WEATHER_CLOUD_GENUS_CUMULONIMBUS;
        break;
    case WEATHER_PHENOMENON_STRONG_WIND:
        genus = WEATHER_CLOUD_GENUS_CIRRUS;
        break;
    case WEATHER_PHENOMENON_CLEAR:
    case WEATHER_PHENOMENON_FROST:
    case WEATHER_PHENOMENON_DUST_STORM:
    case WEATHER_PHENOMENON_HEAT_WAVE:
    case WEATHER_PHENOMENON_COLD_SNAP:
    case WEATHER_PHENOMENON_AURORA:
    case WEATHER_PHENOMENON_COUNT:
        break;
    }
    if (genus != WEATHER_CLOUD_GENUS_NONE && sample.cloudCover >= 0.07f) {
        WeatherFieldSampleForceCloudGenus(&sample, genus, sample.cloudCover);
    }
    return sample;
}

static WeatherFieldSample WeatherApplyForcedCloud(WeatherFieldSample sample)
{
    if (forcedCloudActive) {
        WeatherFieldSampleForceCloudGenus(
            &sample, forcedCloudGenus, forcedCloudCoverage);
    }
    return sample;
}

static WeatherFieldSample WeatherManualSample(Weather weather)
{
    if (weather == WEATHER_RAIN) {
        return WeatherPhenomenonSample(WEATHER_PHENOMENON_HEAVY_RAIN, 0.86f);
    }
    if (weather == WEATHER_SNOW) {
        return WeatherPhenomenonSample(WEATHER_PHENOMENON_SNOW, 0.72f);
    }
    return WeatherPhenomenonSample(WEATHER_PHENOMENON_CLEAR, 0.0f);
}

WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z)
{
    return WeatherFieldSampleAtWorldTime(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

WeatherFieldSample WeatherFieldSampleAtWorldTime(
    int x, int z, double simulationTime)
{
    if (forcedActive) {
        return WeatherApplyForcedCloud(
            WeatherPhenomenonSample(forcedPhenomenon, forcedIntensity));
    }
    if (manualTimer > 0.0f) {
        return WeatherApplyForcedCloud(WeatherManualSample(manualWeather));
    }
    if (!isfinite(simulationTime) || simulationTime < 0.0) {
        return (WeatherFieldSample){ 0 };
    }

    WeatherFieldInput input;
    if (!WeatherInputAt((Vector3){
            (float)x + 0.5f, 0.0f, (float)z + 0.5f
        }, simulationTime, &input, NULL)) {
        return (WeatherFieldSample){ 0 };
    }
    WeatherSampleCacheEntry *cached =
        &weatherSampleCache[WeatherSampleCacheIndex(&input)];
    if (WeatherSampleCacheMatches(cached, &input)) {
        return WeatherApplyForcedCloud(cached->sample);
    }

    WeatherFieldSample sample = WeatherFieldSampleAt(&input);
    *cached = (WeatherSampleCacheEntry){
        .valid = true,
        .input = input,
        .sample = sample
    };
    return WeatherApplyForcedCloud(sample);
}

bool WeatherLocalClimateAtWorldTime(int x, int z, double simulationTime,
                                    LocalClimateState *outClimate)
{
    if (!outClimate || !isfinite(simulationTime) || simulationTime < 0.0) {
        return false;
    }
    WeatherFieldInput input;
    return WeatherInputAt((Vector3){
        (float)x + 0.5f, 0.0f, (float)z + 0.5f
    }, simulationTime, &input, outClimate);
}

float WeatherWindAngleAtWorld(int x, int z)
{
    return WeatherWindAngleAtWorldTime(
        x, z, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

float WeatherWindAngleAtWorldTime(int x, int z, double simulationTime)
{
    if (forcedActive) {
        return WeatherPhenomenonSample(
            forcedPhenomenon, forcedIntensity).windAngle;
    }
    if (!isfinite(simulationTime) || simulationTime < 0.0) return 0.0f;
    WeatherFieldInput input;
    if (!WeatherInputAt((Vector3){
            (float)x + 0.5f, 0.0f, (float)z + 0.5f
        }, simulationTime, &input, NULL)) {
        return 0.0f;
    }
    return WeatherFieldSampleAt(&input).windAngle;
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
    else if (fieldSample.snow + fieldSample.sleet >
             fieldSample.rain + fieldSample.freezingRain) {
        current = WEATHER_SNOW;
    }
    else current = WEATHER_RAIN;

    bool shouldPlayRain = fieldSample.rain >
        (rainAudioActive ? 0.035f : 0.075f);
    if (shouldPlayRain != rainAudioActive) {
        rainAudioActive = shouldPlayRain;
        GameEffectsSetRain(rainAudioActive);
    }
}

void WeatherInit(void)
{
    current = WEATHER_CLEAR;
    fieldSample = (WeatherFieldSample){ 0 };
    manualTimer = 0.0f;
    manualWeather = WEATHER_CLEAR;
    forcedActive = false;
    forcedPhenomenon = WEATHER_PHENOMENON_CLEAR;
    forcedIntensity = 0.0f;
    forcedFramesRemaining = 0u;
    forcedCloudActive = false;
    forcedCloudGenus = WEATHER_CLOUD_GENUS_NONE;
    forcedCloudCoverage = 0.0f;
    forcedCloudFramesRemaining = 0u;
    rainEmissionAccumulator = 0.0f;
    snowEmissionAccumulator = 0.0f;
    rainAudioActive = false;
    particleRandomState = 0x91e10da5u;
    particleWindAngle = 0.0f;
    weatherDaylight = 0.5f;
    memset(weatherSampleCache, 0, sizeof(weatherSampleCache));
    GameEffectsSetRain(false);
}

const char *WeatherName(void)
{
    return WeatherPhenomenonName(fieldSample.dominantPhenomenon);
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

WeatherFieldSample WeatherCurrentSample(void)
{
    return fieldSample;
}

bool WeatherForcePhenomenon(WeatherPhenomenon phenomenon, float intensity,
                            unsigned frames)
{
    if ((unsigned)phenomenon >= WEATHER_PHENOMENON_COUNT ||
        !isfinite(intensity) || intensity < 0.0f || intensity > 1.0f ||
        frames == 0u) {
        return false;
    }
    forcedActive = true;
    forcedPhenomenon = phenomenon;
    forcedIntensity = intensity;
    forcedFramesRemaining = frames;
    fieldSample = WeatherApplyForcedCloud(
        WeatherPhenomenonSample(phenomenon, intensity));
    particleWindAngle = fieldSample.windAngle;
    WeatherUpdateTypeAndAudio();
    return true;
}

bool WeatherForceCloudGenus(WeatherCloudGenus genus, float coverage,
                            unsigned frames)
{
    if (genus <= WEATHER_CLOUD_GENUS_NONE ||
        genus >= WEATHER_CLOUD_GENUS_COUNT || !isfinite(coverage) ||
        coverage < 0.0f || coverage > 1.0f || frames == 0u) {
        return false;
    }
    forcedCloudActive = true;
    forcedCloudGenus = genus;
    forcedCloudCoverage = coverage;
    forcedCloudFramesRemaining = frames;
    fieldSample = WeatherApplyForcedCloud(fieldSample);
    return true;
}

void WeatherClearForcedCloud(void)
{
    forcedCloudActive = false;
    forcedCloudGenus = WEATHER_CLOUD_GENUS_NONE;
    forcedCloudCoverage = 0.0f;
    forcedCloudFramesRemaining = 0u;
}

void WeatherClearForced(void)
{
    forcedActive = false;
    forcedPhenomenon = WEATHER_PHENOMENON_CLEAR;
    forcedIntensity = 0.0f;
    forcedFramesRemaining = 0u;
    WeatherClearForcedCloud();
}

unsigned WeatherForcedFramesRemaining(void)
{
    return forcedFramesRemaining;
}

unsigned WeatherForcedCloudFramesRemaining(void)
{
    return forcedCloudFramesRemaining;
}

void WeatherCycle(void)
{
    WeatherClearForced();
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
    GameEffectsEmitParticleOne(
        (Vector3){ x, y, z },
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
    GameEffectsEmitParticleOne(
        (Vector3){ x, y, z }, (Vector3){ driftX, -2.2f, driftZ },
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

    if (forcedActive && forcedFramesRemaining == 0u) {
        forcedActive = false;
    }
    if (forcedCloudActive && forcedCloudFramesRemaining == 0u) {
        forcedCloudActive = false;
    }
    if (forcedActive) {
        fieldSample = WeatherPhenomenonSample(
            forcedPhenomenon, forcedIntensity);
        forcedFramesRemaining--;
    } else if (manualTimer > 0.0f) {
        manualTimer = fmaxf(manualTimer - updateDt, 0.0f);
        fieldSample = WeatherManualSample(manualWeather);
    } else {
        WeatherFieldInput input;
        if (WeatherInputAt(
                playerPosition,
                SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
                &input, NULL)) {
            fieldSample = WeatherFieldSampleAt(&input);
            particleWindAngle = isfinite(fieldSample.windAngle) ?
                fieldSample.windAngle : 0.0f;
        } else {
            particleWindAngle = 0.0f;
            fieldSample = (WeatherFieldSample){ 0 };
        }
    }
    if (forcedCloudActive) {
        fieldSample = WeatherApplyForcedCloud(fieldSample);
        forcedCloudFramesRemaining--;
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

void WeatherSetDaylight(float daylight)
{
    if (!isfinite(daylight)) return;
    weatherDaylight = WeatherClamp(daylight);
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
        GameEffectsSetRain(false);
    }
}
