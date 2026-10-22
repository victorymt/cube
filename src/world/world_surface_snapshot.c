#include "world/world_surface_snapshot.h"
#include "world/terrain_generation.h"

static uint32_t terrainGenerationVersion = TERRAIN_GENERATION_CURRENT;

uint32_t TerrainGenerationVersion(void)
{
    return terrainGenerationVersion;
}

bool TerrainGenerationSetVersion(uint32_t version)
{
    if (version < TERRAIN_GENERATION_MIN || version > TERRAIN_GENERATION_CURRENT) {
        return false;
    }
    terrainGenerationVersion = version;
    return true;
}

static WorldSurfaceSnapshot currentSurfaceSnapshot = { 0 };
static uint64_t currentWorldGeneration;
static WorldSurfaceTopologyPolicy currentTopologyPolicy = WORLD_SURFACE_TOPOLOGY_DEFAULT;
/* Off by default; see the header contract. */
static bool cubeRuntimeEnabled;

bool WorldCubeRuntimeEnabled(void)
{
    return cubeRuntimeEnabled;
}

void WorldSetCubeRuntimeEnabled(bool enabled)
{
    cubeRuntimeEnabled = enabled;
}

bool WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot)
{
    if (snapshot) {
        const WorldSurfaceContext *context = &snapshot->context;
        if (context->kind < WORLD_SURFACE_NONE || context->kind > WORLD_SURFACE_PLANET ||
            context->active != (context->kind != WORLD_SURFACE_NONE) ||
            !(SurfaceTopologyIsRuntimeSupported(context->topology) ||
              (cubeRuntimeEnabled &&
               SurfaceTopologyIsKnown(context->topology))) ||
            (context->active && context->topology.kind == SURFACE_TOPOLOGY_NONE) ||
            (!context->active && context->topology.kind != SURFACE_TOPOLOGY_NONE)) return false;
        SurfaceTopologyVersion expected = WorldTopologyForSurface(context->kind);
        if (context->topology.kind != expected.kind ||
            context->topology.version != expected.version) return false;
    }
    currentWorldGeneration++;
    if (currentWorldGeneration == 0u) currentWorldGeneration = 1u;
    currentSurfaceSnapshot = snapshot
        ? *snapshot : (WorldSurfaceSnapshot){ 0 };
    return true;
}

WorldSurfaceTopologyPolicy WorldCurrentTopologyPolicy(void)
{
    return currentTopologyPolicy;
}

bool WorldInstallTopologyPolicy(WorldSurfaceTopologyPolicy policy)
{
    if (WorldSurfaceTopologyPolicyIsSupported(policy)) {
        currentTopologyPolicy = policy;
        return true;
    }
    /* The pure policy check only accepts the legacy grid. An explicitly
     * enabled cube runtime additionally admits a fully-known cube policy. */
    if (cubeRuntimeEnabled &&
        SurfaceTopologyIsKnown(policy.home) &&
        SurfaceTopologyIsKnown(policy.planets) &&
        policy.home.kind == SURFACE_TOPOLOGY_CUBE_GRID &&
        policy.planets.kind == SURFACE_TOPOLOGY_CUBE_GRID) {
        currentTopologyPolicy = policy;
        return true;
    }
    return false;
}

void WorldResetTopologyPolicy(void)
{
    currentTopologyPolicy = (WorldSurfaceTopologyPolicy)WORLD_SURFACE_TOPOLOGY_DEFAULT;
}

SurfaceTopologyVersion WorldTopologyForSurface(WorldSurfaceKind kind)
{
    if (kind == WORLD_SURFACE_HOME) return currentTopologyPolicy.home;
    if (kind == WORLD_SURFACE_PLANET) return currentTopologyPolicy.planets;
    return (SurfaceTopologyVersion){ SURFACE_TOPOLOGY_NONE, 0u };
}

const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void)
{
    return &currentSurfaceSnapshot;
}

uint64_t WorldCurrentGeneration(void)
{
    return currentWorldGeneration;
}
