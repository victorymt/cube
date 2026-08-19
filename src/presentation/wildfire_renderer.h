#ifndef VOXELCRAFT_WILDFIRE_RENDERER_H
#define VOXELCRAFT_WILDFIRE_RENDERER_H

#include "presentation/render_quality.h"
#include "presentation/weather_visual.h"
#include "world/weather_impact.h"

#include "raylib.h"

#define WILDFIRE_RENDER_MAX_FIRES 48u

void DrawWildfires(const Camera3D *camera,
                   const WeatherImpactFireSnapshot *fires,
                   unsigned fireCount, GraphicsQuality quality,
                   const WeatherVisualState *weather,
                   double simulationTime, float daylight);
void DrawWildfireHaze(float smokeHaze);

#endif
