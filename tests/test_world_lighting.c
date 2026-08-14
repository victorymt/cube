#include "world_lighting.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool Near(float actual, float expected)
{
    return fabsf(actual - expected) < 0.0001f;
}

static WorldLightingState PhysicalLighting(void)
{
    return (WorldLightingState){
        .sunDirection = { 0.3f, 0.9f, 0.2f },
        .directStrength = 1.2f,
        .ambientStrength = 0.6f,
        .shadowStrength = 0.7f,
        .fogStart = 38.0f,
        .exposure = 1.0f,
        .saturation = 1.0f,
        .waveStrength = 0.18f,
        .shadowsEnabled = true
    };
}

static EnvironmentPresentationInput ClearInput(void)
{
    return (EnvironmentPresentationInput){
        .scene = ENVIRONMENT_SCENE_HOME,
        .quality = GRAPHICS_QUALITY_MEDIUM,
        .daylight = 1.0f
    };
}

static void TestClearAndStormComposition(void)
{
    EnvironmentPresentationInput input = ClearInput();
    EnvironmentPresentationState clear = EnvironmentPresentationEvaluate(&input);
    WorldLightingState clearLighting = WorldLightingCompose(
        PhysicalLighting(), &clear);
    assert(Near(clearLighting.directStrength, 1.2f));
    assert(Near(clearLighting.ambientStrength, 0.6f));
    assert(clearLighting.fogDensity == 0.0f);
    assert(clearLighting.wetness == 0.0f);

    input.weather = (WeatherVisualState){
        .active = true,
        .cloudOpacity = 0.84f,
        .fogDensity = 0.62f,
        .visibility = 0.44f,
        .precipitationVeil = 0.78f,
        .stormDarkening = 0.72f
    };
    EnvironmentPresentationState storm = EnvironmentPresentationEvaluate(&input);
    WorldLightingState stormLighting = WorldLightingCompose(
        PhysicalLighting(), &storm);
    assert(Near(stormLighting.directStrength,
                1.2f * storm.directLightScale));
    assert(Near(stormLighting.ambientStrength,
                0.6f * storm.ambientLightScale));
    assert(Near(stormLighting.shadowStrength,
                0.7f * storm.directLightScale));
    assert(Near(stormLighting.fogDensity, storm.fogDensity));
    assert(Near(stormLighting.wetness, storm.wetness));
}

static void TestUnderwaterFogPalette(void)
{
    Color shallow = WorldLightingUnderwaterFogColor(0.0f);
    Color deep = WorldLightingUnderwaterFogColor(
        UNDERWATER_DEEP_REFERENCE_DEPTH);
    assert(shallow.r == 43 && shallow.g == 132 && shallow.b == 151);
    assert(deep.r == 8 && deep.g == 38 && deep.b == 61);
    assert(deep.r < shallow.r && deep.g < shallow.g && deep.b < shallow.b);
}

static void TestNetherComposition(void)
{
    EnvironmentPresentationInput input = ClearInput();
    input.scene = ENVIRONMENT_SCENE_NETHER;
    EnvironmentPresentationState nether = EnvironmentPresentationEvaluate(&input);
    WorldLightingState physical = PhysicalLighting();
    physical.directStrength = 1.0f;
    physical.ambientStrength = 1.0f;
    WorldLightingState lighting = WorldLightingCompose(physical, &nether);
    assert(Near(lighting.directStrength, 0.18f));
    assert(Near(lighting.ambientStrength, 0.72f));
    assert(Near(lighting.fogDensity, 0.010f));
    assert(lighting.shadowStrength == 0.0f);
    assert(!lighting.shadowsEnabled);
}

static void TestFallbacks(void)
{
    WorldLightingState physical = PhysicalLighting();
    WorldLightingState neutral = WorldLightingCompose(physical, NULL);
    assert(Near(neutral.directStrength, physical.directStrength));
    assert(Near(neutral.ambientStrength, physical.ambientStrength));

    WeatherVisualState weather = {
        .active = true,
        .cloudOpacity = 0.7f,
        .precipitationVeil = 0.8f,
        .stormDarkening = 0.6f
    };
    EnvironmentPresentationState fallback =
        WorldLightingFallbackPresentation(0.7f, 0.0f, &weather, false);
    assert(fallback.directLightScale < 1.0f);
    assert(fallback.wetness > 0.0f);
    WorldLightingState fallbackLighting = WorldLightingCompose(
        physical, &fallback);
    assert(Near(fallbackLighting.directStrength,
                physical.directStrength * fallback.directLightScale));
}

int main(void)
{
    TestClearAndStormComposition();
    TestUnderwaterFogPalette();
    TestNetherComposition();
    TestFallbacks();
    puts("world lighting tests passed");
    return 0;
}
