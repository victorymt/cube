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
