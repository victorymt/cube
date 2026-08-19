#include "world/world_environment.h"
#include "world/terrain.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static bool homeSurfaceActive = true;
static bool planetWorldActive = false;
static float planetGravityScale = 1.0f;
static uint32_t planetSeed = 0u;

TerrainMode terrainMode = TERRAIN_VARIED;

TerrainMode WorldTerrainMode(void)
{
    return terrainMode;
}

void WorldSetTerrainMode(TerrainMode mode)
{
    terrainMode = mode;
}

bool HomeWorldSurfaceIsActive(void)
{
    return homeSurfaceActive && !planetWorldActive;
}

bool PlanetWorldIsActive(void)
{
    return planetWorldActive;
}

float PlanetWorldGravityScale(void)
{
    return planetWorldActive ? planetGravityScale : 1.0f;
}

uint32_t PlanetWorldSeed(void)
{
    return planetSeed;
}

int PlanetWorldOriginX(void)
{
    return planetWorldActive ? 1200 : 0;
}

int PlanetWorldOriginZ(void)
{
    return planetWorldActive ? -340 : 0;
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

static void SetEnvironment(bool homeActive, bool planetActive,
                           float gravityScale, uint32_t seed)
{
    homeSurfaceActive = homeActive;
    planetWorldActive = planetActive;
    planetGravityScale = gravityScale;
    planetSeed = seed;
    WorldSetNetherActive(false);
}

static void TestBlockRegions(void)
{
    SetEnvironment(true, false, 1.0f, 0u);
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

    SetEnvironment(false, false, 1.0f, 0u);
    assert(WorldBlockRegionAt(SPACE_LAYER_Y) == WORLD_BLOCK_REGION_SPACE);
    assert(WorldBlockRegionAt(SPACE_LAYER_TOP - 1) == WORLD_BLOCK_REGION_SPACE);
    assert(WorldBlockRegionAt(SPACE_LAYER_TOP) == WORLD_BLOCK_REGION_NONE);
}

static void TestHomeAndNether(void)
{
    SetEnvironment(true, false, 1.0f, 0u);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_HOME);
    assert(WorldCurrentDimensionAt(-40.0f) == WORLD_DIMENSION_HOME);
    assert(WorldIsSurfaceActive());
    assert(!WorldIsSpaceActive());
    assert(WorldCanAccessBlockY(4));
    assert(WorldCanAccessBlockY(-40));
    WorldSetNetherActive(true);
    assert(WorldNetherIsActive());
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
    SetEnvironment(false, true, 1.35f, 0x1234u);
    assert(WorldSurfaceMinY() == SURFACE_GENERATION_MIN_Y);
    assert(WorldSurfaceMaxYExclusive() ==
           SURFACE_GENERATION_MAX_Y_EXCLUSIVE);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_PLANET);
    assert(WorldCurrentDimensionAt(-40.0f) == WORLD_DIMENSION_PLANET);
    assert(WorldIsSurfaceDimension(WorldCurrentDimension()));
    assert(WorldGravityScale() == 1.35f);
    assert(WorldCurrentSurfaceId() == 0x1234u);
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
    SetEnvironment(false, false, 1.0f, 0u);
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
    SetEnvironment(true, false, 1.0f, 0u);
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
    SetEnvironment(false, false, 1.0f, 0u);
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
