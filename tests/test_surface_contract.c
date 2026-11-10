/* The topology-neutral surface contract must give legacy and cube consumers
 * the same identity, cell, adjacency, sampling, region and coverage semantics.
 * These tests also pin transactional contract installation and ownership. */
#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/surface_contract_factory.h"
#include "world/surface_topology.h"
#include "world/world_cube.h"
#include "world/world_surface_snapshot.h"
#include "world/hydrology_runtime.h"
#include "world/radiation_runtime.h"
#include "surface_contract_fixture.h"

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

typedef struct LegacyChunkLayout {
    int primaryCx;
    int primaryCz;
    int twinCx;
    int twinCz;
    int sectionY;
} LegacyChunkLayout;

typedef struct CubeChunkLayout {
    SurfaceCubeChunkKey primary;
    SurfaceCubeChunkKey twin;
    int sectionY;
} CubeChunkLayout;

static Chunk *InstallLegacyChunkAt(int slot, int cx, int cz)
{
    assert(slot >= 0 && slot < MAX_ACTIVE_CHUNKS);
    Chunk *chunk = &chunks[slot];
    memset(chunk, 0, sizeof(*chunk));
    chunk->loaded = true;
    chunk->spherical = true;
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->surfaceKey = ChunkSurfaceKeyAt(cx, cz);
    ChunkIndexInsert(chunk);
    return chunk;
}

static Chunk *InstallCubeChunkAt(int slot, SurfaceCubeChunkKey key)
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
    ChunkIndexInsert(chunk);
    return chunk;
}

static void InstallLegacyFixtureChunks(void *context)
{
    const LegacyChunkLayout *layout = context;
    assert(layout);
    UnloadAllChunks();
    ChunkIndexReset();
    Chunk *primary = InstallLegacyChunkAt(
        0, layout->primaryCx, layout->primaryCz);
    Chunk *twin = InstallLegacyChunkAt(1, layout->twinCx, layout->twinCz);
    assert(ChunkMarkTerrainSectionResolved(primary, layout->sectionY));
    assert(ChunkMarkTerrainSectionResolved(twin, layout->sectionY));
}

static void InstallCubeFixtureChunks(void *context)
{
    const CubeChunkLayout *layout = context;
    assert(layout);
    UnloadAllChunks();
    ChunkIndexReset();
    Chunk *primary = InstallCubeChunkAt(0, layout->primary);
    Chunk *twin = InstallCubeChunkAt(1, layout->twin);
    assert(ChunkMarkTerrainSectionResolved(primary, layout->sectionY));
    assert(ChunkMarkTerrainSectionResolved(twin, layout->sectionY));
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

static void AssertWorldCellFacade(const SurfaceContract *contract,
                                  SurfaceAddress address)
{
    SurfaceMapCell canonical;
    assert(SurfaceAddressCanonicalMapCell(address, &canonical));
    int localX = canonical.x - WorldSurfaceMapOriginX();
    int localZ = canonical.z - WorldSurfaceMapOriginZ();
    WorldCell direct;
    WorldCell wrapper;
    assert(SurfaceContractCellRead(contract, address, &direct));
    assert(WorldSurfaceCellAt(localX, address.radial, localZ, &wrapper));
    assert(WorldCellEqual(direct, wrapper));
    assert(WorldSurfaceCellKnownAt(localX, address.radial, localZ));
    Biome biome = BIOME_PLAINS;
    assert(WorldSurfaceBiomeAt(localX, localZ, &biome));
    SurfaceContractSample directSample;
    SurfaceContractSample worldSample;
    assert(SurfaceContractSampleAt(contract, address, &directSample));
    assert(WorldSurfaceSampleAt(localX, localZ, &worldSample));
    assert(worldSample.elevation == directSample.elevation);
    assert(worldSample.seaLevel == directSample.seaLevel);
    assert(worldSample.waterSurface == directSample.waterSurface);
    assert(worldSample.slope == directSample.slope);
    assert(worldSample.seabedY == directSample.seabedY);
    assert(worldSample.waterDepth == directSample.waterDepth);
    assert(worldSample.bathymetryZone == directSample.bathymetryZone);
    assert(worldSample.bathymetryMaterial == directSample.bathymetryMaterial);
    assert(worldSample.biome == directSample.biome);
    assert(worldSample.planetBiome == directSample.planetBiome);
    assert(worldSample.hasPlanetBiome == directSample.hasPlanetBiome);
    assert(WorldSurfaceHeightAt(localX, localZ) ==
           (int)lroundf(directSample.elevation));
    BathymetrySample bathymetry;
    assert(WorldSurfaceBathymetryAt(localX, localZ, &bathymetry));
    assert(bathymetry.seabedY == directSample.seabedY);
    assert(bathymetry.waterDepth == directSample.waterDepth);
    assert(!directSample.hasPlanetBiome);
}

static int RuntimeRegionCoordinate(int mapCoordinate)
{
    int64_t value = mapCoordinate;
    if (value >= 0) return (int)(value / HYDROLOGY_RUNTIME_REGION_SIZE);
    return (int)(-((-value + HYDROLOGY_RUNTIME_REGION_SIZE - 1) /
                  HYDROLOGY_RUNTIME_REGION_SIZE));
}

static void AssertRegionRuntimeFacade(const SurfaceContract *contract,
                                      SurfaceAddress address,
                                      SurfaceAddress sameRegion,
                                      SurfaceAddress outsideRegion)
{
    SurfaceRegionHandle region =
        SurfaceContractRegionForAddress(contract, address);
    assert(region.opaque[0] != 0u || region.opaque[1] != 0u);
    assert(SurfaceContractSameRegion(contract, address, sameRegion));
    assert(!SurfaceContractSameRegion(contract, address, outsideRegion));

    HydrologyRuntime hydrology;
    assert(HydrologyRuntimeInit(&hydrology, NULL));
    assert(HydrologyRuntimeBindAddress(&hydrology, address));
    assert(hydrology.location.hasRegion);
    assert(hydrology.location.region.opaque[0] == region.opaque[0] &&
           hydrology.location.region.opaque[1] == region.opaque[1]);

    SurfaceMapCell primaryCell;
    SurfaceMapCell sameRegionCell;
    assert(SurfaceAddressCanonicalMapCell(address, &primaryCell));
    assert(SurfaceAddressCanonicalMapCell(sameRegion, &sameRegionCell));
    int primaryGridX = (primaryCell.x -
        RuntimeRegionCoordinate(primaryCell.x) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    int primaryGridZ = (primaryCell.z -
        RuntimeRegionCoordinate(primaryCell.z) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    int sameGridX = (sameRegionCell.x -
        RuntimeRegionCoordinate(sameRegionCell.x) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    int sameGridZ = (sameRegionCell.z -
        RuntimeRegionCoordinate(sameRegionCell.z) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    assert(primaryGridX != sameGridX || primaryGridZ != sameGridZ);
    assert(HydrologyRuntimeAddPrecipitationCell(
        &hydrology, primaryGridX, primaryGridZ, 1.0f));

    HydrologyRuntimeFeedback feedback;
    assert(HydrologyRuntimeFeedbackAtAddress(
        &hydrology, address, &feedback));
    assert(feedback.surfaceWaterM > 0.0f);
    assert(HydrologyRuntimeFeedbackAtAddress(
        &hydrology, sameRegion, &feedback));
    assert(feedback.surfaceWaterM == 0.0f);
    assert(!HydrologyRuntimeFeedbackAtAddress(
        &hydrology, outsideRegion, &feedback));
    HydrologyRuntimeFree(&hydrology);

    assert(RadiationRuntimeGlobalReset(NULL));
    assert(RadiationRuntimeGlobalBindAddress(address, 288.0));
    PlanetProfile profile = {
        .receivedIrradiance = 1.0f,
        .equilibriumTempK = 288.0f,
        .albedo = 0.30f,
        .greenhouseEffect = 0.40f,
        .orbitalEccentricity = 0.0167f,
        .axialTilt = 23.44f * 3.14159265358979323846f / 180.0f,
        .yearLength = 365.25f,
        .rotationRate = 360.0f
    };
    RadiationRuntimeLocationTime location = {
        .latitudeRad = 0.4,
        .longitudeRad = -0.6,
        .simulationTimeGameTime = 4.0
    };
    unsigned steps = 0u;
    assert(RadiationRuntimeGlobalAdvance(
        &profile, &location, 0.1, &steps));
    assert(steps > 0u);
    double offsetK = 0.0;
    assert(RadiationRuntimeGlobalTemperatureOffsetAtAddress(
        address, &offsetK));
    assert(isfinite(offsetK));
    assert(!RadiationRuntimeGlobalTemperatureOffsetAtAddress(
        outsideRegion, &offsetK));
    assert(offsetK == 0.0);

    RadiationRuntimeSnapshot before = { 0 };
    assert(RadiationRuntimeGlobalSnapshot(&before));
    FILE *file = tmpfile();
    assert(file);
    assert(RadiationRuntimeGlobalSaveState(file));
    rewind(file);
    assert(RadiationRuntimeGlobalReset(NULL));
    assert(RadiationRuntimeGlobalLoadState(file));
    fclose(file);
    RadiationRuntimeSnapshot after = { 0 };
    assert(RadiationRuntimeGlobalSnapshot(&after));
    assert(after.stepCount == before.stepCount);
    assert(RadiationRuntimeGlobalTemperatureOffsetAtAddress(
        address, &offsetK));
    assert(RadiationRuntimeGlobalBindAddress(address, 999.0));
    assert(RadiationRuntimeGlobalSnapshot(&after));
    assert(after.stepCount == before.stepCount);
}

static void AssertHydrologySamplingRegionTransition(
    const SurfaceContract *contract, SurfaceAddress first,
    SurfaceAddress next)
{
    assert(SurfaceContractSameRegion(contract, first, next));
    SurfaceMapCell firstCell;
    SurfaceMapCell nextCell;
    assert(SurfaceAddressCanonicalMapCell(first, &firstCell));
    assert(SurfaceAddressCanonicalMapCell(next, &nextCell));
    assert(RuntimeRegionCoordinate(firstCell.x) !=
           RuntimeRegionCoordinate(nextCell.x));

    HydrologyRuntime hydrology;
    assert(HydrologyRuntimeInit(&hydrology, NULL));
    assert(HydrologyRuntimeBindAddress(&hydrology, first));
    int gridX = (firstCell.x -
        RuntimeRegionCoordinate(firstCell.x) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    int gridZ = (firstCell.z -
        RuntimeRegionCoordinate(firstCell.z) *
            HYDROLOGY_RUNTIME_REGION_SIZE) /
        (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE);
    assert(HydrologyRuntimeAddPrecipitationCell(
        &hydrology, gridX, gridZ, 1.0f));

    assert(HydrologyRuntimeBindAddress(&hydrology, next));
    assert(hydrology.location.regionX ==
           RuntimeRegionCoordinate(nextCell.x));
    assert(hydrology.location.regionZ ==
           RuntimeRegionCoordinate(nextCell.z));
    HydrologyCell resetCell;
    assert(HydrologyRuntimeQueryCell(
        &hydrology, gridX, gridZ, &resetCell));
    assert(resetCell.surfaceWaterDepthM == 0.0f);
    HydrologyRuntimeFeedback feedback;
    assert(!HydrologyRuntimeFeedbackAtAddress(
        &hydrology, first, &feedback));
    assert(HydrologyRuntimeFeedbackAtAddress(
        &hydrology, next, &feedback));
    HydrologyRuntimeFree(&hydrology);
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
    SurfaceAddress sameRegion = SurfaceAddressFromMapCoordinates(
        0, 127.0f, 127.0f, 0);
    SurfaceAddress twin = SurfaceAddressFromMapCoordinates(0, 260.0f, 100.0f, 0);
    assert(SurfaceContractAddressIsValid(address));
    assert(SurfaceContractAddressIsValid(twin));
    assert(!SurfaceContractCellIsKnown(contract, address));

    SurfaceMapCell canonical;
    assert(SurfaceAddressCanonicalMapCell(address, &canonical));
    int primaryCx, primaryCz, lx, lz;
    WorldToChunkLocal(canonical.x, canonical.z,
                      &primaryCx, &primaryCz, &lx, &lz);
    (void)lx;
    (void)lz;
    SurfaceMapCell twinCanonical;
    assert(SurfaceAddressCanonicalMapCell(twin, &twinCanonical));
    int twinCx, twinCz;
    WorldToChunkLocal(twinCanonical.x, twinCanonical.z,
                      &twinCx, &twinCz, &lx, &lz);
    assert(primaryCx != twinCx || primaryCz != twinCz);

    LegacyChunkLayout layout = {
        .primaryCx = primaryCx,
        .primaryCz = primaryCz,
        .twinCx = twinCx,
        .twinCz = twinCz,
        .sectionY = SurfaceSectionYFromBlockY(address.radial)
    };
    InstallLegacyFixtureChunks(&layout);
    assert(SurfaceContractCellIsKnown(contract, address));

    SurfaceContractFixture fixture = {
        .contract = contract,
        .primary = address,
        .twin = twin,
        .hasTwin = true,
        .reload = InstallLegacyFixtureChunks,
        .reloadContext = &layout
    };
    SurfaceContractFixtureAssert(&fixture);
    AssertWorldCellFacade(contract, address);
    AssertRegionRuntimeFacade(contract, address, sameRegion, twin);
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

    SurfaceAddress address = SurfaceAddressFromMapCoordinates(
        0, 100.0f, 100.0f, 8);
    assert(SurfaceContractAddressIsValid(address));
    SurfaceAddress twin = {0, address.face == SURFACE_FACE_POS_X
        ? SURFACE_FACE_POS_Y : SURFACE_FACE_POS_X, 8, 8, 8};
    twin.u = address.u;
    twin.v = address.v;
    twin.radial = address.radial;
    SurfaceAddress sameRegion = SurfaceAddressFromMapCoordinates(
        0, 127.0f, 127.0f, address.radial);
    assert(!SurfaceContractCellIsKnown(contract, address));
    SurfaceCubeChunkKey primaryKey;
    SurfaceCubeChunkKey twinKey;
    assert(SurfaceCubeChunkAt(address, &primaryKey));
    assert(SurfaceCubeChunkAt(twin, &twinKey));
    assert(primaryKey.face != twinKey.face);
    assert(primaryKey.u == twinKey.u && primaryKey.v == twinKey.v);
    CubeChunkLayout layout = {
        .primary = primaryKey,
        .twin = twinKey,
        .sectionY = SurfaceSectionYFromBlockY(address.radial)
    };
    InstallCubeFixtureChunks(&layout);
    assert(SurfaceContractCellIsKnown(contract, address));

    SurfaceAddress edge = {0, SURFACE_FACE_POS_Y, 4095, 17, 0};
    SurfaceContractFixture fixture = {
        .contract = contract,
        .primary = address,
        .twin = twin,
        .hasTwin = true,
        .crossEdge = edge,
        .crossDirection = SURFACE_DIRECTION_U_PLUS,
        .hasCrossEdge = true,
        .crossEdgeChangesFace = true,
        .reload = InstallCubeFixtureChunks,
        .reloadContext = &layout
    };
    SurfaceContractFixtureAssert(&fixture);
    AssertWorldCellFacade(contract, address);
    AssertRegionRuntimeFacade(contract, address, sameRegion, twin);
    AssertHydrologySamplingRegionTransition(
        contract, address,
        SurfaceAddressFromMapCoordinates(0, 300.0f, 100.0f, address.radial));
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
