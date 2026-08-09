#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void AssertRelativeNear(double actual, double expected, double tolerance)
{
    double scale = fmax(fabs(expected), 1.0);
    assert(fabs(actual - expected) <= tolerance * scale);
}

static void TestRoundTrips(void)
{
    AssertRelativeNear(SpaceUnitsGameDistanceToKilometers(340.0),
                       SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 1e-12);
    AssertRelativeNear(SpaceUnitsGameMassToKilograms(1.0),
                       SPACE_UNITS_EARTH_MASS_KG, 1e-12);
    AssertRelativeNear(
        SpaceUnitsKilometersToGameDistance(
            SpaceUnitsGameDistanceToKilometers(123.456)),
        123.456, 1e-12);
    AssertRelativeNear(
        SpaceUnitsSecondsToGameTime(SpaceUnitsGameTimeToSeconds(42.5)),
        42.5, 1e-12);
    AssertRelativeNear(
        SpaceUnitsKilogramsToGameMass(
            SpaceUnitsGameMassToKilograms(3.25)),
        3.25, 1e-12);
    AssertRelativeNear(
        SpaceUnitsKilometersPerSecondToGameVelocity(
            SpaceUnitsGameVelocityToKilometersPerSecond(7.5)),
        7.5, 1e-12);
    AssertRelativeNear(
        SpaceUnitsKilometersPerSecondSquaredToGameAcceleration(
            SpaceUnitsGameAccelerationToKilometersPerSecondSquared(2.75)),
        2.75, 1e-12);
}

static void TestEarthOrbit(void)
{
    double periodSeconds = SpaceUnitsKeplerPeriodSeconds(
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_SOLAR_MASS_KG);
    double periodGame = SpaceUnitsSecondsToGameTime(periodSeconds);
    AssertRelativeNear(periodGame, 365.2514, 2e-5);

    double velocity = SpaceUnitsCircularOrbitVelocityKilometersPerSecond(
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_SOLAR_MASS_KG);
    AssertRelativeNear(velocity, 29.7851, 2e-5);
    AssertRelativeNear(
        SpaceUnitsGameVelocityToKilometersPerSecond(
            SpaceUnitsKilometersPerSecondToGameVelocity(velocity)),
        velocity, 1e-12);
}

static void TestGravityConstants(void)
{
    AssertRelativeNear(
        SpaceUnitsGravitationalParameterKm(SPACE_UNITS_SOLAR_MASS_KG),
        1.3271645e11, 2e-7);
    AssertRelativeNear(
        SpaceUnitsGravitationalParameterKm(SPACE_UNITS_EARTH_MASS_KG),
        3.986025e5, 2e-7);

    double distanceScale = SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
    double timeScale = SPACE_UNITS_SECONDS_PER_GAME_TIME;
    double solarMuFromGame = SpaceUnitsGravitationalParameterGame(
                                 SPACE_UNITS_SOLAR_MASS_KG) *
                             distanceScale * distanceScale * distanceScale /
                             (timeScale * timeScale);
    AssertRelativeNear(solarMuFromGame, 1.3271645e11, 2e-7);

    double earthSoi = SpaceUnitsLaplaceSphereOfInfluenceKm(
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_EARTH_MASS_KG,
        SPACE_UNITS_SOLAR_MASS_KG);
    AssertRelativeNear(earthSoi, 924600.0, 0.002);
}

int main(void)
{
    TestRoundTrips();
    TestEarthOrbit();
    TestGravityConstants();
    puts("space_units tests passed");
    return 0;
}
