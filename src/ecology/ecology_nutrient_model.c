#include "ecology/ecology_nutrient_model.h"

#include <math.h>
#include <stddef.h>

static double Clamp01(double value)
{
    if (!isfinite(value)) return 0.0;
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

static double NonNegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

static double PositiveOr(double value, double fallback)
{
    return isfinite(value) && value > 0.0 ? value : fallback;
}

static EcologyNutrientParameters SanitizeParameters(
    const EcologyNutrientParameters *input)
{
    EcologyNutrientParameters result = EcologyNutrientDefaultParameters();
    if (input == NULL) return result;
    result = *input;
    result.decompositionPerDay =
        PositiveOr(result.decompositionPerDay, 0.02);
    result.plantTurnoverPerDay =
        PositiveOr(result.plantTurnoverPerDay, 0.004);
    result.maxGrowthPerDay = PositiveOr(result.maxGrowthPerDay, 0.01);
    result.germinationPerDay = PositiveOr(result.germinationPerDay, 0.001);
    result.seedProductionPerBiomass =
        NonNegative(result.seedProductionPerBiomass);
    result.nitrogenFraction = PositiveOr(result.nitrogenFraction, 0.03);
    result.phosphorusFraction = PositiveOr(result.phosphorusFraction, 0.004);
    result.leachingPerMm = NonNegative(result.leachingPerMm);
    return result;
}

static EcologyNutrientEnvironment SanitizeEnvironment(
    const EcologyNutrientEnvironment *input)
{
    EcologyNutrientEnvironment result = EcologyNutrientDefaultEnvironment();
    if (input == NULL) return result;
    result = *input;
    result.temperatureSuitability = Clamp01(result.temperatureSuitability);
    result.moistureSuitability = Clamp01(result.moistureSuitability);
    result.competition = Clamp01(result.competition);
    result.rainfallMm = NonNegative(result.rainfallMm);
    return result;
}

EcologyNutrientParameters EcologyNutrientDefaultParameters(void)
{
    return (EcologyNutrientParameters){
        .decompositionPerDay = 0.02,
        .plantTurnoverPerDay = 0.004,
        .maxGrowthPerDay = 0.01,
        .germinationPerDay = 0.001,
        .seedProductionPerBiomass = 0.02,
        .nitrogenFraction = 0.03,
        .phosphorusFraction = 0.004,
        .leachingPerMm = 0.001,
    };
}

EcologyNutrientEnvironment EcologyNutrientDefaultEnvironment(void)
{
    return (EcologyNutrientEnvironment){
        .temperatureSuitability = 1.0,
        .moistureSuitability = 1.0,
        .competition = 0.0,
        .rainfallMm = 0.0,
    };
}

bool EcologyNutrientStateValid(const EcologyNutrientState *state)
{
    if (state == NULL) return false;
    const double *values = (const double *)state;
    for (size_t index = 0; index < sizeof(*state) / sizeof(double); index++) {
        if (!isfinite(values[index]) || values[index] < 0.0) return false;
    }
    return true;
}

double EcologyNutrientTotalNitrogen(const EcologyNutrientState *state)
{
    if (!EcologyNutrientStateValid(state)) return 0.0;
    return state->mineralNitrogen + state->soilOrganicNitrogen +
           state->plantNitrogen + state->litterNitrogen;
}

double EcologyNutrientTotalPhosphorus(const EcologyNutrientState *state)
{
    if (!EcologyNutrientStateValid(state)) return 0.0;
    return state->mineralPhosphorus + state->soilOrganicPhosphorus +
           state->plantPhosphorus + state->litterPhosphorus;
}

static double PoolTransfer(double *source, double requested)
{
    if (source == NULL) return 0.0;
    double available = NonNegative(*source);
    double moved = fmin(available, NonNegative(requested));
    *source = available - moved;
    return moved;
}

bool EcologyNutrientStep(EcologyNutrientState *state,
                         const EcologyNutrientParameters *parameters,
                         const EcologyNutrientEnvironment *environment,
                         double days,
                         EcologyNutrientFlux *flux)
{
    if (state == NULL || !EcologyNutrientStateValid(state) ||
        !isfinite(days) || days < 0.0) {
        return false;
    }
    EcologyNutrientParameters p = SanitizeParameters(parameters);
    EcologyNutrientEnvironment e = SanitizeEnvironment(environment);
    EcologyNutrientFlux local = { 0 };
    if (days == 0.0) {
        if (flux) *flux = local;
        return true;
    }

    /* Decomposition mineralizes the same nutrient mass that leaves litter. */
    double decomposition = fmin(state->litterBiomass,
                                state->litterBiomass * p.decompositionPerDay *
                                    days);
    double litterRatio = state->litterBiomass > 0.0
        ? decomposition / state->litterBiomass : 0.0;
    double decomposedN = state->litterNitrogen * litterRatio;
    double decomposedP = state->litterPhosphorus * litterRatio;
    state->litterBiomass -= decomposition;
    state->litterNitrogen -= decomposedN;
    state->litterPhosphorus -= decomposedP;
    state->mineralNitrogen += decomposedN;
    state->mineralPhosphorus += decomposedP;
    local.mineralizedNitrogen = decomposedN;
    local.mineralizedPhosphorus = decomposedP;

    double turnover = fmin(state->plantBiomass,
                           state->plantBiomass * p.plantTurnoverPerDay *
                               days);
    double plantRatio = state->plantBiomass > 0.0
        ? turnover / state->plantBiomass : 0.0;
    double turnoverN = state->plantNitrogen * plantRatio;
    double turnoverP = state->plantPhosphorus * plantRatio;
    state->plantBiomass -= turnover;
    state->plantNitrogen -= turnoverN;
    state->plantPhosphorus -= turnoverP;
    state->litterBiomass += turnover;
    state->litterNitrogen += turnoverN;
    state->litterPhosphorus += turnoverP;
    local.turnoverBiomass = turnover;

    double suitability = e.temperatureSuitability * e.moistureSuitability *
                         (1.0 - e.competition);
    double desiredGrowth = state->plantBiomass * p.maxGrowthPerDay * days *
                           suitability;
    double demandN = desiredGrowth * p.nitrogenFraction;
    double demandP = desiredGrowth * p.phosphorusFraction;
    double uptakeN = PoolTransfer(&state->mineralNitrogen, demandN);
    double uptakeP = PoolTransfer(&state->mineralPhosphorus, demandP);
    double growthN = uptakeN / p.nitrogenFraction;
    double growthP = uptakeP / p.phosphorusFraction;
    double growth = fmin(growthN, growthP);
    /* Return unused limiting uptake so nutrient mass remains exact. */
    double usedN = growth * p.nitrogenFraction;
    double usedP = growth * p.phosphorusFraction;
    state->mineralNitrogen += uptakeN - usedN;
    state->mineralPhosphorus += uptakeP - usedP;
    state->plantBiomass += growth;
    state->plantNitrogen += usedN;
    state->plantPhosphorus += usedP;
    local.uptakeNitrogen = usedN;
    local.uptakePhosphorus = usedP;
    local.growthBiomass = growth;

    double producedSeeds = growth * p.seedProductionPerBiomass;
    state->seedBank += producedSeeds;
    local.producedSeeds = producedSeeds;
    double germinated = fmin(state->seedBank,
                             state->seedBank * p.germinationPerDay * days *
                                 e.moistureSuitability);
    state->seedBank -= germinated;
    local.germinatedSeeds = germinated;
    state->meanPlantAgeDays += days;
    if (growth > 0.0 && state->meanPlantAgeDays > days) {
        state->meanPlantAgeDays =
            fmax(0.0, state->meanPlantAgeDays - days * growth /
                                      fmax(1e-12, state->plantBiomass));
    }

    double leachFraction = Clamp01(e.rainfallMm * p.leachingPerMm);
    local.leachedNitrogen = state->mineralNitrogen * leachFraction;
    local.leachedPhosphorus = state->mineralPhosphorus * leachFraction;
    state->mineralNitrogen -= local.leachedNitrogen;
    state->mineralPhosphorus -= local.leachedPhosphorus;
    if (!EcologyNutrientStateValid(state)) return false;
    if (flux) *flux = local;
    return true;
}
