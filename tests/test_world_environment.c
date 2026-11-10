#include "world/world_environment.h"
#include "world/terrain_cube.h"
#include "world/terrain.h"
#include "world/surface_contract_internal.h"
#include "world/world_surface_snapshot.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

TerrainMode terrainMode = TERRAIN_VARIED;

TerrainMode WorldTerrainMode(void)
{
    return terrainMode;
}

void WorldSetTerrainMode(TerrainMode mode)
{
    terrainMode = mode;
}

int TerrainHeight(int x, int z, TerrainMode mode)
{
    return x + z + (int)mode;
}

int PlanetTerrainHeight(int x, int z)
{
    return 100 + x - z;
}

BathymetrySample TerrainBathymetryAt(int x, int z, TerrainMode mode)
{
    (void)x;
    (void)z;
    return (BathymetrySample){
        .seaLevel = mode == TERRAIN_FLAT ? -1 : HOME_SEA_LEVEL,
        .seabedY = 20,
        .waterDepth = mode == TERRAIN_FLAT ? 0 : HOME_SEA_LEVEL - 20
    };
}

BathymetrySample PlanetBathymetryAt(int x, int z)
{
    (void)x;
    (void)z;
    return (BathymetrySample){
        .seaLevel = 80,
        .seabedY = 30,
        .waterDepth = 50
    };
}


static int cubeSampleHeight = 42;
static float cubeSampleSlope = 1.25f;
static bool cubeSampleOk = true;

static bool FakeContractSample(const SurfaceContract *contract,
                               SurfaceAddress address,
                               SurfaceContractSample *out)
{
    if (!contract || !out) return false;
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    SurfaceMapCell cell;
    if (!SurfaceAddressCanonicalMapCell(address, &cell)) return false;
    cell.x -= context.originX;
    cell.z -= context.originZ;
    if (context.topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) {
        if (!cubeSampleOk) return false;
        *out = (SurfaceContractSample){
            .elevation = (float)cubeSampleHeight, .seaLevel = 80.0f,
            .waterSurface = 81.0f, .slope = cubeSampleSlope,
            .seabedY = 30, .waterDepth = 50,
            .bathymetryZone = BATHYMETRY_ZONE_SHELF,
            .bathymetryMaterial = BATHYMETRY_MATERIAL_ROCK,
            .biome = BIOME_PLAINS,
            .planetBiome = PLANET_BIOME_PLAINS,
            .hasPlanetBiome = false
        };
        return true;
    }
    if (context.kind == WORLD_SURFACE_PLANET) {
        *out = (SurfaceContractSample){
            .elevation = (float)(100 + cell.x - cell.z),
            .seaLevel = 80.0f, .waterSurface = 81.0f,
            .slope = 0.5f, .seabedY = 30, .waterDepth = 50,
            .bathymetryZone = BATHYMETRY_ZONE_SHELF,
            .bathymetryMaterial = BATHYMETRY_MATERIAL_SAND,
            .biome = BIOME_PLAINS,
            .planetBiome = PLANET_BIOME_BADLANDS,
            .hasPlanetBiome = true
        };
        return true;
    }
    bool flat = terrainMode == TERRAIN_FLAT;
    *out = (SurfaceContractSample){
        .elevation = (float)(cell.x + cell.z + (int)terrainMode),
        .seaLevel = flat ? -1.0f : (float)HOME_SEA_LEVEL,
        .waterSurface = flat ? -1.0f : 81.0f,
        .slope = 2.5f,
        .seabedY = 20, .waterDepth = flat ? 0 : HOME_SEA_LEVEL - 20,
        .bathymetryZone = flat ? BATHYMETRY_ZONE_LAND : BATHYMETRY_ZONE_COAST,
        .bathymetryMaterial = BATHYMETRY_MATERIAL_SEDIMENT,
        .biome = BIOME_PLAINS,
        .planetBiome = PLANET_BIOME_PLAINS,
        .hasPlanetBiome = false
    };
    return true;
}

static void FakeContractRelease(SurfaceContract *contract)
{
    (void)contract;
}

static const SurfaceContractOps fakeOps = {
    .sample_at = FakeContractSample,
    .release = FakeContractRelease
};
static SurfaceContract fakeContracts[32];
static unsigned fakeContractCount;

static SurfaceContract *NextFakeContract(const WorldSurfaceSnapshot *snapshot)
{
    if (!snapshot->context.active) return NULL;
    if (fakeContractCount >= sizeof(fakeContracts) / sizeof(fakeContracts[0])) {
        return NULL;
    }
    SurfaceContract *contract = &fakeContracts[fakeContractCount++];
    *contract = (SurfaceContract){
        .ops = &fakeOps,
        .implementation = &fakeContracts[fakeContractCount - 1],
        .topology = snapshot->context.topology,
        .bodyId = snapshot->context.bodyId
    };
    return contract;
}

static void SetEnvironment(WorldSurfaceKind kind, float gravityScale,
                           uint32_t bodyId, uint32_t seed,
                           int originX, int originZ)
{
    const WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = kind != WORLD_SURFACE_NONE,
            .kind = kind,
            .topology = WorldTopologyForSurface(kind),
            .bodyId = bodyId,
            .seed = seed,
            .originX = originX,
            .originZ = originZ
        },
        .planetProfile = {
            .seed = seed,
            .style = SOLAR_STYLE_TEMPERATE,
            .surfaceGravity = gravityScale,
            .hasSolidSurface = true
        }
    };
    assert(WorldInstallSurfaceSnapshotWithContract(
        &snapshot, NextFakeContract(&snapshot)));
    WorldSetNetherActive(false);
}

static void TestBlockRegions(void)
{
    SetEnvironment(WORLD_SURFACE_HOME, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    assert(WorldSurfaceMinY() == SURFACE_MIN_Y);
    assert(WorldSurfaceMaxYExclusive() == SURFACE_MAX_Y_EXCLUSIVE);
    assert(strcmp(WorldDimensionName(WORLD_DIMENSION_HOME), "home") == 0);
    assert(strcmp(WorldDimensionName(WORLD_DIMENSION_PLANET), "planet") == 0);
    assert(strcmp(WorldDimensionName(WORLD_DIMENSION_SPACE), "space") == 0);
    assert(strcmp(WorldDimensionName(WORLD_DIMENSION_NETHER), "nether") == 0);
    assert(WorldBlockRegionAt(SURFACE_MIN_Y - 1) == WORLD_BLOCK_REGION_NONE);
    assert(WorldBlockRegionAt(SURFACE_MIN_Y) == WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(-1) == WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(SPACE_LAYER_Y) == WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(SURFACE_MAX_Y_EXCLUSIVE - 1) ==
           WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(SURFACE_MAX_Y_EXCLUSIVE) ==
           WORLD_BLOCK_REGION_NONE);
    WorldSetNetherActive(true);
    assert(WorldBlockRegionAt(NETHER_LAYER_Y) == WORLD_BLOCK_REGION_NETHER);
    assert(WorldBlockRegionAt(NETHER_LAYER_TOP - 1) == WORLD_BLOCK_REGION_NETHER);
    assert(WorldBlockRegionAt(NETHER_LAYER_TOP) == WORLD_BLOCK_REGION_NONE);
    assert(WorldBlockRegionAt(-1) == WORLD_BLOCK_REGION_NONE);
    assert(WorldBlockRegionAt(0) == WORLD_BLOCK_REGION_NONE);
    WorldSetNetherActive(false);
    assert(WorldBlockRegionAt(0) == WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(WORLD_HEIGHT - 1) == WORLD_BLOCK_REGION_SURFACE);
    assert(WorldBlockRegionAt(WORLD_HEIGHT) == WORLD_BLOCK_REGION_SURFACE);

    SetEnvironment(WORLD_SURFACE_NONE, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    assert(WorldBlockRegionAt(SPACE_LAYER_Y) == WORLD_BLOCK_REGION_SPACE);
    assert(WorldBlockRegionAt(SPACE_LAYER_TOP - 1) == WORLD_BLOCK_REGION_SPACE);
    assert(WorldBlockRegionAt(SPACE_LAYER_TOP) == WORLD_BLOCK_REGION_NONE);
}

static void TestHomeAndNether(void)
{
    SetEnvironment(WORLD_SURFACE_HOME, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    assert(context.active && context.kind == WORLD_SURFACE_HOME);
    assert(context.bodyId == 0u && context.seed == DEFAULT_WORLD_SEED);
    assert(context.originX == 0 && context.originZ == 0);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_HOME);
    assert(WorldCurrentDimensionAt(-40.0f) == WORLD_DIMENSION_HOME);
    assert(WorldIsSurfaceActive());
    assert(!WorldIsSpaceActive());
    assert(WorldCanAccessBlockY(4));
    assert(WorldCanAccessBlockY(-40));
    WorldSetNetherActive(true);
    assert(WorldNetherIsActive());
    context = WorldCurrentSurfaceContext();
    assert(context.active && context.kind == WORLD_SURFACE_HOME);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_NETHER);
    assert(WorldCurrentDimensionAt(80.0f) == WORLD_DIMENSION_NETHER);
    assert(WorldCanAccessBlockY(-40));
    assert(!WorldCanAccessBlockY(4));
    WorldSetNetherActive(false);
    assert(WorldGravityScale() == 1.0f);
    assert(WorldCurrentSurfaceId() == 0u);
    assert(WorldSurfaceHeightAt(3, 4) == 7 + (int)terrainMode);
    SurfaceContractSample sample;
    assert(WorldSurfaceSampleAt(3, 4, &sample));
    assert((int)lroundf(sample.elevation) == WorldSurfaceHeightAt(3, 4));
    assert(sample.slope == 2.5f);
    assert(sample.bathymetryZone == BATHYMETRY_ZONE_COAST);
    assert(sample.bathymetryMaterial == BATHYMETRY_MATERIAL_SEDIMENT);
    assert(!sample.hasPlanetBiome);
    float waterSurface = 0.0f;
    assert(WorldProceduralWaterSurfaceAt(3, 4, &waterSurface));
    assert(waterSurface == (float)HOME_SEA_LEVEL + 1.0f);
    assert(!WorldIsProceduralOceanWaterAt(3, 20, 4));
    assert(WorldIsProceduralOceanWaterAt(3, 21, 4));
    assert(WorldIsProceduralOceanWaterAt(3, HOME_SEA_LEVEL, 4));
    assert(!WorldIsProceduralOceanWaterAt(3, HOME_SEA_LEVEL + 1, 4));

    WorldSetTerrainMode(TERRAIN_FLAT);
    assert(!WorldIsProceduralOceanWaterAt(3, 21, 4));
    WorldSetTerrainMode(TERRAIN_VARIED);
    WorldSetNetherActive(true);
    assert(!WorldIsProceduralOceanWaterAt(3, 21, 4));
    WorldSetNetherActive(false);
}

static void TestPlanet(void)
{
    SetEnvironment(WORLD_SURFACE_PLANET, 1.35f, 0x91u, 0x1234u,
                   1200, -340);
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    assert(context.active && context.kind == WORLD_SURFACE_PLANET);
    assert(context.bodyId == 0x91u && context.seed == 0x1234u);
    assert(context.bodyId != context.seed);
    assert(context.originX == 1200 && context.originZ == -340);
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    assert(snapshot->context.bodyId == 0x91u);
    assert(snapshot->planetProfile.seed == 0x1234u);
    assert(snapshot->planetProfile.style == SOLAR_STYLE_TEMPERATE);
    assert(fabsf(snapshot->planetProfile.surfaceGravity - 1.35f) < 0.001f);
    assert(WorldSurfaceMinY() == SURFACE_GENERATION_MIN_Y);
    assert(WorldSurfaceMaxYExclusive() ==
           SURFACE_GENERATION_MAX_Y_EXCLUSIVE);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_PLANET);
    assert(WorldCurrentDimensionAt(-40.0f) == WORLD_DIMENSION_PLANET);
    assert(WorldIsSurfaceDimension(WorldCurrentDimension()));
    assert(WorldGravityScale() == 1.35f);
    assert(WorldCurrentSurfaceId() == 0x91u);
    assert(WorldSurfaceMapOriginX() == 1200);
    assert(WorldSurfaceMapOriginZ() == -340);
    assert(WorldSurfaceHeightAt(7, 2) == 105);
    SurfaceContractSample sample;
    assert(WorldSurfaceSampleAt(7, 2, &sample));
    assert(sample.hasPlanetBiome &&
           sample.planetBiome == PLANET_BIOME_BADLANDS);
    PlanetBiome planetBiome = PLANET_BIOME_BASALT_PLAINS;
    assert(WorldSurfacePlanetBiomeAt(7, 2, &planetBiome));
    assert(planetBiome == PLANET_BIOME_BADLANDS);
    BathymetrySample bathymetry;
    assert(WorldSurfaceBathymetryAt(7, 2, &bathymetry));
    assert(bathymetry.waterDepth == 50);
    assert(bathymetry.zone == BATHYMETRY_ZONE_SHELF);
    assert(!WorldCanAccessBlockY(-40));
    assert(WorldCanAccessBlockY(SURFACE_GENERATION_MIN_Y));
    assert(WorldCanAccessBlockY(SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 1));
    assert(!WorldCanAccessBlockY(SURFACE_GENERATION_MAX_Y_EXCLUSIVE));
    float waterSurface = 0.0f;
    assert(WorldProceduralWaterSurfaceAt(7, 2, &waterSurface));
    assert(waterSurface == 81.0f);
    assert(!WorldIsProceduralOceanWaterAt(7, 30, 2));
    assert(WorldIsProceduralOceanWaterAt(7, 31, 2));
    assert(WorldIsProceduralOceanWaterAt(7, 80, 2));
    assert(!WorldIsProceduralOceanWaterAt(7, 81, 2));
}

static void TestSpace(void)
{
    SetEnvironment(WORLD_SURFACE_NONE, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    assert(!context.active && context.kind == WORLD_SURFACE_NONE);
    assert(context.bodyId == 0u && context.seed == DEFAULT_WORLD_SEED);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_SPACE);
    assert(WorldIsSpaceActive());
    assert(!WorldIsSurfaceActive());
    assert(!WorldCanAccessBlockY(4));
    assert(!WorldCanAccessBlockY(-40));
    assert(WorldCanAccessBlockY(SPACE_LAYER_Y));
    assert(!WorldProceduralWaterSurfaceAt(0, 0, NULL));
    assert(!WorldIsProceduralOceanWaterAt(0, 40, 0));
    assert(WorldGravityScale() == 0.0f);
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.001f;
}

static void TestSurfacePoseRebaseEvents(void)
{
    SetEnvironment(WORLD_SURFACE_HOME, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    WorldResetSurfaceRebaseEvent();
    WorldSurfaceRebaseEvent event = WorldLastSurfaceRebaseEvent();
    assert(!event.valid && event.sequence == 0u);

    Vector3 position = { 8193.25f, 75.0f, 12.0f };
    Vector3 velocity = { 2.0f, -3.0f, 4.0f };
    float yaw = 0.4f;
    assert(WorldCanonicalizeSurfacePose(&position, &velocity, &yaw));
    assert(Near(position.x, -8190.75f));
    assert(Near(position.z, 12.0f));
    assert(Near(velocity.z, 4.0f));
    assert(Near(yaw, 0.4f));
    event = WorldLastSurfaceRebaseEvent();
    assert(event.valid && event.sequence == 1u && event.bodyId == 0u);
    assert(Near(event.previous.x, 8193.25f));
    assert(Near(event.canonical.x, -8190.75f));
    assert(event.northDirection == 1.0f);

    position = (Vector3){ 0.0f, 80.0f, 4097.25f };
    velocity = (Vector3){ 1.0f, 2.0f, 3.0f };
    yaw = 0.4f;
    assert(WorldCanonicalizeSurfacePose(&position, &velocity, &yaw));
    assert(Near(position.x, -8192.0f));
    assert(Near(position.z, 4094.75f));
    assert(Near(velocity.z, -3.0f));
    assert(Near(yaw, atan2f(sinf(0.4f), -cosf(0.4f))));
    event = WorldLastSurfaceRebaseEvent();
    assert(event.valid && event.sequence == 2u);
    assert(event.northDirection == -1.0f);

    WorldResetSurfaceRebaseEvent();
    assert(!WorldLastSurfaceRebaseEvent().valid);
    SetEnvironment(WORLD_SURFACE_NONE, 1.0f, 0u, DEFAULT_WORLD_SEED, 0, 0);
    assert(!WorldCanonicalizeSurfacePose(&position, &velocity, &yaw));
    assert(!WorldLastSurfaceRebaseEvent().valid);
}

/* Cube terrain stubs. This fixture replaces the terrain layer wholesale, so the
 * height routing is what is under test here, not cube sampling itself. */
TerrainCubeContext *TerrainCubeContextCreate(const WorldSurfaceSnapshot *s,
                                             TerrainMode mode, uint64_t generation)
{
    (void)s; (void)mode; (void)generation;
    return (TerrainCubeContext *)&cubeSampleHeight;
}

void TerrainCubeContextRelease(TerrainCubeContext *context)
{
    (void)context;
}

bool TerrainCubeSampleColumn(const TerrainCubeContext *context,
                             SurfaceAddress address, TerrainCubeColumn *out)
{
    (void)context; (void)address;
    if (!cubeSampleOk || !out) return false;
    *out = (TerrainCubeColumn){0};
    out->terrain.elevation = (float)cubeSampleHeight;
    return true;
}

/* On a cube surface the height must come from cube sampling, not the legacy
 * analytic terrain function, otherwise every terrain query disagrees with the
 * blocks that are actually generated. */
static void TestCubeHeightRoutesToCubeSampling(void)
{
    cubeSampleHeight = 321;
    cubeSampleOk = true;
    /* Installation of a cube snapshot requires the runtime gate. */
    WorldSetCubeRuntimeEnabled(true);
    WorldSurfaceTopologyPolicy cubePolicy = {
        { SURFACE_TOPOLOGY_CUBE_GRID, SURFACE_CUBE_TOPOLOGY_VERSION },
        { SURFACE_TOPOLOGY_CUBE_GRID, SURFACE_CUBE_TOPOLOGY_VERSION }
    };
    assert(WorldInstallTopologyPolicy(cubePolicy));
    WorldSurfaceSnapshot snapshot = {0};
    snapshot.context.active = true;
    snapshot.context.kind = WORLD_SURFACE_HOME;
    snapshot.context.topology = cubePolicy.home;
    snapshot.context.seed = 7u;
    assert(WorldInstallSurfaceSnapshotWithContract(
        &snapshot, NextFakeContract(&snapshot)));

    assert(WorldSurfaceHeightAt(3, 4) == 321);
    SurfaceContractSample sample;
    assert(WorldSurfaceSampleAt(3, 4, &sample));
    assert(sample.slope == 1.25f);
    assert(sample.bathymetryZone == BATHYMETRY_ZONE_SHELF);

    /* Sampling failure must not fabricate a height. */
    cubeSampleOk = false;
    assert(WorldSurfaceHeightAt(3, 4) == 0);
    assert(!WorldSurfaceSampleAt(3, 4, &sample));

    assert(WorldCurrentSurfaceContract() != NULL);
    assert(WorldInstallSurfaceSnapshot(NULL));
    WorldSetCubeRuntimeEnabled(false);
    WorldResetTopologyPolicy();
}

static void TestLandformWaterlineSample(void)
{
    SurfaceTerrainSample terrain = {
        .elevation = 82.25f,
        .seaLevel = 80.0f,
        .slope = 0.75f,
        .biome = BIOME_SWAMP,
        .landform = {
            .hasWater = true,
            .waterLevel = 84.0f
        },
        .bathymetry = {
            .seaLevel = 80,
            .seabedY = 82,
            .waterDepth = 0,
            .zone = BATHYMETRY_ZONE_LAND,
            .material = BATHYMETRY_MATERIAL_ROCK
        }
    };
    SurfaceContractSample sample;
    assert(SurfaceContractSampleFromTerrain(
        &terrain, false, PLANET_BIOME_PLAINS, &sample));
    assert(sample.elevation == terrain.elevation);
    assert(sample.waterSurface == 84.0f);
    assert(sample.waterDepth == 2);
    assert(sample.seabedY == 82);
    assert(sample.bathymetryZone == BATHYMETRY_ZONE_COAST);
    assert(sample.bathymetryMaterial == BATHYMETRY_MATERIAL_SEDIMENT);
}

int main(void)
{
    assert(WorldInstallTopologyPolicy(
        (WorldSurfaceTopologyPolicy)WORLD_SURFACE_TOPOLOGY_LEGACY));
    TestBlockRegions();
    TestHomeAndNether();
    TestPlanet();
    TestSpace();
    TestSurfacePoseRebaseEvents();
    TestCubeHeightRoutesToCubeSampling();
    TestLandformWaterlineSample();
    puts("world_environment tests passed");
    return 0;
}
