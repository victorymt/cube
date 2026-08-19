#ifndef VOXELCRAFT_ENVIRONMENT_PRESENTATION_H
#define VOXELCRAFT_ENVIRONMENT_PRESENTATION_H

#include "presentation/render_quality.h"
#include "presentation/weather_visual.h"
#include "world/tornado_model.h"

#include <stdbool.h>

#define UNDERWATER_DEEP_REFERENCE_DEPTH 200.0f

typedef enum EnvironmentScene {
    ENVIRONMENT_SCENE_HOME = 0,
    ENVIRONMENT_SCENE_PLANET,
    ENVIRONMENT_SCENE_SPACE,
    ENVIRONMENT_SCENE_NETHER
} EnvironmentScene;

typedef struct EnvironmentPresentationInput {
    EnvironmentScene scene;
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
} EnvironmentPresentationInput;

typedef struct EnvironmentPresentationState {
    EnvironmentScene scene;
    float exposure;
    float saturation;
    float warmth;
    float skyDarkening;
    float directLightScale;
    float ambientLightScale;
    float fogDensity;
    float fogStart;
    float underwaterAmount;
    float underwaterDepth;
    float causticStrength;
    float wetness;
    float waveStrength;
    float cloudOpacity;
    float cloudDistanceScale;
    int cloudRaySteps;
    int cloudLightSteps;
    float precipitation;
    float snowFraction;
    float hailFraction;
    float dust;
    float frost;
    float rainbow;
    float aurora;
    float lightningFlash;
    float starVisibility;
    float audioRain;
    float audioWind;
    float audioForest;
    float audioWater;
    float audioCave;
    float audioNether;
    float audioShip;
    float audioTornado;
} EnvironmentPresentationState;

EnvironmentPresentationState EnvironmentPresentationEvaluate(
    const EnvironmentPresentationInput *input);
EnvironmentPresentationState EnvironmentPresentationAdvance(
    EnvironmentPresentationState current,
    EnvironmentPresentationState target, float dt);

#endif
