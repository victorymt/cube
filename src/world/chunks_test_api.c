#include "world/chunks.h"

#ifdef CHUNKS_TESTING
#include "world/chunks_internal.h"

bool ChunksTestBuildSurfaceWaterMeshDataWithSnapshot(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char *waterVolumes, int layerY, int chunkX, int chunkZ,
    const FloraStructureInstance *structures, int structureCount,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, const ChunkTestBoundarySnapshot *boundary,
    Mesh *outMesh)
{
    return BuildChunkSurfaceWaterMeshDataWithSnapshot(
        blocks, waterVolumes, layerY, chunkX, chunkZ, structures,
        structureCount, faces, nearbyTorchIndices, nearbyTorchCount,
        (const SurfaceBoundarySnapshot *)boundary, outMesh);
}

bool ChunksTestBuildMeshDataFilteredWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const ChunkTestBoundarySnapshot *boundary, Mesh *outMesh)
{
    return BuildMeshDataFilteredWithSnapshot(
        blocks, height, layerY, chunkX, chunkZ, transparent, includePlants,
        plantsOnly, excludeWater, faces, nearbyTorchIndices,
        nearbyTorchCount, (const SurfaceBoundarySnapshot *)boundary, outMesh);
}

bool ChunksTestSurfaceBoundaryCellAt(
    const ChunkTestBoundarySnapshot *boundary, int lx, int y, int lz,
    BlockType *outBlock, unsigned char *outVolume)
{
    return SurfaceBoundaryCellAt(
        (const SurfaceBoundarySnapshot *)boundary, lx, y, lz,
        outBlock, outVolume);
}

bool ChunksTestBuildLodHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const ChunkTestBoundarySnapshot *boundary, Mesh *outMesh)
{
    return BuildChunkLodHeightfieldMeshData(
        blocks, chunkX, chunkZ, lod,
        (const SurfaceBoundarySnapshot *)boundary, outMesh);
}
#endif
