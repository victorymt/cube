#include "world/surface_topology_version.h"

bool SurfaceTopologyIsKnown(SurfaceTopologyVersion topology)
{
    if (topology.kind == SURFACE_TOPOLOGY_NONE) return topology.version == 0u;
    return (topology.kind == SURFACE_TOPOLOGY_LEGACY_MAP ||
            topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) && topology.version == 1u;
}

bool SurfaceTopologyIsRuntimeSupported(SurfaceTopologyVersion topology)
{
    return SurfaceTopologyIsKnown(topology) &&
           topology.kind != SURFACE_TOPOLOGY_CUBE_GRID;
}

const char *SurfaceTopologyName(SurfaceTopologyVersion topology)
{
    if (!SurfaceTopologyIsKnown(topology)) return "unknown";
    switch (topology.kind) {
    case SURFACE_TOPOLOGY_NONE: return "none";
    case SURFACE_TOPOLOGY_LEGACY_MAP: return "legacy-map";
    case SURFACE_TOPOLOGY_CUBE_GRID: return "cube-grid";
    }
    return "unknown";
}

bool WorldSurfaceTopologyPolicyIsSupported(WorldSurfaceTopologyPolicy policy)
{
    /* Without the cube runtime gate the policy must be legacy on both
     * surfaces; this keeps the historical contract byte-for-byte. */
    bool legacyOnly = policy.home.kind == SURFACE_TOPOLOGY_LEGACY_MAP &&
                      policy.planets.kind == SURFACE_TOPOLOGY_LEGACY_MAP;
    if (!SurfaceTopologyIsKnown(policy.home) ||
        !SurfaceTopologyIsKnown(policy.planets)) return false;
    if (legacyOnly)
        return SurfaceTopologyIsRuntimeSupported(policy.home) &&
               SurfaceTopologyIsRuntimeSupported(policy.planets);
    /* Non-legacy policies are refused here. The runtime gate that admits the
     * cube grid lives in the snapshot module, which owns installation. */
    return false;
}
