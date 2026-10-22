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

typedef struct CubeCoverageStatus {
    unsigned requested, admitted, ready, pending, missing;
    unsigned submitted, released;
    bool saturated;
} CubeCoverageStatus;

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
// Resident cube columns whose current-generation sections already own a mesh.
unsigned ChunksCubeMeshReadyCount(void);

#endif
