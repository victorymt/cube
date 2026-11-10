#ifndef VOXELCRAFT_CHUNKS_CUBE_H
#define VOXELCRAFT_CHUNKS_CUBE_H

#include "world/terrain_cube.h"
#include "world/surface_coverage.h"

typedef enum CubeChunkRequestResult {
    CUBE_CHUNK_UNAVAILABLE = 0,
    CUBE_CHUNK_PENDING,
    CUBE_CHUNK_READY
} CubeChunkRequestResult;

// Main-thread admission into the shared production pool. Does not evict other
// columns to hide budget saturation. The coverage owner releases unwanted
// columns explicitly. Context generation must match the installed world.
CubeChunkRequestResult ChunksRequestCubeColumn(TerrainCubeContext *context,
                                               SurfaceCubeChunkKey key, bool preload);
CubeChunkRequestResult ChunksRequestCubeSection(TerrainCubeContext *context,
                                                SurfaceCubeChunkKey key, int sectionY);
bool ChunksReleaseCubeColumn(SurfaceCubeChunkKey key);
/* True when the cube column is resident and its blocks are readable. Readiness
 * queries use this instead of the legacy map-indexed chunk lookup, which cannot
 * find a cube column at all. */
bool ChunksCubeColumnIsLoaded(SurfaceCubeChunkKey key);

typedef struct CubeCoverageStatus {
    unsigned requested, admitted, ready, pending, missing;
    /* duplicates counts admitted columns absorbed by a current-generation
     * resident; stale counts old-world residents evicted by this observation. */
    unsigned submitted, released, duplicates, stale;
    bool saturated;
} CubeCoverageStatus;

typedef struct CubeCoverageObservation {
    uint64_t sequence;
    uint32_t bodyId;
    unsigned requested;
    unsigned admitted;
    bool saturated;
} CubeCoverageObservation;

// Main-thread physical coverage owner. Invalid input leaves the pool untouched.
// Evicts only cube columns outside the admitted cap (or from stale worlds).
// The caller supplies the per-update work budget; zero performs no generation.
bool ChunksUpdateCubeCoverage(TerrainCubeContext *context, SurfaceVector focus,
                              double radius, unsigned submissionBudget,
                              CubeCoverageStatus *outStatus);

// Read-only diagnostics for the most recent coverage update. Returns false
// before the first update. A saturated observation stays saturated: callers
// must not re-derive "complete coverage" from these counters.
bool ChunksLastCubeCoverage(CubeCoverageStatus *outStatus);
bool ChunksCubeCoverageIsActive(void);
bool ChunksCubeCoverageObservation(CubeCoverageObservation *outObservation);
bool ChunksCubeCoverageAdmissionAt(unsigned index,
                                   SurfaceCubeCoverageCandidate *outCandidate);
bool ChunksCubeCoverageContains(SurfaceCubeChunkKey key);
// Admitted cube columns whose materialized sections are ready at target LOD.
unsigned ChunksCubeTargetLodReadyCount(void);
unsigned ChunksCubeTargetLodMissingSections(void);

#endif
