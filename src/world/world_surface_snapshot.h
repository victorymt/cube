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
void WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot);
const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void);

#endif
