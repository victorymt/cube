#ifndef VOXELCRAFT_PRESENTATION_WORLD_LIGHTING_H
#define VOXELCRAFT_PRESENTATION_WORLD_LIGHTING_H

#include "presentation/environment_presentation.h"
#include "world/world_lighting.h"

EnvironmentPresentationState WorldLightingFallbackPresentation(
    float daylight, float sunset, const WeatherVisualState *weatherVisual,
    bool inNether);
Color WorldLightingUnderwaterFogColor(float underwaterDepth);
WorldLightingState WorldLightingCompose(
    WorldLightingState physical,
    const EnvironmentPresentationState *presentation);

#endif
