#include "space_barycenter.h"

#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

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

int main(void)
{
    TestSingleStar();
    TestBinaryMassRatioAndPeriod();
    TestHierarchicalTriple();
    puts("space_barycenter tests passed");
    return 0;
}
