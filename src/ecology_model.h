#ifndef VOXELCRAFT_ECOLOGY_MODEL_H
#define VOXELCRAFT_ECOLOGY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct PlanetLifeHistory {
    float planetAgeGyr;
    float originProbability;
    float originRoll;
    float complexLifeProbability;
    float complexLifeRoll;
    float evolutionProgress;
    bool lifeOriginated;
    bool hasComplexLife;
} PlanetLifeHistory;

typedef enum PlanetEcologyLimitingFactor {
    PLANET_ECOLOGY_LIMIT_NONE = 0,
    PLANET_ECOLOGY_LIMIT_WATER,
    PLANET_ECOLOGY_LIMIT_TEMPERATURE,
    PLANET_ECOLOGY_LIMIT_LIGHT,
    PLANET_ECOLOGY_LIMIT_STORM,
    PLANET_ECOLOGY_LIMIT_TERRAIN,
    PLANET_ECOLOGY_LIMIT_SEASON
} PlanetEcologyLimitingFactor;

typedef struct PlanetLocalEnvironment {
    float meanTemperatureK;
    float currentTemperatureK;
    float seasonalAmplitudeK;
    float liquidWaterAccess;
    float soilMoisture;
    float meanPrecipitation;
    float precipitationRate;
    float meanUsableLight;
    float currentUsableLight;
    float stormExposure;
    float currentStorm;
    float elevation;
    float slope;
    float shelter;
    float biomeSupport;
} PlanetLocalEnvironment;

typedef struct PlanetEcologyTraits {
    float preferredTemperatureK;
    float temperatureToleranceK;
    float waterDependence;
    float lightDependence;
    float stormResistance;
    float altitudeTolerance;
    float slopeTolerance;
    float foodWebDependence;
    float nocturnalFraction;
} PlanetEcologyTraits;

typedef struct PlanetEcologySuitability {
    float carryingCapacity;
    float floraCapacity;
    float faunaCapacity;
    float floraActivity;
    float faunaActivity;
    float waterScore;
    float temperatureScore;
    float lightScore;
    float stormScore;
    float terrainScore;
    float seasonScore;
    PlanetEcologyLimitingFactor limitingFactor;
} PlanetEcologySuitability;

PlanetLifeHistory PlanetLifeHistoryDerive(uint32_t seed, float planetAgeGyr,
                                          float environmentalSupport,
                                          bool hasSolidSurface);
float PlanetLifeHistoryDensity(const PlanetLifeHistory *history,
                               float environmentalSupport);
PlanetEcologySuitability PlanetEcologyEvaluateLocal(
    const PlanetLocalEnvironment *environment,
    const PlanetEcologyTraits *traits,
    float globalFloraPotential, float globalFaunaPotential);
const char *PlanetEcologyLimitingFactorName(PlanetEcologyLimitingFactor factor);

#endif
