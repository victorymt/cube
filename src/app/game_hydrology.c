#include "app/game_hydrology.h"

#include "app/game_debug_trace.h"
#include "app/game_runtime.h"
#include "core/simulation_time.h"
#include "world/hydrology_runtime.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>

#define GAME_HYDROLOGY_CELL_COUNT \
    (HYDROLOGY_RUNTIME_GRID_SIZE * HYDROLOGY_RUNTIME_GRID_SIZE)
#define GAME_HYDROLOGY_WEATHER_SAMPLE_BUDGET 32u
#define GAME_HYDROLOGY_TERRAIN_SAMPLE_BUDGET 32u
#define GAME_HYDROLOGY_WEATHER_REFRESH_SECONDS 0.5

typedef struct GameHydrologyInputCache {
    bool valid;
    uint32_t surfaceId;
    uint32_t seed;
    TerrainMode terrainMode;
    int regionX;
    int regionZ;
    SurfaceMapCell cells[GAME_HYDROLOGY_CELL_COUNT];
    uint64_t runtimeGeneration;
    uint64_t terrainRevision;
    uint64_t terrainPassRevision;
    unsigned terrainCursor;
    unsigned terrainRemaining;
    unsigned weatherCursor;
    unsigned weatherRemaining;
    uint64_t weatherRevision;
    double weatherPassStarted;
    double lastSimulationTime;
    bool weatherSeeded;
    bool weatherUniform;
    bool weatherInputsValid;
    GameHydrologyInputStats stats;
} GameHydrologyInputCache;

static GameHydrologyInputCache hydrologyInputCache;

static void GameHydrologyInvalidateInputCache(void)
{
    memset(&hydrologyInputCache, 0, sizeof(hydrologyInputCache));
}

static int FloorDivide(int value, int divisor)
{
    int64_t wide = value;
    if (wide >= 0) return (int)(wide / divisor);
    return (int)(-((-wide + divisor - 1) / divisor));
}

static int RegionOrigin(int region)
{
    return region * HYDROLOGY_RUNTIME_REGION_SIZE;
}

static float HydrologySoilDepthAt(int elevation)
{
    int seaLevel = TerrainSeaLevel(WorldTerrainMode());
    if (seaLevel >= 0 && elevation <= seaLevel) return 0.15f;
    return 1.0f;
}

static bool GameHydrologyLocalCell(unsigned index, int *x, int *z)
{
    const SurfaceMapCell *cell = &hydrologyInputCache.cells[index];
    /* Store canonical coordinates: a floating-origin rebase must not restart
     * sampling or leave cached local positions pointing at another column. */
    int64_t localX = (int64_t)cell->x - WorldSurfaceMapOriginX();
    int64_t localZ = (int64_t)cell->z - WorldSurfaceMapOriginZ();
    if (localX < INT_MIN || localX > INT_MAX ||
        localZ < INT_MIN || localZ > INT_MAX) return false;
    *x = (int)localX;
    *z = (int)localZ;
    return true;
}

static bool GameHydrologyRefreshTerrainCell(unsigned index)
{
    int localX, localZ;
    if (!GameHydrologyLocalCell(index, &localX, &localZ)) return false;
    int elevation = WorldSurfaceHeightAt(localX, localZ);
    if (elevation < SURFACE_MIN_Y) elevation = SURFACE_MIN_Y;
    if (!HydrologyRuntimeGlobalSetTerrainCell(
            (int)(index % HYDROLOGY_RUNTIME_GRID_SIZE),
            (int)(index / HYDROLOGY_RUNTIME_GRID_SIZE),
            (float)elevation, HydrologySoilDepthAt(elevation))) return false;
    hydrologyInputCache.stats.terrainSamples++;
    return true;
}

static bool GameHydrologyBindRegion(GameRuntime *game)
{
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive() ||
        !isfinite(game->player.position.x) ||
        !isfinite(game->player.position.z)) return false;
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + floorf(game->player.position.x),
        (float)originZ + floorf(game->player.position.z));
    uint32_t surfaceId = WorldCurrentSurfaceId();
    if (!HydrologyRuntimeGlobalBind(surfaceId, canonical.x, canonical.z)) {
        return false;
    }
    uint64_t runtimeGeneration = HydrologyRuntimeGlobalGeneration();
    uint32_t seed = WorldCurrentSurfaceContext().seed;
    TerrainMode terrainMode = WorldTerrainMode();
    int regionX = FloorDivide(canonical.x, HYDROLOGY_RUNTIME_REGION_SIZE);
    int regionZ = FloorDivide(canonical.z, HYDROLOGY_RUNTIME_REGION_SIZE);
    bool regionChanged = !hydrologyInputCache.valid ||
        hydrologyInputCache.runtimeGeneration != runtimeGeneration ||
        hydrologyInputCache.seed != seed ||
        hydrologyInputCache.terrainMode != terrainMode ||
        hydrologyInputCache.surfaceId != surfaceId ||
        hydrologyInputCache.regionX != regionX ||
        hydrologyInputCache.regionZ != regionZ;
    if (!regionChanged) return true;

    GameHydrologyInvalidateInputCache();
    int baseX = RegionOrigin(regionX);
    int baseZ = RegionOrigin(regionZ);
    for (int z = 0; z < HYDROLOGY_RUNTIME_GRID_SIZE; z++) {
        for (int x = 0; x < HYDROLOGY_RUNTIME_GRID_SIZE; x++) {
            unsigned index = (unsigned)(z * HYDROLOGY_RUNTIME_GRID_SIZE + x);
            int mapX = baseX + x *
                (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE) + 2;
            int mapZ = baseZ + z *
                (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE) + 2;
            hydrologyInputCache.cells[index] = SurfaceCanonicalMapCell(
                (float)mapX, (float)mapZ);
            /* Coordinates are prepared before the bounded terrain pass. */
        }
    }
    hydrologyInputCache.valid = true;
    hydrologyInputCache.surfaceId = surfaceId;
    hydrologyInputCache.seed = seed;
    hydrologyInputCache.terrainMode = terrainMode;
    hydrologyInputCache.regionX = regionX;
    hydrologyInputCache.regionZ = regionZ;
    hydrologyInputCache.runtimeGeneration = runtimeGeneration;
    hydrologyInputCache.terrainPassRevision = WorldGetEditRevision();
    hydrologyInputCache.terrainRevision = UINT64_MAX;
    hydrologyInputCache.terrainCursor = 0u;
    hydrologyInputCache.terrainRemaining = GAME_HYDROLOGY_CELL_COUNT;
    return true;
}

static bool GameHydrologyRefreshTerrainInputs(void)
{
    GameHydrologyInputCache *cache = &hydrologyInputCache;
    uint64_t revision = WorldGetEditRevision();
    if (cache->terrainRemaining == 0u && revision != cache->terrainRevision) {
        cache->terrainPassRevision = revision;
        cache->terrainRemaining = GAME_HYDROLOGY_CELL_COUNT;
    }
    /* New edits during a pass schedule another pass after completion. They
     * never reset the cursor or repeatedly trigger a 1024-cell rebuild. */
    for (unsigned n = 0; n < GAME_HYDROLOGY_TERRAIN_SAMPLE_BUDGET &&
         cache->terrainRemaining > 0u; n++) {
        if (!GameHydrologyRefreshTerrainCell(cache->terrainCursor)) return false;
        cache->terrainCursor = (cache->terrainCursor + 1u) %
            GAME_HYDROLOGY_CELL_COUNT;
        if (--cache->terrainRemaining == 0u) {
            cache->terrainRevision = cache->terrainPassRevision;
        }
    }
    return true;
}

static float GameHydrologyPrecipitationRate(WeatherFieldSample weather)
{
    /* A normalized intensity of one is one micrometre/second, or 86.4 mm/day. */
    return isfinite(weather.precipitation)
        ? fmaxf(weather.precipitation, 0.0f) * 0.000001f : 0.0f;
}

static bool GameHydrologyRefreshWeatherInputs(double simulationTime)
{
    GameHydrologyInputCache *cache = &hydrologyInputCache;
    uint64_t revision = WeatherInputRevision();
    bool changed = revision != cache->weatherRevision ||
        simulationTime < cache->lastSimulationTime;
    cache->lastSimulationTime = simulationTime;
    cache->stats.weatherReusedCells = GAME_HYDROLOGY_CELL_COUNT;

    /* The runtime's 1024 precipitation rates are the coarse input cache.
     * Reuse them before evaluating any climate/terrain/light for weather. */
    if (changed || !cache->weatherSeeded) {
        WeatherFieldSample override = { 0 };
        cache->weatherUniform = WeatherFieldSampleOverride(&override);
        if (cache->weatherUniform) {
            if (!HydrologyRuntimeGlobalSetUniformPrecipitationRate(
                    GameHydrologyPrecipitationRate(override))) return false;
            cache->weatherSeeded = true;
            cache->weatherInputsValid = true;
            cache->weatherRemaining = 0u;
            cache->stats.weatherReusedCells = 0u;
        } else {
            cache->weatherSeeded = false;
            cache->weatherInputsValid = false;
            cache->weatherRemaining = 0u;
        }
        cache->weatherRevision = revision;
    }
    if (cache->weatherUniform) return true;

    if (cache->weatherRemaining == 0u) {
        if (cache->weatherInputsValid &&
            simulationTime - cache->weatherPassStarted <
                GAME_HYDROLOGY_WEATHER_REFRESH_SECONDS) return true;
        cache->weatherPassStarted = simulationTime;
        cache->weatherRemaining = GAME_HYDROLOGY_CELL_COUNT;
    }
    /* Finish the pass even if another 0.5-second interval starts. At 60 Hz,
     * 1024/32 takes 32 steps; resetting every 30 steps starves the last rows. */
    double sampleTime = SimulationTimePeriodic(cache->weatherPassStarted);
    if (!isfinite(sampleTime) || sampleTime < 0.0) return false;
    for (unsigned n = 0; n < GAME_HYDROLOGY_WEATHER_SAMPLE_BUDGET &&
         cache->weatherRemaining > 0u; n++) {
        unsigned index = cache->weatherCursor;
        int localX, localZ;
        if (!GameHydrologyLocalCell(index, &localX, &localZ)) return false;
        WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(
            localX, localZ, sampleTime);
        cache->stats.weatherSamples++;
        float rate = GameHydrologyPrecipitationRate(weather);
        if (!cache->weatherSeeded) {
            /* Bootstrap from the first local sample so an override ending
             * cannot leave stale forced rain in the unsampled rows. The
             * spatial field replaces this provisional rate over one pass. */
            if (!HydrologyRuntimeGlobalSetUniformPrecipitationRate(rate)) {
                return false;
            }
            cache->weatherSeeded = true;
            cache->stats.weatherReusedCells = 0u;
        } else {
            if (!HydrologyRuntimeGlobalSetPrecipitationRateCell(
                    (int)(index % HYDROLOGY_RUNTIME_GRID_SIZE),
                    (int)(index / HYDROLOGY_RUNTIME_GRID_SIZE), rate)) return false;
            if (cache->stats.weatherReusedCells > 0u) {
                cache->stats.weatherReusedCells--;
            }
        }
        cache->weatherCursor = (index + 1u) % GAME_HYDROLOGY_CELL_COUNT;
        if (--cache->weatherRemaining == 0u) {
            cache->weatherInputsValid = true;
            cache->stats.weatherPasses++;
        }
    }
    return true;
}

void GameAdvanceHydrologySimulationStep(GameRuntime *game,
                                        double simulationSeconds,
                                        double stepSeconds)
{
    if (!game || !isfinite(stepSeconds) || stepSeconds <= 0.0 ||
        !isfinite(simulationSeconds) || simulationSeconds < 0.0) return;
    GameHydrologyInputStats *stats = &hydrologyInputCache.stats;
    stats->weatherSamples = 0u;
    stats->weatherReusedCells = 0u;
    stats->terrainSamples = 0u;
    stats->inputsMainCpuMs = 0.0;
    stats->solverMainCpuMs = 0.0;
    if (!WorldIsSurfaceActive() || WorldNetherIsActive()) {
        GameHydrologyInvalidateInputCache();
        (void)HydrologyRuntimeGlobalReset(NULL);
        return;
    }
    double started = game->debugTraceEnabled ? GameDebugTraceMainCpuNowMs() : 0.0;
    if (!GameHydrologyBindRegion(game)) return;
    if (!GameHydrologyRefreshTerrainInputs()) return;
    if (hydrologyInputCache.terrainRemaining > 0u) {
        if (game->debugTraceEnabled) {
            stats->inputsMainCpuMs = fmax(
                GameDebugTraceMainCpuNowMs() - started, 0.0);
        }
        return;
    }
    if (!GameHydrologyRefreshWeatherInputs(simulationSeconds)) return;
    double solverStarted = 0.0;
    if (game->debugTraceEnabled) {
        solverStarted = GameDebugTraceMainCpuNowMs();
        stats->inputsMainCpuMs = fmax(solverStarted - started, 0.0);
    }
    (void)HydrologyRuntimeGlobalAdvance(stepSeconds, NULL);
    if (game->debugTraceEnabled) {
        stats->solverMainCpuMs = fmax(
            GameDebugTraceMainCpuNowMs() - solverStarted, 0.0);
    }
}

bool GameHydrologyGetInputStats(const GameRuntime *game,
                                GameHydrologyInputStats *outStats)
{
    if (!outStats) return false;
    *outStats = (GameHydrologyInputStats){ 0 };
    const GameHydrologyInputCache *cache = &hydrologyInputCache;
    if (!game || !cache->valid || !WorldIsSurfaceActive() || WorldNetherIsActive() ||
        !isfinite(game->player.position.x) || !isfinite(game->player.position.z) ||
        cache->runtimeGeneration != HydrologyRuntimeGlobalGeneration() ||
        cache->surfaceId != WorldCurrentSurfaceId() ||
        cache->seed != WorldCurrentSurfaceContext().seed ||
        cache->terrainMode != WorldTerrainMode()) return false;
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)WorldSurfaceMapOriginX() + floorf(game->player.position.x),
        (float)WorldSurfaceMapOriginZ() + floorf(game->player.position.z));
    if (cache->regionX != FloorDivide(cell.x, HYDROLOGY_RUNTIME_REGION_SIZE) ||
        cache->regionZ != FloorDivide(cell.z, HYDROLOGY_RUNTIME_REGION_SIZE)) {
        return false;
    }
    *outStats = cache->stats;
    outStats->active = true;
    outStats->weatherCursor = cache->weatherCursor;
    outStats->weatherPending = cache->weatherRemaining;
    outStats->terrainPending = cache->terrainRemaining;
    bool weatherCurrent = cache->weatherRevision == WeatherInputRevision();
    outStats->weatherReady = weatherCurrent && cache->weatherInputsValid;
    outStats->weatherUniform = weatherCurrent && cache->weatherUniform;
    return true;
}

bool GameHydrologyFeedbackAt(int x, int z, double simulationTime,
                             LocalClimateHydrologyFeedback *outFeedback,
                             void *userData)
{
    (void)simulationTime;
    (void)userData;
    if (!outFeedback || !WorldIsSurfaceActive() || WorldNetherIsActive()) {
        return false;
    }
    int mapX = WorldSurfaceMapOriginX() + x;
    int mapZ = WorldSurfaceMapOriginZ() + z;
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)mapX, (float)mapZ);
    HydrologyRuntimeFeedback feedback = { 0 };
    if (!HydrologyRuntimeGlobalFeedbackAt(
            WorldCurrentSurfaceId(), canonical.x, canonical.z, &feedback)) {
        return false;
    }
    *outFeedback = (LocalClimateHydrologyFeedback){
        .active = feedback.active,
        .surfaceWaterM = feedback.surfaceWaterM,
        .soilMoisture = feedback.soilMoisture,
        .groundwaterM = feedback.groundwaterM,
        .runoffM3PerS = feedback.runoffM3PerS,
        .evaporationMPerS = feedback.evaporationMPerS
    };
    return true;
}
