#include "app/game_hydrology.h"

#include "app/game_runtime.h"
#include "app/game_radiation.h"
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

void GameAdvanceSimulationStep(double simulationSeconds,
                               double stepSeconds, void *context)
{
    GameRuntime *game = context;
    SpaceAdvanceTime((float)stepSeconds);
    GameAdvanceRadiationSimulationStep(game, simulationSeconds, stepSeconds);
    GameAdvanceHydrologySimulationStep(game, simulationSeconds, stepSeconds);
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

static bool GameHydrologyBindAndInputs(GameRuntime *game,
                                       double simulationTime)
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
    int regionX = FloorDivide(canonical.x, HYDROLOGY_RUNTIME_REGION_SIZE);
    int regionZ = FloorDivide(canonical.z, HYDROLOGY_RUNTIME_REGION_SIZE);
    int baseX = RegionOrigin(regionX);
    int baseZ = RegionOrigin(regionZ);
    double periodicTime = SimulationTimePeriodic(simulationTime);
    if (!isfinite(periodicTime) || periodicTime < 0.0) periodicTime = 0.0;

    for (int z = 0; z < HYDROLOGY_RUNTIME_GRID_SIZE; z++) {
        for (int x = 0; x < HYDROLOGY_RUNTIME_GRID_SIZE; x++) {
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
            WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(
                localX, localZ, periodicTime);
            float precipitation = isfinite(weather.precipitation)
                ? fmaxf(weather.precipitation, 0.0f) : 0.0f;
            /* The weather field is a normalized intensity.  One micro-metre
             * per second at full intensity is 86.4 mm/day. */
            float precipitationRate = precipitation * 0.000001f;
            HydrologyRuntimeCellInput input = {
                .elevationM = (float)elevation,
                .soilDepthM = HydrologySoilDepthAt(localX, localZ, elevation),
                .precipitationMPerS = precipitationRate
            };
            if (!HydrologyRuntimeGlobalSetCellInput(x, z, input)) return false;
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
        (void)HydrologyRuntimeGlobalReset(NULL);
        return;
    }
    if (!GameHydrologyBindAndInputs(game, simulationSeconds)) return;
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
