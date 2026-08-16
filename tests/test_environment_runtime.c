#include "presentation/environment_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestDimensionMapping(void)
{
    assert(EnvironmentSceneForDimension(WORLD_DIMENSION_HOME) ==
           ENVIRONMENT_SCENE_HOME);
    assert(EnvironmentSceneForDimension(WORLD_DIMENSION_PLANET) ==
           ENVIRONMENT_SCENE_PLANET);
    assert(EnvironmentSceneForDimension(WORLD_DIMENSION_SPACE) ==
           ENVIRONMENT_SCENE_SPACE);
    assert(EnvironmentSceneForDimension(WORLD_DIMENSION_NETHER) ==
           ENVIRONMENT_SCENE_NETHER);
    assert(EnvironmentSceneForDimension((WorldDimension)99) ==
           ENVIRONMENT_SCENE_HOME);
}

static EnvironmentRuntimeSample RainSample(void)
{
    return (EnvironmentRuntimeSample){
        .dimension = WORLD_DIMENSION_PLANET,
        .quality = GRAPHICS_QUALITY_HIGH,
        .weather = {
            .active = true,
            .cloudOpacity = 0.8f,
            .visibility = 0.4f,
            .precipitationVeil = 0.9f,
            .stormDarkening = 0.7f,
            .windDrift = 0.6f
        },
        .simulationTime = 42.5,
        .daylight = 0.6f,
        .sunset = 0.2f,
        .atmosphereFade = 0.1f,
        .altitude = 28.0f,
        .underwater = false,
        .sheltered = false,
        .forest = true,
        .nearWater = true,
        .shipInterior = false
    };
}

static void TestInputAssembly(void)
{
    EnvironmentRuntimeSample sample = RainSample();
    EnvironmentPresentationInput input =
        EnvironmentPresentationInputForSample(&sample);
    assert(input.scene == ENVIRONMENT_SCENE_PLANET);
    assert(input.quality == sample.quality);
    assert(input.weather.precipitationVeil ==
           sample.weather.precipitationVeil);
    assert(input.simulationTime == sample.simulationTime);
    assert(input.daylight == sample.daylight);
    assert(input.sunset == sample.sunset);
    assert(input.atmosphereFade == sample.atmosphereFade);
    assert(input.altitude == sample.altitude);
    assert(input.forest && input.nearWater);
    assert(!input.underwater && !input.sheltered && !input.shipInterior);
}

static void TestRuntimeTransitionsAndReset(void)
{
    EnvironmentPresentationRuntime runtime = { 0 };
    EnvironmentRuntimeSample sample = RainSample();
    EnvironmentPresentationState rain = EnvironmentPresentationRuntimeUpdate(
        &runtime, &sample, 1.0f / 60.0f);
    assert(runtime.ready);
    assert(rain.wetness > 0.8f);

    sample.weather = (WeatherVisualState){ 0 };
    EnvironmentPresentationState drying = EnvironmentPresentationRuntimeUpdate(
        &runtime, &sample, 1.0f);
    assert(drying.wetness < rain.wetness);
    assert(drying.wetness > 0.0f);
    assert(isfinite(drying.exposure));

    EnvironmentPresentationRuntimeReset(&runtime);
    assert(!runtime.ready);
    EnvironmentPresentationState clear = EnvironmentPresentationRuntimeUpdate(
        &runtime, &sample, 1.0f / 60.0f);
    assert(clear.wetness == 0.0f);
}

int main(void)
{
    TestDimensionMapping();
    TestInputAssembly();
    TestRuntimeTransitionsAndReset();
    puts("environment runtime tests passed");
    return 0;
}
