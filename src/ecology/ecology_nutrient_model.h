#ifndef VOXELCRAFT_ECOLOGY_NUTRIENT_MODEL_H
#define VOXELCRAFT_ECOLOGY_NUTRIENT_MODEL_H

#include <stdbool.h>

/* Nitrogen and phosphorus pools are expressed as arbitrary mass units. */
typedef struct EcologyNutrientState {
    double mineralNitrogen;
    double mineralPhosphorus;
    double soilOrganicNitrogen;
    double soilOrganicPhosphorus;
    double plantNitrogen;
    double plantPhosphorus;
    double litterNitrogen;
    double litterPhosphorus;
    double plantBiomass;
    double litterBiomass;
    double seedBank;
    double meanPlantAgeDays;
} EcologyNutrientState;

typedef struct EcologyNutrientParameters {
    double decompositionPerDay;
    double plantTurnoverPerDay;
    double maxGrowthPerDay;
    double germinationPerDay;
    double seedProductionPerBiomass;
    double nitrogenFraction;
    double phosphorusFraction;
    double leachingPerMm;
} EcologyNutrientParameters;

typedef struct EcologyNutrientEnvironment {
    double temperatureSuitability;
    double moistureSuitability;
    double competition;
    double rainfallMm;
} EcologyNutrientEnvironment;

typedef struct EcologyNutrientFlux {
    double mineralizedNitrogen;
    double mineralizedPhosphorus;
    double uptakeNitrogen;
    double uptakePhosphorus;
    double growthBiomass;
    double turnoverBiomass;
    double germinatedSeeds;
    double producedSeeds;
    double leachedNitrogen;
    double leachedPhosphorus;
} EcologyNutrientFlux;

EcologyNutrientParameters EcologyNutrientDefaultParameters(void);
EcologyNutrientEnvironment EcologyNutrientDefaultEnvironment(void);
bool EcologyNutrientStateValid(const EcologyNutrientState *state);
double EcologyNutrientTotalNitrogen(const EcologyNutrientState *state);
double EcologyNutrientTotalPhosphorus(const EcologyNutrientState *state);

/* Advance one off-screen ecological cell by a finite number of days. */
bool EcologyNutrientStep(EcologyNutrientState *state,
                         const EcologyNutrientParameters *parameters,
                         const EcologyNutrientEnvironment *environment,
                         double days,
                         EcologyNutrientFlux *flux);

#endif
