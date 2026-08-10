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

typedef struct PlanetFaunaRuntimeState {
    float activityRatio;
    float movementScale;
    float animationScale;
    float visualScale;
    float visualPresence;
    bool dormant;
} PlanetFaunaRuntimeState;

typedef struct PlanetFloraRuntimeState {
    float activityRatio;
    float growthScale;
    float visualScale;
    float visualPresence;
    bool dormant;
} PlanetFloraRuntimeState;

typedef struct PlanetPopulationInput {
    float floraCapacity;
    float faunaCapacity;
    float floraActivity;
    float faunaActivity;
} PlanetPopulationInput;

typedef struct PlanetRegionalPopulation {
    float floraDensity;
    float faunaDensity;
    float floraCarryingCapacity;
    float faunaCarryingCapacity;
    float seasonalMemory;
} PlanetRegionalPopulation;

typedef struct PlanetMigrationHabitat {
    float floraSuitability;
    float faunaSuitability;
    float stormPressure;
} PlanetMigrationHabitat;

typedef struct PlanetPopulationMigrationFlux {
    float flora;
    float fauna;
} PlanetPopulationMigrationFlux;

typedef enum PlanetHabitatDirection {
    PLANET_HABITAT_NONE = 0,
    PLANET_HABITAT_NORTH,
    PLANET_HABITAT_EAST,
    PLANET_HABITAT_SOUTH,
    PLANET_HABITAT_WEST
} PlanetHabitatDirection;

typedef struct PlanetHabitatChoice {
    float currentActivity;
    float selectedActivity;
    float improvement;
    PlanetHabitatDirection direction;
    bool shouldSeek;
} PlanetHabitatChoice;

PlanetLifeHistory PlanetLifeHistoryDerive(uint32_t seed, float planetAgeGyr,
                                          float environmentalSupport,
                                          bool hasSolidSurface);
float PlanetLifeHistoryDensity(const PlanetLifeHistory *history,
                               float environmentalSupport);
PlanetEcologySuitability PlanetEcologyEvaluateLocal(
    const PlanetLocalEnvironment *environment,
    const PlanetEcologyTraits *traits,
    float globalFloraPotential, float globalFaunaPotential);
PlanetFaunaRuntimeState PlanetEcologyFaunaRuntime(float faunaActivity,
                                                   float faunaCapacity);
PlanetFloraRuntimeState PlanetEcologyFloraRuntime(float floraActivity,
                                                   float floraCapacity);
float PlanetEcologyWindDrift(float windStrength, bool airborne);
PlanetRegionalPopulation PlanetPopulationInitialize(
    const PlanetPopulationInput *input, float floraOccupancy,
    float faunaOccupancy);
void PlanetPopulationAdvance(PlanetRegionalPopulation *population,
                             const PlanetPopulationInput *input,
                             double elapsedTime);
float PlanetPopulationFloraPresence(
    const PlanetRegionalPopulation *population);
float PlanetPopulationFaunaPresence(
    const PlanetRegionalPopulation *population);
PlanetPopulationMigrationFlux PlanetPopulationMigrationBetween(
    const PlanetRegionalPopulation *first,
    const PlanetMigrationHabitat *firstHabitat,
    const PlanetRegionalPopulation *second,
    const PlanetMigrationHabitat *secondHabitat,
    float windFromFirstToSecond, double elapsedTime);
PlanetHabitatChoice PlanetEcologyChooseHabitat(
    float currentActivity, const float neighborActivities[4]);
const char *PlanetEcologyLimitingFactorName(PlanetEcologyLimitingFactor factor);

#endif
