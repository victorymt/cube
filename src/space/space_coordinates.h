#ifndef VOXELCRAFT_SPACE_COORDINATES_H
#define VOXELCRAFT_SPACE_COORDINATES_H

#include "world/world_types.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct CelestialVector3 {
    double x;
    double y;
    double z;
} CelestialVector3;

typedef struct CelestialPosition {
    int64_t systemAnchorX;
    int64_t systemAnchorZ;
    CelestialVector3 offsetKm;
} CelestialPosition;

bool CelestialVectorIsFinite(CelestialVector3 value);
CelestialVector3 CelestialVectorAdd(CelestialVector3 left,
                                    CelestialVector3 right);
CelestialVector3 CelestialVectorSubtract(CelestialVector3 left,
                                         CelestialVector3 right);
CelestialVector3 CelestialVectorScale(CelestialVector3 value, double scale);
double CelestialVectorLength(CelestialVector3 value);
CelestialVector3 CelestialVectorFromGameDistance(Vector3 value);
Vector3 CelestialVectorToGameDistance(CelestialVector3 value);
CelestialVector3 CelestialVelocityFromGame(Vector3 value);
Vector3 CelestialVelocityToGame(CelestialVector3 value);
bool CelestialProjectRelative(CelestialPosition position,
                              CelestialPosition camera,
                              Vector3 *outLocal);

#endif
