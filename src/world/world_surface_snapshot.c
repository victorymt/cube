#include "world/world_surface_snapshot.h"
#include "world/surface_contract_internal.h"
#include "world/terrain_generation.h"

#include <stddef.h>

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
static SurfaceContract *currentSurfaceContract;
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

static bool SurfaceSnapshotIsValid(const WorldSurfaceSnapshot *snapshot)
{
    if (!snapshot) return true;
    const WorldSurfaceContext *context = &snapshot->context;
    if (context->kind < WORLD_SURFACE_NONE ||
        context->kind > WORLD_SURFACE_PLANET ||
        context->active != (context->kind != WORLD_SURFACE_NONE) ||
        !SurfaceTopologyIsRuntimeSupported(context->topology) ||
        (context->active && context->topology.kind == SURFACE_TOPOLOGY_NONE) ||
        (!context->active &&
         context->topology.kind != SURFACE_TOPOLOGY_NONE)) return false;
    if (context->active && context->topology.kind ==
            SURFACE_TOPOLOGY_CUBE_GRID && !cubeRuntimeEnabled) return false;
    SurfaceTopologyVersion expected = WorldTopologyForSurface(context->kind);
    return context->topology.kind == expected.kind &&
           context->topology.version == expected.version;
}

bool WorldInstallSurfaceSnapshotWithContract(
    const WorldSurfaceSnapshot *snapshot, SurfaceContract *contract)
{
    if (!SurfaceSnapshotIsValid(snapshot)) return false;
    if (contract) {
        if (!snapshot || currentSurfaceContract == contract ||
            !SurfaceContractIsValid(contract) ||
            !SurfaceContractMatchesTopology(
                contract, snapshot->context.topology,
                snapshot->context.bodyId)) return false;
    }
    SurfaceContract *previousContract = currentSurfaceContract;
    currentSurfaceSnapshot = snapshot
        ? *snapshot : (WorldSurfaceSnapshot){ 0 };
    currentSurfaceContract = contract;
    currentWorldGeneration++;
    if (currentWorldGeneration == 0u) currentWorldGeneration = 1u;
    SurfaceContractRelease(previousContract);
    return true;
}

bool WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot)
{
    return WorldInstallSurfaceSnapshotWithContract(snapshot, NULL);
}

uint64_t WorldNextSurfaceGeneration(void)
{
    uint64_t next = currentWorldGeneration + 1u;
    return next == 0u ? 1u : next;
}

WorldSurfaceTopologyPolicy WorldCurrentTopologyPolicy(void)
{
    return currentTopologyPolicy;
}

bool WorldInstallTopologyPolicy(WorldSurfaceTopologyPolicy policy)
{
    if (!WorldSurfaceTopologyPolicyIsSupported(policy)) return false;
    currentTopologyPolicy = policy;
    return true;
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

const SurfaceContract *WorldCurrentSurfaceContract(void)
{
    return currentSurfaceContract;
}

uint64_t WorldCurrentGeneration(void)
{
    return currentWorldGeneration;
}
