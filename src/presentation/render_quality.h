#ifndef VOXELCRAFT_RENDER_QUALITY_H
#define VOXELCRAFT_RENDER_QUALITY_H

#include <stdbool.h>

typedef enum GraphicsQuality {
    GRAPHICS_QUALITY_LOW = 0,
    GRAPHICS_QUALITY_MEDIUM,
    GRAPHICS_QUALITY_HIGH,
    GRAPHICS_QUALITY_COUNT
} GraphicsQuality;

typedef struct GraphicsQualityProfile {
    int shadowMapSize;
    int shadowUpdateInterval;
    int shadowChunkRadius;
    int cloudGridRadius;
    int cloudRaySteps;
    int cloudLightSteps;
    int wildfireMaxFires;
    int wildfireFlameTongues;
    int wildfireSmokePuffs;
    float precipitationScale;
    bool postProcessing;
} GraphicsQualityProfile;

GraphicsQualityProfile GraphicsQualityProfileFor(GraphicsQuality quality);
const char *GraphicsQualityName(GraphicsQuality quality);

#endif
