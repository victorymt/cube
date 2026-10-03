#include "world/world_surface_snapshot.h"

static WorldSurfaceSnapshot currentSurfaceSnapshot = { 0 };
static uint64_t currentWorldGeneration;

void WorldInstallSurfaceSnapshot(const WorldSurfaceSnapshot *snapshot)
{
    currentWorldGeneration++;
    if (currentWorldGeneration == 0u) currentWorldGeneration = 1u;
    currentSurfaceSnapshot = snapshot
        ? *snapshot : (WorldSurfaceSnapshot){ 0 };
}

const WorldSurfaceSnapshot *WorldCurrentSurfaceSnapshot(void)
{
    return &currentSurfaceSnapshot;
}

uint64_t WorldCurrentGeneration(void)
{
    return currentWorldGeneration;
}
