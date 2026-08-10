#ifndef VOXELCRAFT_WEATHER_H
#define VOXELCRAFT_WEATHER_H

#include "raylib.h"
#include "weather_model.h"

typedef enum Weather {
    WEATHER_CLEAR = 0,
    WEATHER_RAIN,
    WEATHER_SNOW
} Weather;

void WeatherInit(void);
void WeatherUpdate(float dt, Vector3 playerPosition);
void WeatherSuspend(void);
Weather WeatherGetCurrent(void);
float WeatherSkyFactor(void);
float WeatherCloudCover(void);
float WeatherPrecipitationRate(void);
float WeatherStormIntensity(void);
float WeatherWindIntensity(void);
const char *WeatherName(void);
void WeatherCycle(void);

#endif
