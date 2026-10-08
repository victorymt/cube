#ifndef VOXELCRAFT_SURFACE_GEOMETRY_H
#define VOXELCRAFT_SURFACE_GEOMETRY_H

#include "world/surface_pose.h"
#include "world/world_location.h"

// Fractions within a cube cell, not Cartesian distances. A slab, stair step
// or overlay uses the same projection as a full block and the renderer.
typedef struct SurfaceCellBox {
    double minU, minV, minRadial;
    double maxU, maxV, maxRadial;
} SurfaceCellBox;

#define SURFACE_CELL_BOX_FULL ((SurfaceCellBox){0, 0, 0, 1, 1, 1})

typedef struct SurfaceTriangle {
    SurfaceVector a, b, c;
    WorldBlockSide side;
} SurfaceTriangle;

typedef struct SurfaceGeometryHit {
    double distance;
    SurfaceVector point, normal;
    WorldBlockSide side;
} SurfaceGeometryHit;

// Same twelve triangles for exact near rendering, ray tests and collision.
// Outward winding; side IDs use the integer neighbor contract.
bool SurfaceCellTriangles(SurfaceAddress cell, SurfaceCellBox box,
                           SurfaceTriangle outTriangles[12]);
bool SurfaceRayCellBox(SurfaceAddress cell, SurfaceCellBox box,
                       SurfaceVector origin, SurfaceVector direction,
                       double maxDistance, SurfaceGeometryHit *outHit);
// Continuous SAT sweep against cell boundary triangles. basis.origin is the
// box center; halfSize and displacement are physical local distances. On a
// hit, distance is a fraction in [0,1], not a distance in blocks, and point
// is the box center at contact. At an edge, side may belong to either incident
// triangle; the separating normal, not that side ID, determines support.
bool SurfaceSweepCellBox(SurfaceAddress cell, SurfaceCellBox shape,
                         const SurfaceBasis *basis, Vector3 halfSize,
                         Vector3 displacement, SurfaceGeometryHit *outHit);

#endif
