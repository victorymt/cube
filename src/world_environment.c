#include "world_environment.h"

#include "space.h"
#include "terrain.h"
#include "world.h"

static bool netherActive = false;

bool WorldNetherIsActive(void)
{
    return netherActive && HomeWorldSurfaceIsActive() &&
           !PlanetWorldIsActive();
}

void WorldSetNetherActive(bool active)
{
    netherActive = active;
}

WorldDimension WorldCurrentDimensionAt(float y)
{
    (void)y;
    if (PlanetWorldIsActive()) return WORLD_DIMENSION_PLANET;
    if (!HomeWorldSurfaceIsActive()) return WORLD_DIMENSION_SPACE;
    if (WorldNetherIsActive()) return WORLD_DIMENSION_NETHER;
    return WORLD_DIMENSION_HOME;
}

WorldDimension WorldCurrentDimension(void)
{
    return WorldCurrentDimensionAt(0.0f);
}

const char *WorldDimensionName(WorldDimension dimension)
{
    switch (dimension) {
    case WORLD_DIMENSION_PLANET: return "planet";
    case WORLD_DIMENSION_SPACE: return "space";
    case WORLD_DIMENSION_NETHER: return "nether";
    case WORLD_DIMENSION_HOME:
    default: return "home";
    }
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    if (y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP) return WORLD_BLOCK_REGION_SPACE;
    if (WorldNetherIsActive() &&
        y >= NETHER_LAYER_Y && y < NETHER_LAYER_TOP) {
        return WORLD_BLOCK_REGION_NETHER;
    }
    if (!WorldNetherIsActive() && y >= 0 && y < WORLD_HEIGHT) {
        return WORLD_BLOCK_REGION_SURFACE;
    }
    return WORLD_BLOCK_REGION_NONE;
}

bool WorldIsSurfaceActive(void)
{
    return HomeWorldSurfaceIsActive() || PlanetWorldIsActive();
}

bool WorldIsSpaceActive(void)
{
    return !WorldIsSurfaceActive();
}

bool WorldIsSurfaceDimension(WorldDimension dimension)
{
    return dimension == WORLD_DIMENSION_HOME || dimension == WORLD_DIMENSION_PLANET;
}

bool WorldCanAccessBlockY(int y)
{
    WorldBlockRegion region = WorldBlockRegionAt(y);
    if (region == WORLD_BLOCK_REGION_SPACE) return true;
    if (region == WORLD_BLOCK_REGION_NETHER) return WorldNetherIsActive();
    return region != WORLD_BLOCK_REGION_NONE && WorldIsSurfaceActive();
}

float WorldGravityScale(void)
{
    if (PlanetWorldIsActive()) return PlanetWorldGravityScale();
    if (WorldIsSpaceActive()) return 0.0f;
    return 1.0f;
}

int WorldSurfaceHeightAt(int x, int z)
{
    return PlanetWorldIsActive() ? PlanetTerrainHeight(x, z)
                                 : TerrainHeight(x, z, WorldTerrainMode());
}

uint32_t WorldCurrentSurfaceId(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : 0u;
}
