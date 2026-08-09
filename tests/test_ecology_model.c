#include "ecology_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

static void AssertNear(float actual, float expected, float tolerance)
{
    assert(fabsf(actual - expected) <= tolerance);
}

static float TestUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
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

int main(void)
{
    TestDeterministicHistory();
    TestAgeAndSurfaceGates();
    TestPopulationIsMostlyBarren();
    TestComplexEcologyRemainsRareOnHabitableWorlds();
    TestDensityCapsPreComplexLife();
    puts("ecology_model tests passed");
    return 0;
}
