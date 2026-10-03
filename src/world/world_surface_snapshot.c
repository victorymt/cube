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
