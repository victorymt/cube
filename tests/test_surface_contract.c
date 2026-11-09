/* The topology-neutral surface contract must give legacy and cube consumers
 * the same identity, cell, adjacency, sampling, region and coverage semantics.
 * These tests also pin transactional contract installation and ownership. */
#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/surface_contract_factory.h"
#include "world/surface_topology.h"
#include "world/world_cube.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

uint8_t FluidGetVolumeAt(int x, int y, int z)
{
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    return chunk ? ChunkGetLocalWaterVolume(chunk, lx, y, lz) : 0u;
}

bool FluidSetVolumeAt(int x, int y, int z, uint8_t volume)
{
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    return chunk && chunk->loaded &&
        ChunkSetLocalWaterVolume(chunk, lx, y, lz, volume);
}

static WorldSurfaceSnapshot Snapshot(WorldSurfaceKind kind,
                                     SurfaceTopologyVersion topology,
                                     uint32_t bodyId, uint32_t seed,
                                     int originX, int originZ)
{
    return (WorldSurfaceSnapshot){
        .context = {
            .active = true,
            .kind = kind,
            .topology = topology,
            .bodyId = bodyId,
            .seed = seed,
            .originX = originX,
            .originZ = originZ
        },
        .planetProfile = {
            .seed = seed,
            .style = SOLAR_STYLE_TEMPERATE,
            .hasSolidSurface = true,
            .surfaceGravity = 1.0f
        }
    };
}

static void ResetWorldMemory(void)
{
    assert(WorldInstallSurfaceSnapshot(NULL));
    UnloadAllChunks();
    ChunkIndexReset();
    WorldCubeClearEdits();
    WorldResetTopologyPolicy();
    WorldSetCubeRuntimeEnabled(false);
}

static Chunk *InstallLegacyChunk(int cx, int cz)
{
    Chunk *chunk = &chunks[0];
    memset(chunk, 0, sizeof(*chunk));
    chunk->loaded = true;
    chunk->spherical = true;
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->surfaceKey = ChunkSurfaceKeyAt(cx, cz);
    ChunkIndexInsert(chunk);
    return chunk;
}

static Chunk *InstallCubeChunk(SurfaceCubeChunkKey key)
{
    Chunk *chunk = &chunks[0];
    memset(chunk, 0, sizeof(*chunk));
    chunk->loaded = true;
    chunk->spherical = true;
    chunk->cubeGrid = true;
    chunk->cubeKey = key;
    chunk->cubeWorldGeneration = WorldCurrentGeneration();
    chunk->cx = key.u;
    chunk->cz = key.v;
    ChunkIndexInsert(chunk);
    return chunk;
}

static void TestAddressIdentity(void)
{
    SurfaceAddress a = {7, SURFACE_FACE_POS_Z, 4, 9, -12};
    SurfaceAddress b = a;
    assert(SurfaceContractAddressIsValid(a));
    assert(SurfaceContractAddressEqual(a, b));
    assert(SurfaceContractAddressHash(a) == SurfaceContractAddressHash(b));
    b.u++;
    assert(!SurfaceContractAddressEqual(a, b));
    assert(SurfaceContractAddressHash(a) != SurfaceContractAddressHash(b));
    assert(!SurfaceContractAddressIsValid((SurfaceAddress){
        .face = SURFACE_FACE_COUNT, .u = 0, .v = 0 }));
    assert(!SurfaceContractAddressIsValid((SurfaceAddress){
        .face = SURFACE_FACE_POS_X, .u = -1, .v = 0 }));
}

static void TestLegacyContract(void)
{
    ResetWorldMemory();
    WorldSetSeed(0x12345678u);
    WorldSurfaceSnapshot snapshot = Snapshot(
        WORLD_SURFACE_HOME, (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1, 0, 0x12345678u, 0, 0);
    SurfaceContract *contract = SurfaceContractCreate(
        &snapshot, TERRAIN_VARIED, WorldNextSurfaceGeneration());
    assert(contract && SurfaceContractIsValid(contract));
    assert(strcmp(SurfaceContractBackendName(contract), "legacy-map") == 0);
    assert(SurfaceContractBodyId(contract) == 0u);
    assert(WorldInstallSurfaceSnapshotWithContract(&snapshot, contract));
    assert(WorldCurrentSurfaceContract() == contract);

    SurfaceAddress address = SurfaceAddressFromMapCoordinates(0, 100.0f, 100.0f, 0);
    assert(SurfaceContractAddressIsValid(address));
    assert(!SurfaceContractCellIsKnown(contract, address));

    SurfaceMapCell canonical;
    assert(SurfaceAddressCanonicalMapCell(address, &canonical));
    int cx, cz, lx, lz;
    WorldToChunkLocal(canonical.x, canonical.z, &cx, &cz, &lx, &lz);
    (void)lx;
    (void)lz;
    Chunk *chunk = InstallLegacyChunk(cx, cz);
    assert(!SurfaceContractCellIsKnown(contract, address));
    assert(ChunkMarkTerrainSectionResolved(
        chunk, SurfaceSectionYFromBlockY(address.radial)));
    assert(SurfaceContractCellIsKnown(contract, address));
    WorldCell cell;
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(cell.block == BLOCK_AIR && cell.waterVolume == 0u);

    WorldCell write = { BLOCK_STONE, 0u };
    assert(SurfaceContractCellWrite(contract, address, write));
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(WorldCellEqual(cell, write));
    WorldCell water = { BLOCK_AIR, 127u };
    assert(SurfaceContractCellWrite(contract, address, water));
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(WorldCellEqual(cell, water));

    SurfaceChunkHandle chunkHandle, chunkBack;
    assert(SurfaceContractChunkFromAddress(contract, address, &chunkHandle));
    assert(SurfaceContractChunkIsValid(contract, chunkHandle));
    SurfaceAddress anchor;
    assert(SurfaceContractChunkAnchorAddress(contract, chunkHandle, &anchor));
    assert(SurfaceContractChunkFromAddress(contract, anchor, &chunkBack));
    assert(SurfaceContractChunkEqual(contract, chunkHandle, chunkBack));
    assert(SurfaceContractChunkHash(contract, chunkHandle) ==
           SurfaceContractChunkHash(contract, chunkBack));
    SurfaceSectionHandle section;
    assert(SurfaceContractSectionFromAddress(contract, address, &section));
    assert(SurfaceContractSectionIsValid(contract, section));

    SurfaceAddress east;
    SurfaceDirection back;
    assert(SurfaceContractNeighbor(contract, address,
                                   SURFACE_DIRECTION_U_PLUS, &east, &back));
    assert(back == SURFACE_DIRECTION_U_MINUS);
    SurfaceAddress eastBack;
    assert(SurfaceContractNeighbor(contract, east, back, &eastBack, NULL));
    assert(SurfaceContractAddressEqual(address, eastBack));

    SurfaceContractSample sample;
    assert(SurfaceContractSampleAt(contract, address, &sample));
    assert(isfinite(sample.elevation));
    assert(sample.biome >= BIOME_PLAINS && sample.biome <= BIOME_SWAMP);
    SurfaceRegionHandle region = SurfaceContractRegionForAddress(contract, address);
    assert(region.opaque[0] != 0u);
    assert(SurfaceContractSameRegion(contract, address, east));
    assert(!SurfaceContractSupportsPhysicalCoverage(contract));
    assert(!SurfaceContractSupportsPhysicalRaycast(contract));
    assert(!SurfaceContractSupportsPhysicalPose(contract));
}

static void TestCubeContract(void)
{
    ResetWorldMemory();
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy policy = {
        { SURFACE_TOPOLOGY_CUBE_GRID, 1u }, SURFACE_TOPOLOGY_MAP_V1
    };
    assert(WorldInstallTopologyPolicy(policy));
    WorldSurfaceSnapshot snapshot = Snapshot(
        WORLD_SURFACE_HOME, policy.home, 0, 0x87654321u, 0, 0);
    SurfaceContract *contract = SurfaceContractCreate(
        &snapshot, TERRAIN_VARIED, WorldNextSurfaceGeneration());
    assert(contract && SurfaceContractIsValid(contract));
    assert(strcmp(SurfaceContractBackendName(contract), "cube-grid") == 0);
    assert(WorldInstallSurfaceSnapshotWithContract(&snapshot, contract));

    SurfaceAddress address = {0, SURFACE_FACE_POS_Z, 8, 8, 8};
    assert(!SurfaceContractCellIsKnown(contract, address));
    SurfaceCubeChunkKey key;
    assert(SurfaceCubeChunkAt(address, &key));
    Chunk *chunk = InstallCubeChunk(key);
    assert(ChunkSetLocalCell(chunk, 8, 8, 8,
                             (WorldCell){ BLOCK_STONE, 0u }));
    assert(ChunkMarkTerrainSectionResolved(chunk, 0));
    assert(SurfaceContractCellIsKnown(contract, address));
    WorldCell cell;
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(cell.block == BLOCK_STONE && cell.waterVolume == 0u);

    assert(SurfaceContractCellWrite(contract, address,
                                    (WorldCell){ BLOCK_GOLD_ORE, 0u }));
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(cell.block == BLOCK_GOLD_ORE && cell.waterVolume == 0u);
    assert(!SurfaceContractCellWrite(contract, address,
                                     (WorldCell){ BLOCK_BRICK, 255u }));
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(cell.block == BLOCK_GOLD_ORE && cell.waterVolume == 0u);
    assert(SurfaceContractCellWrite(contract, address,
                                    (WorldCell){ BLOCK_AIR, 128u }));
    assert(SurfaceContractCellRead(contract, address, &cell));
    assert(cell.block == BLOCK_AIR && cell.waterVolume == 128u);

    SurfaceChunkHandle handle, handleBack;
    assert(SurfaceContractChunkFromAddress(contract, address, &handle));
    assert(SurfaceContractChunkIsValid(contract, handle));
    SurfaceAddress anchor;
    assert(SurfaceContractChunkAnchorAddress(contract, handle, &anchor));
    assert(SurfaceContractChunkFromAddress(contract, anchor, &handleBack));
    assert(SurfaceContractChunkEqual(contract, handle, handleBack));
    SurfaceSectionHandle section;
    assert(SurfaceContractSectionFromAddress(contract, address, &section));
    assert(SurfaceContractSectionIsValid(contract, section));

    SurfaceAddress edge = {0, SURFACE_FACE_POS_Y, 4095, 17, 0};
    SurfaceAddress across;
    SurfaceDirection back;
    assert(SurfaceContractNeighbor(contract, edge,
                                   SURFACE_DIRECTION_U_PLUS, &across, &back));
    assert(across.face != edge.face);
    SurfaceAddress acrossBack;
    assert(SurfaceContractNeighbor(contract, across, back, &acrossBack, NULL));
    assert(SurfaceContractAddressEqual(edge, acrossBack));

    SurfaceContractSample sample;
    assert(SurfaceContractSampleAt(contract, address, &sample));
    assert(isfinite(sample.elevation));
    assert(sample.biome >= BIOME_PLAINS && sample.biome <= BIOME_SWAMP);
    SurfaceRegionHandle region = SurfaceContractRegionForAddress(contract, address);
    assert(region.opaque[0] != 0u);
    SurfaceAddress sameFace = address;
    sameFace.u++;
    assert(SurfaceContractSameRegion(contract, address, sameFace));
    assert(!SurfaceContractSameRegion(contract, address, edge));
    assert(SurfaceContractSupportsPhysicalCoverage(contract));
    assert(SurfaceContractSupportsPhysicalRaycast(contract));
    assert(SurfaceContractSupportsPhysicalPose(contract));

    SurfaceCoverageCandidate candidates[32];
    SurfaceCoverageResult coverage;
    assert(SurfaceContractCoverageCollect(contract, address, 24.0,
                                          candidates, 32, &coverage));
    assert(coverage.requested > 0u && coverage.admitted > 0u);
    for (unsigned i = 0; i < coverage.admitted; i++) {
        assert(SurfaceContractChunkIsValid(contract, candidates[i].chunk));
        if (i) assert(candidates[i].centerDistance >=
                      candidates[i - 1].centerDistance);
    }
    SurfaceCoverageStatus status;
    assert(SurfaceContractCoverageUpdate(contract, address, 24.0, 0, &status));
    assert(status.requested == coverage.requested);
}

static void TestMixedPolicyAndTransaction(void)
{
    ResetWorldMemory();
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy mixed = {
        { SURFACE_TOPOLOGY_CUBE_GRID, 1u }, SURFACE_TOPOLOGY_MAP_V1
    };
    assert(WorldInstallTopologyPolicy(mixed));
    WorldSurfaceSnapshot home = Snapshot(
        WORLD_SURFACE_HOME, mixed.home, 0, 11u, 0, 0);
    WorldSurfaceSnapshot planet = Snapshot(
        WORLD_SURFACE_PLANET, mixed.planets, 77u, 12u, 100, -50);
    SurfaceContract *homeContract = SurfaceContractCreate(
        &home, TERRAIN_VARIED, WorldNextSurfaceGeneration());
    SurfaceContract *planetContract = SurfaceContractCreate(
        &planet, TERRAIN_VARIED, WorldNextSurfaceGeneration());
    assert(homeContract && planetContract);
    assert(strcmp(SurfaceContractBackendName(homeContract), "cube-grid") == 0);
    assert(strcmp(SurfaceContractBackendName(planetContract), "legacy-map") == 0);
    SurfaceContractRelease(planetContract);

    uint64_t before = WorldCurrentGeneration();
    assert(WorldNextSurfaceGeneration() == before + 1u);
    assert(WorldCurrentGeneration() == before);
    assert(WorldInstallSurfaceSnapshotWithContract(&home, homeContract));
    const WorldSurfaceSnapshot *installed = WorldCurrentSurfaceSnapshot();
    uint64_t installedGeneration = WorldCurrentGeneration();
    const SurfaceContract *installedContract = WorldCurrentSurfaceContract();

    WorldSurfaceSnapshot wrong = Snapshot(
        WORLD_SURFACE_HOME, (SurfaceTopologyVersion)SURFACE_TOPOLOGY_MAP_V1, 0, 11u, 0, 0);
    SurfaceContract *wrongContract = SurfaceContractCreate(
        &wrong, TERRAIN_VARIED, WorldNextSurfaceGeneration());
    assert(wrongContract);
    assert(!WorldInstallSurfaceSnapshotWithContract(installed, wrongContract));
    assert(WorldCurrentGeneration() == installedGeneration);
    assert(WorldCurrentSurfaceContract() == installedContract);
    assert(memcmp(WorldCurrentSurfaceSnapshot(), installed, sizeof(*installed)) == 0);
    assert(SurfaceContractIsValid(wrongContract));
    SurfaceContractRelease(wrongContract);
}

int main(void)
{
    TestAddressIdentity();
    TestLegacyContract();
    TestCubeContract();
    TestMixedPolicyAndTransaction();
    ResetWorldMemory();
    puts("topology-neutral surface contract tests passed");
    return 0;
}
