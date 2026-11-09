/* Verify that cube fluid uses the surface contract for section lookup and
 * cross-face neighbours. The test installs a real cube contract, two loaded
 * chunks and a bounded floor, then checks that water crossing a face edge keeps
 * its total volume. It intentionally does not claim visual or save integration;
 * those are covered by the surface-cube and V30 tests. */
#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/fluid.h"
#include "world/surface_contract_factory.h"
#include "world/surface_topology.h"
#include "world/world_cube.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

BlockType SpaceBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

BlockType NetherBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

static WorldSurfaceSnapshot Snapshot(void)
{
    return (WorldSurfaceSnapshot){
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .topology = { SURFACE_TOPOLOGY_CUBE_GRID, 1u },
            .bodyId = 0u,
            .seed = 0x24681357u
        },
        .planetProfile = {
            .seed = 0x24681357u,
            .style = SOLAR_STYLE_TEMPERATE,
            .hasSolidSurface = true,
            .surfaceGravity = 1.0f
        }
    };
}

static Chunk *InstallCubeChunk(SurfaceCubeChunkKey key, int slot)
{
    assert(slot >= 0 && slot < MAX_ACTIVE_CHUNKS);
    Chunk *chunk = &chunks[slot];
    memset(chunk, 0, sizeof(*chunk));
    chunk->loaded = true;
    chunk->spherical = true;
    chunk->cubeGrid = true;
    chunk->cubeKey = key;
    chunk->cubeWorldGeneration = WorldCurrentGeneration();
    chunk->cx = key.u;
    chunk->cz = key.v;
    chunk->surfaceAddress = (SurfaceAddress){
        key.bodyId, key.face, key.u * CHUNK_SIZE, key.v * CHUNK_SIZE, 0
    };
    ChunkIndexInsert(chunk);

    ChunkSection *section = ChunkGetSection(chunk, 0, true);
    assert(section);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int z = 0; z < CHUNK_SIZE; z++) {
            section->blocks[x][0][z] = BLOCK_STONE;
        }
    }
    assert(ChunkMarkTerrainSectionResolved(chunk, 0));
    return chunk;
}

static void ResetWorld(void)
{
    FluidReset();
    assert(WorldInstallSurfaceSnapshot(NULL));
    UnloadAllChunks();
    ChunkIndexReset();
    WorldCubeClearEdits();
    WorldResetTopologyPolicy();
    WorldSetCubeRuntimeEnabled(false);
}

static void TestCrossFaceWaterFlow(void)
{
    ResetWorld();
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy policy = {
        { SURFACE_TOPOLOGY_CUBE_GRID, 1u }, SURFACE_TOPOLOGY_MAP_V1
    };
    assert(WorldInstallTopologyPolicy(policy));
    WorldSurfaceSnapshot snapshot = Snapshot();
    SurfaceContract *contract = SurfaceContractCreate(
        &snapshot, TERRAIN_FLAT, WorldNextSurfaceGeneration());
    assert(contract && WorldInstallSurfaceSnapshotWithContract(&snapshot, contract));

    SurfaceAddress source = {0, SURFACE_FACE_POS_Y, 4095, 1024, 1};
    SurfaceAddress target;
    SurfaceDirection back = SURFACE_DIRECTION_RADIAL_MINUS;
    assert(SurfaceContractNeighbor(contract, source,
                                   SURFACE_DIRECTION_U_PLUS,
                                   &target, &back));
    assert(target.face != source.face);
    SurfaceCubeChunkKey sourceKey;
    SurfaceCubeChunkKey targetKey;
    assert(SurfaceCubeChunkAt(source, &sourceKey));
    assert(SurfaceCubeChunkAt(target, &targetKey));
    assert(!SurfaceCubeChunkKeyEqual(sourceKey, targetKey));
    InstallCubeChunk(sourceKey, 0);
    InstallCubeChunk(targetKey, 1);
    assert(SurfaceContractCellIsKnown(contract, source));
    assert(SurfaceContractCellIsKnown(contract, target));

    SurfaceMapCell sourceMap;
    SurfaceMapCell targetMap;
    assert(SurfaceAddressCanonicalMapCell(source, &sourceMap));
    assert(SurfaceAddressCanonicalMapCell(target, &targetMap));
    assert(FluidSetVolumeAt(sourceMap.x, 1, sourceMap.z, FLUID_CAPACITY));
    assert(FluidGetVolumeAt(sourceMap.x, 1, sourceMap.z) == FLUID_CAPACITY);

    FluidStepTicks(40u);
    uint8_t sourceVolume = FluidGetVolumeAt(sourceMap.x, 1, sourceMap.z);
    uint8_t targetVolume = FluidGetVolumeAt(targetMap.x, 1, targetMap.z);
    assert(targetVolume > 0u);
    assert(FluidLoadedVolume() == FLUID_CAPACITY);
    assert(sourceVolume > 0u || targetVolume > 0u);
}

int main(void)
{
    TestCrossFaceWaterFlow();
    ResetWorld();
    puts("cube fluid cross-face tests passed");
    return 0;
}
