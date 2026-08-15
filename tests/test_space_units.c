#include "space_units.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void AssertRelativeNear(double actual, double expected, double tolerance)
{
    double scale = fmax(fabs(expected), 1.0);
    assert(fabs(actual - expected) <= tolerance * scale);
}

static void TestRoundTrips(void)
{
    AssertRelativeNear(SpaceUnitsGameDistanceToKilometers(
                           SPACE_UNITS_GAME_DISTANCE_PER_AU),
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
        SpaceUnitsGameTimeToGigayears(
            SpaceUnitsGigayearsToGameTime(4.57)),
        4.57, 1e-12);
    AssertRelativeNear(SpaceUnitsGigayearsToGameTime(1.0),
                       SPACE_UNITS_GAME_TIME_PER_GIGAYEAR, 0.0);
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

static void TestMeanAnomalyTime(void)
{
    double anomaly = 0.0;
    assert(SpaceUnitsMeanAnomalyAtTime(0.5, 1.0, 0.0, &anomaly));
    AssertRelativeNear(anomaly, 0.5, 1e-12);
    assert(SpaceUnitsMeanAnomalyAtTime(0.5, 1.0,
                                       2.0 * 3.14159265358979323846,
                                       &anomaly));
    AssertRelativeNear(anomaly, 0.5, 1e-12);

    double first = 0.0;
    double second = 0.0;
    assert(SpaceUnitsMeanAnomalyAtTime(0.5, 1.0, DBL_MAX, &first));
    assert(SpaceUnitsMeanAnomalyAtTime(0.5, 1.0, DBL_MAX, &second));
    assert(first == second && isfinite(first));
    anomaly = 17.0;
    assert(!SpaceUnitsMeanAnomalyAtTime(NAN, 1.0, 0.0, &anomaly));
    assert(anomaly == 0.0);
    assert(!SpaceUnitsMeanAnomalyAtTime(0.0, 0.0, 0.0, &anomaly));
    assert(!SpaceUnitsMeanAnomalyAtTime(0.0, -1.0, 0.0, &anomaly));
    assert(!SpaceUnitsMeanAnomalyAtTime(0.0, INFINITY, 0.0, &anomaly));
    assert(!SpaceUnitsMeanAnomalyAtTime(0.0, 1.0, NAN, &anomaly));
    assert(!SpaceUnitsMeanAnomalyAtTime(0.0, 1.0, 0.0, NULL));
}

static void TestEccentricAnomalySolver(void)
{
    static const double eccentricities[] = {
        0.0, 0.35, 0.8, 0.999999, 0.999999999999,
        0.9999999999999999
    };
    static const double meanAnomalies[] = {
        -1.0e12, -3.5, -0.001, -1.0e-9, 0.0, 1.0e-9, 0.001, 3.5,
        1.0e12
    };
    for (size_t eccentricityIndex = 0;
         eccentricityIndex < sizeof(eccentricities) /
                             sizeof(eccentricities[0]); eccentricityIndex++) {
        for (size_t anomalyIndex = 0;
             anomalyIndex < sizeof(meanAnomalies) /
                             sizeof(meanAnomalies[0]); anomalyIndex++) {
            double eccentricAnomaly = 0.0;
            double meanAnomaly = meanAnomalies[anomalyIndex];
            assert(SpaceUnitsSolveEccentricAnomaly(
                meanAnomaly, eccentricities[eccentricityIndex],
                &eccentricAnomaly));
            double normalizedMeanAnomaly = fmod(meanAnomaly,
                                                2.0 * 3.14159265358979323846);
            if (normalizedMeanAnomaly > 3.14159265358979323846) {
                normalizedMeanAnomaly -= 2.0 * 3.14159265358979323846;
            } else if (normalizedMeanAnomaly < -3.14159265358979323846) {
                normalizedMeanAnomaly += 2.0 * 3.14159265358979323846;
            }
            long double residual = (long double)eccentricAnomaly -
                (long double)eccentricities[eccentricityIndex] *
                sinl((long double)eccentricAnomaly) -
                (long double)normalizedMeanAnomaly;
            assert(isfinite(eccentricAnomaly));
            assert(fabsl(residual) <= 2e-12L);
        }
    }
    double eccentricAnomaly = 42.0;
    assert(!SpaceUnitsSolveEccentricAnomaly(NAN, 0.5, &eccentricAnomaly));
    assert(eccentricAnomaly == 0.0);
    assert(!SpaceUnitsSolveEccentricAnomaly(0.0, 1.0, &eccentricAnomaly));
    assert(!SpaceUnitsSolveEccentricAnomaly(0.0, 0.5, NULL));

    double nearParabolicEccentricity = 0.9999999999999999;
    double periapsisDerivative = SpaceUnitsEccentricAnomalyDerivative(
        0.0, nearParabolicEccentricity);
    assert(periapsisDerivative == 1.0 - nearParabolicEccentricity);
    assert(SpaceUnitsEccentricAnomalyDerivative(
               NAN, nearParabolicEccentricity) == 0.0);
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

    double earthHill = SpaceUnitsHillSphereKm(
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_EARTH_MASS_KG,
        SPACE_UNITS_SOLAR_MASS_KG);
    AssertRelativeNear(earthHill, 1496500.0, 0.002);
    assert(earthSoi < earthHill);

    assert(isfinite(SpaceUnitsGravitationalParameterKm(DBL_MAX)) &&
           SpaceUnitsGravitationalParameterKm(DBL_MAX) > 0.0);
    assert(isfinite(SpaceUnitsGravitationalParameterGame(DBL_MAX)) &&
           SpaceUnitsGravitationalParameterGame(DBL_MAX) > 0.0);
    assert(SpaceUnitsSurfaceGravityKmPerSecondSquared(1.0, DBL_MIN) == 0.0);
    assert(SpaceUnitsKeplerMeanMotionGame(1.0e103,
                                          SPACE_UNITS_SOLAR_MASS_KG) == 0.0);
    assert(SpaceUnitsKeplerPeriodSeconds(
               1.0e-200, SPACE_UNITS_SOLAR_MASS_KG) == 0.0);
    assert(SpaceUnitsLaplaceSphereOfInfluenceKm(
               DBL_MAX, DBL_MAX, DBL_MIN) == 0.0);
    assert(SpaceUnitsHillSphereKm(DBL_MAX, DBL_MAX, DBL_MIN) == 0.0);
    assert(SpaceUnitsProxySurfaceGravityGame(
               DBL_MAX, DBL_MIN) == 0.0);
}

static void TestProxyScaleContract(void)
{
    double linearEarthRadius = SpaceUnitsKilometersToGameDistance(
        SPACE_UNITS_EARTH_RADIUS_KM);
    AssertRelativeNear(
        linearEarthRadius,
        SPACE_UNITS_EARTH_RADIUS_KM /
            SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE,
        1e-12);
    double radiusScale = SpaceUnitsProxyRadiusScale(
        SPACE_UNITS_EARTH_RADIUS_KM, 62.0);
    assert(radiusScale > 70000.0 && radiusScale < 75000.0);

    double proxyGravity = SpaceUnitsProxySurfaceGravityGame(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    AssertRelativeNear(proxyGravity,
                       SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME,
                       1e-12);
    double proxyMu = SpaceUnitsProxyGravitationalParameterGame(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM, 62.0);
    AssertRelativeNear(proxyMu / (62.0 * 62.0), proxyGravity, 1e-12);

    double converted = SpaceUnitsGameDistanceToKilometers(
        SpaceUnitsKilometersToGameDistance(SPACE_UNITS_EARTH_RADIUS_KM));
    assert(SpaceUnitsWithinRelativeError(
        converted, SPACE_UNITS_EARTH_RADIUS_KM,
        SPACE_UNITS_MAX_RELATIVE_ERROR));
    assert(!SpaceUnitsWithinRelativeError(101.0, 100.0,
                                          SPACE_UNITS_MAX_RELATIVE_ERROR));
}

int main(void)
{
    TestRoundTrips();
    TestEarthOrbit();
    TestMeanAnomalyTime();
    TestEccentricAnomalySolver();
    TestGravityConstants();
    TestProxyScaleContract();
    puts("space_units tests passed");
    return 0;
}
