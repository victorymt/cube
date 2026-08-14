#ifndef VOXELCRAFT_WORLD_LIGHTING_H
#define VOXELCRAFT_WORLD_LIGHTING_H

#include "environment_presentation.h"
#include "world_renderer.h"

EnvironmentPresentationState WorldLightingFallbackPresentation(
    float daylight, float sunset, const WeatherVisualState *weatherVisual,
    bool inNether);
Color WorldLightingUnderwaterFogColor(float underwaterDepth);
WorldLightingState WorldLightingCompose(
    WorldLightingState physical,
    const EnvironmentPresentationState *presentation);

#endif
