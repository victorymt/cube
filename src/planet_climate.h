#ifndef VOXELCRAFT_PLANET_CLIMATE_H
#define VOXELCRAFT_PLANET_CLIMATE_H

#include <stdbool.h>

typedef struct PlanetClimateInput {
    double stellarIrradianceEarth;
    float volatileInventory;
    float greenhouseGasFraction;
    float surfaceReflectivity;
    float surfaceGravityEarth;
    float rotationRate;
    float tidalLockFactor;
    bool gasGiant;
} PlanetClimateInput;

typedef struct PlanetClimateState {
    float surfacePressureAtm;
    float atmosphereDensity;
    float albedo;
    float greenhouseOpticalDepth;
    float radiativeTemperatureK;
    float surfaceTemperatureK;
    float liquidWaterCoverage;
    float iceCoverage;
    float cloudCoverage;
    float windStrength;
    double absorbedIrradianceEarth;
} PlanetClimateState;

bool PlanetClimateSolve(const PlanetClimateInput *input,
                        PlanetClimateState *out);

#endif
