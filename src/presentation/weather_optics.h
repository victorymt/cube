#ifndef VOXELCRAFT_WEATHER_OPTICS_H
#define VOXELCRAFT_WEATHER_OPTICS_H

#include "raylib.h"

/* Point on the primary-rainbow cone around the anti-solar direction. */
Vector3 WeatherRainbowDirection(Vector3 sunDirection,
                                float angularRadiusRadians,
                                float phaseRadians);

#endif
