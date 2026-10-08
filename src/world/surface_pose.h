#ifndef VOXELCRAFT_SURFACE_POSE_H
#define VOXELCRAFT_SURFACE_POSE_H

#include "world/surface_cube.h"

typedef struct SurfaceVector { double x, y, z; } SurfaceVector;

// Authoritative continuous state, relative to the body's center. Neither
// velocity nor heading is measured in face cells or geographic map units.
typedef struct SurfacePose {
    uint32_t bodyId;
    SurfaceVector position;
    SurfaceVector velocity;
    SurfaceVector heading;
    double pitch;
} SurfacePose;

typedef struct SurfaceCoordinates {
    SurfaceFace face;
    double u, v, radial;
} SurfaceCoordinates;

typedef struct SurfaceBasis {
    SurfaceVector origin, right, up, forward;
} SurfaceBasis;

bool SurfaceVectorIsFinite(SurfaceVector v);
double SurfaceVectorLength(SurfaceVector v);
SurfaceVector SurfaceVectorAdd(SurfaceVector a, SurfaceVector b);
SurfaceVector SurfaceVectorSubtract(SurfaceVector a, SurfaceVector b);
SurfaceVector SurfaceVectorScale(SurfaceVector v, double scale);
double SurfaceVectorDot(SurfaceVector a, SurfaceVector b);
SurfaceVector SurfaceVectorCross(SurfaceVector a, SurfaceVector b);
SurfaceVector SurfaceVectorNormalize(SurfaceVector v);

// Vertex coordinates include the end of the face. Inverse projection uses
// dominant-axis ties X, then Y, then Z, matching SurfaceAddressFromDirection.
// On failure every output remains unchanged.
bool SurfacePointFromCoordinates(SurfaceCoordinates coordinates,
                                  SurfaceVector *outPoint);
bool SurfaceCoordinatesFromPoint(SurfaceVector point,
                                  SurfaceCoordinates *outCoordinates);
bool SurfaceCellFromPoint(uint32_t bodyId, SurfaceVector point,
                          SurfaceAddress *outCell);
bool SurfaceCellCenter(SurfaceAddress cell, SurfaceVector *outPoint);
bool SurfacePoseFromGeographic(uint32_t bodyId, double longitude,
                               double latitude, double radial, double yaw,
                               double pitch, SurfacePose *outPose);
bool SurfacePoseIsValid(const SurfacePose *pose);
double SurfacePoseAltitude(const SurfacePose *pose);
bool SurfacePoseBasis(const SurfacePose *pose, SurfaceBasis *outBasis);
Vector3 SurfacePointToLocal(const SurfaceBasis *basis, SurfaceVector point);
SurfaceVector SurfacePointFromLocal(const SurfaceBasis *basis, Vector3 point);
// Move on a great circle by physical tangent distance, plus radial height.
// Carries heading AND velocity by the same minimal rotation. Collision must
// approve each substep before committing the returned pose.
bool SurfacePoseDisplace(const SurfacePose *pose, double right, double forward,
                          double radial, SurfacePose *outPose);

#endif
