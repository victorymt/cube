#ifndef VOXELCRAFT_WEATHER_VISUAL_H
#define VOXELCRAFT_WEATHER_VISUAL_H

#include "world/weather_model.h"

#include <stdbool.h>

typedef struct WeatherVisualInput {
    WeatherFieldSample weather;
    float atmosphereDensity;
    float daylight;
    float windAngle;
    bool atmosphereActive;
} WeatherVisualInput;

typedef struct WeatherVisualState {
    bool active;
    float atmosphereDensity;
    float daylight;
    float cloudCover;
    float cloudBaseHeight;
    float cloudThickness;
    float cloudOpacity;
    float fogDensity;
    float visibility;
    float precipitationVeil;
    float stormDarkening;
    float windDrift;
    float windAngle;
    float snowFraction;
} WeatherVisualState;

/* Derive renderer-facing weather values without mutating simulation state. */
WeatherVisualState WeatherVisualStateEvaluate(const WeatherVisualInput *input);

/* Shared vertical envelope for volumetric cloud density, from base 0 to top 1. */
float WeatherCloudVerticalDensity(float normalizedHeight);

#endif
