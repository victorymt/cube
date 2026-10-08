#ifndef VOXELCRAFT_SURFACE_TOPOLOGY_VERSION_H
#define VOXELCRAFT_SURFACE_TOPOLOGY_VERSION_H

#include <stdbool.h>
#include <stdint.h>

// Independent of terrain generation and save-format versions. A cube address
// in a legacy save is not evidence that its voxels use the cube grid.
typedef enum SurfaceTopologyKind {
    SURFACE_TOPOLOGY_NONE = 0,
    SURFACE_TOPOLOGY_LEGACY_MAP = 1,
    SURFACE_TOPOLOGY_CUBE_GRID = 2
} SurfaceTopologyKind;

typedef struct SurfaceTopologyVersion {
    SurfaceTopologyKind kind;
    uint32_t version;
} SurfaceTopologyVersion;

typedef struct WorldSurfaceTopologyPolicy {
    SurfaceTopologyVersion home;
    SurfaceTopologyVersion planets;
} WorldSurfaceTopologyPolicy;

#define SURFACE_TOPOLOGY_MAP_V1 { SURFACE_TOPOLOGY_LEGACY_MAP, 1u }
#define WORLD_SURFACE_TOPOLOGY_DEFAULT \
    { SURFACE_TOPOLOGY_MAP_V1, SURFACE_TOPOLOGY_MAP_V1 }

bool SurfaceTopologyIsKnown(SurfaceTopologyVersion topology);
bool SurfaceTopologyIsRuntimeSupported(SurfaceTopologyVersion topology);
const char *SurfaceTopologyName(SurfaceTopologyVersion topology);
bool WorldSurfaceTopologyPolicyIsSupported(WorldSurfaceTopologyPolicy policy);

#endif
