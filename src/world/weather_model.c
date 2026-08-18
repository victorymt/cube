#include "world/weather_model.h"

#include <ctype.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define WEATHER_FIELD_SPATIAL_SCALE 192.0
#define WEATHER_FIELD_TIME_SCALE 90.0

static float WeatherModelClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float WeatherModelSmooth(float value)
{
    value = WeatherModelClamp(value);
    return value * value * (3.0f - 2.0f * value);
}

static float WeatherModelSmoothStep(float low, float high, float value)
{
    if (!(high > low)) return value >= high ? 1.0f : 0.0f;
    return WeatherModelSmooth((value - low) / (high - low));
}

static float WeatherModelLerp(float start, float end, float amount)
{
    return start + (end - start) * amount;
}

static float WeatherModelDewPoint(float temperatureK, float humidity)
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

static float WeatherModelWetBulb(float temperatureK, float humidity)
{
    float temperatureC = fminf(fmaxf(temperatureK - 273.15f, -80.0f), 60.0f);
    float relativePercent = WeatherModelClamp(humidity) * 100.0f;
    float wetBulbC = temperatureC *
        atanf(0.151977f * sqrtf(relativePercent + 8.313659f)) +
        atanf(temperatureC + relativePercent) -
        atanf(relativePercent - 1.676331f) +
        0.00391838f * powf(relativePercent, 1.5f) *
        atanf(0.023101f * relativePercent) - 4.686035f;
    if (!isfinite(wetBulbC)) wetBulbC = temperatureC;
    return fminf(fmaxf(wetBulbC + 273.15f, 150.0f), temperatureK);
}

static uint32_t WeatherModelMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t WeatherModelFoldCoordinate(int64_t coordinate)
{
    uint64_t bits = (uint64_t)coordinate;
    return (uint32_t)bits ^ (uint32_t)(bits >> 32);
}

static float WeatherModelLattice(uint32_t seed, int64_t x, int64_t z,
                                 int64_t time, uint32_t lane)
{
    uint32_t hash = seed ^ lane * 0x9e3779b9u;
    hash ^= WeatherModelFoldCoordinate(x) * 0x85ebca6bu;
    hash ^= WeatherModelFoldCoordinate(z) * 0xc2b2ae35u;
    hash ^= WeatherModelFoldCoordinate(time) * 0x27d4eb2fu;
    hash = WeatherModelMix(hash);
    return (float)(hash & 0x00ffffffu) / 16777215.0f;
}

static float WeatherModelNoise3D(uint32_t seed, double x, double z,
                                 double time, uint32_t lane)
{
    int64_t x0 = (int64_t)floor(x);
    int64_t z0 = (int64_t)floor(z);
    int64_t t0 = (int64_t)floor(time);
    float tx = WeatherModelSmooth((float)(x - (double)x0));
    float tz = WeatherModelSmooth((float)(z - (double)z0));
    float tt = WeatherModelSmooth((float)(time - (double)t0));
    float layer[2];
    for (int timeOffset = 0; timeOffset < 2; timeOffset++) {
        float north = WeatherModelLerp(
            WeatherModelLattice(seed, x0, z0, t0 + timeOffset, lane),
            WeatherModelLattice(seed, x0 + 1, z0, t0 + timeOffset, lane), tx);
        float south = WeatherModelLerp(
            WeatherModelLattice(seed, x0, z0 + 1, t0 + timeOffset, lane),
            WeatherModelLattice(seed, x0 + 1, z0 + 1, t0 + timeOffset, lane), tx);
        layer[timeOffset] = WeatherModelLerp(north, south, tz);
    }
    return WeatherModelLerp(layer[0], layer[1], tt);
}

WeatherFieldSample WeatherFieldSampleAt(const WeatherFieldInput *input)
{
    WeatherFieldSample sample = { 0 };
    if (!input || !isfinite(input->simulationTime) ||
        !isfinite(input->worldX) || !isfinite(input->worldZ) ||
        !isfinite(input->temperatureK) || !isfinite(input->moisture) ||
        !isfinite(input->cloudPotential) || !isfinite(input->windStrength) ||
        !isfinite(input->prevailingWindAngle) ||
        !isfinite(input->surfacePressureAtm) ||
        !isfinite(input->atmosphereDensity) ||
        !isfinite(input->relativeHumidity) ||
        !isfinite(input->dewPointK) || !isfinite(input->wetBulbK) ||
        !isfinite(input->instability) || !isfinite(input->orographicLift) ||
        !isfinite(input->aridity) || !isfinite(input->dustAvailability) ||
        !isfinite(input->latitude) || !isfinite(input->magneticLatitude) ||
        !isfinite(input->magneticFieldStrength) ||
        !isfinite(input->daylight) || !isfinite(input->solarElevation) ||
        input->temperatureK < 0.0f || input->surfacePressureAtm < 0.0f) {
        return sample;
    }

    sample.dominantPhenomenon = WEATHER_PHENOMENON_CLEAR;
    sample.temperatureK = input->temperatureK;
    sample.pressureAtm = input->surfacePressureAtm;
    sample.dewPointK = input->dewPointK;
    sample.wetBulbK = input->wetBulbK;
    sample.visibility = 1.0f;
    if (!input->atmosphereActive || input->surfacePressureAtm < 0.006f ||
        input->atmosphereDensity <= 0.01f) {
        sample.phenomena = WEATHER_PHENOMENON_FLAG(WEATHER_PHENOMENON_CLEAR);
        return sample;
    }

    double simulationTime = fmax(input->simulationTime, 0.0);
    float windStrength = WeatherModelClamp(input->windStrength);
    double advection = simulationTime * (0.18 + 0.52 * (double)windStrength);
    double fieldX = ((double)input->worldX +
        cos((double)input->prevailingWindAngle) * advection) /
        WEATHER_FIELD_SPATIAL_SCALE;
    double fieldZ = ((double)input->worldZ +
        sin((double)input->prevailingWindAngle) * advection) /
        WEATHER_FIELD_SPATIAL_SCALE;
    double fieldTime = simulationTime / WEATHER_FIELD_TIME_SCALE;

    float pressureField = WeatherModelNoise3D(input->seed, fieldX, fieldZ,
                                              fieldTime, 0x51f15eu);
    const double gradientStep = 0.08;
    float pressureEast = WeatherModelNoise3D(
        input->seed, fieldX + gradientStep, fieldZ, fieldTime, 0x51f15eu);
    float pressureWest = WeatherModelNoise3D(
        input->seed, fieldX - gradientStep, fieldZ, fieldTime, 0x51f15eu);
    float pressureSouth = WeatherModelNoise3D(
        input->seed, fieldX, fieldZ + gradientStep, fieldTime, 0x51f15eu);
    float pressureNorth = WeatherModelNoise3D(
        input->seed, fieldX, fieldZ - gradientStep, fieldTime, 0x51f15eu);
    float gradientX = (pressureEast - pressureWest) /
                      (float)(gradientStep * 2.0);
    float gradientZ = (pressureSouth - pressureNorth) /
                      (float)(gradientStep * 2.0);
    float pressureGradient = WeatherModelClamp(
        sqrtf(gradientX * gradientX + gradientZ * gradientZ) * 1.7f);
    float convection = WeatherModelNoise3D(input->seed, fieldX * 1.73 + 7.2,
                                           fieldZ * 1.73 - 3.9,
                                           fieldTime * 1.31 + 5.7, 0xc0a1e5u);
    float gust = WeatherModelNoise3D(input->seed, fieldX * 2.41 - 11.0,
                                     fieldZ * 2.41 + 8.3,
                                     fieldTime * 1.87 + 2.1, 0x7e57a11u);
    float humidityField = WeatherModelNoise3D(
        input->seed, fieldX * 0.83 + 19.0, fieldZ * 0.83 - 13.0,
        fieldTime * 0.71 + 9.0, 0x8a17c9u);
    float rareField = WeatherModelNoise3D(
        input->seed, fieldX * 0.37 - 31.0, fieldZ * 0.37 + 23.0,
        fieldTime * 0.23 + 17.0, 0xa07a5u);
    float moisture = WeatherModelClamp(input->moisture);
    float cloudPotential = WeatherModelClamp(input->cloudPotential);
    float baseHumidity = input->relativeHumidity > 0.0f ?
        WeatherModelClamp(input->relativeHumidity * 0.62f +
                          moisture * 0.26f + cloudPotential * 0.12f) :
        WeatherModelClamp(moisture * 0.68f + cloudPotential * 0.32f);
    sample.relativeHumidity = WeatherModelClamp(
        baseHumidity + (humidityField - 0.5f) * 0.24f);
    float frontalLift = WeatherModelClamp(
        (1.0f - fabsf(pressureField - 0.5f) * 2.0f) * 0.68f +
        pressureGradient * 0.52f + WeatherModelClamp(input->orographicLift) * 0.34f);
    sample.pressureAnomaly = (pressureField - 0.5f) * 2.0f;
    sample.pressureAtm = fmaxf(input->surfacePressureAtm *
        (1.0f + sample.pressureAnomaly * 0.035f), 0.0f);
    float thermalFront = (pressureField - 0.5f) * 15.0f;
    float heatPulse = WeatherModelSmoothStep(0.82f, 0.98f, rareField) * 7.0f;
    float coldPulse = WeatherModelSmoothStep(0.02f, 0.16f,
                                             1.0f - rareField) * 7.0f;
    sample.temperatureAnomalyK = thermalFront + heatPulse - coldPulse;
    sample.temperatureK = fminf(fmaxf(input->temperatureK +
                                      sample.temperatureAnomalyK,
                                      120.0f), 500.0f);
    sample.dewPointK = WeatherModelDewPoint(sample.temperatureK,
                                            sample.relativeHumidity);
    sample.wetBulbK = WeatherModelWetBulb(sample.temperatureK,
                                          sample.relativeHumidity);
    sample.instability = WeatherModelClamp(
        WeatherModelClamp(input->instability) * 0.62f + convection * 0.28f +
        WeatherModelSmoothStep(292.0f, 320.0f, sample.temperatureK) * 0.18f);

    float saturation = 1.0f - WeatherModelSmoothStep(
        1.5f, 10.0f, sample.temperatureK - sample.dewPointK);
    sample.cloudCover = WeatherModelClamp(
        cloudPotential * 0.32f + sample.relativeHumidity * 0.32f +
        humidityField * 0.16f + frontalLift * 0.34f);
    float wetSignal = sample.relativeHumidity * 0.40f +
        saturation * 0.30f + sample.cloudCover * 0.24f + frontalLift * 0.24f;
    sample.precipitation = input->supportsWaterCycle ?
        WeatherModelSmoothStep(0.58f, 0.91f, wetSignal) : 0.0f;
    float stormSignal = sample.instability * 0.48f + convection * 0.24f +
                        pressureGradient * 0.18f +
                        sample.precipitation * 0.24f + windStrength * 0.15f;
    sample.storm = sample.precipitation *
                   WeatherModelSmoothStep(0.52f, 0.88f, stormSignal);

    float snowWeight = 1.0f - WeatherModelSmoothStep(
        268.5f, 273.2f, sample.wetBulbK);
    float sleetWeight = WeatherModelClamp(
        1.0f - fabsf(sample.wetBulbK - 272.2f) / 3.2f) * 0.72f;
    float freezingWeight = sample.temperatureK < 273.15f ?
        WeatherModelSmoothStep(270.0f, 274.5f, sample.wetBulbK) *
        WeatherModelSmoothStep(0.55f, 0.90f, pressureField) * 0.82f : 0.0f;
    float hailWeight = WeatherModelSmoothStep(0.52f, 0.90f, sample.storm) *
        WeatherModelSmoothStep(0.55f, 0.92f, sample.instability) *
        (1.0f - WeatherModelSmoothStep(304.0f, 316.0f, sample.temperatureK));
    float rainWeight = WeatherModelSmoothStep(270.5f, 276.0f,
                                               sample.wetBulbK);
    float phaseTotal = snowWeight + sleetWeight + freezingWeight +
                       hailWeight + rainWeight;
    if (sample.precipitation > 0.0f && phaseTotal > 0.0f) {
        sample.snow = sample.precipitation * snowWeight / phaseTotal;
        sample.sleet = sample.precipitation * sleetWeight / phaseTotal;
        sample.freezingRain = sample.precipitation * freezingWeight / phaseTotal;
        sample.hail = sample.precipitation * hailWeight / phaseTotal;
        sample.rain = sample.precipitation * rainWeight / phaseTotal;
    }
    sample.drizzle = sample.rain *
        (1.0f - WeatherModelSmoothStep(0.22f, 0.52f, sample.precipitation));
    sample.lightning = WeatherModelClamp(sample.storm *
        WeatherModelSmoothStep(0.48f, 0.88f, sample.instability) *
        WeatherModelSmoothStep(0.70f, 0.97f, rareField));
    sample.cloudCover = WeatherModelClamp(
        fmaxf(sample.cloudCover,
              sample.precipitation * 0.76f + sample.storm * 0.16f));
    sample.cloudBaseHeight = fminf(fmaxf(
        125.0f * (sample.temperatureK - sample.dewPointK), 20.0f), 2500.0f);
    float spread = sample.temperatureK - sample.dewPointK;
    sample.fog = WeatherModelClamp(
        (1.0f - WeatherModelSmoothStep(0.8f, 5.5f, spread)) *
        (1.0f - WeatherModelSmoothStep(0.35f, 0.82f, windStrength)) *
        (0.68f + sample.cloudCover * 0.32f));
    sample.frost = sample.temperatureK < 273.15f ? WeatherModelClamp(
        (1.0f - WeatherModelSmoothStep(1.0f, 7.0f, spread)) *
        WeatherModelSmoothStep(248.0f, 273.15f, sample.temperatureK)) : 0.0f;
    float baseWindX = cosf(input->prevailingWindAngle) * windStrength;
    float baseWindZ = sinf(input->prevailingWindAngle) * windStrength;
    float gradientScale = pressureGradient * 0.52f;
    float gradientLength = sqrtf(gradientX * gradientX + gradientZ * gradientZ);
    if (gradientLength > 0.00001f) {
        baseWindX -= gradientX / gradientLength * gradientScale;
        baseWindZ -= gradientZ / gradientLength * gradientScale;
    }
    sample.windAngle = atan2f(baseWindZ, baseWindX);
    sample.wind = WeatherModelClamp(
        sqrtf(baseWindX * baseWindX + baseWindZ * baseWindZ) *
        (0.68f + gust * 0.32f) + sample.storm * 0.24f);
    sample.gust = WeatherModelClamp(
        sample.wind + gust * 0.28f + sample.storm * 0.22f);
    sample.dust = WeatherModelClamp(
        WeatherModelClamp(input->aridity) *
        WeatherModelClamp(input->dustAvailability) *
        WeatherModelSmoothStep(0.42f, 0.86f, sample.gust) *
        (1.0f - sample.precipitation) * 1.45f);
    sample.heatWave = WeatherModelSmoothStep(
        5.0f, 11.0f, sample.temperatureAnomalyK) *
        WeatherModelSmoothStep(0.72f, 0.97f, rareField);
    sample.coldSnap = WeatherModelSmoothStep(
        5.0f, 11.0f, -sample.temperatureAnomalyK) *
        WeatherModelSmoothStep(0.72f, 0.97f, 1.0f - rareField);
    float daylight = WeatherModelClamp(input->daylight);
    float solarElevation = WeatherModelClamp(input->solarElevation);
    sample.rainbow = WeatherModelClamp(
        sample.rain * daylight *
        (1.0f - WeatherModelSmoothStep(0.62f, 0.96f, sample.cloudCover)) *
        WeatherModelSmoothStep(0.08f, 0.28f, solarElevation) *
        (1.0f - WeatherModelSmoothStep(0.68f, 0.92f, solarElevation)) * 3.0f);
    sample.aurora = WeatherModelClamp(
        (1.0f - daylight) *
        WeatherModelSmoothStep(0.55f, 0.92f,
            fabsf(input->magneticLatitude)) *
        WeatherModelClamp(input->magneticFieldStrength) *
        (1.0f - sample.cloudCover) *
        WeatherModelSmoothStep(0.68f, 0.97f, rareField) * 1.8f);
    sample.visibility = WeatherModelClamp(
        1.0f - sample.fog * 0.78f - sample.precipitation * 0.24f -
        sample.dust * 0.72f - sample.storm * 0.12f);

    sample.phenomena = sample.cloudCover >= 0.38f
        ? WEATHER_PHENOMENON_FLAG(WEATHER_PHENOMENON_CLOUDY)
        : WEATHER_PHENOMENON_FLAG(WEATHER_PHENOMENON_CLEAR);
#define ADD_PHENOMENON(field, threshold, phenomenon) \
    do { if (sample.field >= (threshold)) sample.phenomena |= \
        WEATHER_PHENOMENON_FLAG(phenomenon); } while (0)
    ADD_PHENOMENON(fog, 0.28f, WEATHER_PHENOMENON_FOG);
    ADD_PHENOMENON(frost, 0.30f, WEATHER_PHENOMENON_FROST);
    ADD_PHENOMENON(drizzle, 0.08f, WEATHER_PHENOMENON_DRIZZLE);
    if (sample.rain >= 0.18f && sample.precipitation < 0.68f) {
        sample.phenomena |= WEATHER_PHENOMENON_FLAG(WEATHER_PHENOMENON_SHOWERS);
    }
    ADD_PHENOMENON(rain, 0.58f, WEATHER_PHENOMENON_HEAVY_RAIN);
    ADD_PHENOMENON(storm, 0.36f, WEATHER_PHENOMENON_THUNDERSTORM);
    ADD_PHENOMENON(lightning, 0.18f, WEATHER_PHENOMENON_LIGHTNING);
    ADD_PHENOMENON(sleet, 0.08f, WEATHER_PHENOMENON_SLEET);
    ADD_PHENOMENON(freezingRain, 0.08f, WEATHER_PHENOMENON_FREEZING_RAIN);
    ADD_PHENOMENON(hail, 0.06f, WEATHER_PHENOMENON_HAIL);
    ADD_PHENOMENON(snow, 0.08f, WEATHER_PHENOMENON_SNOW);
    if (sample.snow >= 0.36f && sample.gust >= 0.62f) {
        sample.phenomena |= WEATHER_PHENOMENON_FLAG(WEATHER_PHENOMENON_BLIZZARD);
    }
    ADD_PHENOMENON(gust, 0.72f, WEATHER_PHENOMENON_STRONG_WIND);
    ADD_PHENOMENON(dust, 0.36f, WEATHER_PHENOMENON_DUST_STORM);
    ADD_PHENOMENON(heatWave, 0.32f, WEATHER_PHENOMENON_HEAT_WAVE);
    ADD_PHENOMENON(coldSnap, 0.32f, WEATHER_PHENOMENON_COLD_SNAP);
    ADD_PHENOMENON(rainbow, 0.18f, WEATHER_PHENOMENON_RAINBOW);
    ADD_PHENOMENON(aurora, 0.18f, WEATHER_PHENOMENON_AURORA);
#undef ADD_PHENOMENON

    static const WeatherPhenomenon priority[] = {
        WEATHER_PHENOMENON_BLIZZARD, WEATHER_PHENOMENON_THUNDERSTORM,
        WEATHER_PHENOMENON_DUST_STORM, WEATHER_PHENOMENON_HEAVY_RAIN,
        WEATHER_PHENOMENON_HAIL, WEATHER_PHENOMENON_FREEZING_RAIN,
        WEATHER_PHENOMENON_SLEET, WEATHER_PHENOMENON_SNOW,
        WEATHER_PHENOMENON_SHOWERS, WEATHER_PHENOMENON_DRIZZLE,
        WEATHER_PHENOMENON_FOG, WEATHER_PHENOMENON_STRONG_WIND,
        WEATHER_PHENOMENON_COLD_SNAP, WEATHER_PHENOMENON_HEAT_WAVE,
        WEATHER_PHENOMENON_AURORA, WEATHER_PHENOMENON_RAINBOW,
        WEATHER_PHENOMENON_FROST, WEATHER_PHENOMENON_CLOUDY,
        WEATHER_PHENOMENON_CLEAR
    };
    for (unsigned index = 0; index < sizeof(priority) / sizeof(priority[0]);
         index++) {
        if (sample.phenomena & WEATHER_PHENOMENON_FLAG(priority[index])) {
            sample.dominantPhenomenon = priority[index];
            break;
        }
    }
    return sample;
}

float WeatherFieldSkyFactor(WeatherFieldSample sample)
{
    return WeatherModelClamp(sample.cloudCover * 0.55f +
                              sample.precipitation * 0.25f +
                              sample.storm * 0.14f + sample.dust * 0.06f);
}

const char *WeatherPhenomenonName(WeatherPhenomenon phenomenon)
{
    static const char *const names[WEATHER_PHENOMENON_COUNT] = {
        "Clear", "Cloudy", "Fog", "Frost", "Drizzle", "Showers",
        "Heavy rain", "Thunderstorm", "Lightning", "Sleet",
        "Freezing rain", "Hail", "Snow", "Blizzard", "Strong wind",
        "Dust storm", "Heat wave", "Cold snap", "Rainbow", "Aurora"
    };
    if (phenomenon < 0 || phenomenon >= WEATHER_PHENOMENON_COUNT) {
        return "Unknown";
    }
    return names[phenomenon];
}

static bool WeatherNameEqual(const char *left, const char *right)
{
    while (*left || *right) {
        while (*left == '-' || *left == '_' || isspace((unsigned char)*left)) {
            left++;
        }
        while (*right == '-' || *right == '_' || isspace((unsigned char)*right)) {
            right++;
        }
        if (tolower((unsigned char)*left) != tolower((unsigned char)*right)) {
            return false;
        }
        if (*left) left++;
        if (*right) right++;
    }
    return true;
}

bool WeatherPhenomenonFromName(const char *name,
                               WeatherPhenomenon *outPhenomenon)
{
    if (!name || !outPhenomenon) return false;
    for (int index = 0; index < WEATHER_PHENOMENON_COUNT; index++) {
        if (WeatherNameEqual(name, WeatherPhenomenonName(
                                      (WeatherPhenomenon)index))) {
            *outPhenomenon = (WeatherPhenomenon)index;
            return true;
        }
    }
    return false;
}

bool WeatherSampleHasPhenomenon(WeatherFieldSample sample,
                                WeatherPhenomenon phenomenon)
{
    return phenomenon >= 0 && phenomenon < WEATHER_PHENOMENON_COUNT &&
           (sample.phenomena & WEATHER_PHENOMENON_FLAG(phenomenon)) != 0u;
}
