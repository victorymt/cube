#ifndef VOXELCRAFT_SPACE_BARYCENTER_H
#define VOXELCRAFT_SPACE_BARYCENTER_H

#include "types.h"

#define SPACE_BARYCENTER_MAX_BODIES 3

typedef enum SpaceBarycenterMotion {
    SPACE_BARYCENTER_BOUND = 0,
    SPACE_BARYCENTER_OUTER_FREE_FLIGHT,
    SPACE_BARYCENTER_FREE_FLIGHT
} SpaceBarycenterMotion;

typedef struct SpaceBarycenterOrbit {
    int bodyCount;
    SpaceBarycenterMotion motion;
    double massKg[SPACE_BARYCENTER_MAX_BODIES];
    double innerSeparationKm;
    double outerSeparationKm;
    double innerEccentricity;
    double outerEccentricity;
    double innerPhaseRad;
    double outerPhaseRad;
    double innerArgumentPeriapsisRad;
    double outerArgumentPeriapsisRad;
    double innerInclinationRad;
    double outerInclinationRad;
    double innerNodeRad;
    double outerNodeRad;
    Vector3 freeFlightOffsetGame[SPACE_BARYCENTER_MAX_BODIES];
    Vector3 freeFlightVelocityGame[SPACE_BARYCENTER_MAX_BODIES];
    Vector3 outerFreeOffsetGame;
    Vector3 outerFreeVelocityGame;
} SpaceBarycenterOrbit;

typedef struct SpaceBarycenterBodyState {
    Vector3 offsetGame;
    Vector3 velocityGame;
} SpaceBarycenterBodyState;

int SpaceBarycenterSolve(const SpaceBarycenterOrbit *orbit,
                         double simulationTime,
                         SpaceBarycenterBodyState *out, int maxCount);

#endif
