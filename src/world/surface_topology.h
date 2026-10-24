#ifndef VOXELCRAFT_SURFACE_TOPOLOGY_H
#define VOXELCRAFT_SURFACE_TOPOLOGY_H

#include "world/surface_types.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct SurfaceFrame {
    SurfaceAddress anchor;
    Vector3 origin;
    Vector3 east;
    Vector3 north;
    Vector3 up;
} SurfaceFrame;

bool SurfaceFaceIsValid(SurfaceFace face);
bool SurfaceAddressIsValid(SurfaceAddress address);
bool SurfaceAddressEqual(SurfaceAddress a, SurfaceAddress b);
Vector3 SurfaceAddressDirection(SurfaceAddress address);
SurfaceAddress SurfaceAddressFromDirection(uint32_t bodyId, Vector3 direction,
                                           int radial);
SurfaceAddress SurfaceAddressFromLatLon(uint32_t bodyId, float longitude,
                                        float latitude, int radial);
SurfaceMapProjection SurfaceProjectMapCoordinates(float mapX, float mapZ);
Vector2 SurfaceCanonicalMapPosition(float mapX, float mapZ,
                                    float *outNorthDirection);
SurfaceMapCell SurfaceCanonicalMapCell(float mapX, float mapZ);
SurfaceSpatialKey SurfaceSpatialKeyFromMapCoordinates(
    uint32_t bodyId, float mapX, float mapZ, int radial);
bool SurfaceSpatialKeyEqual(SurfaceSpatialKey a, SurfaceSpatialKey b);
bool SurfaceChunkKeyEqual(SurfaceChunkKey a, SurfaceChunkKey b);
SurfaceMapOffset SurfaceShortestMapOffset(float fromX, float fromZ,
                                          float toX, float toZ);
uint32_t SurfaceCanonicalMapHash(uint32_t bodyId, float mapX, float mapZ,
                                 int radial);
Vector3 SurfaceDirectionFromMapCoordinates(float mapX, float mapZ);
/* Inverse of SurfaceDirectionFromMapCoordinates, returned in canonical map
 * coordinates: X in [-equator/2, equator/2), Z in [-poleToPole/2, poleToPole/2).
 * A direction on a pole is ambiguous in longitude; X is then 0. Returns false
 * for a non-finite or zero-length direction. */
bool SurfaceMapCoordinatesFromDirection(Vector3 direction, Vector2 *outMap);
SurfaceAddress SurfaceAddressFromMapCoordinates(uint32_t bodyId, float x,
                                                float z, int radial);
bool SurfaceAddressCanonicalMapCell(SurfaceAddress address,
                                    SurfaceMapCell *outCell);
void SurfaceAddressLatLon(SurfaceAddress address, float *outLongitude,
                          float *outLatitude);
SurfaceAddress SurfaceAddressOffset(SurfaceAddress address, int deltaU,
                                    int deltaV, int deltaRadial);
SurfaceFrame SurfaceFrameAt(SurfaceAddress anchor);
SurfaceFrame SurfaceFrameAtMapCoordinates(uint32_t bodyId, float mapX,
                                          float mapZ, int radial);
SurfaceFrame SurfaceLocalFrameAtOffset(float offsetX, float offsetZ,
                                       int radial);
Vector3 SurfaceFrameLocalToPlanet(const SurfaceFrame *frame, Vector3 local);
Vector3 SurfaceFramePlanetToLocal(const SurfaceFrame *frame, Vector3 planet);
Vector3 SurfaceFrameTransformPoint(const SurfaceFrame *from,
                                   const SurfaceFrame *to, Vector3 point);
Vector3 SurfaceFrameTransformVector(const SurfaceFrame *from,
                                    const SurfaceFrame *to, Vector3 vector);
Matrix SurfacePatchTransform(SurfaceAddress reference,
                             Vector3 referenceLocalOrigin,
                             SurfaceAddress patch, int radialBase);
Matrix SurfacePatchTransformAtMap(
    uint32_t bodyId, Vector2 referenceMap, Vector3 referenceLocalOrigin,
    Vector2 patchMap, int radialBase);

#endif
