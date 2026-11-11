#include "world/chunks_internal.h"
#include "world/chunks.h"
#include "world/chunks_cube.h"
#include "world/surface_coverage.h"
#include "world/surface_contract.h"
#include "world/surface_topology.h"
#include "world/world_surface_snapshot.h"

bool RebuildDirtyChunkMeshAt(int x, int y, int z)
{
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    ChunkSection *section = chunk ? ChunkGetSection(
        chunk, SurfaceSectionYFromBlockY(y), false) : NULL;
    return ChunksRebuildDirtySectionMesh(chunk, section);
}

bool RebuildDirtyChunkMeshAtAddress(WorldBlockAddress address)
{
    if (!WorldBlockAddressIsValid(address)) return false;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    if (SurfaceContractSupportsPhysicalCoverage(contract)) {
        SurfaceCubeChunkKey key;
        if (!SurfaceCubeChunkAt(address.at.surface, &key)) return false;
        Chunk *chunk = FindCubeChunk(key);
        ChunkSection *section = chunk ? ChunkGetSection(
            chunk, SurfaceSectionYFromBlockY(address.at.surface.radial),
            false) : NULL;
        return ChunksRebuildDirtySectionMesh(chunk, section);
    }
    SurfaceMapCell canonical;
    if (!SurfaceAddressCanonicalMapCell(address.at.surface, &canonical))
        return false;
    return RebuildDirtyChunkMeshAt(
        canonical.x - WorldSurfaceMapOriginX(),
        address.at.surface.radial,
        canonical.z - WorldSurfaceMapOriginZ());
}

bool ChunksBlockMeshReadyAtAddress(WorldBlockAddress address)
{
    if (!WorldBlockAddressIsValid(address)) return false;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    Chunk *chunk = NULL;
    if (SurfaceContractSupportsPhysicalCoverage(contract)) {
        SurfaceCubeChunkKey key;
        if (!SurfaceCubeChunkAt(address.at.surface, &key)) return false;
        chunk = FindCubeChunk(key);
    } else {
        SurfaceMapCell canonical;
        if (!SurfaceAddressCanonicalMapCell(address.at.surface, &canonical))
            return false;
        int cx = 0, cz = 0, lx = 0, lz = 0;
        WorldToChunkLocal(canonical.x - WorldSurfaceMapOriginX(),
                          canonical.z - WorldSurfaceMapOriginZ(),
                          &cx, &cz, &lx, &lz);
        (void)lx;
        (void)lz;
        chunk = FindChunk(cx, cz);
    }
    if (!chunk || !chunk->loaded) return false;
    const ChunkSection *section = ChunkGetSectionConst(
        chunk, SurfaceSectionYFromBlockY(address.at.surface.radial));
    return section && !section->dirty &&
           ChunkSectionLodReady(section, chunk->targetLod);
}

bool ChunksBlockMeshReadyAtMapPosition(int x, int y, int z)
{
    int cx = 0, cz = 0, lx = 0, lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    (void)lx;
    (void)lz;
    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk || !chunk->loaded) return false;
    const ChunkSection *section = ChunkGetSectionConst(
        chunk, SurfaceSectionYFromBlockY(y));
    return section && !section->dirty &&
           ChunkSectionLodReady(section, chunk->targetLod);
}

ChunkTopologyCounts ChunksGetTopologyCounts(void)
{
    ChunkTopologyCounts counts = { 0 };
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded) continue;
        if (chunks[i].cubeGrid) counts.cube++;
        else counts.legacy++;
    }
    return counts;
}
