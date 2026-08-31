#include "world/world_surface_snapshot.h"

static WorldSurfaceSnapshot currentSurfaceSnapshot = { 0 };

void WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot)
{
    currentSurfaceSnapshot = snapshot
        ? *snapshot : (WorldSurfaceSnapshot){ 0 };
}

const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void)
{
    return &currentSurfaceSnapshot;
}
