#ifndef VOXELCRAFT_SURFACE_TOPOLOGY_H
#define VOXELCRAFT_SURFACE_TOPOLOGY_H

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

#define SURFACE_FACE_BLOCKS 4096
#define SURFACE_FACE_CHUNKS (SURFACE_FACE_BLOCKS / 16)
#define SURFACE_EQUATOR_BLOCKS (SURFACE_FACE_BLOCKS * 4)
#define SURFACE_POLE_TO_POLE_BLOCKS (SURFACE_EQUATOR_BLOCKS / 2)
#define SURFACE_RADIUS_BLOCKS \
    ((float)SURFACE_EQUATOR_BLOCKS / (2.0f * PI))

typedef enum SurfaceFace {
    SURFACE_FACE_POS_X = 0,
    SURFACE_FACE_NEG_X,
    SURFACE_FACE_POS_Y,
    SURFACE_FACE_NEG_Y,
    SURFACE_FACE_POS_Z,
    SURFACE_FACE_NEG_Z,
    SURFACE_FACE_COUNT
} SurfaceFace;

typedef struct SurfaceAddress {
    uint32_t bodyId;
    SurfaceFace face;
    int u;
    int v;
    int radial;
} SurfaceAddress;

typedef struct SurfaceFrame {
    SurfaceAddress anchor;
    Vector3 origin;
    Vector3 east;
    Vector3 north;
    Vector3 up;
} SurfaceFrame;

typedef struct SurfaceMapProjection {
    float longitude;
    float latitude;
    float northDirection;
} SurfaceMapProjection;

typedef struct SurfaceMapCell {
    int x;
    int z;
} SurfaceMapCell;

typedef struct SurfaceSpatialKey {
    uint32_t bodyId;
    int mapX;
    int mapZ;
    int radial;
} SurfaceSpatialKey;

typedef struct SurfaceChunkKey {
    uint32_t bodyId;
    SurfaceMapCell corners[4];
} SurfaceChunkKey;

typedef struct SurfaceMapOffset {
    float x;
    float z;
    float northDirection;
} SurfaceMapOffset;

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
