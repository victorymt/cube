#include "ecology_model.h"

#include <math.h>

static float EcologyModelClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static uint32_t EcologyModelMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float EcologyModelUnit(uint32_t seed, uint32_t lane)
{
    uint32_t hash = EcologyModelMix(seed ^ (lane * 0x9e3779b9u));
    return (float)(hash & 0x00ffffffu) / 16777215.0f;
}

static float EcologyModelTemperatureResponse(float temperatureK,
                                             float preferredK,
                                             float toleranceK)
{
    float tolerance = fmaxf(toleranceK, 1.0f);
    float distance = (temperatureK - preferredK) / tolerance;
    return expf(-0.5f * distance * distance);
}

static float EcologyModelLerp(float start, float end, float amount)
{
    return start + (end - start) * EcologyModelClamp(amount);
}

static void EcologyModelChooseLimit(PlanetEcologySuitability *result,
                                    float score,
                                    PlanetEcologyLimitingFactor factor,
                                    float *lowest)
{
    if (score < *lowest) {
        *lowest = score;
        result->limitingFactor = factor;
    }
}

PlanetLifeHistory PlanetLifeHistoryDerive(uint32_t seed, float planetAgeGyr,
                                          float environmentalSupport,
                                          bool hasSolidSurface)
{
    PlanetLifeHistory result = { 0 };
    result.planetAgeGyr = fmaxf(planetAgeGyr, 0.0f);
    result.originRoll = EcologyModelUnit(seed, 0x51f15eu);
    result.complexLifeRoll = EcologyModelUnit(seed, 0xc0a1e5u);

    float support = EcologyModelClamp(environmentalSupport);
    if (!hasSolidSurface || support < 0.015f) return result;

    float originOpportunity = EcologyModelClamp((result.planetAgeGyr - 0.10f) / 3.90f);
    result.originProbability = EcologyModelClamp(
        powf(support, 1.45f) * (0.04f + originOpportunity * 0.48f));
    result.lifeOriginated = result.originRoll < result.originProbability;
    if (!result.lifeOriginated) return result;

    result.evolutionProgress = EcologyModelClamp(
        (result.planetAgeGyr - 0.10f) / 4.40f);
    float complexOpportunity = EcologyModelClamp(
        (result.planetAgeGyr - 1.25f) / 4.75f);
    result.complexLifeProbability = EcologyModelClamp(
        0.36f * powf(support, 1.35f) * complexOpportunity);
    result.hasComplexLife = result.complexLifeRoll <
                            result.complexLifeProbability;
    return result;
}

float PlanetLifeHistoryDensity(const PlanetLifeHistory *history,
                               float environmentalSupport)
{
    if (!history || !history->lifeOriginated) return 0.0f;

    float density = EcologyModelClamp(environmentalSupport) *
                    (0.16f + 0.84f * history->evolutionProgress);
    if (!history->hasComplexLife && density > 0.19f) density = 0.19f;
    return EcologyModelClamp(density);
}

PlanetEcologySuitability PlanetEcologyEvaluateLocal(
    const PlanetLocalEnvironment *environment,
    const PlanetEcologyTraits *traits,
    float globalFloraPotential, float globalFaunaPotential)
{
    PlanetEcologySuitability result = { 0 };
    if (!environment || !traits) return result;

    float waterDependence = EcologyModelClamp(traits->waterDependence);
    float lightDependence = EcologyModelClamp(traits->lightDependence);
    float waterSignal = EcologyModelClamp(
        environment->liquidWaterAccess * 0.50f +
        environment->soilMoisture * 0.30f +
        environment->meanPrecipitation * 0.20f);
    result.waterScore = EcologyModelLerp(0.68f, sqrtf(waterSignal),
                                         waterDependence);

    result.temperatureScore = EcologyModelTemperatureResponse(
        environment->meanTemperatureK, traits->preferredTemperatureK,
        traits->temperatureToleranceK);
    result.seasonScore = 0.0f;
    for (int sample = 0; sample < 12; sample++) {
        float phase = (2.0f * 3.14159265358979323846f * (float)sample) / 12.0f;
        float seasonalTemperature = environment->meanTemperatureK +
            sinf(phase) * fmaxf(environment->seasonalAmplitudeK, 0.0f);
        result.seasonScore += EcologyModelTemperatureResponse(
            seasonalTemperature, traits->preferredTemperatureK,
            traits->temperatureToleranceK);
    }
    result.seasonScore /= 12.0f;

    result.lightScore = EcologyModelLerp(
        0.76f, sqrtf(EcologyModelClamp(environment->meanUsableLight)),
        lightDependence);
    float stormResistance = EcologyModelClamp(traits->stormResistance);
    result.stormScore = EcologyModelClamp(
        1.0f - EcologyModelClamp(environment->stormExposure) *
        (1.0f - stormResistance * 0.78f));

    float slopeStress = EcologyModelClamp(environment->slope) *
                        (1.0f - EcologyModelClamp(traits->slopeTolerance));
    float altitudeStress = EcologyModelClamp(environment->elevation) *
                           (1.0f - EcologyModelClamp(traits->altitudeTolerance));
    float terrainShape = EcologyModelClamp(
        1.0f - slopeStress * 0.70f - altitudeStress * 0.38f);
    float shelter = 0.80f + EcologyModelClamp(environment->shelter) * 0.20f;
    result.terrainScore = EcologyModelClamp(
        environment->biomeSupport * terrainShape * shelter);

    result.limitingFactor = PLANET_ECOLOGY_LIMIT_NONE;
    float lowest = 2.0f;
    EcologyModelChooseLimit(&result, result.waterScore,
                            PLANET_ECOLOGY_LIMIT_WATER, &lowest);
    EcologyModelChooseLimit(&result, result.temperatureScore,
                            PLANET_ECOLOGY_LIMIT_TEMPERATURE, &lowest);
    EcologyModelChooseLimit(&result, result.lightScore,
                            PLANET_ECOLOGY_LIMIT_LIGHT, &lowest);
    EcologyModelChooseLimit(&result, result.stormScore,
                            PLANET_ECOLOGY_LIMIT_STORM, &lowest);
    EcologyModelChooseLimit(&result, result.terrainScore,
                            PLANET_ECOLOGY_LIMIT_TERRAIN, &lowest);
    EcologyModelChooseLimit(&result, result.seasonScore,
                            PLANET_ECOLOGY_LIMIT_SEASON, &lowest);

    bool lacksRequiredWater = waterDependence > 0.50f &&
        environment->liquidWaterAccess < 0.01f &&
        environment->soilMoisture < 0.01f &&
        environment->meanPrecipitation < 0.01f;
    if (lacksRequiredWater || result.seasonScore < 0.005f ||
        result.terrainScore <= 0.0f) {
        return result;
    }

    const float scores[] = {
        result.waterScore, result.temperatureScore, result.lightScore,
        result.stormScore, result.terrainScore, result.seasonScore
    };
    const float weights[] = { 0.26f, 0.24f, 0.14f, 0.10f, 0.14f, 0.12f };
    float weightedLog = 0.0f;
    float weightTotal = 0.0f;
    for (int index = 0; index < 6; index++) {
        weightedLog += weights[index] * logf(fmaxf(scores[index], 0.03f));
        weightTotal += weights[index];
    }
    float combined = expf(weightedLog / weightTotal);
    result.carryingCapacity = EcologyModelClamp(
        combined * (0.70f + EcologyModelClamp(lowest) * 0.30f));
    result.floraCapacity = EcologyModelClamp(globalFloraPotential) *
                           result.carryingCapacity;

    float relativeFlora = globalFloraPotential > 0.0001f
        ? result.floraCapacity / globalFloraPotential : 0.0f;
    float foodSupport = EcologyModelLerp(
        0.75f, relativeFlora, traits->foodWebDependence);
    result.faunaCapacity = EcologyModelClamp(globalFaunaPotential) *
                           result.carryingCapacity * foodSupport;

    float currentTemperature = EcologyModelTemperatureResponse(
        environment->currentTemperatureK, traits->preferredTemperatureK,
        traits->temperatureToleranceK);
    float currentLight = EcologyModelClamp(environment->currentUsableLight);
    float producerLight = EcologyModelLerp(0.22f, currentLight,
                                           lightDependence);
    float currentStorm = EcologyModelClamp(environment->currentStorm);
    float stormActivity = EcologyModelClamp(
        1.0f - currentStorm * (0.82f - stormResistance * 0.48f));
    float gentleRain = EcologyModelClamp(environment->precipitationRate) *
                       (1.0f - currentStorm);
    float hydrationActivity = 1.0f + gentleRain * 0.16f;

    result.floraActivity = EcologyModelClamp(
        result.floraCapacity * currentTemperature * producerLight *
        stormActivity * hydrationActivity);

    float producerActivity = result.floraCapacity > 0.0001f
        ? EcologyModelClamp(result.floraActivity / result.floraCapacity) : 0.0f;
    float foodActivity = EcologyModelLerp(
        1.0f, producerActivity, traits->foodWebDependence);

    float daylightActivity = 0.55f + currentLight * 0.45f;
    float darknessActivity = 0.55f + (1.0f - currentLight) * 0.45f;
    float lightActivity = EcologyModelLerp(
        daylightActivity, darknessActivity, traits->nocturnalFraction);
    result.faunaActivity = EcologyModelClamp(
        result.faunaCapacity * (0.35f + currentTemperature * 0.65f) *
        lightActivity * stormActivity * hydrationActivity * foodActivity);
    return result;
}

PlanetFaunaRuntimeState PlanetEcologyFaunaRuntime(float faunaActivity,
                                                   float faunaCapacity)
{
    PlanetFaunaRuntimeState result = {
        .animationScale = 0.08f,
        .visualScale = 0.80f,
        .visualPresence = 0.48f,
        .dormant = true
    };
    if (!isfinite(faunaActivity) || !isfinite(faunaCapacity) ||
        faunaCapacity <= 0.0001f) {
        return result;
    }

    result.activityRatio = EcologyModelClamp(faunaActivity / faunaCapacity);
    float response = EcologyModelClamp((result.activityRatio - 0.08f) / 0.92f);
    response = response * response * (3.0f - 2.0f * response);
    result.dormant = result.activityRatio < 0.14f;
    result.movementScale = result.dormant ? 0.0f : 0.18f + response * 0.82f;
    result.animationScale = 0.08f + response * 0.92f;
    result.visualScale = 0.80f + response * 0.20f;
    result.visualPresence = 0.48f + sqrtf(result.activityRatio) * 0.52f;
    return result;
}

PlanetFloraRuntimeState PlanetEcologyFloraRuntime(float floraActivity,
                                                   float floraCapacity)
{
    PlanetFloraRuntimeState result = {
        .growthScale = 0.06f,
        .visualScale = 0.62f,
        .visualPresence = 0.34f,
        .dormant = true
    };
    if (!isfinite(floraActivity) || !isfinite(floraCapacity) ||
        floraCapacity <= 0.0001f) {
        return result;
    }

    result.activityRatio = EcologyModelClamp(floraActivity / floraCapacity);
    float response = EcologyModelClamp((result.activityRatio - 0.03f) / 0.97f);
    response = response * response * (3.0f - 2.0f * response);
    result.dormant = result.activityRatio < 0.10f;
    result.growthScale = result.dormant ? 0.06f : 0.16f + response * 0.84f;
    result.visualScale = 0.62f + response * 0.38f;
    result.visualPresence = 0.34f + sqrtf(result.activityRatio) * 0.66f;
    return result;
}

PlanetHabitatChoice PlanetEcologyChooseHabitat(
    float currentActivity, const float neighborActivities[4])
{
    PlanetHabitatChoice result = {
        .currentActivity = isfinite(currentActivity)
            ? EcologyModelClamp(currentActivity) : 0.0f,
        .direction = PLANET_HABITAT_NONE
    };
    result.selectedActivity = result.currentActivity;
    if (!neighborActivities) return result;

    for (int index = 0; index < 4; index++) {
        float candidate = isfinite(neighborActivities[index])
            ? EcologyModelClamp(neighborActivities[index]) : 0.0f;
        if (candidate > result.selectedActivity) {
            result.selectedActivity = candidate;
            result.direction = (PlanetHabitatDirection)(index + 1);
        }
    }

    result.improvement = result.selectedActivity - result.currentActivity;
    result.shouldSeek = result.improvement >= 0.06f &&
                        result.selectedActivity >= 0.12f;
    if (!result.shouldSeek) result.direction = PLANET_HABITAT_NONE;
    return result;
}

const char *PlanetEcologyLimitingFactorName(PlanetEcologyLimitingFactor factor)
{
    switch (factor) {
    case PLANET_ECOLOGY_LIMIT_WATER:       return "Water";
    case PLANET_ECOLOGY_LIMIT_TEMPERATURE: return "Temperature";
    case PLANET_ECOLOGY_LIMIT_LIGHT:       return "Light";
    case PLANET_ECOLOGY_LIMIT_STORM:       return "Storm";
    case PLANET_ECOLOGY_LIMIT_TERRAIN:     return "Terrain";
    case PLANET_ECOLOGY_LIMIT_SEASON:      return "Season";
    case PLANET_ECOLOGY_LIMIT_NONE:
    default:                               return "None";
    }
}
