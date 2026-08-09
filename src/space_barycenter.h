#ifndef VOXELCRAFT_SPACE_BARYCENTER_H
#define VOXELCRAFT_SPACE_BARYCENTER_H

#include "types.h"

#define SPACE_BARYCENTER_MAX_BODIES 3

typedef struct SpaceBarycenterOrbit {
    int bodyCount;
    double massKg[SPACE_BARYCENTER_MAX_BODIES];
    double innerSeparationKm;
    double outerSeparationKm;
    double innerPhaseRad;
    double outerPhaseRad;
    double innerInclinationRad;
    double outerInclinationRad;
    double innerNodeRad;
    double outerNodeRad;
} SpaceBarycenterOrbit;

typedef struct SpaceBarycenterBodyState {
    Vector3 offsetGame;
    Vector3 velocityGame;
} SpaceBarycenterBodyState;

int SpaceBarycenterSolve(const SpaceBarycenterOrbit *orbit,
                         double simulationTime,
                         SpaceBarycenterBodyState *out, int maxCount);

#endif
