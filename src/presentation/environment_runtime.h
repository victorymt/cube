#ifndef VOXELCRAFT_ENVIRONMENT_RUNTIME_H
#define VOXELCRAFT_ENVIRONMENT_RUNTIME_H

#include "presentation/environment_presentation.h"
#include "world/world_environment.h"
#include "world/tornado_model.h"

typedef struct EnvironmentRuntimeSample {
    WorldDimension dimension;
    GraphicsQuality quality;
    WeatherVisualState weather;
    double simulationTime;
    float daylight;
    float sunset;
    float atmosphereFade;
    float altitude;
    float underwaterDepth;
    bool underwater;
    bool sheltered;
    bool forest;
    bool nearWater;
    bool shipInterior;
    TornadoState tornado;
    float tornadoDistance;
    float tornadoExposure;
    float fireHeatExposure;
    float fireSmokeExposure;
    float nearestFireDistance;
} EnvironmentRuntimeSample;

typedef struct EnvironmentPresentationRuntime {
    EnvironmentPresentationState state;
    bool ready;
} EnvironmentPresentationRuntime;

EnvironmentScene EnvironmentSceneForDimension(WorldDimension dimension);
EnvironmentPresentationInput EnvironmentPresentationInputForSample(
    const EnvironmentRuntimeSample *sample);
void EnvironmentPresentationRuntimeReset(
    EnvironmentPresentationRuntime *runtime);
EnvironmentPresentationState EnvironmentPresentationRuntimeUpdate(
    EnvironmentPresentationRuntime *runtime,
    const EnvironmentRuntimeSample *sample, float dt);

#endif
