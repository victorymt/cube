#ifndef VOXELCRAFT_SURFACE_COVERAGE_H
#define VOXELCRAFT_SURFACE_COVERAGE_H

#include "world/surface_cube.h"
#include "world/surface_pose.h"

#define SURFACE_COVERAGE_MAX_RADIUS 208.0
#define SURFACE_COVERAGE_MAX_CANDIDATES 4096u

typedef struct SurfaceCubeCoverageCandidate {
    SurfaceCubeChunkKey key;
    double centerDistance;
    double minimumDistance; // Conservative lower bound, in physical blocks.
} SurfaceCubeCoverageCandidate;

typedef struct SurfaceCubeCoverageResult {
    unsigned requested;
    unsigned admitted;
    bool saturated;
} SurfaceCubeCoverageResult;

/* Stable evidence surface for M5. `ready` means admitted columns have terrain
 * data; meshReady is resident mesh presence. Neither alone proves what the
 * renderer drew. visualReady is a conservative local mesh-readiness signal, not
 * a full-viewport or occlusion certificate. */
typedef struct SurfaceCoverageEvidence {
    unsigned requested;
    unsigned admitted;
    unsigned ready;
    unsigned pending;
    unsigned missing;
    unsigned submitted;
    unsigned released;
    // Admitted requests already resident vs. old-generation residents evicted.
    unsigned duplicates;
    unsigned stale;
    unsigned meshReady;
    bool saturated;
    bool localTerrainReady;
    bool visualReady;
} SurfaceCoverageEvidence;

// The same physical metric used for admission and draw-distance checks.
bool SurfaceCubeChunkDistance(SurfaceCubeChunkKey key, SurfaceVector focus,
                               SurfaceCubeCoverageCandidate *outCandidate);

// Conservative sphere enclosing the ENTIRE curved patch and radial interval.
bool SurfaceCubeChunkBounds(SurfaceCubeChunkKey key, double minRadial, double maxRadial,
                             SurfaceVector *outCenter, double *outRadius);
// Enumerate the spherical cap through exact cross-face neighbors. Sort by
// physical center distance, then canonical key; retain the nearest capacity
// entries. A saturated result is valid but must never report full coverage.
// focus is body-centered; its altitude does not alter the reference surface
// metric. This is ground coverage, not a promise to fill a high-altitude view.
bool SurfaceCoverageCollect(uint32_t bodyId, SurfaceVector focus, double radius,
                             SurfaceCubeCoverageCandidate *out, unsigned capacity,
                             SurfaceCubeCoverageResult *outResult);

/* Freeze the translation from backend counters to shared evidence. */
SurfaceCoverageEvidence SurfaceCoverageEvidenceBuild(
    unsigned requested, unsigned admitted, unsigned ready, unsigned pending,
    unsigned missing, unsigned submitted, unsigned released, bool saturated,
    unsigned duplicates, unsigned stale, unsigned meshReady,
    bool localTerrainReady);

#endif
