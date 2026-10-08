#ifndef VOXELCRAFT_WORLD_SURFACE_SNAPSHOT_H
#define VOXELCRAFT_WORLD_SURFACE_SNAPSHOT_H

#include "space/planet_profile.h"
#include "world/world_environment.h"

typedef struct WorldSurfaceSnapshot {
    WorldSurfaceContext context;
    PlanetProfile planetProfile;
} WorldSurfaceSnapshot;

// Surface transitions install an immutable-by-convention value snapshot after
// chunk generation has drained. Passing NULL clears it to the neutral value.
// Invalid/unsupported topology leaves both the snapshot and generation intact.
bool WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot);
const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void);
uint64_t WorldCurrentGeneration(void);

// Main-thread policy, installed only after draining chunk workers. Every planet
// inherits the same policy; there are no per-body overrides in this stage.
WorldSurfaceTopologyPolicy WorldCurrentTopologyPolicy(void);
bool WorldInstallTopologyPolicy(WorldSurfaceTopologyPolicy policy);
void WorldResetTopologyPolicy(void);
SurfaceTopologyVersion WorldTopologyForSurface(WorldSurfaceKind kind);

#endif
