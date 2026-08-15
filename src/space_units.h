#ifndef VOXELCRAFT_SPACE_UNITS_H
#define VOXELCRAFT_SPACE_UNITS_H

#include <stdbool.h>

// Canonical physical constants use kilometers, seconds, and kilograms.
// Orbital scene coordinates use a linear system projection: 20 game distance
// units are 1 AU. One unpaused gameplay second advances one game time unit, while one
// game time unit represents one physical Earth day in the celestial simulation.
//
// Orbital body meshes, gravity, and encounter spheres use the same linear
// physical scale. Voxel surfaces remain local gameplay reference frames after
// a landing transition; their terrain dimensions are not celestial radii.
extern const double SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
extern const double SPACE_UNITS_GAME_DISTANCE_PER_AU;
extern const double SPACE_UNITS_EARTH_MASS_KG;
extern const double SPACE_UNITS_SOLAR_MASS_KG;
extern const double SPACE_UNITS_EARTH_RADIUS_KM;
extern const double SPACE_UNITS_SOLAR_RADIUS_KM;
extern const double SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2;
extern const double SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
extern const double SPACE_UNITS_SECONDS_PER_GAME_TIME;
extern const double SPACE_UNITS_GAME_TIME_PER_GIGAYEAR;
extern const double SPACE_UNITS_KILOGRAMS_PER_GAME_MASS;
extern const double SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME;
extern const double SPACE_UNITS_MAX_RELATIVE_ERROR;

double SpaceUnitsGameDistanceToKilometers(double gameDistance);
double SpaceUnitsKilometersToGameDistance(double kilometers);
double SpaceUnitsGameTimeToSeconds(double gameTime);
double SpaceUnitsSecondsToGameTime(double seconds);
double SpaceUnitsGameTimeToGigayears(double gameTime);
double SpaceUnitsGigayearsToGameTime(double gigayears);
double SpaceUnitsGameMassToKilograms(double gameMass);
double SpaceUnitsKilogramsToGameMass(double kilograms);
double SpaceUnitsGameVelocityToKilometersPerSecond(double gameVelocity);
double SpaceUnitsKilometersPerSecondToGameVelocity(double kilometersPerSecond);
double SpaceUnitsGameAccelerationToKilometersPerSecondSquared(double gameAcceleration);
double SpaceUnitsKilometersPerSecondSquaredToGameAcceleration(
    double kilometersPerSecondSquared);

double SpaceUnitsGravitationalParameterKm(double massKg);
double SpaceUnitsGravitationalParameterGame(double massKg);
double SpaceUnitsSurfaceGravityKmPerSecondSquared(double massKg, double radiusKm);
double SpaceUnitsKeplerMeanMotionGame(double semiMajorAxisKm,
                                      double centralMassKg);
double SpaceUnitsEccentricAnomalyDerivative(double eccentricAnomalyRad,
                                            double eccentricity);
bool SpaceUnitsSolveEccentricAnomaly(double meanAnomalyRad,
                                     double eccentricity,
                                     double *outEccentricAnomalyRad);
bool SpaceUnitsMeanAnomalyAtTime(double meanAnomalyAtEpochRad,
                                 double meanMotion,
                                 double simulationTime,
                                 double *out);
double SpaceUnitsKeplerPeriodSeconds(double semiMajorAxisKm,
                                     double centralMassKg);
double SpaceUnitsCircularOrbitVelocityKilometersPerSecond(double radiusKm,
                                                          double centralMassKg);
double SpaceUnitsLaplaceSphereOfInfluenceKm(double semiMajorAxisKm,
                                            double bodyMassKg,
                                            double parentMassKg);
double SpaceUnitsHillSphereKm(double semiMajorAxisKm, double bodyMassKg,
                              double parentMassKg);

double SpaceUnitsProxyRadiusScale(double physicalRadiusKm,
                                  double proxyRadiusGame);
double SpaceUnitsProxySurfaceGravityGame(double massKg,
                                         double physicalRadiusKm);
double SpaceUnitsProxyGravitationalParameterGame(double massKg,
                                                 double physicalRadiusKm,
                                                 double proxyRadiusGame);
double SpaceUnitsRelativeError(double actual, double expected);
bool SpaceUnitsWithinRelativeError(double actual, double expected,
                                   double tolerance);

#endif
