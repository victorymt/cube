#include "space_units.h"

#include <math.h>

const double SPACE_UNITS_ASTRONOMICAL_UNIT_KM = 149597870.7;
const double SPACE_UNITS_EARTH_MASS_KG = 5.9722e24;
const double SPACE_UNITS_SOLAR_MASS_KG = 1.98847e30;
const double SPACE_UNITS_EARTH_RADIUS_KM = 6371.0;
const double SPACE_UNITS_SOLAR_RADIUS_KM = 695700.0;
const double SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 = 6.67430e-20;
const double SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE = 149597870.7 / 340.0;
const double SPACE_UNITS_SECONDS_PER_GAME_TIME = 86400.0;
const double SPACE_UNITS_KILOGRAMS_PER_GAME_MASS = 5.9722e24;

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
    return SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * massKg;
}

double SpaceUnitsGravitationalParameterGame(double massKg)
{
    double distanceScale = SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
    double timeScale = SPACE_UNITS_SECONDS_PER_GAME_TIME;
    return SpaceUnitsGravitationalParameterKm(massKg) * timeScale * timeScale /
           (distanceScale * distanceScale * distanceScale);
}

double SpaceUnitsSurfaceGravityKmPerSecondSquared(double massKg, double radiusKm)
{
    if (!(radiusKm > 0.0) || !isfinite(radiusKm)) return 0.0;
    return SpaceUnitsGravitationalParameterKm(massKg) / (radiusKm * radiusKm);
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
    return radiansPerSecond * SPACE_UNITS_SECONDS_PER_GAME_TIME;
}

double SpaceUnitsKeplerPeriodSeconds(double semiMajorAxisKm,
                                     double centralMassKg)
{
    double meanMotionGame = SpaceUnitsKeplerMeanMotionGame(semiMajorAxisKm,
                                                            centralMassKg);
    if (!(meanMotionGame > 0.0)) return 0.0;
    double periodGame = (2.0 * 3.14159265358979323846) / meanMotionGame;
    return SpaceUnitsGameTimeToSeconds(periodGame);
}

double SpaceUnitsCircularOrbitVelocityKilometersPerSecond(double radiusKm,
                                                          double centralMassKg)
{
    if (!(radiusKm > 0.0) || !isfinite(radiusKm)) return 0.0;
    double mu = SpaceUnitsGravitationalParameterKm(centralMassKg);
    return mu > 0.0 ? sqrt(mu / radiusKm) : 0.0;
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
    return semiMajorAxisKm * pow(bodyMassKg / parentMassKg, 0.4);
}
