#ifndef VOXELCRAFT_WEATHER_PRESENTATION_H
#define VOXELCRAFT_WEATHER_PRESENTATION_H

#include "presentation/weather_visual.h"

#include "raylib.h"

/* Compose renderer-facing weather from neutral world and atmosphere values. */
WeatherVisualState WeatherVisualStateAtWorld(
    Vector3 position, double simulationTime, float daylight);

#endif
