#include "ecology/ecology_nutrient_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static int NearlyEqual(double a, double b)
{
    return fabs(a - b) < 1e-9;
}

static void TestConservationWithoutLeaching(void)
{
    EcologyNutrientState state = {
        .mineralNitrogen = 10.0,
        .mineralPhosphorus = 4.0,
        .soilOrganicNitrogen = 8.0,
        .soilOrganicPhosphorus = 3.0,
        .plantNitrogen = 1.0,
        .plantPhosphorus = 0.2,
        .litterNitrogen = 2.0,
        .litterPhosphorus = 0.5,
        .plantBiomass = 30.0,
        .litterBiomass = 10.0,
        .seedBank = 20.0,
        .meanPlantAgeDays = 40.0,
    };
    EcologyNutrientEnvironment environment =
        EcologyNutrientDefaultEnvironment();
    EcologyNutrientParameters parameters =
        EcologyNutrientDefaultParameters();
    parameters.leachingPerMm = 0.0;
    double initialN = EcologyNutrientTotalNitrogen(&state);
    double initialP = EcologyNutrientTotalPhosphorus(&state);
    EcologyNutrientFlux flux;
    assert(EcologyNutrientStep(&state, &parameters, &environment, 30.0,
                               &flux));
    assert(EcologyNutrientStateValid(&state));
    assert(NearlyEqual(EcologyNutrientTotalNitrogen(&state), initialN));
    assert(NearlyEqual(EcologyNutrientTotalPhosphorus(&state), initialP));
    assert(flux.mineralizedNitrogen > 0.0);
    assert(flux.growthBiomass >= 0.0);
    assert(flux.producedSeeds >= 0.0);
}

static void TestRainfallLeachingAndCompetition(void)
{
    EcologyNutrientState state = {
        .mineralNitrogen = 10.0,
        .mineralPhosphorus = 5.0,
        .plantBiomass = 10.0,
        .plantNitrogen = 1.0,
        .plantPhosphorus = 0.2,
    };
    EcologyNutrientParameters parameters =
        EcologyNutrientDefaultParameters();
    parameters.leachingPerMm = 0.01;
    EcologyNutrientEnvironment environment =
        EcologyNutrientDefaultEnvironment();
    environment.rainfallMm = 20.0;
    environment.competition = 1.0;
    double beforeN = EcologyNutrientTotalNitrogen(&state);
    double beforeP = EcologyNutrientTotalPhosphorus(&state);
    EcologyNutrientFlux flux;
    assert(EcologyNutrientStep(&state, &parameters, &environment, 1.0,
                               &flux));
    assert(flux.leachedNitrogen > 0.0);
    assert(flux.leachedPhosphorus > 0.0);
    assert(EcologyNutrientTotalNitrogen(&state) < beforeN);
    assert(EcologyNutrientTotalPhosphorus(&state) < beforeP);
    assert(flux.growthBiomass == 0.0);
}

static void TestDeterminismAndInvalidInput(void)
{
    EcologyNutrientState first = {
        .mineralNitrogen = 2.0, .mineralPhosphorus = 1.0,
        .plantBiomass = 5.0, .plantNitrogen = 0.4,
        .plantPhosphorus = 0.1, .litterBiomass = 3.0,
        .litterNitrogen = 0.2, .litterPhosphorus = 0.05,
        .seedBank = 2.0, .meanPlantAgeDays = 7.0};
    EcologyNutrientState second = first;
    EcologyNutrientEnvironment environment =
        EcologyNutrientDefaultEnvironment();
    EcologyNutrientFlux firstFlux;
    EcologyNutrientFlux secondFlux;
    assert(EcologyNutrientStep(&first, NULL, &environment, 5.0, &firstFlux));
    assert(EcologyNutrientStep(&second, NULL, &environment, 5.0,
                               &secondFlux));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(memcmp(&firstFlux, &secondFlux, sizeof(firstFlux)) == 0);
    assert(!EcologyNutrientStep(&first, NULL, &environment, -1.0, NULL));
    assert(!EcologyNutrientStep(&first, NULL, &environment, NAN, NULL));
}

int main(void)
{
    TestConservationWithoutLeaching();
    TestRainfallLeachingAndCompetition();
    TestDeterminismAndInvalidInput();
    puts("ecology nutrient model tests passed");
    return 0;
}
