#include "world_environment.h"

#include <assert.h>
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

int TerrainHeight(int x, int z, TerrainMode mode)
{
    return x + z + (int)mode;
}

int PlanetTerrainHeight(int x, int z)
{
    return 100 + x - z;
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
}

static void TestPlanet(void)
{
    SetEnvironment(false, true, 1.35f, 0x1234u);
    assert(WorldCurrentDimension() == WORLD_DIMENSION_PLANET);
    assert(WorldCurrentDimensionAt(-40.0f) == WORLD_DIMENSION_PLANET);
    assert(WorldIsSurfaceDimension(WorldCurrentDimension()));
    assert(WorldGravityScale() == 1.35f);
    assert(WorldCurrentSurfaceId() == 0x1234u);
    assert(WorldSurfaceHeightAt(7, 2) == 105);
    assert(WorldCanAccessBlockY(-40));
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
    assert(WorldGravityScale() == 0.0f);
}

int main(void)
{
    TestBlockRegions();
    TestHomeAndNether();
    TestPlanet();
    TestSpace();
    puts("world_environment tests passed");
    return 0;
}
