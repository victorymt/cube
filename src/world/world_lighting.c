#include "world/world_lighting.h"

#include "raymath.h"

Color WorldLightingUnderwaterFogColor(float underwaterDepth)
{
    float deep = Clamp(underwaterDepth / UNDERWATER_DEEP_REFERENCE_DEPTH,
                       0.0f, 1.0f);
    Color shallow = { 43, 132, 151, 255 };
    Color depthColor = { 8, 38, 61, 255 };
    return (Color){
        (unsigned char)Lerp((float)shallow.r, (float)depthColor.r, deep),
        (unsigned char)Lerp((float)shallow.g, (float)depthColor.g, deep),
        (unsigned char)Lerp((float)shallow.b, (float)depthColor.b, deep),
        255
    };
}

EnvironmentPresentationState WorldLightingFallbackPresentation(
    float daylight, float sunset, const WeatherVisualState *weatherVisual,
    bool inNether)
{
    EnvironmentPresentationInput input = {
        .scene = inNether ? ENVIRONMENT_SCENE_NETHER : ENVIRONMENT_SCENE_HOME,
        .quality = GRAPHICS_QUALITY_MEDIUM,
        .daylight = daylight,
        .sunset = sunset
    };
    if (weatherVisual) input.weather = *weatherVisual;
    return EnvironmentPresentationEvaluate(&input);
}

WorldLightingState WorldLightingCompose(
    WorldLightingState physical,
    const EnvironmentPresentationState *presentation)
{
    if (!presentation) return WorldLightingStateSanitize(physical);

    physical.directStrength *= presentation->directLightScale;
    physical.ambientStrength *= presentation->ambientLightScale;
    physical.shadowStrength *= presentation->directLightScale;
    physical.fogDensity = presentation->fogDensity;
    physical.fogStart = presentation->fogStart;
    physical.underwaterAmount = presentation->underwaterAmount;
    physical.underwaterDepth = presentation->underwaterDepth;
    physical.causticStrength = presentation->causticStrength;
    if (presentation->underwaterAmount > 0.001f) {
        physical.fogColor = WorldLightingUnderwaterFogColor(
            presentation->underwaterDepth);
    }
    physical.wetness = presentation->wetness;
    physical.exposure = presentation->exposure;
    physical.saturation = presentation->saturation;
    physical.warmth = presentation->warmth;
    physical.waveStrength = presentation->waveStrength;
    if (presentation->scene == ENVIRONMENT_SCENE_NETHER) {
        physical.shadowStrength = 0.0f;
        physical.shadowsEnabled = false;
    }
    return WorldLightingStateSanitize(physical);
}
