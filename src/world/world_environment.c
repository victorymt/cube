#include "world/world_environment.h"

#include "world/terrain.h"
#include "world/world.h"
#include "world/world_surface_snapshot.h"

#include <math.h>

static bool netherActive = false;
static WorldSurfaceRebaseEvent lastSurfaceRebase = { 0 };
static uint64_t surfaceRebaseSequence = 0u;

WorldSurfaceContext WorldCurrentSurfaceContext(void)
{
    return WorldCurrentSurfaceSnapshot()->context;
}

bool WorldNetherIsActive(void)
{
    return netherActive &&
           WorldCurrentSurfaceContext().kind == WORLD_SURFACE_HOME;
}

void WorldSetNetherActive(bool active)
{
    netherActive = active;
}

WorldDimension WorldCurrentDimensionAt(float y)
{
    (void)y;
    WorldSurfaceKind surface = WorldCurrentSurfaceContext().kind;
    if (surface == WORLD_SURFACE_PLANET) return WORLD_DIMENSION_PLANET;
    if (surface != WORLD_SURFACE_HOME) return WORLD_DIMENSION_SPACE;
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
    return WorldCurrentSurfaceContext().active;
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
    return WorldCurrentSurfaceContext().kind == WORLD_SURFACE_PLANET
        ? SURFACE_GENERATION_MIN_Y : SURFACE_MIN_Y;
}

int WorldSurfaceMaxYExclusive(void)
{
    return WorldCurrentSurfaceContext().kind == WORLD_SURFACE_PLANET
        ? SURFACE_GENERATION_MAX_Y_EXCLUSIVE : SURFACE_MAX_Y_EXCLUSIVE;
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
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    if (snapshot->context.kind == WORLD_SURFACE_PLANET) {
        return snapshot->planetProfile.surfaceGravity;
    }
    return snapshot->context.kind == WORLD_SURFACE_HOME ? 1.0f : 0.0f;
}

int WorldSurfaceHeightAt(int x, int z)
{
    return WorldCurrentSurfaceContext().kind == WORLD_SURFACE_PLANET
        ? PlanetTerrainHeight(x, z)
        : TerrainHeight(x, z, WorldTerrainMode());
}

static bool WorldProceduralOceanBathymetryAt(int x, int z,
                                              BathymetrySample *outSample)
{
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!outSample || !context.active || WorldNetherIsActive()) {
        return false;
    }
    BathymetrySample sample = context.kind == WORLD_SURFACE_PLANET
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
    return WorldCurrentSurfaceContext().bodyId;
}

int WorldSurfaceMapOriginX(void)
{
    return WorldCurrentSurfaceContext().originX;
}

int WorldSurfaceMapOriginZ(void)
{
    return WorldCurrentSurfaceContext().originZ;
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
    WorldSurfaceContext context = WorldCurrentSurfaceContext();
    if (!position || !context.active ||
        !isfinite(position->x) || !isfinite(position->z)) {
        return false;
    }
    int originX = context.originX;
    int originZ = context.originZ;
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
            .bodyId = context.bodyId,
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
