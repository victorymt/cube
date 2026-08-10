#include "weather_model.h"

#include <math.h>
#include <stdint.h>

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
        !isfinite(input->prevailingWindAngle)) {
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

    float front = WeatherModelNoise3D(input->seed, fieldX, fieldZ,
                                      fieldTime, 0x51f15eu);
    float convection = WeatherModelNoise3D(input->seed, fieldX * 1.73 + 7.2,
                                           fieldZ * 1.73 - 3.9,
                                           fieldTime * 1.31 + 5.7, 0xc0a1e5u);
    float gust = WeatherModelNoise3D(input->seed, fieldX * 2.41 - 11.0,
                                     fieldZ * 2.41 + 8.3,
                                     fieldTime * 1.87 + 2.1, 0x7e57a11u);
    float moisture = WeatherModelClamp(input->moisture);
    float cloudPotential = WeatherModelClamp(input->cloudPotential);
    float humidity = moisture * 0.58f + cloudPotential * 0.42f;

    float wetSignal = humidity * 0.65f + front * 0.55f;
    sample.precipitation = WeatherModelSmoothStep(0.57f, 0.84f, wetSignal);
    float warmConvection = WeatherModelSmoothStep(292.0f, 326.0f,
                                                   input->temperatureK);
    float stormSignal = convection * 0.52f + windStrength * 0.25f +
                        warmConvection * convection * 0.25f +
                        sample.precipitation * 0.20f;
    sample.storm = sample.precipitation *
                   WeatherModelSmoothStep(0.55f, 0.90f, stormSignal);

    float snowFraction = 1.0f - WeatherModelSmoothStep(
        267.0f, 278.0f, input->temperatureK);
    sample.snow = sample.precipitation * snowFraction;
    sample.rain = sample.precipitation * (1.0f - snowFraction);
    sample.cloudCover = WeatherModelClamp(
        cloudPotential * 0.42f + moisture * 0.20f + front * 0.45f);
    sample.cloudCover = fmaxf(sample.cloudCover,
                              sample.precipitation * 0.76f + sample.storm * 0.16f);
    sample.wind = WeatherModelClamp(
        windStrength * (0.62f + gust * 0.38f) + sample.storm * 0.32f);
    return sample;
}
