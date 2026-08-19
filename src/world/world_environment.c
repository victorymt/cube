#include "world/world_environment.h"

#include "space/space_state.h"
#include "world/terrain.h"
#include "world/world.h"

#include <math.h>

static bool netherActive = false;
static WorldSurfaceRebaseEvent lastSurfaceRebase = { 0 };
static uint64_t surfaceRebaseSequence = 0u;

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
    if (WorldIsSpaceActive()) {
        return y >= SPACE_LAYER_Y && y < SPACE_LAYER_TOP
            ? WORLD_BLOCK_REGION_SPACE : WORLD_BLOCK_REGION_NONE;
    }
    if (WorldNetherIsActive() &&
        y >= NETHER_LAYER_Y && y < NETHER_LAYER_TOP) {
        return WORLD_BLOCK_REGION_NETHER;
    }
    if (!WorldNetherIsActive() &&
        y >= WorldSurfaceMinY() && y < WorldSurfaceMaxYExclusive()) {
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

int WorldSurfaceMinY(void)
{
    return PlanetWorldIsActive() ? SURFACE_GENERATION_MIN_Y : SURFACE_MIN_Y;
}

int WorldSurfaceMaxYExclusive(void)
{
    return PlanetWorldIsActive() ? SURFACE_GENERATION_MAX_Y_EXCLUSIVE
                                 : SURFACE_MAX_Y_EXCLUSIVE;
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

static bool WorldProceduralOceanBathymetryAt(int x, int z,
                                              BathymetrySample *outSample)
{
    if (!outSample || !WorldIsSurfaceActive() || WorldNetherIsActive()) {
        return false;
    }
    BathymetrySample sample = PlanetWorldIsActive()
        ? PlanetBathymetryAt(x, z)
        : TerrainBathymetryAt(x, z, WorldTerrainMode());
    if (sample.waterDepth <= 0 || sample.seaLevel < 0 ||
        sample.seabedY >= sample.seaLevel) {
        return false;
    }
    *outSample = sample;
    return true;
}

bool WorldProceduralWaterSurfaceAt(int x, int z, float *outSurfaceY)
{
    BathymetrySample sample = { 0 };
    if (!outSurfaceY ||
        !WorldProceduralOceanBathymetryAt(x, z, &sample)) return false;
    *outSurfaceY = (float)sample.seaLevel + 1.0f;
    return true;
}

bool WorldIsProceduralOceanWaterAt(int x, int y, int z)
{
    BathymetrySample sample = { 0 };
    if (!WorldProceduralOceanBathymetryAt(x, z, &sample)) return false;
    return y > sample.seabedY && y <= sample.seaLevel;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : 0u;
}

int WorldSurfaceMapOriginX(void)
{
    return 0;
}

int WorldSurfaceMapOriginZ(void)
{
    return 0;
}

void WorldResetSurfaceRebaseEvent(void)
{
    lastSurfaceRebase = (WorldSurfaceRebaseEvent){ 0 };
    surfaceRebaseSequence = 0u;
}

WorldSurfaceRebaseEvent WorldLastSurfaceRebaseEvent(void)
{
    return lastSurfaceRebase;
}

bool WorldCanonicalizeSurfacePose(Vector3 *position, Vector3 *velocity,
                                  float *yaw)
{
    if (!position || !WorldIsSurfaceActive() ||
        !isfinite(position->x) || !isfinite(position->z)) {
        return false;
    }
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    float northDirection = 1.0f;
    Vector2 canonical = SurfaceCanonicalMapPosition(
        (float)originX + position->x,
        (float)originZ + position->z, &northDirection);
    Vector3 next = *position;
    next.x = canonical.x - (float)originX;
    next.z = canonical.y - (float)originZ;
    bool changed = fabsf(next.x - position->x) > 0.001f ||
                   fabsf(next.z - position->z) > 0.001f;
    if (changed) {
        lastSurfaceRebase = (WorldSurfaceRebaseEvent){
            .valid = true,
            .sequence = ++surfaceRebaseSequence,
            .bodyId = WorldCurrentSurfaceId(),
            .previous = { (float)originX + position->x,
                          (float)originZ + position->z },
            .canonical = canonical,
            .northDirection = northDirection
        };
    }
    *position = next;
    if (northDirection < 0.0f) {
        if (velocity) velocity->z = -velocity->z;
        if (yaw && isfinite(*yaw)) {
            *yaw = atan2f(sinf(*yaw), -cosf(*yaw));
        }
    }
    return changed;
}
