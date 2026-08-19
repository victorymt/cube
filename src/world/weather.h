#ifndef VOXELCRAFT_WEATHER_H
#define VOXELCRAFT_WEATHER_H

#include "raylib.h"
#include "world/local_climate.h"
#include "world/weather_model.h"
#include "presentation/weather_visual.h"

typedef enum Weather {
    WEATHER_CLEAR = 0,
    WEATHER_RAIN,
    WEATHER_SNOW
} Weather;

void WeatherInit(void);
void WeatherUpdate(float dt, Vector3 playerPosition);
void WeatherSetParticleScale(float scale);
void WeatherSetSheltered(bool sheltered);
void WeatherSetDaylight(float daylight);
void WeatherSuspend(void);
Weather WeatherGetCurrent(void);
/* Sample the deterministic weather field at a local world cell. */
WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z);
WeatherFieldSample WeatherFieldSampleAtWorldTime(
    int x, int z, double simulationTime);
bool WeatherLocalClimateAtWorldTime(int x, int z, double simulationTime,
                                    LocalClimateState *outClimate);
float WeatherWindAngleAtWorld(int x, int z);
float WeatherWindAngleAtWorldTime(int x, int z, double simulationTime);
WeatherVisualState WeatherVisualStateAtWorld(
    Vector3 position, double simulationTime, float daylight);
float WeatherSkyFactor(void);
float WeatherCloudCover(void);
float WeatherPrecipitationRate(void);
float WeatherStormIntensity(void);
float WeatherWindIntensity(void);
WeatherFieldSample WeatherCurrentSample(void);
bool WeatherForcePhenomenon(WeatherPhenomenon phenomenon, float intensity,
                            unsigned frames);
bool WeatherForceCloudGenus(WeatherCloudGenus genus, float coverage,
                            unsigned frames);
void WeatherClearForced(void);
void WeatherClearForcedCloud(void);
unsigned WeatherForcedFramesRemaining(void);
unsigned WeatherForcedCloudFramesRemaining(void);
const char *WeatherName(void);
void WeatherCycle(void);

#endif
