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
    puts("ecology_model tests passed");
    return 0;
}
