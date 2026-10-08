#ifndef VOXELCRAFT_TERRAIN_CUBE_H
#define VOXELCRAFT_TERRAIN_CUBE_H

#include "world/terrain.h"
#include "world/world_surface_snapshot.h"
#include "world/surface_cube.h"

#define TERRAIN_GENERATION_CUBE 9u
// Keep the entire generated shell outside the center of the existing globe.
// Shallow water retains its scale; deep oceans are compressed in v9.
#define TERRAIN_CUBE_MIN_RADIAL (-2048)

typedef struct TerrainCubeContext TerrainCubeContext;
typedef struct TerrainCubeColumn {
    const TerrainCubeContext *owner;
    SurfaceAddress address;
    Vector3 direction;
    SurfaceTerrainSample terrain;
    PlanetSurfaceSample planet;
    uint64_t generation;
} TerrainCubeColumn;

// Creates an independent immutable snapshot and atlas. Retain before handing
// to a worker; release its reference on every completion/cancellation path.
// No sampling operation consults the currently installed world or its caches.
TerrainCubeContext *TerrainCubeContextCreate(const WorldSurfaceSnapshot *snapshot,
                                             TerrainMode mode, uint64_t generation);
TerrainCubeContext *TerrainCubeContextRetain(TerrainCubeContext *context);
void TerrainCubeContextRelease(TerrainCubeContext *context);
const WorldSurfaceSnapshot *TerrainCubeContextSnapshot(const TerrainCubeContext *context);
uint64_t TerrainCubeContextGeneration(const TerrainCubeContext *context);
bool TerrainCubeSampleColumn(const TerrainCubeContext *context,
                              SurfaceAddress address, TerrainCubeColumn *outColumn);
bool TerrainCubeBlock(const TerrainCubeContext *context, const TerrainCubeColumn *column,
                       int radial, BlockType *outBlock);
bool TerrainCubeGenerateSection(const TerrainCubeContext *context,
                                 SurfaceCubeChunkKey key, int sectionY,
                                 unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]);
// Staging-chunk adapters. A new column contains sections around the ground
// and liquid surfaces; deeper sections are generated explicitly on demand.
// Callers must discard failed staging results, and apply edits after success.
bool TerrainCubeGenerateChunk(const TerrainCubeContext *context,
                              SurfaceCubeChunkKey key, Chunk *staging);
bool TerrainCubeGenerateChunkSection(const TerrainCubeContext *context,
                                     Chunk *staging, int sectionY);

#endif
