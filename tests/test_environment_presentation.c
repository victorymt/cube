#include "environment_presentation.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static EnvironmentPresentationInput StormInput(void)
{
    return (EnvironmentPresentationInput){
        .scene = ENVIRONMENT_SCENE_HOME,
        .quality = GRAPHICS_QUALITY_MEDIUM,
        .weather = {
            .active = true,
            .daylight = 0.55f,
            .cloudOpacity = 0.84f,
            .fogDensity = 0.62f,
            .visibility = 0.44f,
            .precipitationVeil = 0.78f,
            .stormDarkening = 0.72f,
            .windDrift = 0.81f,
            .snowFraction = 0.0f
        },
        .simulationTime = 17.02,
        .daylight = 0.55f,
        .sunset = 0.1f,
        .forest = true,
        .nearWater = true
    };
}

static void TestSceneContracts(void)
{
    EnvironmentPresentationInput input = StormInput();
    EnvironmentPresentationState open = EnvironmentPresentationEvaluate(&input);
    assert(open.precipitation > 0.5f);
    assert(open.wetness > 0.5f);
    assert(open.audioRain > 0.5f);
    assert(open.fogDensity > 0.0f);
    assert(open.lightningFlash > 0.0f);

    input.sheltered = true;
    EnvironmentPresentationState sheltered = EnvironmentPresentationEvaluate(&input);
    assert(sheltered.precipitation < open.precipitation);
    assert(sheltered.audioRain < open.audioRain);
    assert(sheltered.lightningFlash == 0.0f);
    assert(sheltered.audioCave > 0.0f);

    input.scene = ENVIRONMENT_SCENE_SPACE;
    input.shipInterior = false;
    EnvironmentPresentationState vacuum = EnvironmentPresentationEvaluate(&input);
    assert(vacuum.fogDensity == 0.0f);
    assert(vacuum.audioRain == 0.0f);
    assert(vacuum.audioWind == 0.0f);
    assert(vacuum.audioShip == 0.0f);
    input.shipInterior = true;
    assert(EnvironmentPresentationEvaluate(&input).audioShip > 0.0f);

    input.scene = ENVIRONMENT_SCENE_NETHER;
    EnvironmentPresentationState nether = EnvironmentPresentationEvaluate(&input);
    assert(nether.audioNether > 0.0f);
    assert(nether.audioCave == 0.0f);
    assert(nether.starVisibility == 0.0f);
}

static void TestQualityAndTransitions(void)
{
    EnvironmentPresentationInput input = StormInput();
    input.quality = GRAPHICS_QUALITY_LOW;
    EnvironmentPresentationState lowState = EnvironmentPresentationEvaluate(&input);
    float low = lowState.precipitation;
    input.quality = GRAPHICS_QUALITY_HIGH;
    EnvironmentPresentationState target = EnvironmentPresentationEvaluate(&input);
    assert(target.precipitation > low);
    assert(target.cloudRaySteps > lowState.cloudRaySteps);
    assert(target.cloudLightSteps > lowState.cloudLightSteps);

    EnvironmentPresentationState current = target;
    current.wetness = 1.0f;
    target.wetness = 0.0f;
    EnvironmentPresentationState drying = EnvironmentPresentationAdvance(
        current, target, 1.0f);
    assert(drying.wetness < 1.0f && drying.wetness > 0.8f);
    assert(isfinite(drying.exposure));
}

static void TestDeepWaterAttenuation(void)
{
    EnvironmentPresentationInput input = StormInput();
    input.underwater = true;
    input.underwaterDepth = 2.0f;
    EnvironmentPresentationState shallow = EnvironmentPresentationEvaluate(&input);
    assert(shallow.cloudOpacity == 0.0f);
    assert(shallow.precipitation == 0.0f);
    assert(shallow.lightningFlash == 0.0f);
    assert(shallow.starVisibility == 0.0f);
    input.underwaterDepth = 64.0f;
    EnvironmentPresentationState deep = EnvironmentPresentationEvaluate(&input);
    input.underwaterDepth = UNDERWATER_DEEP_REFERENCE_DEPTH;
    EnvironmentPresentationState trench = EnvironmentPresentationEvaluate(&input);
    assert(deep.fogDensity > shallow.fogDensity);
    assert(deep.fogStart < shallow.fogStart);
    assert(deep.exposure < shallow.exposure);
    assert(deep.directLightScale < shallow.directLightScale);
    assert(trench.fogDensity >= deep.fogDensity);
    assert(trench.exposure <= deep.exposure);
    assert(trench.causticStrength <= deep.causticStrength);
}

int main(void)
{
    TestSceneContracts();
    TestQualityAndTransitions();
    TestDeepWaterAttenuation();
    puts("environment presentation tests passed");
    return 0;
}
