#ifndef VOXELCRAFT_WEATHER_H
#define VOXELCRAFT_WEATHER_H

#include "raylib.h"

typedef enum Weather {
    WEATHER_CLEAR = 0,
    WEATHER_RAIN,
    WEATHER_SNOW
} Weather;

void WeatherInit(void);
void WeatherUpdate(float dt, Vector3 playerPosition, bool coldArea);
Weather WeatherGetCurrent(void);
float WeatherSkyFactor(void);
const char *WeatherName(void);
void WeatherCycle(bool coldArea);

#endif
