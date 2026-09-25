#include "app/game_hydrology.h"

#include "app/game_runtime.h"
#include "core/simulation_time.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/hydrology_runtime.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

#define GAME_HYDROLOGY_CELL_COUNT \
    (HYDROLOGY_RUNTIME_GRID_SIZE * HYDROLOGY_RUNTIME_GRID_SIZE)
#define GAME_HYDROLOGY_WEATHER_SAMPLE_BUDGET 32u
#define GAME_HYDROLOGY_WEATHER_REFRESH_SECONDS 0.5

typedef struct GameHydrologyInputCache {
    bool valid;
    uint32_t surfaceId;
    int regionX;
    int regionZ;
    int originX;
    int originZ;
    int localX[GAME_HYDROLOGY_CELL_COUNT];
    int localZ[GAME_HYDROLOGY_CELL_COUNT];
    uint64_t runtimeGeneration;
    uint64_t terrainRevision;
    unsigned weatherCursor;
    uint64_t weatherBucket;
    uint64_t weatherRevision;
    bool weatherInputsValid;
} GameHydrologyInputCache;

static GameHydrologyInputCache hydrologyInputCache;

static void GameHydrologyInvalidateInputCache(void)
{
    memset(&hydrologyInputCache, 0, sizeof(hydrologyInputCache));
    hydrologyInputCache.weatherBucket = UINT64_MAX;
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

static float HydrologySoilDepthAt(int x, int z, int elevation)
{
    int seaLevel = TerrainSeaLevel(WorldTerrainMode());
    if (seaLevel >= 0 && elevation <= seaLevel) return 0.15f;
    (void)x;
    (void)z;
    return 1.0f;
}

static bool GameHydrologyBindRegion(GameRuntime *game)
{
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive()) return false;
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
    int regionX = FloorDivide(canonical.x, HYDROLOGY_RUNTIME_REGION_SIZE);
    int regionZ = FloorDivide(canonical.z, HYDROLOGY_RUNTIME_REGION_SIZE);
    bool regionChanged = !hydrologyInputCache.valid ||
        hydrologyInputCache.runtimeGeneration != runtimeGeneration ||
        hydrologyInputCache.terrainRevision != WorldGetEditRevision() ||
        hydrologyInputCache.surfaceId != surfaceId ||
        hydrologyInputCache.regionX != regionX ||
        hydrologyInputCache.regionZ != regionZ ||
        hydrologyInputCache.originX != originX ||
        hydrologyInputCache.originZ != originZ;
    if (!regionChanged) return true;

    int baseX = RegionOrigin(regionX);
    int baseZ = RegionOrigin(regionZ);
    for (int z = 0; z < HYDROLOGY_RUNTIME_GRID_SIZE; z++) {
        for (int x = 0; x < HYDROLOGY_RUNTIME_GRID_SIZE; x++) {
            unsigned index = (unsigned)(z * HYDROLOGY_RUNTIME_GRID_SIZE + x);
            int mapX = baseX + x *
                (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE) + 2;
            int mapZ = baseZ + z *
                (HYDROLOGY_RUNTIME_REGION_SIZE / HYDROLOGY_RUNTIME_GRID_SIZE) + 2;
            SurfaceMapCell sampleCell = SurfaceCanonicalMapCell(
                (float)mapX, (float)mapZ);
            int localX = sampleCell.x - originX;
            int localZ = sampleCell.z - originZ;
            int elevation = PlanetWorldIsActive()
                ? PlanetTerrainHeight(localX, localZ)
                : WorldSurfaceHeightAt(localX, localZ);
            if (elevation < SURFACE_MIN_Y) elevation = SURFACE_MIN_Y;
            if (!HydrologyRuntimeGlobalSetTerrainCell(
                    x, z, (float)elevation,
                    HydrologySoilDepthAt(localX, localZ, elevation))) {
                return false;
            }
            hydrologyInputCache.localX[index] = localX;
            hydrologyInputCache.localZ[index] = localZ;
        }
    }
    hydrologyInputCache.valid = true;
    hydrologyInputCache.surfaceId = surfaceId;
    hydrologyInputCache.regionX = regionX;
    hydrologyInputCache.regionZ = regionZ;
    hydrologyInputCache.originX = originX;
    hydrologyInputCache.originZ = originZ;
    hydrologyInputCache.runtimeGeneration = runtimeGeneration;
    hydrologyInputCache.terrainRevision = WorldGetEditRevision();
    hydrologyInputCache.weatherCursor = 0u;
    hydrologyInputCache.weatherBucket = UINT64_MAX;
    hydrologyInputCache.weatherRevision = 0u;
    hydrologyInputCache.weatherInputsValid = false;
    return true;
}

static bool GameHydrologyRefreshWeatherInputs(double simulationTime)
{
    if (!hydrologyInputCache.valid || !isfinite(simulationTime) ||
        simulationTime < 0.0) return false;
    double periodicTime = SimulationTimePeriodic(simulationTime);
    if (!isfinite(periodicTime) || periodicTime < 0.0) periodicTime = 0.0;

    uint64_t weatherBucket = (uint64_t)floor(
        periodicTime / GAME_HYDROLOGY_WEATHER_REFRESH_SECONDS);
    uint64_t weatherRevision = WeatherInputRevision();
    if (weatherBucket != hydrologyInputCache.weatherBucket ||
        weatherRevision != hydrologyInputCache.weatherRevision) {
        hydrologyInputCache.weatherBucket = weatherBucket;
        hydrologyInputCache.weatherRevision = weatherRevision;
        hydrologyInputCache.weatherCursor = 0u;
        hydrologyInputCache.weatherInputsValid = false;
    }
    if (hydrologyInputCache.weatherInputsValid) return true;

    double sampleTime = (double)weatherBucket *
        GAME_HYDROLOGY_WEATHER_REFRESH_SECONDS;
    WeatherFieldSample overrideSample = { 0 };
    bool hasOverride = WeatherFieldSampleOverride(&overrideSample);
    for (unsigned count = 0u;
         count < GAME_HYDROLOGY_WEATHER_SAMPLE_BUDGET;
         count++) {
        unsigned index = hydrologyInputCache.weatherCursor++;
        int x = (int)(index % HYDROLOGY_RUNTIME_GRID_SIZE);
        int z = (int)(index / HYDROLOGY_RUNTIME_GRID_SIZE);
        WeatherFieldSample weather = hasOverride
            ? overrideSample
            : WeatherFieldSampleAtWorldTime(
                hydrologyInputCache.localX[index],
                hydrologyInputCache.localZ[index], sampleTime);
        float precipitation = isfinite(weather.precipitation)
            ? fmaxf(weather.precipitation, 0.0f) : 0.0f;
        /* The weather field is a normalized intensity.  One micro-metre
         * per second at full intensity is 86.4 mm/day. */
        if (!HydrologyRuntimeGlobalSetPrecipitationRateCell(
                x, z, precipitation * 0.000001f)) return false;
        if (hydrologyInputCache.weatherCursor >= GAME_HYDROLOGY_CELL_COUNT) {
            hydrologyInputCache.weatherCursor = 0u;
            hydrologyInputCache.weatherInputsValid = true;
            break;
        }
    }
    return true;
}

void GameAdvanceHydrologySimulationStep(GameRuntime *game,
                                        double simulationSeconds,
                                        double stepSeconds)
{
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive() ||
        !isfinite(stepSeconds) || stepSeconds <= 0.0) {
        GameHydrologyInvalidateInputCache();
        (void)HydrologyRuntimeGlobalReset(NULL);
        return;
    }
    if (!GameHydrologyBindRegion(game)) return;
    if (!GameHydrologyRefreshWeatherInputs(simulationSeconds)) return;
    (void)HydrologyRuntimeGlobalAdvance(stepSeconds, NULL);
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
