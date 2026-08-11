#include "space_system.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static float TestUnit(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state & 0x00ffffffu) / 16777215.0f;
}

static void AssertFormationValid(const SpaceSystemFormation *formation)
{
    assert(formation);
    assert(isfinite(formation->metallicity));
    assert(formation->metallicity >= 0.05f && formation->metallicity <= 1.0f);
    assert(isfinite(formation->diskMassEarth));
    assert(formation->diskMassEarth >= 0.08f);
    assert(isfinite(formation->snowLineGame));
    assert(isfinite(formation->habitableInnerGame));
    assert(isfinite(formation->habitableOuterGame));
    assert(formation->snowLineGame > 0.0f);
    assert(formation->habitableInnerGame < formation->habitableOuterGame);
    assert(formation->planetCount >= 1 &&
           formation->planetCount <= SPACE_SYSTEM_MAX_PLANETS);

    float previousOrbit = 0.0f;
    float planetaryMass = 0.0f;
    for (int index = 0; index < formation->planetCount; index++) {
        const SpaceSystemFormationPlanet *planet = &formation->planets[index];
        assert(isfinite(planet->orbitGame));
        assert(isfinite(planet->massEarth));
        assert(isfinite(planet->radiusEarth));
        assert(planet->orbitGame >= formation->innerStableOrbitGame);
        assert(planet->orbitGame <= formation->outerStableOrbitGame + 0.001f);
        assert(planet->massEarth >= 0.08f);
        assert(planet->radiusEarth >= 0.35f);
        if (index > 0) {
            assert(planet->orbitGame > previousOrbit);
            assert(planet->orbitGame - previousOrbit >= 37.99f);
            assert(planet->orbitGame >= previousOrbit * 1.199f);
        }
        if (planet->gasGiant) {
            assert(planet->orbitGame >= formation->snowLineGame * 0.78f);
            assert(planet->massEarth >= 10.0f);
            assert(planet->radiusEarth >= 2.6f);
        } else {
            assert(planet->radiusEarth <= 1.75f);
        }
        planetaryMass += planet->massEarth;
        previousOrbit = planet->orbitGame;
    }
    assert(planetaryMass <= formation->diskMassEarth + 0.001f);
}

static void TestDeterminism(void)
{
    SpaceSystemFormationInput input = {
        .seed = 0x27d4eb2fu,
        .stellarMassSolar = 1.05f,
        .stellarLuminositySolar = 1.18f,
        .stellarAgeGyr = 4.1f,
        .stellarCount = 1,
        .innerStabilityLimitGame = 180.0f,
        .outerLimitGame = 650.0f
    };
    SpaceSystemFormation first;
    SpaceSystemFormation second;
    memset(&first, 0x5a, sizeof(first));
    memset(&second, 0xa5, sizeof(second));
    assert(SpaceSystemFormationGenerate(&input, &first));
    assert(SpaceSystemFormationGenerate(&input, &second));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    AssertFormationValid(&first);
}

static void TestCausalStellarInputs(void)
{
    SpaceSystemFormationInput input = {
        .seed = 7u,
        .stellarMassSolar = 1.0f,
        .stellarLuminositySolar = 0.25f,
        .stellarAgeGyr = 2.0f,
        .stellarCount = 1,
        .innerStabilityLimitGame = 180.0f,
        .outerLimitGame = 650.0f
    };
    SpaceSystemFormation dim;
    SpaceSystemFormation bright;
    assert(SpaceSystemFormationGenerate(&input, &dim));
    input.stellarLuminositySolar = 4.0f;
    assert(SpaceSystemFormationGenerate(&input, &bright));
    assert(bright.snowLineGame > dim.snowLineGame);
    assert(bright.habitableInnerGame > dim.habitableInnerGame);
    assert(bright.habitableOuterGame > dim.habitableOuterGame);
    AssertFormationValid(&dim);
    AssertFormationValid(&bright);

    input.stellarLuminositySolar = 1.0f;
    input.stellarMassSolar = 0.8f;
    input.stellarAgeGyr = 2.0f;
    input.stellarCount = 1;
    SpaceSystemFormation lowMass;
    SpaceSystemFormation highMass;
    SpaceSystemFormation old;
    SpaceSystemFormation multiple;
    assert(SpaceSystemFormationGenerate(&input, &lowMass));
    input.stellarMassSolar = 1.6f;
    assert(SpaceSystemFormationGenerate(&input, &highMass));
    assert(highMass.diskMassEarth > lowMass.diskMassEarth);
    input.stellarMassSolar = 0.8f;
    input.stellarAgeGyr = 10.0f;
    assert(SpaceSystemFormationGenerate(&input, &old));
    assert(old.metallicity < lowMass.metallicity);
    assert(old.diskMassEarth < lowMass.diskMassEarth);
    input.stellarAgeGyr = 2.0f;
    input.stellarCount = 3;
    assert(SpaceSystemFormationGenerate(&input, &multiple));
    assert(multiple.diskMassEarth < lowMass.diskMassEarth);
}

static void TestRandomizedProperties(void)
{
    uint32_t state = 0x6a31e2d7u;
    int gasSystems = 0;
    int sixPlanetSystems = 0;
    for (int sample = 0; sample < 20000; sample++) {
        SpaceSystemFormationInput input = {
            .seed = state,
            .stellarMassSolar = 0.08f + TestUnit(&state) * 12.0f,
            .stellarLuminositySolar = 0.01f + TestUnit(&state) * 80.0f,
            .stellarAgeGyr = TestUnit(&state) * 12.0f,
            .stellarCount = 1 + (int)(TestUnit(&state) * 3.0f),
            .innerStabilityLimitGame = 120.0f + TestUnit(&state) * 460.0f,
            .outerLimitGame = 640.0f + TestUnit(&state) * 250.0f
        };
        SpaceSystemFormation formation;
        assert(SpaceSystemFormationGenerate(&input, &formation));
        AssertFormationValid(&formation);
        if (formation.planetCount == SPACE_SYSTEM_MAX_PLANETS) sixPlanetSystems++;
        for (int index = 0; index < formation.planetCount; index++) {
            if (formation.planets[index].gasGiant) gasSystems++;
        }
    }
    assert(gasSystems > 0);
    assert(sixPlanetSystems > 0);
}

static void TestLowMassDiskBudget(void)
{
    for (uint32_t seed = 1u; seed <= 1024u; seed++) {
        SpaceSystemFormationInput input = {
            .seed = seed,
            .stellarMassSolar = 0.08f,
            .stellarLuminositySolar = 0.01f,
            .stellarAgeGyr = 20.0f,
            .stellarCount = 3,
            .innerStabilityLimitGame = 180.0f,
            .outerLimitGame = 650.0f
        };
        SpaceSystemFormation formation;
        assert(SpaceSystemFormationGenerate(&input, &formation));
        AssertFormationValid(&formation);
    }
}

static void TestInvalidInput(void)
{
    SpaceSystemFormation output;
    const SpaceSystemFormation cleared = { 0 };
    SpaceSystemFormationInput input = { 0 };
    input.stellarMassSolar = NAN;
    input.stellarLuminositySolar = 1.0f;
    input.stellarAgeGyr = 1.0f;
    memset(&output, 0xa5, sizeof(output));
    assert(!SpaceSystemFormationGenerate(&input, &output));
    assert(memcmp(&output, &cleared, sizeof(output)) == 0);
    memset(&output, 0xa5, sizeof(output));
    assert(!SpaceSystemFormationGenerate(NULL, &output));
    assert(memcmp(&output, &cleared, sizeof(output)) == 0);
    assert(!SpaceSystemFormationGenerate(&input, NULL));
}

int main(void)
{
    TestDeterminism();
    TestCausalStellarInputs();
    TestRandomizedProperties();
    TestLowMassDiskBudget();
    TestInvalidInput();
    puts("space system tests passed");
    return 0;
}
