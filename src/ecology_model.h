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

PlanetLifeHistory PlanetLifeHistoryDerive(uint32_t seed, float planetAgeGyr,
                                          float environmentalSupport,
                                          bool hasSolidSurface);
float PlanetLifeHistoryDensity(const PlanetLifeHistory *history,
                               float environmentalSupport);

#endif
