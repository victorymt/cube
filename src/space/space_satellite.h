#ifndef VOXELCRAFT_SPACE_SATELLITE_H
#define VOXELCRAFT_SPACE_SATELLITE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct SpaceSatelliteVector3 {
    double x;
    double y;
    double z;
} SpaceSatelliteVector3;

typedef struct SpaceSatelliteOrbit {
    bool exists;
    double semiMajorAxisKm;
    double eccentricity;
    double inclinationRad;
    double longitudeAscendingNodeRad;
    double argumentPeriapsisRad;
    double meanAnomalyAtEpochRad;
    double radiusKm;
    double massKg;
} SpaceSatelliteOrbit;

typedef struct SpaceSatelliteState {
    SpaceSatelliteVector3 positionKm;
    SpaceSatelliteVector3 velocityKmPerSecond;
} SpaceSatelliteState;

bool SpaceSatelliteGenerate(uint32_t seed, double planetMassKg,
                            double planetRadiusKm,
                            double planetSemiMajorAxisKm,
                            double starMassKg, double occurrenceProbability,
                            bool forceExists, SpaceSatelliteOrbit *out);
double SpaceSatelliteFluidRocheLimitKm(double planetMassKg,
                                       double planetRadiusKm,
                                       double satelliteMassKg,
                                       double satelliteRadiusKm);
double SpaceSatelliteOrbitalPeriodSeconds(const SpaceSatelliteOrbit *orbit,
                                          double planetMassKg);
SpaceSatelliteVector3 SpaceSatellitePositionAtSeconds(
    const SpaceSatelliteOrbit *orbit, double planetMassKg,
    double physicalTimeSeconds);
bool SpaceSatelliteStateAtSeconds(const SpaceSatelliteOrbit *orbit,
                                  double planetMassKg,
                                  double physicalTimeSeconds,
                                  SpaceSatelliteState *out);

double SpaceSatelliteSolarOccultationFraction(
    SpaceSatelliteVector3 observerPositionKm,
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    SpaceSatelliteVector3 sourcePositionKm, double sourceRadiusKm);
double SpaceSatellitePlanetUmbraFraction(
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    double planetRadiusKm, SpaceSatelliteVector3 sourcePositionKm,
    double sourceRadiusKm);

#endif
