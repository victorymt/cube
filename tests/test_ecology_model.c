#include "ecology_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void AssertNear(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static float TestUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static PlanetLocalEnvironment TemperateEnvironment(void)
{
    PlanetLocalEnvironment environment = { 0 };
    environment.meanTemperatureK = 288.0f;
    environment.currentTemperatureK = 288.0f;
    environment.seasonalAmplitudeK = 12.0f;
    environment.liquidWaterAccess = 0.78f;
    environment.soilMoisture = 0.72f;
    environment.meanPrecipitation = 0.60f;
    environment.precipitationRate = 0.0f;
    environment.meanUsableLight = 0.82f;
    environment.currentUsableLight = 0.90f;
    environment.stormExposure = 0.18f;
    environment.currentStorm = 0.0f;
    environment.elevation = 0.18f;
    environment.slope = 0.12f;
    environment.shelter = 0.65f;
    environment.biomeSupport = 0.92f;
    return environment;
}

static PlanetEcologyTraits CarbonTraits(void)
{
    PlanetEcologyTraits traits = { 0 };
    traits.preferredTemperatureK = 288.0f;
    traits.temperatureToleranceK = 42.0f;
    traits.waterDependence = 0.92f;
    traits.lightDependence = 0.78f;
    traits.stormResistance = 0.28f;
    traits.altitudeTolerance = 0.25f;
    traits.slopeTolerance = 0.22f;
    traits.foodWebDependence = 0.86f;
    traits.nocturnalFraction = 0.18f;
    return traits;
}

static void AssertSuitabilityValid(PlanetEcologySuitability suitability)
{
#define ASSERT_UNIT(field) do { \
    assert(isfinite(suitability.field)); \
    assert(suitability.field >= 0.0f); \
    assert(suitability.field <= 1.0f); \
} while (0)
    ASSERT_UNIT(carryingCapacity);
    ASSERT_UNIT(floraCapacity);
    ASSERT_UNIT(faunaCapacity);
    ASSERT_UNIT(floraActivity);
    ASSERT_UNIT(faunaActivity);
    ASSERT_UNIT(waterScore);
    ASSERT_UNIT(temperatureScore);
    ASSERT_UNIT(lightScore);
    ASSERT_UNIT(stormScore);
    ASSERT_UNIT(terrainScore);
    ASSERT_UNIT(seasonScore);
#undef ASSERT_UNIT
    assert(suitability.limitingFactor >= PLANET_ECOLOGY_LIMIT_NONE);
    assert(suitability.limitingFactor <= PLANET_ECOLOGY_LIMIT_SEASON);
}

static void TestDeterministicHistory(void)
{
    PlanetLifeHistory first = PlanetLifeHistoryDerive(0x12345678u, 4.6f,
                                                       0.82f, true);
    PlanetLifeHistory second = PlanetLifeHistoryDerive(0x12345678u, 4.6f,
                                                        0.82f, true);
    AssertNear(first.originProbability, second.originProbability, 0.000001f);
    AssertNear(first.originRoll, second.originRoll, 0.000001f);
    AssertNear(first.complexLifeRoll, second.complexLifeRoll, 0.000001f);
    assert(first.lifeOriginated == second.lifeOriginated);
    assert(first.hasComplexLife == second.hasComplexLife);
}

static void TestAgeAndSurfaceGates(void)
{
    PlanetLifeHistory young = PlanetLifeHistoryDerive(42u, 0.7f, 0.95f, true);
    PlanetLifeHistory old = PlanetLifeHistoryDerive(42u, 7.0f, 0.95f, true);
    PlanetLifeHistory gas = PlanetLifeHistoryDerive(42u, 7.0f, 0.95f, false);

    assert(young.originProbability < old.originProbability);
    assert(young.complexLifeProbability == 0.0f);
    assert(!young.hasComplexLife);
    assert(gas.originProbability == 0.0f);
    assert(!gas.lifeOriginated);
    assert(!gas.hasComplexLife);
}

static void TestPopulationIsMostlyBarren(void)
{
    const int sampleCount = 30000;
    int originated = 0;
    int complex = 0;
    uint32_t state = 0x9f27a4bdu;

    for (int i = 0; i < sampleCount; i++) {
        float environment = TestUnit(&state);
        environment *= environment;
        float ageGyr = 0.1f + TestUnit(&state) * 8.9f;
        PlanetLifeHistory history = PlanetLifeHistoryDerive(
            (uint32_t)i * 0x9e3779b9u, ageGyr, environment, true);
        if (history.lifeOriginated) originated++;
        if (history.hasComplexLife) complex++;
        assert(!history.hasComplexLife || history.lifeOriginated);
    }

    float originRate = (float)originated / (float)sampleCount;
    float complexRate = (float)complex / (float)sampleCount;
    assert(originRate > 0.04f && originRate < 0.15f);
    assert(complexRate > 0.002f && complexRate < 0.03f);
}

static void TestComplexEcologyRemainsRareOnHabitableWorlds(void)
{
    const int sampleCount = 20000;
    int originated = 0;
    int complex = 0;
    for (int i = 0; i < sampleCount; i++) {
        PlanetLifeHistory history = PlanetLifeHistoryDerive(
            (uint32_t)i * 0x85ebca6bu, 4.5f, 0.82f, true);
        if (history.lifeOriginated) originated++;
        if (history.hasComplexLife) complex++;
    }

    float originRate = (float)originated / (float)sampleCount;
    float complexRate = (float)complex / (float)sampleCount;
    assert(originRate > 0.34f && originRate < 0.44f);
    assert(complexRate > 0.05f && complexRate < 0.10f);
}

static void TestDensityCapsPreComplexLife(void)
{
    bool foundMicrobial = false;
    bool foundComplex = false;
    for (uint32_t seed = 0; seed < 10000u; seed++) {
        PlanetLifeHistory history = PlanetLifeHistoryDerive(seed, 8.0f,
                                                            1.0f, true);
        float density = PlanetLifeHistoryDensity(&history, 1.0f);
        if (!history.lifeOriginated) {
            assert(density == 0.0f);
        } else if (!history.hasComplexLife) {
            assert(density <= 0.19f);
            foundMicrobial = true;
        } else {
            assert(density > 0.19f);
            foundComplex = true;
        }
    }
    assert(foundMicrobial);
    assert(foundComplex);
}

static void TestLocalSuitabilityIsDeterministic(void)
{
    PlanetLocalEnvironment environment = TemperateEnvironment();
    PlanetEcologyTraits traits = CarbonTraits();
    PlanetEcologySuitability first = PlanetEcologyEvaluateLocal(
        &environment, &traits, 0.82f, 0.56f);
    PlanetEcologySuitability second = PlanetEcologyEvaluateLocal(
        &environment, &traits, 0.82f, 0.56f);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    AssertSuitabilityValid(first);
}

static void TestLocalEnvironmentalControls(void)
{
    PlanetLocalEnvironment good = TemperateEnvironment();
    PlanetEcologyTraits traits = CarbonTraits();
    PlanetEcologySuitability baseline = PlanetEcologyEvaluateLocal(
        &good, &traits, 0.82f, 0.56f);

    PlanetLocalEnvironment dry = good;
    dry.liquidWaterAccess = 0.0f;
    dry.soilMoisture = 0.0f;
    dry.meanPrecipitation = 0.0f;
    PlanetEcologySuitability dryResult = PlanetEcologyEvaluateLocal(
        &dry, &traits, 0.82f, 0.56f);
    assert(dryResult.carryingCapacity == 0.0f);
    assert(dryResult.limitingFactor == PLANET_ECOLOGY_LIMIT_WATER);

    PlanetLocalEnvironment lethal = good;
    lethal.meanTemperatureK = 470.0f;
    lethal.currentTemperatureK = 470.0f;
    PlanetEcologySuitability lethalResult = PlanetEcologyEvaluateLocal(
        &lethal, &traits, 0.82f, 0.56f);
    assert(lethalResult.carryingCapacity < baseline.carryingCapacity * 0.20f);

    PlanetLocalEnvironment dark = good;
    dark.meanUsableLight = 0.01f;
    PlanetEcologySuitability darkResult = PlanetEcologyEvaluateLocal(
        &dark, &traits, 0.82f, 0.56f);
    assert(darkResult.floraCapacity < baseline.floraCapacity);

    PlanetLocalEnvironment stormy = good;
    stormy.stormExposure = 1.0f;
    PlanetEcologySuitability stormResult = PlanetEcologyEvaluateLocal(
        &stormy, &traits, 0.82f, 0.56f);
    assert(stormResult.carryingCapacity < baseline.carryingCapacity);

    PlanetLocalEnvironment mountain = good;
    mountain.elevation = 1.0f;
    mountain.slope = 1.0f;
    mountain.shelter = 0.0f;
    PlanetEcologySuitability mountainResult = PlanetEcologyEvaluateLocal(
        &mountain, &traits, 0.82f, 0.56f);
    assert(mountainResult.carryingCapacity < baseline.carryingCapacity);

    PlanetLocalEnvironment seasonal = good;
    seasonal.seasonalAmplitudeK = 105.0f;
    PlanetEcologySuitability seasonalResult = PlanetEcologyEvaluateLocal(
        &seasonal, &traits, 0.82f, 0.56f);
    assert(seasonalResult.seasonScore < baseline.seasonScore);
    assert(seasonalResult.carryingCapacity < baseline.carryingCapacity);
}

static void TestWeatherChangesActivityNotPermanentCapacity(void)
{
    PlanetLocalEnvironment dryMoment = TemperateEnvironment();
    PlanetEcologyTraits traits = CarbonTraits();
    PlanetEcologySuitability clear = PlanetEcologyEvaluateLocal(
        &dryMoment, &traits, 0.82f, 0.56f);

    PlanetLocalEnvironment rainyMoment = dryMoment;
    rainyMoment.precipitationRate = 0.85f;
    PlanetEcologySuitability rain = PlanetEcologyEvaluateLocal(
        &rainyMoment, &traits, 0.82f, 0.56f);
    AssertNear(rain.carryingCapacity, clear.carryingCapacity, 0.0f);
    AssertNear(rain.floraCapacity, clear.floraCapacity, 0.0f);
    assert(rain.floraActivity > clear.floraActivity);
    assert(rain.faunaActivity > clear.faunaActivity);

    PlanetLocalEnvironment storm = rainyMoment;
    storm.currentStorm = 1.0f;
    PlanetEcologySuitability stormActivity = PlanetEcologyEvaluateLocal(
        &storm, &traits, 0.82f, 0.56f);
    AssertNear(stormActivity.faunaCapacity, clear.faunaCapacity, 0.0f);
    assert(stormActivity.faunaActivity < clear.faunaActivity);

    PlanetFaunaRuntimeState clearRuntime = PlanetEcologyFaunaRuntime(
        clear.faunaActivity, clear.faunaCapacity);
    PlanetFaunaRuntimeState stormRuntime = PlanetEcologyFaunaRuntime(
        stormActivity.faunaActivity, stormActivity.faunaCapacity);
    assert(stormRuntime.activityRatio < clearRuntime.activityRatio);
    assert(stormRuntime.movementScale <= clearRuntime.movementScale);
    assert(stormRuntime.animationScale < clearRuntime.animationScale);
    PlanetFloraRuntimeState clearFlora = PlanetEcologyFloraRuntime(
        clear.floraActivity, clear.floraCapacity);
    PlanetFloraRuntimeState stormFlora = PlanetEcologyFloraRuntime(
        stormActivity.floraActivity, stormActivity.floraCapacity);
    assert(stormFlora.growthScale < clearFlora.growthScale);
    assert(stormFlora.visualPresence < clearFlora.visualPresence);
}

static void TestCurrentSeasonChangesActivityOnly(void)
{
    PlanetLocalEnvironment summer = TemperateEnvironment();
    PlanetEcologyTraits traits = CarbonTraits();
    PlanetLocalEnvironment winter = summer;
    winter.currentTemperatureK = 218.0f;
    winter.currentUsableLight = 0.22f;

    PlanetEcologySuitability warm = PlanetEcologyEvaluateLocal(
        &summer, &traits, 0.82f, 0.56f);
    PlanetEcologySuitability cold = PlanetEcologyEvaluateLocal(
        &winter, &traits, 0.82f, 0.56f);
    AssertNear(warm.carryingCapacity, cold.carryingCapacity, 0.0f);
    AssertNear(warm.floraCapacity, cold.floraCapacity, 0.0f);
    assert(cold.floraActivity < warm.floraActivity);
    assert(cold.faunaActivity < warm.faunaActivity);

    PlanetFaunaRuntimeState warmRuntime = PlanetEcologyFaunaRuntime(
        warm.faunaActivity, warm.faunaCapacity);
    PlanetFaunaRuntimeState coldRuntime = PlanetEcologyFaunaRuntime(
        cold.faunaActivity, cold.faunaCapacity);
    assert(coldRuntime.activityRatio < warmRuntime.activityRatio);
    assert(coldRuntime.movementScale <= warmRuntime.movementScale);
    assert(coldRuntime.visualPresence < warmRuntime.visualPresence);
    PlanetFloraRuntimeState warmFlora = PlanetEcologyFloraRuntime(
        warm.floraActivity, warm.floraCapacity);
    PlanetFloraRuntimeState coldFlora = PlanetEcologyFloraRuntime(
        cold.floraActivity, cold.floraCapacity);
    assert(coldFlora.growthScale < warmFlora.growthScale);
    assert(coldFlora.visualPresence < warmFlora.visualPresence);
}

static void TestProducerActivityFeedsFaunaActivity(void)
{
    PlanetLocalEnvironment clear = TemperateEnvironment();
    PlanetEcologyTraits nocturnalGrazer = CarbonTraits();
    nocturnalGrazer.nocturnalFraction = 1.0f;

    PlanetEcologySuitability productive = PlanetEcologyEvaluateLocal(
        &clear, &nocturnalGrazer, 0.82f, 0.56f);
    PlanetLocalEnvironment shaded = clear;
    shaded.currentUsableLight = 0.02f;
    PlanetEcologySuitability foodPoor = PlanetEcologyEvaluateLocal(
        &shaded, &nocturnalGrazer, 0.82f, 0.56f);

    assert(foodPoor.floraActivity < productive.floraActivity);
    assert(foodPoor.faunaActivity < productive.faunaActivity);
    assert(foodPoor.faunaActivity < productive.faunaActivity * 0.60f);
    assert(foodPoor.faunaCapacity == productive.faunaCapacity);
}

static void AssertRuntimeStateValid(PlanetFaunaRuntimeState state)
{
#define ASSERT_RUNTIME_UNIT(field) do { \
    assert(isfinite(state.field)); \
    assert(state.field >= 0.0f); \
    assert(state.field <= 1.0f); \
} while (0)
    ASSERT_RUNTIME_UNIT(activityRatio);
    ASSERT_RUNTIME_UNIT(movementScale);
    ASSERT_RUNTIME_UNIT(animationScale);
    ASSERT_RUNTIME_UNIT(visualScale);
    ASSERT_RUNTIME_UNIT(visualPresence);
#undef ASSERT_RUNTIME_UNIT
    if (state.dormant) assert(state.movementScale == 0.0f);
}

static void TestFaunaRuntimeResponse(void)
{
    PlanetFaunaRuntimeState active = PlanetEcologyFaunaRuntime(0.50f, 0.50f);
    PlanetFaunaRuntimeState stressed = PlanetEcologyFaunaRuntime(0.12f, 0.50f);
    PlanetFaunaRuntimeState dormant = PlanetEcologyFaunaRuntime(0.03f, 0.50f);
    PlanetFaunaRuntimeState absent = PlanetEcologyFaunaRuntime(0.0f, 0.0f);

    AssertRuntimeStateValid(active);
    AssertRuntimeStateValid(stressed);
    AssertRuntimeStateValid(dormant);
    AssertRuntimeStateValid(absent);
    assert(!active.dormant);
    assert(!stressed.dormant);
    assert(dormant.dormant);
    assert(absent.dormant);
    assert(active.movementScale > stressed.movementScale);
    assert(stressed.movementScale > dormant.movementScale);
    assert(active.animationScale > stressed.animationScale);
    assert(stressed.animationScale > dormant.animationScale);
    assert(active.visualScale > stressed.visualScale);
    assert(stressed.visualPresence > dormant.visualPresence);

    PlanetFaunaRuntimeState sameRatioA = PlanetEcologyFaunaRuntime(0.08f, 0.10f);
    PlanetFaunaRuntimeState sameRatioB = PlanetEcologyFaunaRuntime(0.40f, 0.50f);
    PlanetFaunaRuntimeState repeated = PlanetEcologyFaunaRuntime(0.08f, 0.10f);
    AssertNear(sameRatioA.activityRatio, sameRatioB.activityRatio, 0.000001f);
    AssertNear(sameRatioA.movementScale, sameRatioB.movementScale, 0.000001f);
    assert(sameRatioA.activityRatio == repeated.activityRatio);
    assert(sameRatioA.movementScale == repeated.movementScale);
    assert(sameRatioA.animationScale == repeated.animationScale);
    assert(sameRatioA.visualScale == repeated.visualScale);
    assert(sameRatioA.visualPresence == repeated.visualPresence);
    assert(sameRatioA.dormant == repeated.dormant);
}

static void TestRandomizedFaunaRuntimeProperties(void)
{
    uint32_t state = 0xa31b5c72u;
    for (int sample = 0; sample < 10000; sample++) {
        float capacity = TestUnit(&state);
        float activity = TestUnit(&state) * 1.2f;
        PlanetFaunaRuntimeState runtime = PlanetEcologyFaunaRuntime(
            activity, capacity);
        AssertRuntimeStateValid(runtime);
    }
}

static void AssertFloraRuntimeStateValid(PlanetFloraRuntimeState state)
{
#define ASSERT_FLORA_UNIT(field) do { \
    assert(isfinite(state.field)); \
    assert(state.field >= 0.0f); \
    assert(state.field <= 1.0f); \
} while (0)
    ASSERT_FLORA_UNIT(activityRatio);
    ASSERT_FLORA_UNIT(growthScale);
    ASSERT_FLORA_UNIT(visualScale);
    ASSERT_FLORA_UNIT(visualPresence);
#undef ASSERT_FLORA_UNIT
}

static void TestFloraRuntimeResponse(void)
{
    PlanetFloraRuntimeState lush = PlanetEcologyFloraRuntime(0.80f, 0.80f);
    PlanetFloraRuntimeState stressed = PlanetEcologyFloraRuntime(0.12f, 0.80f);
    PlanetFloraRuntimeState dormant = PlanetEcologyFloraRuntime(0.02f, 0.80f);
    PlanetFloraRuntimeState absent = PlanetEcologyFloraRuntime(0.0f, 0.0f);

    AssertFloraRuntimeStateValid(lush);
    AssertFloraRuntimeStateValid(stressed);
    AssertFloraRuntimeStateValid(dormant);
    AssertFloraRuntimeStateValid(absent);
    assert(!lush.dormant);
    assert(!stressed.dormant);
    assert(dormant.dormant);
    assert(absent.dormant);
    assert(lush.growthScale > stressed.growthScale);
    assert(stressed.growthScale > dormant.growthScale);
    assert(lush.visualScale > stressed.visualScale);
    assert(stressed.visualPresence > dormant.visualPresence);

    PlanetFloraRuntimeState first = PlanetEcologyFloraRuntime(0.24f, 0.40f);
    PlanetFloraRuntimeState second = PlanetEcologyFloraRuntime(0.60f, 1.00f);
    AssertNear(first.activityRatio, second.activityRatio, 0.000001f);
    AssertNear(first.growthScale, second.growthScale, 0.000001f);
}

static void TestRandomizedFloraRuntimeProperties(void)
{
    uint32_t state = 0x643d91a7u;
    for (int sample = 0; sample < 10000; sample++) {
        float capacity = TestUnit(&state);
        float activity = TestUnit(&state) * 1.2f;
        PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
            activity, capacity);
        AssertFloraRuntimeStateValid(runtime);
    }
}

static void AssertPopulationValid(PlanetRegionalPopulation population)
{
#define ASSERT_POPULATION_UNIT(field) do { \
    assert(isfinite(population.field)); \
    assert(population.field >= 0.0f); \
    assert(population.field <= 1.0f); \
} while (0)
    ASSERT_POPULATION_UNIT(floraDensity);
    ASSERT_POPULATION_UNIT(faunaDensity);
    ASSERT_POPULATION_UNIT(floraCarryingCapacity);
    ASSERT_POPULATION_UNIT(faunaCarryingCapacity);
    ASSERT_POPULATION_UNIT(seasonalMemory);
    ASSERT_POPULATION_UNIT(faunaHarvestPressure);
#undef ASSERT_POPULATION_UNIT
    float floraPresence = PlanetPopulationFloraPresence(&population);
    float faunaPresence = PlanetPopulationFaunaPresence(&population);
    assert(floraPresence >= 0.0f && floraPresence <= 1.0f);
    assert(faunaPresence >= 0.0f && faunaPresence <= 1.0f);
}

static void TestPopulationRecoveryAndMortality(void)
{
    PlanetPopulationInput good = {
        .floraCapacity = 0.82f,
        .faunaCapacity = 0.56f,
        .floraActivity = 0.78f,
        .faunaActivity = 0.50f
    };
    PlanetPopulationInput harsh = good;
    harsh.floraActivity = 0.0f;
    harsh.faunaActivity = 0.0f;

    PlanetRegionalPopulation initial = PlanetPopulationInitialize(
        &good, 0.78f, 0.68f);
    PlanetRegionalPopulation stressed = initial;
    PlanetPopulationAdvance(&stressed, &harsh, 600.0);
    AssertPopulationValid(initial);
    AssertPopulationValid(stressed);
    assert(stressed.floraDensity < initial.floraDensity);
    assert(stressed.faunaDensity < initial.faunaDensity);
    assert(stressed.faunaDensity < initial.faunaDensity * 0.35f);

    PlanetRegionalPopulation shortRecovery = stressed;
    PlanetRegionalPopulation longRecovery = stressed;
    PlanetPopulationAdvance(&shortRecovery, &good, 30.0);
    PlanetPopulationAdvance(&longRecovery, &good, 600.0);
    assert(shortRecovery.floraDensity > stressed.floraDensity);
    assert(longRecovery.floraDensity > shortRecovery.floraDensity);
    assert(longRecovery.faunaDensity > shortRecovery.faunaDensity);
    assert(shortRecovery.faunaDensity < initial.faunaDensity);
}

static void TestPopulationSeasonalLag(void)
{
    PlanetPopulationInput summer = {
        .floraCapacity = 0.90f,
        .faunaCapacity = 0.62f,
        .floraActivity = 0.90f,
        .faunaActivity = 0.62f
    };
    PlanetPopulationInput winter = summer;
    winter.floraActivity = 0.0f;
    winter.faunaActivity = 0.0f;
    PlanetRegionalPopulation population = PlanetPopulationInitialize(
        &summer, 0.85f, 0.72f);
    float summerCapacity = population.floraCarryingCapacity;

    PlanetPopulationAdvance(&population, &winter, 1.0);
    float immediateCapacity = population.floraCarryingCapacity;
    assert(immediateCapacity > summerCapacity * 0.97f);
    assert(population.seasonalMemory > 0.97f);
    PlanetPopulationAdvance(&population, &winter, 600.0);
    assert(population.floraCarryingCapacity < immediateCapacity * 0.55f);
    assert(population.seasonalMemory < 0.02f);
}

static void TestPopulationDeterminismAndFoodChain(void)
{
    PlanetPopulationInput input = {
        .floraCapacity = 0.72f,
        .faunaCapacity = 0.48f,
        .floraActivity = 0.54f,
        .faunaActivity = 0.36f
    };
    PlanetRegionalPopulation first = PlanetPopulationInitialize(
        &input, 0.65f, 0.55f);
    PlanetRegionalPopulation second = first;
    PlanetPopulationAdvance(&first, &input, 137.25);
    PlanetPopulationAdvance(&second, &input, 137.25);
    assert(memcmp(&first, &second, sizeof(first)) == 0);

    PlanetRegionalPopulation foodPoor = first;
    PlanetRegionalPopulation foodRich = first;
    foodPoor.floraDensity = 0.0f;
    foodRich.floraDensity = foodRich.floraCarryingCapacity;
    PlanetPopulationAdvance(&foodPoor, &input, 45.0);
    PlanetPopulationAdvance(&foodRich, &input, 45.0);
    assert(foodPoor.faunaDensity < foodRich.faunaDensity);
}

static void TestPopulationDisturbanceAndRecovery(void)
{
    PlanetPopulationInput healthyInput = {
        .floraCapacity = 0.84f,
        .faunaCapacity = 0.66f,
        .floraActivity = 0.78f,
        .faunaActivity = 0.58f
    };
    PlanetRegionalPopulation healthy = PlanetPopulationInitialize(
        &healthyInput, 0.88f, 0.76f);
    PlanetRegionalPopulation undisturbed = healthy;
    PlanetRegionalPopulation damaged = healthy;
    PlanetPopulationApplyDisturbance(&undisturbed, 0.0f, 0.0f, 240.0);
    PlanetPopulationApplyDisturbance(&damaged, 1.0f, 1.0f, 240.0);
    assert(memcmp(&undisturbed, &healthy, sizeof(healthy)) == 0);
    AssertPopulationValid(damaged);
    assert(damaged.floraDensity < healthy.floraDensity);
    assert(damaged.faunaDensity < healthy.faunaDensity);
    assert(damaged.faunaDensity < healthy.faunaDensity * 0.55f);

    PlanetRegionalPopulation recovering = damaged;
    for (int step = 0; step < 8; step++) {
        PlanetPopulationAdvance(&recovering, &healthyInput, 120.0);
        PlanetPopulationApplyDisturbance(&recovering, 0.0f, 0.0f, 120.0);
    }
    assert(recovering.floraDensity > damaged.floraDensity);
    assert(recovering.faunaDensity > damaged.faunaDensity);
    assert(recovering.faunaDensity <= recovering.faunaCarryingCapacity);

    PlanetRegionalPopulation invalid = healthy;
    PlanetPopulationApplyDisturbance(&invalid, NAN, INFINITY, 120.0);
    AssertPopulationValid(invalid);
    PlanetRegionalPopulation unchanged = healthy;
    PlanetPopulationApplyDisturbance(&unchanged, 1.0f, 1.0f, 0.0);
    assert(memcmp(&unchanged, &healthy, sizeof(healthy)) == 0);
}

static void TestRandomizedPopulationDisturbanceProperties(void)
{
    uint32_t state = 0xa6d2b79fu;
    for (int sample = 0; sample < 10000; sample++) {
        PlanetPopulationInput input = {
            .floraCapacity = TestUnit(&state),
            .faunaCapacity = TestUnit(&state),
            .floraActivity = TestUnit(&state),
            .faunaActivity = TestUnit(&state)
        };
        PlanetRegionalPopulation population = PlanetPopulationInitialize(
            &input, TestUnit(&state), TestUnit(&state));
        PlanetPopulationApplyDisturbance(
            &population, TestUnit(&state) * 1.4f - 0.2f,
            TestUnit(&state) * 1.4f - 0.2f,
            (double)TestUnit(&state) * 2000.0);
        AssertPopulationValid(population);
    }
}

static void TestFaunaHarvestPressureModel(void)
{
    float smallCommon = PlanetFaunaHarvestEventStrength(0.5f, 0.9f);
    float largeCommon = PlanetFaunaHarvestEventStrength(1.8f, 0.9f);
    float largeScarce = PlanetFaunaHarvestEventStrength(1.8f, 0.1f);
    assert(smallCommon > 0.0f);
    assert(largeCommon > smallCommon);
    assert(largeScarce > largeCommon);
    assert(PlanetFaunaHarvestEventStrength(0.0f, 0.5f) == 0.0f);
    assert(PlanetFaunaHarvestEventStrength(NAN, 0.5f) == 0.0f);

    float pressure = PlanetFaunaHarvestPressureAdd(0.0f, largeCommon);
    float repeated = PlanetFaunaHarvestPressureAdd(pressure, largeCommon);
    assert(pressure == largeCommon);
    assert(repeated > pressure && repeated <= 1.0f);
    assert(PlanetFaunaHarvestPressureAdd(pressure, 0.0f) == pressure);

    float oneStep = PlanetFaunaHarvestPressureAdvance(repeated, 240.0);
    float splitStep = PlanetFaunaHarvestPressureAdvance(repeated, 80.0);
    splitStep = PlanetFaunaHarvestPressureAdvance(splitStep, 160.0);
    assert(oneStep < repeated);
    AssertNear(oneStep, splitStep, 0.000001f);
    assert(PlanetFaunaHarvestPressureAdvance(repeated, 0.0) == repeated);

    PlanetRegionalPopulation population = {
        .floraDensity = 0.72f,
        .faunaDensity = 0.58f,
        .floraCarryingCapacity = 0.80f,
        .faunaCarryingCapacity = 0.64f,
        .seasonalMemory = 0.76f
    };
    PlanetRegionalPopulation untouched = population;
    PlanetPopulationApplyFaunaHarvest(&untouched, 0.0f);
    assert(memcmp(&untouched, &population, sizeof(population)) == 0);
    PlanetPopulationApplyFaunaHarvest(&population, largeScarce);
    AssertPopulationValid(population);
    assert(population.faunaDensity < untouched.faunaDensity);
    assert(population.faunaHarvestPressure >
           untouched.faunaHarvestPressure);
    assert(population.floraDensity == untouched.floraDensity);
    assert(population.faunaCarryingCapacity ==
           untouched.faunaCarryingCapacity);
}

static void TestRandomizedFaunaHarvestProperties(void)
{
    uint32_t state = 0x6f48a2d1u;
    for (int sample = 0; sample < 10000; sample++) {
        float strength = PlanetFaunaHarvestEventStrength(
            TestUnit(&state) * 3.0f, TestUnit(&state));
        float pressure = PlanetFaunaHarvestPressureAdd(
            TestUnit(&state), strength);
        float advanced = PlanetFaunaHarvestPressureAdvance(
            pressure, (double)TestUnit(&state) * 4000.0);
        assert(isfinite(strength) && strength >= 0.0f && strength <= 1.0f);
        assert(isfinite(pressure) && pressure >= strength && pressure <= 1.0f);
        assert(isfinite(advanced) && advanced >= 0.0f && advanced <= pressure);

        PlanetRegionalPopulation population = {
            .floraDensity = TestUnit(&state),
            .faunaDensity = TestUnit(&state),
            .floraCarryingCapacity = TestUnit(&state),
            .faunaCarryingCapacity = TestUnit(&state),
            .seasonalMemory = TestUnit(&state)
        };
        float before = population.faunaDensity;
        PlanetPopulationApplyFaunaHarvest(&population, strength);
        AssertPopulationValid(population);
        assert(population.faunaDensity <= before);
    }
}

static void AssertMigrationFluxValid(PlanetPopulationMigrationFlux flux)
{
    assert(isfinite(flux.flora));
    assert(isfinite(flux.fauna));
    assert(flux.flora >= -1.0f && flux.flora <= 1.0f);
    assert(flux.fauna >= -1.0f && flux.fauna <= 1.0f);
}

static void TestPopulationMigrationDrivers(void)
{
    PlanetRegionalPopulation source = {
        .floraDensity = 0.68f,
        .faunaDensity = 0.44f,
        .floraCarryingCapacity = 0.82f,
        .faunaCarryingCapacity = 0.62f,
        .seasonalMemory = 0.75f
    };
    PlanetRegionalPopulation destination = {
        .floraDensity = 0.04f,
        .faunaDensity = 0.02f,
        .floraCarryingCapacity = 0.78f,
        .faunaCarryingCapacity = 0.58f,
        .seasonalMemory = 0.70f
    };
    PlanetMigrationHabitat sourceHabitat = {
        .floraSuitability = 0.72f,
        .faunaSuitability = 0.68f,
        .stormPressure = 0.08f
    };
    PlanetMigrationHabitat destinationHabitat = {
        .floraSuitability = 0.88f,
        .faunaSuitability = 0.82f,
        .stormPressure = 0.04f
    };

    PlanetPopulationMigrationFlux downwind = PlanetPopulationMigrationBetween(
        &source, &sourceHabitat, &destination, &destinationHabitat,
        1.0f, 240.0);
    PlanetPopulationMigrationFlux upwind = PlanetPopulationMigrationBetween(
        &source, &sourceHabitat, &destination, &destinationHabitat,
        -1.0f, 240.0);
    AssertMigrationFluxValid(downwind);
    assert(downwind.flora > 0.0f);
    assert(downwind.fauna > 0.0f);
    assert(downwind.flora > upwind.flora);

    PlanetMigrationHabitat stormDestination = destinationHabitat;
    stormDestination.stormPressure = 1.0f;
    PlanetPopulationMigrationFlux stormward = PlanetPopulationMigrationBetween(
        &source, &sourceHabitat, &destination, &stormDestination,
        1.0f, 240.0);
    assert(stormward.flora < downwind.flora);
    assert(stormward.fauna < downwind.fauna);

    PlanetRegionalPopulation foodPoorSource = source;
    PlanetRegionalPopulation foodRichDestination = destination;
    foodPoorSource.floraDensity = 0.02f;
    foodRichDestination.floraDensity = 0.70f;
    foodRichDestination.faunaDensity = 0.0f;
    PlanetPopulationMigrationFlux followsFood = PlanetPopulationMigrationBetween(
        &foodPoorSource, &sourceHabitat,
        &foodRichDestination, &destinationHabitat,
        0.0f, 240.0);
    assert(followsFood.fauna > 0.0f);

    PlanetRegionalPopulation equal = source;
    equal.floraDensity = 0.30f;
    equal.faunaDensity = 0.20f;
    PlanetPopulationMigrationFlux balanced = PlanetPopulationMigrationBetween(
        &equal, &sourceHabitat, &equal, &sourceHabitat, 0.0f, 240.0);
    assert(balanced.flora == 0.0f);
    assert(balanced.fauna == 0.0f);

    PlanetPopulationMigrationFlux stopped = PlanetPopulationMigrationBetween(
        &source, &sourceHabitat, &destination, &destinationHabitat,
        1.0f, 0.0);
    assert(stopped.flora == 0.0f && stopped.fauna == 0.0f);
    PlanetPopulationMigrationFlux invalid = PlanetPopulationMigrationBetween(
        &source, &sourceHabitat, &destination, &destinationHabitat,
        NAN, 240.0);
    assert(invalid.flora == 0.0f && invalid.fauna == 0.0f);
}

static void TestRandomizedPopulationMigrationProperties(void)
{
    uint32_t state = 0x7f4a7c15u;
    for (int sample = 0; sample < 10000; sample++) {
        PlanetRegionalPopulation population[5];
        PlanetMigrationHabitat habitat[5];
        for (int region = 0; region < 5; region++) {
            population[region].floraCarryingCapacity =
                0.10f + TestUnit(&state) * 0.90f;
            population[region].faunaCarryingCapacity =
                0.10f + TestUnit(&state) * 0.90f;
            population[region].floraDensity =
                population[region].floraCarryingCapacity * TestUnit(&state);
            population[region].faunaDensity =
                population[region].faunaCarryingCapacity * TestUnit(&state);
            population[region].seasonalMemory = TestUnit(&state);
            habitat[region] = (PlanetMigrationHabitat){
                .floraSuitability = TestUnit(&state),
                .faunaSuitability = TestUnit(&state),
                .stormPressure = TestUnit(&state)
            };
        }

        float winds[4];
        double elapsed = 1.0 + (double)TestUnit(&state) * 4000.0;
        PlanetPopulationMigrationFlux fluxes[4];
        double floraDelta[5] = { 0 };
        double faunaDelta[5] = { 0 };
        for (int edge = 0; edge < 4; edge++) {
            winds[edge] = TestUnit(&state) * 2.0f - 1.0f;
            fluxes[edge] = PlanetPopulationMigrationBetween(
                &population[0], &habitat[0],
                &population[edge + 1], &habitat[edge + 1],
                winds[edge], elapsed);
            AssertMigrationFluxValid(fluxes[edge]);
            PlanetPopulationMigrationFlux reverse =
                PlanetPopulationMigrationBetween(
                    &population[edge + 1], &habitat[edge + 1],
                    &population[0], &habitat[0],
                    -winds[edge], elapsed);
            assert(fluxes[edge].flora == -reverse.flora);
            assert(fluxes[edge].fauna == -reverse.fauna);
            if (fluxes[edge].flora >= 0.0f) {
                assert(fluxes[edge].flora <=
                       population[0].floraDensity * 0.14f + 0.000001f);
            } else {
                assert(-fluxes[edge].flora <=
                       population[edge + 1].floraDensity * 0.14f + 0.000001f);
            }
            if (fluxes[edge].fauna >= 0.0f) {
                assert(fluxes[edge].fauna <=
                       population[0].faunaDensity * 0.18f + 0.000001f);
            } else {
                assert(-fluxes[edge].fauna <=
                       population[edge + 1].faunaDensity * 0.18f + 0.000001f);
            }
            floraDelta[0] -= fluxes[edge].flora;
            floraDelta[edge + 1] += fluxes[edge].flora;
            faunaDelta[0] -= fluxes[edge].fauna;
            faunaDelta[edge + 1] += fluxes[edge].fauna;
        }

        double reverseFloraDelta[5] = { 0 };
        double reverseFaunaDelta[5] = { 0 };
        for (int edge = 3; edge >= 0; edge--) {
            reverseFloraDelta[0] -= fluxes[edge].flora;
            reverseFloraDelta[edge + 1] += fluxes[edge].flora;
            reverseFaunaDelta[0] -= fluxes[edge].fauna;
            reverseFaunaDelta[edge + 1] += fluxes[edge].fauna;
        }
        assert(memcmp(floraDelta, reverseFloraDelta,
                      sizeof(floraDelta)) == 0);
        assert(memcmp(faunaDelta, reverseFaunaDelta,
                      sizeof(faunaDelta)) == 0);

        double floraConservation = 0.0;
        double faunaConservation = 0.0;
        for (int region = 0; region < 5; region++) {
            floraConservation += floraDelta[region];
            faunaConservation += faunaDelta[region];
            double newFlora = population[region].floraDensity +
                              floraDelta[region];
            double newFauna = population[region].faunaDensity +
                              faunaDelta[region];
            assert(newFlora >= -0.000001);
            assert(newFauna >= -0.000001);
            assert(newFlora <=
                   population[region].floraCarryingCapacity + 0.000001);
            assert(newFauna <=
                   population[region].faunaCarryingCapacity + 0.000001);
        }
        assert(fabs(floraConservation) < 0.000000001);
        assert(fabs(faunaConservation) < 0.000000001);
    }
}

static void TestRandomizedPopulationProperties(void)
{
    uint32_t state = 0x91e10da5u;
    for (int sample = 0; sample < 10000; sample++) {
        PlanetPopulationInput input = {
            .floraCapacity = TestUnit(&state),
            .faunaCapacity = TestUnit(&state),
            .floraActivity = TestUnit(&state),
            .faunaActivity = TestUnit(&state)
        };
        PlanetRegionalPopulation population = PlanetPopulationInitialize(
            &input, TestUnit(&state), TestUnit(&state));
        for (int step = 0; step < 4; step++) {
            input.floraCapacity = TestUnit(&state);
            input.faunaCapacity = TestUnit(&state);
            input.floraActivity = TestUnit(&state);
            input.faunaActivity = TestUnit(&state);
            PlanetPopulationAdvance(&population, &input,
                                    (double)TestUnit(&state) * 2000.0);
            AssertPopulationValid(population);
        }
    }

    PlanetPopulationInput invalid = {
        .floraCapacity = NAN,
        .faunaCapacity = INFINITY,
        .floraActivity = -INFINITY,
        .faunaActivity = NAN
    };
    PlanetRegionalPopulation sanitized = PlanetPopulationInitialize(
        &invalid, NAN, INFINITY);
    AssertPopulationValid(sanitized);
    sanitized.floraDensity = NAN;
    sanitized.faunaDensity = INFINITY;
    PlanetPopulationAdvance(&sanitized, &invalid, 10.0);
    AssertPopulationValid(sanitized);
}

static void TestWindDriftResponse(void)
{
    assert(PlanetEcologyWindDrift(0.0f, false) == 0.0f);
    assert(PlanetEcologyWindDrift(0.0f, true) == 0.0f);
    assert(PlanetEcologyWindDrift(1.0f, false) > 0.0f);
    assert(PlanetEcologyWindDrift(1.0f, true) >
           PlanetEcologyWindDrift(1.0f, false));
    assert(PlanetEcologyWindDrift(4.0f, true) == 0.42f);
    assert(PlanetEcologyWindDrift(NAN, true) == 0.0f);

    uint32_t state = 0x28c4f91bu;
    for (int index = 0; index < 10000; index++) {
        state = state * 1664525u + 1013904223u;
        float wind = (float)(state & 1023u) / 256.0f - 1.0f;
        float ground = PlanetEcologyWindDrift(wind, false);
        float airborne = PlanetEcologyWindDrift(wind, true);
        assert(isfinite(ground) && isfinite(airborne));
        assert(ground >= 0.0f && ground <= 0.05f);
        assert(airborne >= 0.0f && airborne <= 0.42f);
        assert(airborne >= ground);
    }
}

static void TestHabitatChoice(void)
{
    const float neighbors[] = { 0.18f, 0.42f, 0.31f, 0.42f };
    PlanetHabitatChoice choice = PlanetEcologyChooseHabitat(0.12f, neighbors);
    assert(choice.currentActivity == 0.12f);
    assert(choice.selectedActivity == 0.42f);
    assert(choice.direction == PLANET_HABITAT_EAST);
    assert(choice.shouldSeek);
    AssertNear(choice.improvement, 0.30f, 0.000001f);

    const float flat[] = { 0.21f, 0.22f, 0.20f, 0.21f };
    PlanetHabitatChoice noMove = PlanetEcologyChooseHabitat(0.18f, flat);
    assert(!noMove.shouldSeek);
    assert(noMove.direction == PLANET_HABITAT_NONE);

    PlanetHabitatChoice repeated = PlanetEcologyChooseHabitat(0.12f, neighbors);
    assert(choice.currentActivity == repeated.currentActivity);
    assert(choice.selectedActivity == repeated.selectedActivity);
    assert(choice.improvement == repeated.improvement);
    assert(choice.direction == repeated.direction);
    assert(choice.shouldSeek == repeated.shouldSeek);
}

static void TestRandomizedHabitatChoiceProperties(void)
{
    uint32_t state = 0x4c8e21d9u;
    for (int sample = 0; sample < 10000; sample++) {
        float neighbors[4];
        for (int index = 0; index < 4; index++) neighbors[index] = TestUnit(&state);
        PlanetHabitatChoice choice = PlanetEcologyChooseHabitat(
            TestUnit(&state), neighbors);
        assert(isfinite(choice.currentActivity));
        assert(isfinite(choice.selectedActivity));
        assert(isfinite(choice.improvement));
        assert(choice.currentActivity >= 0.0f && choice.currentActivity <= 1.0f);
        assert(choice.selectedActivity >= choice.currentActivity);
        assert(choice.selectedActivity <= 1.0f);
        assert(choice.direction >= PLANET_HABITAT_NONE &&
               choice.direction <= PLANET_HABITAT_WEST);
        assert(!choice.shouldSeek || choice.improvement >= 0.06f);
    }
}

static void TestFaunaPopulationAndSpawnControls(void)
{
    assert(PlanetFaunaPopulationCap(0.0f, 44) == 0);
    assert(PlanetFaunaPopulationCap(NAN, 44) == 0);
    assert(PlanetFaunaPopulationCap(0.01f, 44) == 1);
    assert(PlanetFaunaPopulationCap(0.50f, 44) == 11);
    assert(PlanetFaunaPopulationCap(1.0f, 44) == 21);
    assert(PlanetFaunaPopulationCap(1.0f, 8) == 8);
    assert(PlanetFaunaPopulationCap(1.0f, 0) == 0);

    assert(!PlanetFaunaSpawnAccepted(0.0f, 0u));
    assert(!PlanetFaunaSpawnAccepted(NAN, 0u));
    assert(PlanetFaunaSpawnAccepted(0.50f, 499u));
    assert(!PlanetFaunaSpawnAccepted(0.50f, 500u));
    assert(PlanetFaunaSpawnAccepted(1.0f, 999u));
    assert(!PlanetFaunaSpawnAccepted(1.0f, 1000u));
}

static void TestFaunaBehaviorDecision(void)
{
    PlanetFaunaRuntimeState active = PlanetEcologyFaunaRuntime(1.0f, 1.0f);
    PlanetFaunaBehaviorInput input = {
        .runtime = active,
        .fleeYaw = 0.73f,
        .wanderYaw = 1.27f,
        .baseThinkInterval = 4.0f,
        .baseWanderDuration = 2.0f,
        .wanderRoll = 54u
    };
    PlanetFaunaBehaviorDecision wander = PlanetFaunaChooseBehavior(&input);
    assert(wander.behavior == PLANET_FAUNA_BEHAVIOR_WANDER);
    assert(wander.yaw == input.wanderYaw);
    assert(wander.moveDuration == 2.0f);
    assert(wander.thinkInterval == 4.0f);
    assert(wander.movementFloor == 0.0f);

    input.wanderRoll = 55u;
    PlanetFaunaBehaviorDecision idle = PlanetFaunaChooseBehavior(&input);
    assert(idle.behavior == PLANET_FAUNA_BEHAVIOR_IDLE);
    assert(idle.moveDuration == 0.0f);

    input.threatened = true;
    PlanetFaunaBehaviorDecision flee = PlanetFaunaChooseBehavior(&input);
    assert(flee.behavior == PLANET_FAUNA_BEHAVIOR_FLEE);
    assert(flee.yaw == input.fleeYaw);
    assert(flee.moveDuration == 0.8f);
    assert(flee.movementFloor == 0.28f);

    input = (PlanetFaunaBehaviorInput){
        .runtime = PlanetEcologyFaunaRuntime(0.20f, 1.0f),
        .habitat = {
            .improvement = 0.40f,
            .direction = PLANET_HABITAT_EAST,
            .shouldSeek = true
        },
        .baseThinkInterval = 3.0f,
        .baseWanderDuration = 1.5f,
        .wanderRoll = 99u
    };
    PlanetFaunaBehaviorDecision seek = PlanetFaunaChooseBehavior(&input);
    assert(seek.behavior == PLANET_FAUNA_BEHAVIOR_SEEK_HABITAT);
    assert(fabsf(seek.yaw - 0.5f * 3.14159265358979323846f) < 0.000001f);
    assert(fabsf(seek.moveDuration - 1.71f) < 0.000001f);
    assert(seek.movementFloor == 0.22f);
    assert(seek.thinkInterval > input.baseThinkInterval);

    input.colony = true;
    PlanetFaunaBehaviorDecision colony = PlanetFaunaChooseBehavior(&input);
    assert(colony.behavior == PLANET_FAUNA_BEHAVIOR_IDLE);
    input.colony = false;
    input.runtime = PlanetEcologyFaunaRuntime(0.0f, 1.0f);
    input.habitat.shouldSeek = false;
    input.wanderRoll = 0u;
    PlanetFaunaBehaviorDecision dormant = PlanetFaunaChooseBehavior(&input);
    assert(dormant.behavior == PLANET_FAUNA_BEHAVIOR_IDLE);
    assert(PlanetFaunaChooseBehavior(NULL).behavior ==
           PLANET_FAUNA_BEHAVIOR_IDLE);
}

static void TestRandomizedFaunaBehaviorProperties(void)
{
    uint32_t state = 0x6d41a2f3u;
    for (int sample = 0; sample < 10000; sample++) {
        float activity = TestUnit(&state);
        float capacity = fmaxf(TestUnit(&state), 0.0002f);
        float neighbors[4];
        for (int index = 0; index < 4; index++) {
            neighbors[index] = TestUnit(&state);
        }
        PlanetFaunaBehaviorInput input = {
            .runtime = PlanetEcologyFaunaRuntime(activity, capacity),
            .habitat = PlanetEcologyChooseHabitat(activity, neighbors),
            .fleeYaw = TestUnit(&state) * 6.28f - 3.14f,
            .wanderYaw = TestUnit(&state) * 6.28f,
            .baseThinkInterval = TestUnit(&state) * 8.0f,
            .baseWanderDuration = TestUnit(&state) * 4.0f,
            .wanderRoll = (uint32_t)(TestUnit(&state) * 99.0f),
            .colony = TestUnit(&state) > 0.82f,
            .threatened = TestUnit(&state) > 0.88f
        };
        PlanetFaunaBehaviorDecision decision =
            PlanetFaunaChooseBehavior(&input);
        PlanetFaunaBehaviorDecision repeated =
            PlanetFaunaChooseBehavior(&input);
        assert(decision.behavior >= PLANET_FAUNA_BEHAVIOR_IDLE &&
               decision.behavior <= PLANET_FAUNA_BEHAVIOR_FLEE);
        assert(isfinite(decision.yaw));
        assert(isfinite(decision.moveDuration) &&
               decision.moveDuration >= 0.0f);
        assert(isfinite(decision.thinkInterval) &&
               decision.thinkInterval >= 0.0f);
        assert(isfinite(decision.movementFloor) &&
               decision.movementFloor >= 0.0f &&
               decision.movementFloor <= 0.28f);
        assert(decision.behavior == repeated.behavior);
        assert(decision.yaw == repeated.yaw);
        assert(decision.moveDuration == repeated.moveDuration);
        assert(decision.thinkInterval == repeated.thinkInterval);
        assert(decision.movementFloor == repeated.movementFloor);
        if (input.threatened) {
            assert(decision.behavior == PLANET_FAUNA_BEHAVIOR_FLEE);
        }
        if (input.colony && !input.threatened) {
            assert(decision.behavior == PLANET_FAUNA_BEHAVIOR_IDLE);
        }
    }
}

static void TestRandomizedLocalProperties(void)
{
    uint32_t state = 0x7d493a21u;
    PlanetEcologyTraits traits = CarbonTraits();
    for (int sample = 0; sample < 10000; sample++) {
        PlanetLocalEnvironment environment = TemperateEnvironment();
        environment.meanTemperatureK = 170.0f + TestUnit(&state) * 330.0f;
        environment.currentTemperatureK = 170.0f + TestUnit(&state) * 330.0f;
        environment.seasonalAmplitudeK = TestUnit(&state) * 130.0f;
        environment.liquidWaterAccess = TestUnit(&state);
        environment.soilMoisture = TestUnit(&state);
        environment.meanPrecipitation = TestUnit(&state);
        environment.precipitationRate = TestUnit(&state);
        environment.meanUsableLight = TestUnit(&state) * 1.4f;
        environment.currentUsableLight = TestUnit(&state) * 1.4f;
        environment.stormExposure = TestUnit(&state);
        environment.currentStorm = TestUnit(&state);
        environment.elevation = TestUnit(&state);
        environment.slope = TestUnit(&state);
        environment.shelter = TestUnit(&state);
        environment.biomeSupport = TestUnit(&state);
        PlanetEcologySuitability suitability = PlanetEcologyEvaluateLocal(
            &environment, &traits, TestUnit(&state), TestUnit(&state));
        AssertSuitabilityValid(suitability);
        assert(suitability.floraActivity <= suitability.floraCapacity + 0.16f);
        assert(suitability.faunaActivity <= suitability.faunaCapacity + 0.16f);
    }
}

int main(void)
{
    TestDeterministicHistory();
    TestAgeAndSurfaceGates();
    TestPopulationIsMostlyBarren();
    TestComplexEcologyRemainsRareOnHabitableWorlds();
    TestDensityCapsPreComplexLife();
    TestLocalSuitabilityIsDeterministic();
    TestLocalEnvironmentalControls();
    TestWeatherChangesActivityNotPermanentCapacity();
    TestCurrentSeasonChangesActivityOnly();
    TestProducerActivityFeedsFaunaActivity();
    TestFaunaRuntimeResponse();
    TestRandomizedLocalProperties();
    TestRandomizedFaunaRuntimeProperties();
    TestFloraRuntimeResponse();
    TestRandomizedFloraRuntimeProperties();
    TestPopulationRecoveryAndMortality();
    TestPopulationSeasonalLag();
    TestPopulationDeterminismAndFoodChain();
    TestPopulationDisturbanceAndRecovery();
    TestRandomizedPopulationDisturbanceProperties();
    TestFaunaHarvestPressureModel();
    TestRandomizedFaunaHarvestProperties();
    TestPopulationMigrationDrivers();
    TestRandomizedPopulationMigrationProperties();
    TestRandomizedPopulationProperties();
    TestWindDriftResponse();
    TestHabitatChoice();
    TestRandomizedHabitatChoiceProperties();
    TestFaunaPopulationAndSpawnControls();
    TestFaunaBehaviorDecision();
    TestRandomizedFaunaBehaviorProperties();
    puts("ecology_model tests passed");
    return 0;
}
