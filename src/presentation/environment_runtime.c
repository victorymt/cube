#include "presentation/environment_runtime.h"

EnvironmentScene EnvironmentSceneForDimension(WorldDimension dimension)
{
    switch (dimension) {
        case WORLD_DIMENSION_PLANET: return ENVIRONMENT_SCENE_PLANET;
        case WORLD_DIMENSION_SPACE: return ENVIRONMENT_SCENE_SPACE;
        case WORLD_DIMENSION_NETHER: return ENVIRONMENT_SCENE_NETHER;
        case WORLD_DIMENSION_HOME:
        default: return ENVIRONMENT_SCENE_HOME;
    }
}

EnvironmentPresentationInput EnvironmentPresentationInputForSample(
    const EnvironmentRuntimeSample *sample)
{
    EnvironmentPresentationInput input = { 0 };
    if (!sample) return input;

    input.scene = EnvironmentSceneForDimension(sample->dimension);
    input.quality = sample->quality;
    input.weather = sample->weather;
    input.simulationTime = sample->simulationTime;
    input.daylight = sample->daylight;
    input.sunset = sample->sunset;
    input.atmosphereFade = sample->atmosphereFade;
    input.altitude = sample->altitude;
    input.underwaterDepth = sample->underwaterDepth;
    input.underwater = sample->underwater;
    input.sheltered = sample->sheltered;
    input.forest = sample->forest;
    input.nearWater = sample->nearWater;
    input.shipInterior = sample->shipInterior;
    input.tornado = sample->tornado;
    input.tornadoDistance = sample->tornadoDistance;
    input.tornadoExposure = sample->tornadoExposure;
    input.fireHeatExposure = sample->fireHeatExposure;
    input.fireSmokeExposure = sample->fireSmokeExposure;
    input.nearestFireDistance = sample->nearestFireDistance;
    return input;
}

void EnvironmentPresentationRuntimeReset(
    EnvironmentPresentationRuntime *runtime)
{
    if (runtime) *runtime = (EnvironmentPresentationRuntime){ 0 };
}

EnvironmentPresentationState EnvironmentPresentationRuntimeUpdate(
    EnvironmentPresentationRuntime *runtime,
    const EnvironmentRuntimeSample *sample, float dt)
{
    EnvironmentPresentationInput input =
        EnvironmentPresentationInputForSample(sample);
    EnvironmentPresentationState target =
        EnvironmentPresentationEvaluate(&input);
    if (!runtime) return target;

    if (!runtime->ready) {
        runtime->state = target;
        runtime->ready = true;
    } else {
        runtime->state = EnvironmentPresentationAdvance(
            runtime->state, target, dt);
    }
    return runtime->state;
}
