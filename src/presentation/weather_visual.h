#ifndef VOXELCRAFT_WEATHER_VISUAL_H
#define VOXELCRAFT_WEATHER_VISUAL_H

#include "world/weather_model.h"

#include <stdbool.h>

#define WEATHER_VISUAL_CLOUD_LAYER_CAPACITY 3
#define WEATHER_VISUAL_CLOUD_TRANSITION_SLOT_COUNT 3
#define WEATHER_VISUAL_CLOUD_RENDER_LAYER_CAPACITY \
    (WEATHER_VISUAL_CLOUD_TRANSITION_SLOT_COUNT * 2)

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
    float cloudAltitudeReference;
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

typedef struct WeatherCloudTransitionSlot {
    bool primaryActive;
    WeatherCloudVisualLayer primary;
    float primaryWeight;
    bool fadingActive;
    WeatherCloudVisualLayer fading;
    float fadingWeight;
} WeatherCloudTransitionSlot;

typedef struct WeatherCloudTransitionState {
    bool initialized;
    double lastSimulationTime;
    WeatherCloudTransitionSlot slots[
        WEATHER_VISUAL_CLOUD_TRANSITION_SLOT_COUNT];
} WeatherCloudTransitionState;

typedef struct WeatherCloudRenderLayers {
    unsigned count;
    WeatherCloudVisualLayer layers[
        WEATHER_VISUAL_CLOUD_RENDER_LAYER_CAPACITY];
} WeatherCloudRenderLayers;

/* Derive renderer-facing weather values without mutating simulation state. */
WeatherVisualState WeatherVisualStateEvaluate(const WeatherVisualInput *input);

/* Shared vertical envelope for volumetric cloud density, from base 0 to top 1. */
float WeatherCloudVerticalDensity(float normalizedHeight);

/* Integrate cloud advection without reprojecting its history when wind turns. */
void WeatherCloudMotionAdvance(WeatherCloudMotionState *state,
                               double simulationTime, float windAngle,
                               float driftSpeed);

/* Convert normalized weather wind to the compressed sky's visual drift rate. */
float WeatherCloudVisualDriftSpeed(float windDrift);

/* Cross-fade cloud genera so classification threshold crossings do not pop. */
void WeatherCloudTransitionAdvance(WeatherCloudTransitionState *state,
                                   const WeatherVisualState *target,
                                   double simulationTime, float visualDt,
                                   WeatherCloudRenderLayers *outLayers);

/* Fixed surface datum; a dry world uses the atmosphere's reference Y=12. */
float WeatherCloudAltitudeReference(float seaLevel);

#endif
