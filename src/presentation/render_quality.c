#include "presentation/render_quality.h"

GraphicsQualityProfile GraphicsQualityProfileFor(GraphicsQuality quality)
{
    switch (quality) {
    case GRAPHICS_QUALITY_LOW:
        return (GraphicsQualityProfile){
            .shadowMapSize = 0,
            .shadowUpdateInterval = 0,
            .shadowChunkRadius = 0,
            .cloudGridRadius = 1,
            .cloudRaySteps = 8,
            .cloudLightSteps = 1,
            .wildfireMaxFires = 12,
            .wildfireFlameTongues = 2,
            .wildfireSmokePuffs = 3,
            .precipitationScale = 0.45f,
            .postProcessing = false
        };
    case GRAPHICS_QUALITY_HIGH:
        return (GraphicsQualityProfile){
            .shadowMapSize = 3072,
            .shadowUpdateInterval = 1,
            .shadowChunkRadius = 7,
            .cloudGridRadius = 3,
            .cloudRaySteps = 20,
            .cloudLightSteps = 3,
            .wildfireMaxFires = 48,
            .wildfireFlameTongues = 5,
            .wildfireSmokePuffs = 8,
            .precipitationScale = 1.45f,
            .postProcessing = true
        };
    case GRAPHICS_QUALITY_MEDIUM:
    default:
        return (GraphicsQualityProfile){
            .shadowMapSize = 2048,
            .shadowUpdateInterval = 2,
            .shadowChunkRadius = 5,
            .cloudGridRadius = 2,
            .cloudRaySteps = 12,
            .cloudLightSteps = 2,
            .wildfireMaxFires = 24,
            .wildfireFlameTongues = 3,
            .wildfireSmokePuffs = 5,
            .precipitationScale = 1.0f,
            .postProcessing = true
        };
    }
}

const char *GraphicsQualityName(GraphicsQuality quality)
{
    switch (quality) {
    case GRAPHICS_QUALITY_LOW: return "Low";
    case GRAPHICS_QUALITY_HIGH: return "High";
    case GRAPHICS_QUALITY_MEDIUM:
    default: return "Medium";
    }
}
