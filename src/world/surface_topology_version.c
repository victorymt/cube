#include "world/surface_topology_version.h"

bool SurfaceTopologyIsKnown(SurfaceTopologyVersion topology)
{
    if (topology.kind == SURFACE_TOPOLOGY_NONE) return topology.version == 0u;
    return (topology.kind == SURFACE_TOPOLOGY_LEGACY_MAP ||
            topology.kind == SURFACE_TOPOLOGY_CUBE_GRID) && topology.version == 1u;
}

bool SurfaceTopologyIsRuntimeSupported(SurfaceTopologyVersion topology)
{
    /* Both adapters exist. The cube runtime gate is an installation/save-load
     * policy, not a statement that the backend is absent. */
    return SurfaceTopologyIsKnown(topology);
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
    /* Homeworld no longer supports the legacy map backend. Planets still do
     * while their migration is pending. */
    return policy.home.kind == SURFACE_TOPOLOGY_CUBE_GRID &&
           policy.home.version == 1u &&
           SurfaceTopologyIsKnown(policy.planets) &&
           SurfaceTopologyIsRuntimeSupported(policy.planets) &&
           policy.planets.kind != SURFACE_TOPOLOGY_NONE;
}
