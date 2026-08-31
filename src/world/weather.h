#ifndef VOXELCRAFT_WEATHER_H
#define VOXELCRAFT_WEATHER_H

#include "raylib.h"
#include "world/local_climate.h"
#include "world/weather_model.h"

typedef enum Weather {
    WEATHER_CLEAR = 0,
    WEATHER_RAIN,
    WEATHER_SNOW
} Weather;

typedef bool (*WeatherHydrologyFeedbackProvider)(
    int x, int z, double simulationTime,
    LocalClimateHydrologyFeedback *outFeedback, void *userData);

typedef struct WeatherSurfaceLightSample {
    float daylight;
    float eclipse;
} WeatherSurfaceLightSample;

typedef bool (*WeatherSurfaceLightProvider)(
    Vector3 surfacePosition, double simulationTime,
    WeatherSurfaceLightSample *outSample, void *userData);

void WeatherInit(void);
void WeatherUpdate(float dt, Vector3 playerPosition, double simulationTime);
void WeatherSetParticleScale(float scale);
void WeatherSetSheltered(bool sheltered);
void WeatherSetDaylight(float daylight);
void WeatherSetHydrologyFeedbackProvider(
    WeatherHydrologyFeedbackProvider provider, void *userData);
void WeatherClearHydrologyFeedbackProvider(void);
void WeatherSetSurfaceLightProvider(
    WeatherSurfaceLightProvider provider, void *userData);
void WeatherSuspend(void);
Weather WeatherGetCurrent(void);
/* Sample the deterministic weather field at a local world cell. */
WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z);
WeatherFieldSample WeatherFieldSampleAtWorldTime(
    int x, int z, double simulationTime);
bool WeatherFieldSampleInterpolatedAtWorldTime(
    float worldX, float worldZ, double simulationTime,
    WeatherFieldSample *outSample);
bool WeatherLocalClimateAtWorldTime(int x, int z, double simulationTime,
                                    LocalClimateState *outClimate);
float WeatherWindAngleAtWorld(int x, int z);
float WeatherWindAngleAtWorldTime(int x, int z, double simulationTime);
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
