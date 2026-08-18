#include "world/local_climate.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static LocalClimateInput Temperate(void)
{
    return (LocalClimateInput){
        .meanTemperatureK = 288.0f,
        .surfacePressureAtm = 1.0f,
        .atmosphereDensity = 0.72f,
        .moisture = 0.66f,
        .cloudPotential = 0.52f,
        .windStrength = 0.42f,
        .latitude = 0.55f,
        .elevation = 120.0f,
        .waterCoverage = 0.34f,
        .iceCoverage = 0.08f,
        .seasonalAmplitudeK = 12.0f,
        .hasAtmosphere = true,
        .supportsWaterCycle = true
    };
}

static void AssertValid(LocalClimateState state)
{
    assert(state.regime >= 0 && state.regime < CLIMATE_REGIME_COUNT);
    assert(isfinite(state.temperatureK));
    assert(isfinite(state.pressureAtm));
    assert(isfinite(state.dewPointK));
    assert(isfinite(state.wetBulbK));
    assert(state.relativeHumidity >= 0.0f && state.relativeHumidity <= 1.0f);
    assert(state.aridity >= 0.0f && state.aridity <= 1.0f);
    assert(state.instability >= 0.0f && state.instability <= 1.0f);
    assert(state.dewPointK <= state.temperatureK);
    assert(state.wetBulbK <= state.temperatureK);
}

static void TestClassification(void)
{
    LocalClimateInput input = Temperate();
    LocalClimateState state;
    assert(LocalClimateEvaluate(&input, &state));
    AssertValid(state);
    assert(state.regime == CLIMATE_REGIME_SEASONAL_TEMPERATE);

    input.meanTemperatureK = 307.0f;
    input.moisture = 0.98f;
    input.cloudPotential = 0.90f;
    input.waterCoverage = 0.72f;
    assert(LocalClimateEvaluate(&input, &state));
    assert(state.regime == CLIMATE_REGIME_TROPICAL_RAINFOREST);

    input = Temperate();
    input.meanTemperatureK = 313.0f;
    input.moisture = 0.02f;
    input.cloudPotential = 0.02f;
    input.waterCoverage = 0.0f;
    assert(LocalClimateEvaluate(&input, &state));
    assert(state.regime == CLIMATE_REGIME_DESERT);

    input = Temperate();
    input.hasAtmosphere = false;
    input.surfacePressureAtm = 0.0f;
    input.atmosphereDensity = 0.0f;
    assert(LocalClimateEvaluate(&input, &state));
    assert(state.regime == CLIMATE_REGIME_VACUUM);
    assert(!state.waterCycleActive);
}

static void TestElevationAndHumidity(void)
{
    LocalClimateInput low = Temperate();
    LocalClimateInput high = low;
    high.elevation = 2500.0f;
    LocalClimateState lowState;
    LocalClimateState highState;
    assert(LocalClimateEvaluate(&low, &lowState));
    assert(LocalClimateEvaluate(&high, &highState));
    assert(highState.temperatureK < lowState.temperatureK - 10.0f);
    assert(highState.pressureAtm < lowState.pressureAtm);
    assert(highState.orographicLift > lowState.orographicLift);
}

static void TestInvalidAndNames(void)
{
    LocalClimateInput input = Temperate();
    LocalClimateState state;
    input.latitude = NAN;
    assert(!LocalClimateEvaluate(&input, &state));
    assert(strcmp(ClimateRegimeName(CLIMATE_REGIME_MONSOON), "Monsoon") == 0);
    assert(strcmp(ClimateRegimeName(CLIMATE_REGIME_COUNT), "Unknown") == 0);
}

int main(void)
{
    TestClassification();
    TestElevationAndHumidity();
    TestInvalidAndNames();
    puts("local_climate tests passed");
    return 0;
}
