#include "app/game_biology.h"

#include "ecology/ecology.h"
#include "ecology/evolution_catalog.h"
#include "world/ecology_hooks.h"

static bool GameWorldFloraCapacityAt(
    int x, int z, float *outCapacity, void *userData)
{
    (void)userData;
    if (!outCapacity) return false;
    *outCapacity = PlanetEcologyStaticSuitabilityAt(x, z).floraCapacity;
    return true;
}

static bool GameWorldFloraVisualAt(
    int x, int z, float daylight, WorldFloraVisualSample *outSample,
    void *userData)
{
    (void)userData;
    if (!outSample) return false;
    PlanetLocalEcology local = PlanetEcologyLocalAt(x, z, daylight);
    PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
        local.suitability.floraActivity,
        local.suitability.floraCapacity);
    *outSample = (WorldFloraVisualSample){
        .activity = local.suitability.floraActivity,
        .capacity = local.suitability.floraCapacity,
        .growthScale = runtime.growthScale,
        .visualPresence = runtime.visualPresence
    };
    return true;
}

static void GameWorldDecorateChunkWithEcology(
    Chunk *chunk, int chunkX, int chunkZ, void *userData)
{
    (void)userData;
    PlanetEcologyApplyToChunk(chunk, chunkX, chunkZ);
}

void GameInstallWorldEcologyHooks(void)
{
    const WorldEcologyHooks hooks = {
        .floraCapacityAt = GameWorldFloraCapacityAt,
        .floraVisualAt = GameWorldFloraVisualAt,
        .decorateChunk = GameWorldDecorateChunkWithEcology
    };
    WorldInstallEcologyHooks(&hooks);
}

int BiologyAtlasNextSlot(int current, int direction)
{
    int start = current < 0 ? (direction > 0 ? -1 : EVOLUTION_CATALOG_MAX_SPECIES) : current;
    for (int step = 0; step < EVOLUTION_CATALOG_MAX_SPECIES; step++) {
        start += direction;
        if (start < 0) start = EVOLUTION_CATALOG_MAX_SPECIES - 1;
        if (start >= EVOLUTION_CATALOG_MAX_SPECIES) start = 0;
        EvolutionCatalogSpecies species = { 0 };
        if (EvolutionCatalogGetSpecies(start, &species)) return start;
    }
    return -1;
}
