#ifndef VOXELCRAFT_SURFACE_CUBE_H
#define VOXELCRAFT_SURFACE_CUBE_H

#include "world/surface_topology.h"

// A separate namespace from legacy latitude/longitude SurfaceChunkKey. Never
// reinterpret an old save's map cells as face cells.
#define SURFACE_CUBE_TOPOLOGY_VERSION 1u
#define SURFACE_CUBE_NEIGHBORHOOD_RADIUS_MAX 12
#define SURFACE_CUBE_NEIGHBORHOOD_CAPACITY \
    (1 + 2 * SURFACE_CUBE_NEIGHBORHOOD_RADIUS_MAX * \
         (SURFACE_CUBE_NEIGHBORHOOD_RADIUS_MAX + 1))

typedef enum SurfaceCubeEdge {
    SURFACE_CUBE_U_MINUS = 0,
    SURFACE_CUBE_U_PLUS,
    SURFACE_CUBE_V_MINUS,
    SURFACE_CUBE_V_PLUS
} SurfaceCubeEdge;

typedef struct SurfaceCubeChunkKey {
    uint32_t bodyId;
    SurfaceFace face;
    int u;
    int v;
} SurfaceCubeChunkKey;

typedef struct SurfaceCubeCandidate {
    SurfaceCubeChunkKey key;
    unsigned int steps;
} SurfaceCubeCandidate;

// Oriented face basis shared by geometry, motion and integer edge transport.
bool SurfaceCubeFaceAxes(SurfaceFace face, Vector3 *normal, Vector3 *u,
                         Vector3 *v);
uint32_t SurfaceCubeCellHash(SurfaceAddress cell);
// Carry a source-face cardinal direction into the destination face. The
// transition must be a single edge step; corner paths remain ordered.
bool SurfaceCubeTransportEdge(SurfaceAddress source, SurfaceCubeEdge crossing,
                              SurfaceCubeEdge direction,
                              SurfaceCubeEdge *outDirection);

bool SurfaceCubeChunkKeyIsValid(SurfaceCubeChunkKey key);
bool SurfaceCubeChunkKeyEqual(SurfaceCubeChunkKey a, SurfaceCubeChunkKey b);
uint32_t SurfaceCubeChunkKeyHash(SurfaceCubeChunkKey key);
bool SurfaceCubeChunkAt(SurfaceAddress cell, SurfaceCubeChunkKey *outKey);

// Exact integer edge folding, NOT projecting an extended face and rounding.
// outReturnEdge is the direction on the destination that returns to the source.
// Coordinates and body/radial identity are preserved on a round trip.
bool SurfaceCubeCellNeighbor(SurfaceAddress cell, SurfaceCubeEdge edge,
                             SurfaceAddress *outCell,
                             SurfaceCubeEdge *outReturnEdge);
bool SurfaceCubeChunkNeighbor(SurfaceCubeChunkKey key, SurfaceCubeEdge edge,
                              SurfaceCubeChunkKey *outKey,
                              SurfaceCubeEdge *outReturnEdge);

// Face coordinates refer to VERTICES [0, SURFACE_FACE_BLOCKS], whereas a
// SurfaceAddress refers to a cell with center (u + .5, v + .5).
bool SurfaceCubeVertex(SurfaceFace face, float u, float v, float radial,
                       Vector3 *outPoint);

// Deterministic breadth-first, four-neighbor graph ball. No map latitude or
// mutable world state. The caller must supply the full worst-case capacity
// 1 + 2*r*(r+1); insufficient space is rejected, never silently truncated.
// steps is graph distance, NOT a distance in physical blocks.
bool SurfaceCubeNeighborhood(SurfaceCubeChunkKey center, unsigned int radius,
                             SurfaceCubeCandidate *outCandidates,
                             unsigned int capacity, unsigned int *outCount);

#endif
