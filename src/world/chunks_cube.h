#ifndef VOXELCRAFT_CHUNKS_CUBE_H
#define VOXELCRAFT_CHUNKS_CUBE_H

#include "world/terrain_cube.h"

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

#endif
