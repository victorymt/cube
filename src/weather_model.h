#ifndef VOXELCRAFT_WEATHER_MODEL_H
#define VOXELCRAFT_WEATHER_MODEL_H

#include <stdint.h>

typedef struct WeatherFieldInput {
    uint32_t seed;
    double simulationTime;
    float worldX;
    float worldZ;
    float temperatureK;
    float moisture;
    float cloudPotential;
    float windStrength;
    float prevailingWindAngle;
} WeatherFieldInput;

typedef struct WeatherFieldSample {
    float cloudCover;
    float precipitation;
    float rain;
    float snow;
    float storm;
    float wind;
} WeatherFieldSample;

WeatherFieldSample WeatherFieldSampleAt(const WeatherFieldInput *input);

#endif
