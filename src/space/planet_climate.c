#include "space/planet_climate.h"

#include <math.h>

#define PLANET_CLIMATE_HALF_PI 1.57079632679489661923f

static float ClimateClamp(float value, float minimum, float maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float ClimateSmoothStep(float minimum, float maximum, float value)
{
    if (maximum <= minimum) return value >= maximum ? 1.0f : 0.0f;
    float t = ClimateClamp((value - minimum) / (maximum - minimum), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

typedef struct ClimateFeedback {
    float liquidWater;
    float ice;
    float cloud;
    float albedo;
    float opticalDepth;
} ClimateFeedback;

static bool PlanetClimateStateIsValid(const PlanetClimateState *state)
{
    return state && isfinite(state->surfacePressureAtm) &&
           state->surfacePressureAtm >= 0.0f &&
           isfinite(state->atmosphereDensity) &&
           state->atmosphereDensity >= 0.0f &&
           state->atmosphereDensity <= 1.0f && isfinite(state->albedo) &&
           state->albedo >= 0.03f && state->albedo <= 0.88f &&
           isfinite(state->greenhouseOpticalDepth) &&
           state->greenhouseOpticalDepth >= 0.0f &&
           state->greenhouseOpticalDepth <= 3.0f &&
           isfinite(state->radiativeTemperatureK) &&
           state->radiativeTemperatureK > 0.0f &&
           isfinite(state->surfaceTemperatureK) &&
           state->surfaceTemperatureK >= state->radiativeTemperatureK &&
           isfinite(state->liquidWaterCoverage) &&
           state->liquidWaterCoverage >= 0.0f &&
           state->liquidWaterCoverage <= 1.0f &&
           isfinite(state->iceCoverage) && state->iceCoverage >= 0.0f &&
           state->iceCoverage <= 1.0f && isfinite(state->cloudCoverage) &&
           state->cloudCoverage >= 0.0f && state->cloudCoverage <= 1.0f &&
           isfinite(state->windStrength) && state->windStrength >= 0.0f &&
           state->windStrength <= 1.0f &&
           isfinite(state->seasonalTemperatureAmplitudeK) &&
           state->seasonalTemperatureAmplitudeK >= 0.0f &&
           isfinite(state->orbitalTemperatureAmplitudeK) &&
           state->orbitalTemperatureAmplitudeK >= 0.0f &&
           isfinite(state->polarIceVariability) &&
           state->polarIceVariability >= 0.0f &&
           state->polarIceVariability <= 1.0f &&
           isfinite(state->seasonalHumidityBias) &&
           state->seasonalHumidityBias >= 0.0f &&
           state->seasonalHumidityBias <= 1.0f &&
           isfinite(state->absorbedIrradianceEarth) &&
           state->absorbedIrradianceEarth > 0.0;
}

static ClimateFeedback ClimateFeedbackAt(
    const PlanetClimateInput *input, float pressureAtm, float atmosphereDensity,
    float waterInventory, float surfaceTemperatureK)
{
    float boilingPointK = 373.15f + 28.0f * logf(fmaxf(pressureAtm, 0.006f));
    boilingPointK = ClimateClamp(boilingPointK, 315.0f, 460.0f);
    float aboveTriplePoint = ClimateSmoothStep(0.004f, 0.020f, pressureAtm);
    float melt = ClimateSmoothStep(250.0f, 278.0f, surfaceTemperatureK);
    float boil = ClimateSmoothStep(boilingPointK - 24.0f,
                                   boilingPointK + 12.0f,
                                   surfaceTemperatureK);
    float freeze = 1.0f - ClimateSmoothStep(244.0f, 274.0f,
                                            surfaceTemperatureK);
    float steam = ClimateSmoothStep(boilingPointK - 30.0f,
                                    boilingPointK + 18.0f,
                                    surfaceTemperatureK);

    ClimateFeedback feedback = { 0 };
    if (!input->gasGiant) {
        feedback.liquidWater = ClimateClamp(
            waterInventory * aboveTriplePoint * melt * (1.0f - boil) * 0.82f,
            0.0f, 0.82f);
        feedback.ice = ClimateClamp(waterInventory * freeze * 0.92f,
                                    0.0f, 0.92f);
    }

    float evaporation = feedback.liquidWater *
                        ClimateSmoothStep(260.0f, 318.0f,
                                          surfaceTemperatureK);
    feedback.cloud = atmosphereDensity *
                     (evaporation * 0.78f + steam * waterInventory * 0.72f +
                      feedback.ice * 0.12f);
    if (input->gasGiant) {
        feedback.cloud = 0.62f + input->volatileInventory * 0.28f;
    }
    feedback.cloud = ClimateClamp(feedback.cloud, 0.0f, 1.0f);

    feedback.albedo = input->surfaceReflectivity -
                      feedback.liquidWater * 0.09f +
                      feedback.ice * 0.52f + feedback.cloud * 0.28f;
    if (input->gasGiant) {
        feedback.albedo = input->surfaceReflectivity + feedback.cloud * 0.22f;
    }
    feedback.albedo = ClimateClamp(feedback.albedo, 0.03f, 0.88f);

    float pressureDepth = log1pf(pressureAtm * 2.0f) / logf(3.0f);
    float waterVapor = evaporation * 0.58f + steam * waterInventory * 0.78f;
    feedback.opticalDepth = atmosphereDensity * pressureDepth *
                            (0.08f + input->greenhouseGasFraction * 1.16f) +
                            waterVapor;
    if (input->gasGiant) feedback.opticalDepth += 0.62f;
    feedback.opticalDepth = ClimateClamp(feedback.opticalDepth, 0.0f, 3.0f);
    return feedback;
}

bool PlanetClimateSolve(const PlanetClimateInput *input,
                        PlanetClimateState *out)
{
    if (!out) return false;
    *out = (PlanetClimateState){ 0 };
    if (!input || !(input->stellarIrradianceEarth > 0.0) ||
        !isfinite(input->stellarIrradianceEarth) ||
        !isfinite(input->axialTiltRad) ||
        !isfinite(input->orbitalEccentricity) ||
        !isfinite(input->volatileInventory) ||
        !isfinite(input->greenhouseGasFraction) ||
        !isfinite(input->surfaceReflectivity) ||
        !isfinite(input->surfaceGravityEarth) ||
        !isfinite(input->rotationRate) || !isfinite(input->tidalLockFactor)) {
        return false;
    }

    PlanetClimateInput climate = *input;
    climate.stellarIrradianceEarth = fmax(climate.stellarIrradianceEarth, 0.00001);
    climate.axialTiltRad = ClimateClamp(
        fabsf(climate.axialTiltRad), 0.0f, PLANET_CLIMATE_HALF_PI);
    climate.orbitalEccentricity = ClimateClamp(
        climate.orbitalEccentricity, 0.0f, 0.95f);
    climate.volatileInventory = ClimateClamp(climate.volatileInventory, 0.0f, 1.0f);
    climate.greenhouseGasFraction = ClimateClamp(climate.greenhouseGasFraction,
                                                 0.0f, 1.0f);
    climate.surfaceReflectivity = ClimateClamp(climate.surfaceReflectivity,
                                               0.03f, 0.72f);
    climate.surfaceGravityEarth = ClimateClamp(climate.surfaceGravityEarth,
                                               0.05f, 4.0f);
    climate.rotationRate = ClimateClamp(climate.rotationRate, 0.0f, 12.0f);
    climate.tidalLockFactor = ClimateClamp(climate.tidalLockFactor, 0.0f, 1.0f);

    float unshieldedTemperature = 278.5f *
        powf((float)climate.stellarIrradianceEarth, 0.25f);
    float gravityRetention = ClimateSmoothStep(0.18f, 1.35f,
                                               climate.surfaceGravityEarth);
    float thermalEscape = ClimateSmoothStep(315.0f, 820.0f,
                                            unshieldedTemperature);
    float coldRetention = 0.42f + 0.58f *
                          ClimateSmoothStep(90.0f, 185.0f,
                                            unshieldedTemperature);
    float retention = (0.10f + gravityRetention * 0.90f) * coldRetention;
    retention *= 1.0f - thermalEscape * (0.82f - gravityRetention * 0.48f);
    retention = ClimateClamp(retention, 0.0f, 1.0f);

    float pressureAtm;
    if (climate.gasGiant) {
        pressureAtm = 8.0f + climate.volatileInventory * 10.0f;
    } else {
        pressureAtm = powf(climate.volatileInventory, 1.35f) *
                      (0.04f + retention * 2.20f);
        pressureAtm += climate.volatileInventory *
                       climate.greenhouseGasFraction * gravityRetention * 0.24f;
        pressureAtm = ClimateClamp(pressureAtm, 0.0f, 5.0f);
    }
    float atmosphereDensity = pressureAtm / (pressureAtm + 0.30f);
    atmosphereDensity = ClimateClamp(atmosphereDensity, 0.0f, 1.0f);
    float waterInventory = climate.gasGiant ? 0.0f :
        climate.volatileInventory *
        (0.34f + (1.0f - climate.greenhouseGasFraction) * 0.52f);
    waterInventory = ClimateClamp(waterInventory, 0.0f, 0.90f);

    float albedo = ClimateClamp(climate.surfaceReflectivity, 0.03f, 0.88f);
    float surfaceTemperature = unshieldedTemperature;
    ClimateFeedback feedback = { 0 };
    float radiativeTemperature = unshieldedTemperature;
    for (int iteration = 0; iteration < 10; iteration++) {
        feedback = ClimateFeedbackAt(&climate, pressureAtm,
                                     atmosphereDensity, waterInventory,
                                     surfaceTemperature);
        albedo = albedo * 0.42f + feedback.albedo * 0.58f;
        double absorbed = climate.stellarIrradianceEarth *
                          fmax(1.0 - (double)albedo, 0.01);
        radiativeTemperature = 278.5f * powf((float)absorbed, 0.25f);
        float targetSurface = radiativeTemperature *
            powf(1.0f + 0.75f * feedback.opticalDepth, 0.25f);
        surfaceTemperature = surfaceTemperature * 0.35f + targetSurface * 0.65f;
    }
    feedback = ClimateFeedbackAt(&climate, pressureAtm, atmosphereDensity,
                                 waterInventory, surfaceTemperature);
    albedo = feedback.albedo;
    double absorbed = climate.stellarIrradianceEarth *
                      fmax(1.0 - (double)albedo, 0.01);
    radiativeTemperature = 278.5f * powf((float)absorbed, 0.25f);
    surfaceTemperature = radiativeTemperature *
        powf(1.0f + 0.75f * feedback.opticalDepth, 0.25f);

    float radiativeForcing = ClimateClamp(
        fabsf(surfaceTemperature - radiativeTemperature) / 120.0f, 0.0f, 1.0f);
    float rotationForcing = ClimateClamp(climate.rotationRate / 5.0f, 0.0f, 1.0f);
    float stellarForcing = ClimateClamp(
        sqrtf((float)climate.stellarIrradianceEarth) * 0.42f, 0.0f, 1.0f);
    float wind = atmosphereDensity *
                 (0.12f + radiativeForcing * 0.34f +
                  rotationForcing * 0.24f + stellarForcing * 0.18f +
                  climate.tidalLockFactor * 0.22f);
    if (climate.gasGiant) wind += 0.28f;

    float thermalBuffer = ClimateClamp(
        atmosphereDensity * 0.48f + feedback.liquidWater * 0.72f +
            feedback.cloud * 0.18f,
        0.0f, 0.88f);
    float seasonalAmplitude = climate.gasGiant ? 0.0f :
        sinf(climate.axialTiltRad) *
            (38.0f + 36.0f * (1.0f - thermalBuffer));
    float eccentricityForcing = ClimateClamp(
        2.0f * climate.orbitalEccentricity /
            fmaxf(1.0f - climate.orbitalEccentricity *
                              climate.orbitalEccentricity,
                  0.05f),
        0.0f, 3.0f);
    float orbitalAmplitude = climate.gasGiant ? 0.0f :
        surfaceTemperature * 0.25f * eccentricityForcing *
            (1.0f - thermalBuffer * 0.72f);
    float polarIceVariability = climate.gasGiant ? 0.0f : ClimateClamp(
        (seasonalAmplitude + orbitalAmplitude * 0.45f) / 58.0f *
            (0.35f + feedback.ice * 0.65f),
        0.0f, 1.0f);
    float seasonalHumidityBias = climate.gasGiant ? 0.0f : ClimateClamp(
        (seasonalAmplitude + orbitalAmplitude * 0.25f) / 82.0f *
            (0.25f + feedback.liquidWater * 0.75f),
        0.0f, 1.0f);

    PlanetClimateState solved = {
        .surfacePressureAtm = pressureAtm,
        .atmosphereDensity = atmosphereDensity,
        .albedo = albedo,
        .greenhouseOpticalDepth = feedback.opticalDepth,
        .radiativeTemperatureK = radiativeTemperature,
        .surfaceTemperatureK = surfaceTemperature,
        .liquidWaterCoverage = feedback.liquidWater,
        .iceCoverage = feedback.ice,
        .cloudCoverage = feedback.cloud,
        .windStrength = ClimateClamp(wind, 0.0f, 1.0f),
        .seasonalTemperatureAmplitudeK = seasonalAmplitude,
        .orbitalTemperatureAmplitudeK = orbitalAmplitude,
        .polarIceVariability = polarIceVariability,
        .seasonalHumidityBias = seasonalHumidityBias,
        .absorbedIrradianceEarth = absorbed
    };
    if (!PlanetClimateStateIsValid(&solved)) return false;
    *out = solved;
    return true;
}
