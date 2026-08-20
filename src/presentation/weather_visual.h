#ifndef VOXELCRAFT_WEATHER_VISUAL_H
#define VOXELCRAFT_WEATHER_VISUAL_H

#include "world/weather_model.h"

#include <stdbool.h>

#define WEATHER_VISUAL_CLOUD_LAYER_CAPACITY 3

typedef struct WeatherVisualInput {
    WeatherFieldSample weather;
    float atmosphereDensity;
    float daylight;
    float windAngle;
    bool atmosphereActive;
} WeatherVisualInput;

typedef struct WeatherCloudVisualLayer {
    WeatherCloudGenus genus;
    float coverage;
    float baseHeight;
    float thickness;
    float opacity;
    float noiseScale;
    float stretch;
    float cellularity;
    float verticalDevelopment;
    float anvil;
    float driftScale;
} WeatherCloudVisualLayer;

typedef struct WeatherVisualState {
    bool active;
    float atmosphereDensity;
    float daylight;
    float cloudCover;
    float cloudBaseHeight;
    float cloudThickness;
    float cloudOpacity;
    WeatherCloudGenus dominantCloudGenus;
    unsigned cloudLayerCount;
    WeatherCloudVisualLayer cloudLayers[WEATHER_VISUAL_CLOUD_LAYER_CAPACITY];
    float fogDensity;
    float dustDensity;
    float visibility;
    float precipitationVeil;
    float stormDarkening;
    float windDrift;
    float windAngle;
    float snowFraction;
    float sleetFraction;
    float freezingRainFraction;
    float hailFraction;
    float frost;
    float lightningIntensity;
    float rainbowStrength;
    float auroraStrength;
    float temperatureAnomalyK;
} WeatherVisualState;

typedef struct WeatherCloudMotionState {
    bool initialized;
    double lastSimulationTime;
    double offsetX;
    double offsetZ;
    float directionX;
    float directionZ;
} WeatherCloudMotionState;

/* Derive renderer-facing weather values without mutating simulation state. */
WeatherVisualState WeatherVisualStateEvaluate(const WeatherVisualInput *input);

/* Shared vertical envelope for volumetric cloud density, from base 0 to top 1. */
float WeatherCloudVerticalDensity(float normalizedHeight);

/* Integrate cloud advection without reprojecting its history when wind turns. */
void WeatherCloudMotionAdvance(WeatherCloudMotionState *state,
                               double simulationTime, float windAngle,
                               float driftSpeed);

float WeatherCloudAltitudeReference(float seaLevel, float height00,
                                    float height10, float height01,
                                    float height11, float tx, float tz);

#endif
