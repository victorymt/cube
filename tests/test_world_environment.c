#include "world/world_environment.h"
#include "world/terrain.h"
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
    assert(WorldInstallSurfaceSnapshot(&snapshot));
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

int main(void)
{
    TestBlockRegions();
    TestHomeAndNether();
    TestPlanet();
    TestSpace();
    TestSurfacePoseRebaseEvents();
    puts("world_environment tests passed");
    return 0;
}
