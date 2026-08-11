#include "space_orbit.h"
#include "space_units.h"

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

static Vector3 VectorSubtract(Vector3 left, Vector3 right)
{
    return (Vector3){ left.x - right.x, left.y - right.y,
                      left.z - right.z };
}

static Vector3 VectorScale(Vector3 value, double scale)
{
    return (Vector3){ (float)((double)value.x * scale),
                      (float)((double)value.y * scale),
                      (float)((double)value.z * scale) };
}

static Vector3 VectorCross(Vector3 left, Vector3 right)
{
    return (Vector3){
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x
    };
}

static void AssertRelative(double actual, double expected, double tolerance)
{
    double scale = fmax(fabs(expected), 1e-12);
    assert(fabs(actual - expected) <= tolerance * scale);
}

static void TestInvalidOrbit(void)
{
    SpaceKeplerState state;
    assert(!SpaceKeplerOrbitIsValid(NULL));
    assert(!SpaceKeplerStateAtTime(NULL, 0.0, &state));

    SpaceKeplerOrbit orbit = {
        .semiMajorAxisKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
        .centralMassKg = SPACE_UNITS_SOLAR_MASS_KG,
        .eccentricity = 0.1
    };
    assert(SpaceKeplerOrbitIsValid(&orbit));
    assert(!SpaceKeplerStateAtTime(&orbit, NAN, &state));
    orbit.eccentricity = 1.0;
    assert(!SpaceKeplerOrbitIsValid(&orbit));
    assert(!SpaceKeplerStateAtTime(&orbit, 0.0, &state));
    assert(!SpaceKeplerStateAtTime(&orbit, 0.0, NULL));

    orbit.eccentricity = 0.1;
    orbit.semiMajorAxisKm = 1.0e100;
    state.positionGame = (Vector3){ 1.0f, 2.0f, 3.0f };
    state.velocityGame = (Vector3){ 4.0f, 5.0f, 6.0f };
    assert(!SpaceKeplerStateAtTime(&orbit, 0.0, &state));
    assert(state.positionGame.x == 0.0f && state.positionGame.y == 0.0f &&
           state.positionGame.z == 0.0f && state.velocityGame.x == 0.0f &&
           state.velocityGame.y == 0.0f && state.velocityGame.z == 0.0f);
}

static void AssertOrbitProperties(const SpaceKeplerOrbit *orbit)
{
    assert(SpaceKeplerOrbitIsValid(orbit));
    double mu = SpaceUnitsGravitationalParameterKm(orbit->centralMassKg);
    double periodSeconds = SpaceUnitsKeplerPeriodSeconds(
        orbit->semiMajorAxisKm, orbit->centralMassKg);
    double periodGame = SpaceUnitsSecondsToGameTime(periodSeconds);
    double expectedEnergy = -mu / (2.0 * orbit->semiMajorAxisKm);
    double expectedAngularMomentum = sqrt(
        mu * orbit->semiMajorAxisKm *
        (1.0 - orbit->eccentricity * orbit->eccentricity));

    for (int sample = 0; sample < 64; sample++) {
        double time = periodGame * (double)sample / 64.0 + 0.375;
        SpaceKeplerState state;
        assert(SpaceKeplerStateAtTime(orbit, time, &state));

        double radiusKm = SpaceUnitsGameDistanceToKilometers(
            VectorLength(state.positionGame));
        double speedKmPerSecond = SpaceUnitsGameVelocityToKilometersPerSecond(
            VectorLength(state.velocityGame));
        double expectedSpeed = sqrt(mu *
            (2.0 / radiusKm - 1.0 / orbit->semiMajorAxisKm));
        AssertRelative(speedKmPerSecond, expectedSpeed, 0.000002);

        double energy = 0.5 * speedKmPerSecond * speedKmPerSecond -
                        mu / radiusKm;
        AssertRelative(energy, expectedEnergy, 0.000004);

        Vector3 angularMomentumGame = VectorCross(state.positionGame,
                                                   state.velocityGame);
        double angularMomentumKm = VectorLength(angularMomentumGame) *
            SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE *
            SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE /
            SPACE_UNITS_SECONDS_PER_GAME_TIME;
        AssertRelative(angularMomentumKm, expectedAngularMomentum, 0.000002);

        double dt = periodGame * 0.0002;
        SpaceKeplerState before;
        SpaceKeplerState after;
        assert(SpaceKeplerStateAtTime(orbit, time - dt, &before));
        assert(SpaceKeplerStateAtTime(orbit, time + dt, &after));
        Vector3 sampledVelocity = VectorScale(
            VectorSubtract(after.positionGame, before.positionGame),
            1.0 / (2.0 * dt));
        double velocityError = VectorLength(VectorSubtract(
            state.velocityGame, sampledVelocity));
        assert(velocityError / VectorLength(state.velocityGame) < 0.0003);
    }

    SpaceKeplerState initial;
    SpaceKeplerState complete;
    assert(SpaceKeplerStateAtTime(orbit, -17.25, &initial));
    assert(SpaceKeplerStateAtTime(orbit, -17.25 + periodGame, &complete));
    assert(VectorLength(VectorSubtract(initial.positionGame,
                                       complete.positionGame)) < 0.003);
    assert(VectorLength(VectorSubtract(initial.velocityGame,
                                       complete.velocityGame)) < 0.00003);

    SpaceKeplerState repeated;
    assert(SpaceKeplerStateAtTime(orbit, -17.25, &repeated));
    assert(memcmp(&initial, &repeated, sizeof(initial)) == 0);

    SpaceKeplerState largeTime;
    SpaceKeplerState repeatedLargeTime;
    assert(SpaceKeplerStateAtTime(orbit, DBL_MAX, &largeTime));
    assert(SpaceKeplerStateAtTime(orbit, DBL_MAX, &repeatedLargeTime));
    assert(memcmp(&largeTime, &repeatedLargeTime,
                  sizeof(largeTime)) == 0);
    assert(isfinite(largeTime.positionGame.x) &&
           isfinite(largeTime.positionGame.y) &&
           isfinite(largeTime.positionGame.z) &&
           isfinite(largeTime.velocityGame.x) &&
           isfinite(largeTime.velocityGame.y) &&
           isfinite(largeTime.velocityGame.z));
}

static void TestOrbitProperties(void)
{
    static const double eccentricities[] = { 0.0, 0.05, 0.35, 0.75 };
    for (int index = 0; index < 4; index++) {
        SpaceKeplerOrbit orbit = {
            .semiMajorAxisKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM *
                               (0.25 + 1.25 * index),
            .centralMassKg = SPACE_UNITS_SOLAR_MASS_KG *
                             (0.35 + 0.6 * index),
            .eccentricity = eccentricities[index],
            .inclinationRad = -0.6 + 0.37 * index,
            .longitudeAscendingNodeRad = 0.3 + 1.1 * index,
            .argumentPeriapsisRad = 0.7 + 0.8 * index,
            .meanAnomalyAtEpochRad = -2.4 + 1.5 * index
        };
        AssertOrbitProperties(&orbit);
    }
}

int main(void)
{
    TestInvalidOrbit();
    TestOrbitProperties();
    puts("space orbit tests passed");
    return 0;
}
