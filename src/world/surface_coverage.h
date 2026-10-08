#ifndef VOXELCRAFT_SURFACE_COVERAGE_H
#define VOXELCRAFT_SURFACE_COVERAGE_H

#include "world/surface_pose.h"

#define SURFACE_COVERAGE_MAX_RADIUS 208.0
#define SURFACE_COVERAGE_MAX_CANDIDATES 4096u

typedef struct SurfaceCoverageCandidate {
    SurfaceCubeChunkKey key;
    double centerDistance;
    double minimumDistance; // Conservative lower bound, in physical blocks.
} SurfaceCoverageCandidate;

typedef struct SurfaceCoverageResult {
    unsigned requested;
    unsigned admitted;
    bool saturated;
} SurfaceCoverageResult;

// Conservative sphere enclosing the ENTIRE curved patch and radial interval.
bool SurfaceCubeChunkBounds(SurfaceCubeChunkKey key, double minRadial, double maxRadial,
                             SurfaceVector *outCenter, double *outRadius);
// Enumerate the spherical cap through exact cross-face neighbors. Sort by
// physical center distance, then canonical key; retain the nearest capacity
// entries. A saturated result is valid but must never report full coverage.
// focus is body-centered; its altitude does not alter the reference surface
// metric. This is ground coverage, not a promise to fill a high-altitude view.
bool SurfaceCoverageCollect(uint32_t bodyId, SurfaceVector focus, double radius,
                             SurfaceCoverageCandidate *out, unsigned capacity,
                             SurfaceCoverageResult *outResult);

#endif
