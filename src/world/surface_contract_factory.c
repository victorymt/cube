#include "world/surface_contract_factory.h"

#include "world/surface_cube_contract.h"
#include "world/surface_legacy_contract.h"
#include <stddef.h>

SurfaceContract *SurfaceContractCreate(const WorldSurfaceSnapshot *snapshot,
                                       TerrainMode mode, uint64_t generation)
{
    if (!snapshot || !snapshot->context.active ||
        !SurfaceTopologyIsRuntimeSupported(snapshot->context.topology)) {
        return NULL;
    }
    switch (snapshot->context.topology.kind) {
    case SURFACE_TOPOLOGY_LEGACY_MAP:
        return SurfaceLegacyContractCreate(snapshot, mode, generation);
    case SURFACE_TOPOLOGY_CUBE_GRID:
        return SurfaceCubeContractCreate(snapshot, mode, generation);
    case SURFACE_TOPOLOGY_NONE:
    default:
        return NULL;
    }
}
