#ifndef VOXELCRAFT_SURFACE_TYPES_H
#define VOXELCRAFT_SURFACE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Plain-old-data surface identity. Deliberately free of raylib: surface keys
 * are stored, hashed and compared in modules that never draw anything, and
 * `raylib.h` alone rebuilds 187 of 235 objects. The one surface type that needs
 * a vector (SurfaceFrame) stays in surface_topology.h. */
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

#endif
