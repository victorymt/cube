#include "environment_presentation.h"

#include <math.h>

static float Unit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static float LightningAt(double time, float storm)
{
    if (!isfinite(time) || storm < 0.42f) return 0.0f;
    double phase = fmod(time, 17.0);
    if (phase < 0.0) phase += 17.0;
    float flash = 0.0f;
    if (phase < 0.10) flash = 1.0f - (float)phase / 0.10f;
    else if (phase > 0.18 && phase < 0.25) {
        flash = 0.62f * (1.0f - ((float)phase - 0.18f) / 0.07f);
    }
    return Unit(flash * storm);
}

EnvironmentPresentationState EnvironmentPresentationEvaluate(
    const EnvironmentPresentationInput *input)
{
    EnvironmentPresentationState state = {
        .scene = ENVIRONMENT_SCENE_HOME,
        .exposure = 1.0f,
        .saturation = 1.0f,
        .directLightScale = 1.0f,
        .ambientLightScale = 1.0f,
        .fogStart = 38.0f,
        .waveStrength = 0.18f,
        .cloudDistanceScale = 1.0f,
        .starVisibility = 1.0f
    };
    if (!input) return state;

    state.scene = input->scene;
    GraphicsQualityProfile quality = GraphicsQualityProfileFor(input->quality);
    float daylight = Unit(input->daylight);
    float sunset = Unit(input->sunset);
    float atmosphereFade = Unit(input->atmosphereFade);
    float night = 1.0f - daylight;
    state.exposure = 0.88f + daylight * 0.18f + night * 0.04f;
    state.warmth = sunset * (1.0f - atmosphereFade) * 0.72f;
    state.saturation = 0.92f + daylight * 0.08f;
    state.cloudDistanceScale = 0.72f + 0.14f * (float)quality.cloudGridRadius;
    state.cloudRaySteps = quality.cloudRaySteps;
    state.cloudLightSteps = quality.cloudLightSteps;

    if (input->scene == ENVIRONMENT_SCENE_SPACE) {
        state.exposure = input->shipInterior ? 0.94f : 0.82f;
        state.saturation = 0.88f;
        state.skyDarkening = 1.0f;
        state.directLightScale = 1.12f;
        state.ambientLightScale = 0.24f;
        state.fogDensity = 0.0f;
        state.fogStart = 10000.0f;
        state.waveStrength = 0.0f;
        state.cloudOpacity = 0.0f;
        state.starVisibility = 1.0f;
        state.audioShip = input->shipInterior ? 0.72f : 0.0f;
        return state;
    }
    if (input->scene == ENVIRONMENT_SCENE_NETHER) {
        state.exposure = 0.92f;
        state.saturation = 0.86f;
        state.warmth = 0.78f;
        state.skyDarkening = 0.64f;
        state.directLightScale = 0.18f;
        state.ambientLightScale = 0.72f;
        state.fogDensity = 0.010f;
        state.fogStart = 24.0f;
        state.waveStrength = 0.22f;
        state.starVisibility = 0.0f;
        state.audioNether = 0.82f;
        return state;
    }

    const WeatherVisualState *weather = &input->weather;
    float cloud = weather->active ? Unit(weather->cloudOpacity) : 0.0f;
    float veil = weather->active ? Unit(weather->precipitationVeil) : 0.0f;
    float storm = weather->active ? Unit(weather->stormDarkening) : 0.0f;
    float wind = weather->active ? Unit(weather->windDrift) : 0.0f;
    float shelter = input->sheltered ? 0.12f : 1.0f;
    float rain = veil * (1.0f - Unit(weather->snowFraction));

    state.skyDarkening = Unit(cloud * 0.46f + storm * 0.34f);
    state.directLightScale = 1.0f - Unit(cloud * 0.62f + storm * 0.22f);
    state.ambientLightScale = 1.0f - Unit(storm * 0.24f);
    state.fogDensity = Unit(weather->fogDensity) * 0.014f + veil * 0.004f;
    state.fogStart = 32.0f + Unit(weather->visibility) * 14.0f;
    state.wetness = rain * shelter;
    state.waveStrength = Unit(0.18f + wind * 0.58f + storm * 0.24f);
    state.cloudOpacity = cloud;
    state.precipitation = veil * shelter * quality.precipitationScale;
    state.snowFraction = Unit(weather->snowFraction);
    state.lightningFlash = input->sheltered ? 0.0f :
        LightningAt(input->simulationTime, storm);
    state.starVisibility = Unit(night * (1.0f - cloud) * (1.0f - atmosphereFade));
    state.audioRain = rain * shelter;
    float altitudeWind = Unit(input->altitude / 96.0f);
    state.audioWind = Unit(wind * (0.32f + 0.48f * (1.0f - shelter) +
                                  altitudeWind * 0.20f));
    state.audioForest = input->forest ? Unit(0.22f + daylight * 0.48f) *
                                       (1.0f - rain * 0.62f) : 0.0f;
    state.audioWater = input->nearWater ? Unit(0.24f + state.waveStrength * 0.46f) : 0.0f;
    state.audioCave = input->sheltered && !input->underwater ?
                          Unit(0.16f + night * 0.10f) : 0.0f;
    if (input->underwater) {
        float depth = fmaxf(input->underwaterDepth, 0.0f);
        float deep = Unit(depth / 32.0f);
        state.underwaterAmount = 1.0f;
        state.underwaterDepth = depth;
        state.causticStrength = input->quality == GRAPHICS_QUALITY_LOW ? 0.0f :
            (0.16f + daylight * 0.34f) * (1.0f - deep * 0.82f);
        state.fogDensity = fmaxf(state.fogDensity, 0.018f + deep * 0.025f);
        state.fogStart = 1.2f - deep * 0.75f;
        state.saturation *= 0.86f - deep * 0.28f;
        state.exposure *= 0.90f - deep * 0.35f;
        state.directLightScale *= 1.0f - deep * 0.70f;
        state.ambientLightScale *= 0.94f - deep * 0.40f;
        state.skyDarkening = fmaxf(state.skyDarkening, 0.08f + deep * 0.44f);
        state.audioRain = 0.0f;
        state.audioWind = 0.0f;
        state.audioForest = 0.0f;
        state.audioWater = 0.42f;
        state.audioCave = 0.0f;
    }
    return state;
}

static float Approach(float current, float target, float dt, float speed)
{
    if (!isfinite(current)) current = target;
    float amount = 1.0f - expf(-fmaxf(dt, 0.0f) * speed);
    return current + (target - current) * amount;
}

EnvironmentPresentationState EnvironmentPresentationAdvance(
    EnvironmentPresentationState current,
    EnvironmentPresentationState target, float dt)
{
    if (!isfinite(dt) || dt <= 0.0f || current.scene != target.scene) return target;
#define ADVANCE(field, speed) current.field = Approach(current.field, target.field, dt, speed)
    ADVANCE(exposure, 3.0f);
    ADVANCE(saturation, 2.0f);
    ADVANCE(warmth, 2.4f);
    ADVANCE(skyDarkening, 2.0f);
    ADVANCE(directLightScale, 2.2f);
    ADVANCE(ambientLightScale, 2.2f);
    ADVANCE(fogDensity, 1.8f);
    ADVANCE(fogStart, 1.8f);
    ADVANCE(underwaterAmount, target.underwaterAmount > current.underwaterAmount ?
                               7.0f : 4.0f);
    ADVANCE(underwaterDepth, 4.0f);
    ADVANCE(causticStrength, 3.0f);
    ADVANCE(wetness, target.wetness > current.wetness ? 1.4f : 0.08f);
    ADVANCE(waveStrength, 1.4f);
    ADVANCE(cloudOpacity, 1.2f);
    ADVANCE(cloudDistanceScale, 2.0f);
    ADVANCE(precipitation, 2.4f);
    ADVANCE(snowFraction, 2.0f);
    ADVANCE(lightningFlash, 18.0f);
    ADVANCE(starVisibility, 2.0f);
    ADVANCE(audioRain, 1.8f);
    ADVANCE(audioWind, 1.2f);
    ADVANCE(audioForest, 0.8f);
    ADVANCE(audioWater, 1.0f);
    ADVANCE(audioCave, 1.0f);
    ADVANCE(audioNether, 1.2f);
    ADVANCE(audioShip, 2.4f);
#undef ADVANCE
    current.cloudRaySteps = target.cloudRaySteps;
    current.cloudLightSteps = target.cloudLightSteps;
    return current;
}
