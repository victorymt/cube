#ifndef VOXELCRAFT_SPACE_ORBIT_H
#define VOXELCRAFT_SPACE_ORBIT_H

#include "types.h"

#include <stdbool.h>

typedef struct SpaceKeplerOrbit {
    double semiMajorAxisKm;
    double centralMassKg;
    double eccentricity;
    double inclinationRad;
    double longitudeAscendingNodeRad;
    double argumentPeriapsisRad;
    double meanAnomalyAtEpochRad;
} SpaceKeplerOrbit;

typedef struct SpaceKeplerState {
    Vector3 positionGame;
    Vector3 velocityGame;
} SpaceKeplerState;

bool SpaceKeplerOrbitIsValid(const SpaceKeplerOrbit *orbit);
bool SpaceKeplerOrbitFromState(const SpaceKeplerState *state,
                               double centralMassKg,
                               SpaceKeplerOrbit *out);
bool SpaceKeplerStateAtTime(const SpaceKeplerOrbit *orbit,
                            double simulationTime,
                            SpaceKeplerState *out);

#endif
