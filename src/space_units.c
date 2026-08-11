#include "space_units.h"

#include <math.h>

#define SPACE_UNITS_PI 3.14159265358979323846
#define SPACE_UNITS_TWO_PI (2.0 * SPACE_UNITS_PI)

const double SPACE_UNITS_ASTRONOMICAL_UNIT_KM = 149597870.7;
const double SPACE_UNITS_GAME_DISTANCE_PER_AU = 340.0;
const double SPACE_UNITS_EARTH_MASS_KG = 5.9722e24;
const double SPACE_UNITS_SOLAR_MASS_KG = 1.98847e30;
const double SPACE_UNITS_EARTH_RADIUS_KM = 6371.0;
const double SPACE_UNITS_SOLAR_RADIUS_KM = 695700.0;
const double SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 = 6.67430e-20;
const double SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE = 149597870.7 / 340.0;
const double SPACE_UNITS_SECONDS_PER_GAME_TIME = 86400.0;
const double SPACE_UNITS_GAME_TIME_PER_GIGAYEAR = 365250000000.0;
const double SPACE_UNITS_KILOGRAMS_PER_GAME_MASS = 5.9722e24;
const double SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME = 4.5;
const double SPACE_UNITS_MAX_RELATIVE_ERROR = 1e-6;

static double SpaceUnitsPositiveFiniteOrZero(double value)
{
    return value > 0.0 && isfinite(value) ? value : 0.0;
}

double SpaceUnitsGameDistanceToKilometers(double gameDistance)
{
    return gameDistance * SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
}

double SpaceUnitsKilometersToGameDistance(double kilometers)
{
    return kilometers / SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
}

double SpaceUnitsGameTimeToSeconds(double gameTime)
{
    return gameTime * SPACE_UNITS_SECONDS_PER_GAME_TIME;
}

double SpaceUnitsSecondsToGameTime(double seconds)
{
    return seconds / SPACE_UNITS_SECONDS_PER_GAME_TIME;
}

double SpaceUnitsGameTimeToGigayears(double gameTime)
{
    return gameTime / SPACE_UNITS_GAME_TIME_PER_GIGAYEAR;
}

double SpaceUnitsGigayearsToGameTime(double gigayears)
{
    return gigayears * SPACE_UNITS_GAME_TIME_PER_GIGAYEAR;
}

double SpaceUnitsGameMassToKilograms(double gameMass)
{
    return gameMass * SPACE_UNITS_KILOGRAMS_PER_GAME_MASS;
}

double SpaceUnitsKilogramsToGameMass(double kilograms)
{
    return kilograms / SPACE_UNITS_KILOGRAMS_PER_GAME_MASS;
}

double SpaceUnitsGameVelocityToKilometersPerSecond(double gameVelocity)
{
    return gameVelocity * SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE /
           SPACE_UNITS_SECONDS_PER_GAME_TIME;
}

double SpaceUnitsKilometersPerSecondToGameVelocity(double kilometersPerSecond)
{
    return kilometersPerSecond * SPACE_UNITS_SECONDS_PER_GAME_TIME /
           SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
}

double SpaceUnitsGameAccelerationToKilometersPerSecondSquared(double gameAcceleration)
{
    return gameAcceleration * SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE /
           (SPACE_UNITS_SECONDS_PER_GAME_TIME *
            SPACE_UNITS_SECONDS_PER_GAME_TIME);
}

double SpaceUnitsKilometersPerSecondSquaredToGameAcceleration(
    double kilometersPerSecondSquared)
{
    return kilometersPerSecondSquared *
           (SPACE_UNITS_SECONDS_PER_GAME_TIME *
            SPACE_UNITS_SECONDS_PER_GAME_TIME) /
           SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
}

double SpaceUnitsGravitationalParameterKm(double massKg)
{
    if (!(massKg > 0.0) || !isfinite(massKg)) return 0.0;
    return SpaceUnitsPositiveFiniteOrZero(
        SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * massKg);
}

double SpaceUnitsGravitationalParameterGame(double massKg)
{
    double distanceScale = SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
    double timeScale = SPACE_UNITS_SECONDS_PER_GAME_TIME;
    return SpaceUnitsPositiveFiniteOrZero(
        SpaceUnitsGravitationalParameterKm(massKg) * timeScale * timeScale /
        (distanceScale * distanceScale * distanceScale));
}

double SpaceUnitsSurfaceGravityKmPerSecondSquared(double massKg, double radiusKm)
{
    if (!(radiusKm > 0.0) || !isfinite(radiusKm)) return 0.0;
    return SpaceUnitsPositiveFiniteOrZero(
        SpaceUnitsGravitationalParameterKm(massKg) /
        (radiusKm * radiusKm));
}

double SpaceUnitsKeplerMeanMotionGame(double semiMajorAxisKm,
                                      double centralMassKg)
{
    if (!(semiMajorAxisKm > 0.0) || !isfinite(semiMajorAxisKm)) return 0.0;
    double mu = SpaceUnitsGravitationalParameterKm(centralMassKg);
    if (!(mu > 0.0)) return 0.0;
    double radiansPerSecond = sqrt(mu /
                                   (semiMajorAxisKm * semiMajorAxisKm *
                                    semiMajorAxisKm));
    return SpaceUnitsPositiveFiniteOrZero(
        radiansPerSecond * SPACE_UNITS_SECONDS_PER_GAME_TIME);
}

bool SpaceUnitsSolveEccentricAnomaly(double meanAnomalyRad,
                                     double eccentricity,
                                     double *outEccentricAnomalyRad)
{
    if (!outEccentricAnomalyRad) return false;
    *outEccentricAnomalyRad = 0.0;
    if (!isfinite(meanAnomalyRad) || !isfinite(eccentricity) ||
        eccentricity < 0.0 || eccentricity >= 1.0) {
        return false;
    }

    if (meanAnomalyRad > SPACE_UNITS_PI) {
        meanAnomalyRad -= SPACE_UNITS_TWO_PI;
    } else if (meanAnomalyRad < -SPACE_UNITS_PI) {
        meanAnomalyRad += SPACE_UNITS_TWO_PI;
    }
    double eccentricAnomaly = eccentricity < 0.8
        ? meanAnomalyRad
        : (meanAnomalyRad < 0.0 ? -SPACE_UNITS_PI : SPACE_UNITS_PI);
    for (int iteration = 0; iteration < 16; iteration++) {
        double sine = sin(eccentricAnomaly);
        double cosine = cos(eccentricAnomaly);
        double denominator = 1.0 - eccentricity * cosine;
        if (!(denominator > 0.0) || !isfinite(denominator)) return false;
        double correction = (eccentricAnomaly - eccentricity * sine -
                             meanAnomalyRad) / denominator;
        if (!isfinite(correction)) return false;
        eccentricAnomaly -= correction;
        if (!isfinite(eccentricAnomaly)) return false;
        if (fabs(correction) < 1e-13) {
            double residual = eccentricAnomaly -
                              eccentricity * sin(eccentricAnomaly) -
                              meanAnomalyRad;
            if (!isfinite(residual) || fabs(residual) > 1e-12) return false;
            *outEccentricAnomalyRad = eccentricAnomaly;
            return true;
        }
    }
    return false;
}

bool SpaceUnitsMeanAnomalyAtTime(double meanAnomalyAtEpochRad,
                                 double meanMotion,
                                 double simulationTime,
                                 double *out)
{
    if (!out) return false;
    *out = 0.0;
    if (!isfinite(meanAnomalyAtEpochRad) ||
        !(meanMotion > 0.0) || !isfinite(meanMotion) ||
        !isfinite(simulationTime)) {
        return false;
    }
    double period = SPACE_UNITS_TWO_PI / meanMotion;
    double reducedTime = isfinite(period) && period > 0.0
        ? fmod(simulationTime, period) : simulationTime;
    double meanAnomaly = fmod(meanAnomalyAtEpochRad +
                              reducedTime * meanMotion,
                              SPACE_UNITS_TWO_PI);
    if (!isfinite(meanAnomaly)) return false;
    if (meanAnomaly > SPACE_UNITS_PI) meanAnomaly -= SPACE_UNITS_TWO_PI;
    if (meanAnomaly < -SPACE_UNITS_PI) meanAnomaly += SPACE_UNITS_TWO_PI;
    *out = meanAnomaly;
    return true;
}

double SpaceUnitsKeplerPeriodSeconds(double semiMajorAxisKm,
                                     double centralMassKg)
{
    double meanMotionGame = SpaceUnitsKeplerMeanMotionGame(semiMajorAxisKm,
                                                            centralMassKg);
    if (!(meanMotionGame > 0.0)) return 0.0;
    double periodGame = (2.0 * 3.14159265358979323846) / meanMotionGame;
    return SpaceUnitsPositiveFiniteOrZero(
        SpaceUnitsGameTimeToSeconds(periodGame));
}

double SpaceUnitsCircularOrbitVelocityKilometersPerSecond(double radiusKm,
                                                          double centralMassKg)
{
    if (!(radiusKm > 0.0) || !isfinite(radiusKm)) return 0.0;
    double mu = SpaceUnitsGravitationalParameterKm(centralMassKg);
    return mu > 0.0 ? SpaceUnitsPositiveFiniteOrZero(sqrt(mu / radiusKm))
                    : 0.0;
}

double SpaceUnitsLaplaceSphereOfInfluenceKm(double semiMajorAxisKm,
                                            double bodyMassKg,
                                            double parentMassKg)
{
    if (!(semiMajorAxisKm > 0.0) || !(bodyMassKg > 0.0) ||
        !(parentMassKg > 0.0) || !isfinite(semiMajorAxisKm) ||
        !isfinite(bodyMassKg) || !isfinite(parentMassKg)) {
        return 0.0;
    }
    return SpaceUnitsPositiveFiniteOrZero(
        semiMajorAxisKm * pow(bodyMassKg / parentMassKg, 0.4));
}

double SpaceUnitsHillSphereKm(double semiMajorAxisKm, double bodyMassKg,
                              double parentMassKg)
{
    if (!(semiMajorAxisKm > 0.0) || !(bodyMassKg > 0.0) ||
        !(parentMassKg > 0.0) || !isfinite(semiMajorAxisKm) ||
        !isfinite(bodyMassKg) || !isfinite(parentMassKg)) {
        return 0.0;
    }
    return SpaceUnitsPositiveFiniteOrZero(
        semiMajorAxisKm * cbrt(bodyMassKg / (3.0 * parentMassKg)));
}

double SpaceUnitsProxyRadiusScale(double physicalRadiusKm,
                                  double proxyRadiusGame)
{
    double physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
        physicalRadiusKm);
    if (!(physicalRadiusGame > 0.0) || !(proxyRadiusGame > 0.0) ||
        !isfinite(physicalRadiusGame) || !isfinite(proxyRadiusGame)) {
        return 0.0;
    }
    return proxyRadiusGame / physicalRadiusGame;
}

double SpaceUnitsProxySurfaceGravityGame(double massKg,
                                         double physicalRadiusKm)
{
    double physicalGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        massKg, physicalRadiusKm);
    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    if (!(physicalGravity > 0.0) || !(earthGravity > 0.0)) return 0.0;
    return SpaceUnitsPositiveFiniteOrZero(
        SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME *
        physicalGravity / earthGravity);
}

double SpaceUnitsProxyGravitationalParameterGame(double massKg,
                                                 double physicalRadiusKm,
                                                 double proxyRadiusGame)
{
    if (!(proxyRadiusGame > 0.0) || !isfinite(proxyRadiusGame)) return 0.0;
    double surfaceGravity = SpaceUnitsProxySurfaceGravityGame(
        massKg, physicalRadiusKm);
    return SpaceUnitsPositiveFiniteOrZero(
        surfaceGravity * proxyRadiusGame * proxyRadiusGame);
}

double SpaceUnitsRelativeError(double actual, double expected)
{
    if (!isfinite(actual) || !isfinite(expected)) return INFINITY;
    if (expected == 0.0) return fabs(actual);
    return fabs(actual - expected) / fabs(expected);
}

bool SpaceUnitsWithinRelativeError(double actual, double expected,
                                   double tolerance)
{
    return tolerance >= 0.0 && isfinite(tolerance) &&
           SpaceUnitsRelativeError(actual, expected) <= tolerance;
}
