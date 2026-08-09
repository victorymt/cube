#ifndef VOXELCRAFT_SPACE_UNITS_H
#define VOXELCRAFT_SPACE_UNITS_H

// Canonical physical constants use kilometers, seconds, and kilograms.
// Scene coordinates remain a compressed view: 340 game distance units are 1 AU,
// and one game time unit advances the celestial simulation by one Earth day.
extern const double SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
extern const double SPACE_UNITS_EARTH_MASS_KG;
extern const double SPACE_UNITS_SOLAR_MASS_KG;
extern const double SPACE_UNITS_EARTH_RADIUS_KM;
extern const double SPACE_UNITS_SOLAR_RADIUS_KM;
extern const double SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2;
extern const double SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
extern const double SPACE_UNITS_SECONDS_PER_GAME_TIME;
extern const double SPACE_UNITS_KILOGRAMS_PER_GAME_MASS;

double SpaceUnitsGameDistanceToKilometers(double gameDistance);
double SpaceUnitsKilometersToGameDistance(double kilometers);
double SpaceUnitsGameTimeToSeconds(double gameTime);
double SpaceUnitsSecondsToGameTime(double seconds);
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
double SpaceUnitsKeplerPeriodSeconds(double semiMajorAxisKm,
                                     double centralMassKg);
double SpaceUnitsCircularOrbitVelocityKilometersPerSecond(double radiusKm,
                                                          double centralMassKg);
double SpaceUnitsLaplaceSphereOfInfluenceKm(double semiMajorAxisKm,
                                            double bodyMassKg,
                                            double parentMassKg);

#endif
