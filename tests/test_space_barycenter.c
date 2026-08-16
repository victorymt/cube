#include "space/space_barycenter.h"

#include "space/space_units.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static double VectorLength(Vector3 value)
{
    return sqrt((double)value.x * value.x + (double)value.y * value.y +
                (double)value.z * value.z);
}

static Vector3 VectorSubtractTest(Vector3 left, Vector3 right)
{
    return (Vector3){ left.x - right.x, left.y - right.y, left.z - right.z };
}

static Vector3 CenterOfMass(const SpaceBarycenterOrbit *orbit,
                            const SpaceBarycenterBodyState *states,
                            bool velocity)
{
    double totalMass = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (int i = 0; i < orbit->bodyCount; i++) {
        Vector3 value = velocity ? states[i].velocityGame : states[i].offsetGame;
        totalMass += orbit->massKg[i];
        x += orbit->massKg[i] * value.x;
        y += orbit->massKg[i] * value.y;
        z += orbit->massKg[i] * value.z;
    }
    return (Vector3){ (float)(x / totalMass), (float)(y / totalMass),
                      (float)(z / totalMass) };
}

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance);
}

static void AssertBarycenterAtRest(const SpaceBarycenterOrbit *orbit,
                                   const SpaceBarycenterBodyState *states)
{
    assert(VectorLength(CenterOfMass(orbit, states, false)) < 0.00001);
    assert(VectorLength(CenterOfMass(orbit, states, true)) < 0.00001);
}

static void AssertStateZero(const SpaceBarycenterBodyState *state)
{
    assert(state->offsetGame.x == 0.0f && state->offsetGame.y == 0.0f &&
           state->offsetGame.z == 0.0f);
    assert(state->velocityGame.x == 0.0f && state->velocityGame.y == 0.0f &&
           state->velocityGame.z == 0.0f);
}

static void TestSingleStar(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 1,
        .massKg = { SPACE_UNITS_SOLAR_MASS_KG }
    };
    SpaceBarycenterBodyState state[3];
    assert(SpaceBarycenterSolve(&orbit, 123.0, state, 3) == 1);
    assert(VectorLength(state[0].offsetGame) == 0.0);
    assert(VectorLength(state[0].velocityGame) == 0.0);
}

static void TestBinaryMassRatioAndPeriod(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 2,
        .massKg = { 1.2 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.4 * SPACE_UNITS_SOLAR_MASS_KG },
        .innerSeparationKm = SpaceUnitsGameDistanceToKilometers(48.0),
        .innerPhaseRad = 0.37,
        .innerInclinationRad = 0.18,
        .innerNodeRad = 1.1
    };
    SpaceBarycenterBodyState initial[3];
    SpaceBarycenterBodyState complete[3];
    assert(SpaceBarycenterSolve(&orbit, 0.0, initial, 3) == 2);
    AssertBarycenterAtRest(&orbit, initial);
    AssertNear(VectorLength(VectorSubtractTest(initial[1].offsetGame,
                                                initial[0].offsetGame)),
               48.0, 0.0001);
    AssertNear(VectorLength(initial[0].offsetGame) /
                   VectorLength(initial[1].offsetGame),
               orbit.massKg[1] / orbit.massKg[0], 0.00001);

    double period = SpaceUnitsSecondsToGameTime(SpaceUnitsKeplerPeriodSeconds(
        orbit.innerSeparationKm, orbit.massKg[0] + orbit.massKg[1]));
    assert(SpaceBarycenterSolve(&orbit, period, complete, 3) == 2);
    assert(VectorLength(VectorSubtractTest(initial[0].offsetGame,
                                           complete[0].offsetGame)) < 0.0001);
    assert(VectorLength(VectorSubtractTest(initial[1].offsetGame,
                                           complete[1].offsetGame)) < 0.0001);
}

static void TestHierarchicalTriple(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 3,
        .massKg = { 1.0 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.65 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.35 * SPACE_UNITS_SOLAR_MASS_KG },
        .innerSeparationKm = SpaceUnitsGameDistanceToKilometers(34.0),
        .outerSeparationKm = SpaceUnitsGameDistanceToKilometers(132.0),
        .innerPhaseRad = 0.8,
        .outerPhaseRad = 2.4,
        .innerInclinationRad = 0.11,
        .outerInclinationRad = -0.16,
        .innerNodeRad = 0.2,
        .outerNodeRad = 1.7
    };
    SpaceBarycenterBodyState states[3];
    assert(SpaceBarycenterSolve(&orbit, 81.25, states, 3) == 3);
    AssertBarycenterAtRest(&orbit, states);
    AssertNear(VectorLength(VectorSubtractTest(states[1].offsetGame,
                                                states[0].offsetGame)),
               34.0, 0.0001);

    double innerMass = orbit.massKg[0] + orbit.massKg[1];
    Vector3 innerCenter = {
        (float)((states[0].offsetGame.x * orbit.massKg[0] +
                 states[1].offsetGame.x * orbit.massKg[1]) / innerMass),
        (float)((states[0].offsetGame.y * orbit.massKg[0] +
                 states[1].offsetGame.y * orbit.massKg[1]) / innerMass),
        (float)((states[0].offsetGame.z * orbit.massKg[0] +
                 states[1].offsetGame.z * orbit.massKg[1]) / innerMass)
    };
    AssertNear(VectorLength(VectorSubtractTest(states[2].offsetGame, innerCenter)),
               132.0, 0.0002);
}

static void TestFreeFlight(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 2,
        .motion = SPACE_BARYCENTER_FREE_FLIGHT,
        .massKg = { 1.0 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.5 * SPACE_UNITS_SOLAR_MASS_KG },
        .freeFlightOffsetGame = {
            { -13.333333f, 0.0f, 0.0f },
            { 26.666666f, 0.0f, 0.0f }
        },
        .freeFlightVelocityGame = {
            { -0.333333f, 0.1f, 0.0f },
            { 0.666666f, -0.2f, 0.0f }
        }
    };
    SpaceBarycenterBodyState initial[3];
    SpaceBarycenterBodyState later[3];
    assert(SpaceBarycenterSolve(&orbit, 0.0, initial, 3) == 2);
    assert(SpaceBarycenterSolve(&orbit, 12.5, later, 3) == 2);
    AssertBarycenterAtRest(&orbit, initial);
    AssertBarycenterAtRest(&orbit, later);
    assert(VectorLength(VectorSubtractTest(later[1].offsetGame,
                                           later[0].offsetGame)) >
           VectorLength(VectorSubtractTest(initial[1].offsetGame,
                                           initial[0].offsetGame)));
    for (int i = 0; i < 2; i++) {
        assert(VectorLength(VectorSubtractTest(
                   later[i].velocityGame,
                   initial[i].velocityGame)) < 0.000001);
    }
}

static void TestOuterFreeFlight(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 3,
        .motion = SPACE_BARYCENTER_OUTER_FREE_FLIGHT,
        .massKg = { 1.0 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.5 * SPACE_UNITS_SOLAR_MASS_KG,
                    0.25 * SPACE_UNITS_SOLAR_MASS_KG },
        .innerSeparationKm = SpaceUnitsGameDistanceToKilometers(40.0),
        .innerEccentricity = 0.24,
        .innerPhaseRad = 0.7,
        .innerArgumentPeriapsisRad = 1.2,
        .innerInclinationRad = 0.17,
        .innerNodeRad = 0.4,
        .outerFreeOffsetGame = { 120.0f, -6.0f, 2.0f },
        .outerFreeVelocityGame = { 0.25f, 0.02f, -0.08f }
    };
    SpaceBarycenterBodyState initial[3];
    SpaceBarycenterBodyState later[3];
    assert(SpaceBarycenterSolve(&orbit, 0.0, initial, 3) == 3);
    assert(SpaceBarycenterSolve(&orbit, 20.0, later, 3) == 3);
    AssertBarycenterAtRest(&orbit, initial);
    AssertBarycenterAtRest(&orbit, later);

    double innerMass = orbit.massKg[0] + orbit.massKg[1];
    Vector3 initialInnerCenter = {
        (float)((initial[0].offsetGame.x * orbit.massKg[0] +
                 initial[1].offsetGame.x * orbit.massKg[1]) / innerMass),
        (float)((initial[0].offsetGame.y * orbit.massKg[0] +
                 initial[1].offsetGame.y * orbit.massKg[1]) / innerMass),
        (float)((initial[0].offsetGame.z * orbit.massKg[0] +
                 initial[1].offsetGame.z * orbit.massKg[1]) / innerMass)
    };
    Vector3 laterInnerCenter = {
        (float)((later[0].offsetGame.x * orbit.massKg[0] +
                 later[1].offsetGame.x * orbit.massKg[1]) / innerMass),
        (float)((later[0].offsetGame.y * orbit.massKg[0] +
                 later[1].offsetGame.y * orbit.massKg[1]) / innerMass),
        (float)((later[0].offsetGame.z * orbit.massKg[0] +
                 later[1].offsetGame.z * orbit.massKg[1]) / innerMass)
    };
    Vector3 initialOuter = VectorSubtractTest(initial[2].offsetGame,
                                               initialInnerCenter);
    Vector3 laterOuter = VectorSubtractTest(later[2].offsetGame,
                                             laterInnerCenter);
    Vector3 expectedOuter = {
        orbit.outerFreeOffsetGame.x + orbit.outerFreeVelocityGame.x * 20.0f,
        orbit.outerFreeOffsetGame.y + orbit.outerFreeVelocityGame.y * 20.0f,
        orbit.outerFreeOffsetGame.z + orbit.outerFreeVelocityGame.z * 20.0f
    };
    assert(VectorLength(VectorSubtractTest(initialOuter,
                                           orbit.outerFreeOffsetGame)) <
           0.0001);
    assert(VectorLength(VectorSubtractTest(laterOuter,
                                           expectedOuter)) < 0.0001);
}

static void TestInvalidInputs(void)
{
    SpaceBarycenterOrbit orbit = {
        .bodyCount = 2,
        .massKg = { SPACE_UNITS_SOLAR_MASS_KG,
                    0.5 * SPACE_UNITS_SOLAR_MASS_KG },
        .innerSeparationKm = SpaceUnitsGameDistanceToKilometers(40.0),
        .innerPhaseRad = 0.2,
        .innerInclinationRad = 0.1,
        .innerNodeRad = 0.4
    };
    SpaceBarycenterBodyState states[3] = {
        { .offsetGame = { 1.0f, 2.0f, 3.0f },
          .velocityGame = { 4.0f, 5.0f, 6.0f } },
        { .offsetGame = { 7.0f, 8.0f, 9.0f },
          .velocityGame = { 10.0f, 11.0f, 12.0f } },
        { .offsetGame = { 13.0f, 14.0f, 15.0f },
          .velocityGame = { 16.0f, 17.0f, 18.0f } }
    };

    orbit.massKg[1] = NAN;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.massKg[1] = 0.5 * SPACE_UNITS_SOLAR_MASS_KG;
    orbit.innerSeparationKm = NAN;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.innerSeparationKm = SpaceUnitsGameDistanceToKilometers(40.0);
    orbit.innerPhaseRad = INFINITY;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.innerPhaseRad = 0.2;
    assert(SpaceBarycenterSolve(&orbit, NAN, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 1) == 0);
    AssertStateZero(&states[0]);
    assert(SpaceBarycenterSolve(&orbit, 0.0, NULL, 3) == 0);

    orbit.motion = (SpaceBarycenterMotion)99;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.motion = SPACE_BARYCENTER_FREE_FLIGHT;
    orbit.freeFlightOffsetGame[0].x = NAN;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.bodyCount = 3;
    orbit.motion = SPACE_BARYCENTER_BOUND;
    orbit.massKg[2] = 0.25 * SPACE_UNITS_SOLAR_MASS_KG;
    orbit.outerSeparationKm = SpaceUnitsGameDistanceToKilometers(120.0);
    orbit.outerPhaseRad = 0.4;
    orbit.outerInclinationRad = 0.2;
    orbit.outerNodeRad = 0.8;
    orbit.outerSeparationKm = INFINITY;
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.outerSeparationKm = SpaceUnitsGameDistanceToKilometers(120.0);
    assert(SpaceBarycenterSolve(&orbit, DBL_MAX, states, 3) == 3);
    for (int i = 0; i < 3; i++) {
        assert(isfinite(states[i].offsetGame.x) &&
               isfinite(states[i].velocityGame.x));
    }

    orbit.motion = SPACE_BARYCENTER_OUTER_FREE_FLIGHT;
    orbit.outerFreeOffsetGame = (Vector3){ NAN, 0.0f, 0.0f };
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);

    orbit.bodyCount = 2;
    orbit.outerFreeOffsetGame = (Vector3){ 0 };
    assert(SpaceBarycenterSolve(&orbit, 0.0, states, 3) == 0);
    for (int i = 0; i < 3; i++) AssertStateZero(&states[i]);
}

static void TestGeneratedBarycenterStates(void)
{
    const int sampleCount = 512;
    for (int seed = 0; seed < sampleCount; seed++) {
        SpaceBarycenterOrbit orbit = {
            .bodyCount = 2 + seed % 2,
            .massKg = {
                (0.55 + (seed % 17) * 0.035) * SPACE_UNITS_SOLAR_MASS_KG,
                (0.20 + (seed % 13) * 0.025) * SPACE_UNITS_SOLAR_MASS_KG,
                (0.15 + (seed % 11) * 0.020) * SPACE_UNITS_SOLAR_MASS_KG
            },
            .innerSeparationKm = SpaceUnitsGameDistanceToKilometers(
                14.0 + (seed % 25)),
            .outerSeparationKm = SpaceUnitsGameDistanceToKilometers(
                96.0 + (seed % 83)),
            .innerPhaseRad = (seed % 31) * 0.19,
            .outerPhaseRad = (seed % 29) * 0.23,
            .innerInclinationRad = -0.25 + (seed % 19) * 0.025,
            .outerInclinationRad = -0.35 + (seed % 23) * 0.031,
            .innerNodeRad = (seed % 37) * 0.17,
            .outerNodeRad = (seed % 41) * 0.13
        };
        SpaceBarycenterBodyState states[3];
        SpaceBarycenterBodyState repeated[3];
        double simulationTime = seed * 0.75 + 0.125;
        assert(SpaceBarycenterSolve(&orbit, simulationTime, states, 3) ==
               orbit.bodyCount);
        assert(SpaceBarycenterSolve(&orbit, simulationTime, repeated, 3) ==
               orbit.bodyCount);
        assert(memcmp(states, repeated, sizeof(states)) == 0);
        AssertBarycenterAtRest(&orbit, states);

        double separation = VectorLength(VectorSubtractTest(
            states[1].offsetGame, states[0].offsetGame));
        AssertNear(separation, 14.0 + (seed % 25), 0.0003);
        if (orbit.bodyCount == 3) {
            assert(VectorLength(states[2].offsetGame) > 0.0);
            assert(VectorLength(states[2].velocityGame) > 0.0);
        }
    }
}

int main(void)
{
    TestSingleStar();
    TestBinaryMassRatioAndPeriod();
    TestHierarchicalTriple();
    TestFreeFlight();
    TestOuterFreeFlight();
    TestInvalidInputs();
    TestGeneratedBarycenterStates();
    puts("space_barycenter tests passed");
    return 0;
}
