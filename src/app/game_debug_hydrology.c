#include "app/game_debug_hydrology.h"

#include "app/game_runtime.h"
#include "world/hydrology_runtime.h"
#include "world/surface_topology.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool Number(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool Boolean(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool HydrologyState(const GameRuntime *game,
                           HydrologyRuntimeSnapshot *outSnapshot,
                           HydrologyRuntimeFeedback *outFeedback)
{
    if (outSnapshot) *outSnapshot = (HydrologyRuntimeSnapshot){ 0 };
    if (outFeedback) *outFeedback = (HydrologyRuntimeFeedback){ 0 };
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive() ||
        !isfinite(game->player.position.x) ||
        !isfinite(game->player.position.z)) return false;
    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + floorf(game->player.position.x),
        (float)originZ + floorf(game->player.position.z));
    HydrologyRuntimeSnapshot snapshot = { 0 };
    HydrologyRuntimeFeedback feedback = { 0 };
    if (!HydrologyRuntimeGlobalSnapshot(&snapshot) || !snapshot.active ||
        !HydrologyRuntimeGlobalFeedbackAt(
            WorldCurrentSurfaceId(), canonical.x, canonical.z, &feedback)) {
        return false;
    }
    if (outSnapshot) *outSnapshot = snapshot;
    if (outFeedback) *outFeedback = feedback;
    return true;
}

static int RegionCoordinate(int mapCoordinate)
{
    int64_t value = mapCoordinate;
    if (value >= 0) return (int)(value / HYDROLOGY_RUNTIME_REGION_SIZE);
    return (int)(-((-value + HYDROLOGY_RUNTIME_REGION_SIZE - 1) /
                  HYDROLOGY_RUNTIME_REGION_SIZE));
}

bool GameDebugHydrologyDslResolve(const GameRuntime *game,
                                  const char *name,
                                  DebugDslValue *outValue)
{
    if (!game || !name || !outValue || strncmp(name, "hydrology.", 10u) != 0) {
        return false;
    }
    HydrologyRuntimeSnapshot snapshot = { 0 };
    HydrologyRuntimeFeedback feedback = { 0 };
    bool active = HydrologyState(game, &snapshot, &feedback);
    if (strcmp(name, "hydrology.active") == 0) {
        return Boolean(outValue, active);
    }
    if (strcmp(name, "hydrology.region_x") == 0 ||
        strcmp(name, "hydrology.region_z") == 0) {
        if (!active) return Number(outValue, -1.0);
        return Number(outValue, strcmp(name, "hydrology.region_x") == 0
            ? snapshot.location.regionX : snapshot.location.regionZ);
    }
    if (strcmp(name, "hydrology.step_count") == 0) {
        return Number(outValue, active ? (double)snapshot.stepCount : 0.0);
    }
    if (strcmp(name, "hydrology.total_water_m3") == 0) {
        return Number(outValue, active ? snapshot.totalWaterVolumeM3 : 0.0);
    }
    if (strcmp(name, "hydrology.total_sediment_kg") == 0) {
        return Number(outValue, active ? snapshot.totalSedimentMassKg : 0.0);
    }
    if (strcmp(name, "hydrology.surface_water") == 0 ||
        strcmp(name, "hydrology.surface_water_m") == 0) {
        return Number(outValue, active ? feedback.surfaceWaterM : 0.0);
    }
    if (strcmp(name, "hydrology.soil_moisture") == 0) {
        return Number(outValue, active ? feedback.soilMoisture : 0.0);
    }
    if (strcmp(name, "hydrology.groundwater") == 0 ||
        strcmp(name, "hydrology.groundwater_m") == 0) {
        return Number(outValue, active ? feedback.groundwaterM : 0.0);
    }
    if (strcmp(name, "hydrology.runoff") == 0 ||
        strcmp(name, "hydrology.runoff_m3_per_s") == 0) {
        return Number(outValue, active ? feedback.runoffM3PerS : 0.0);
    }
    if (strcmp(name, "hydrology.evaporation") == 0 ||
        strcmp(name, "hydrology.evaporation_m_per_s") == 0) {
        return Number(outValue, active ? feedback.evaporationMPerS : 0.0);
    }
    if (strcmp(name, "hydrology.sediment") == 0 ||
        strcmp(name, "hydrology.sediment_kg_m2") == 0) {
        return Number(outValue, active ? feedback.sedimentLoadKgM2 : 0.0);
    }
    /* Keep region helpers useful before the first simulation step. */
    if (strcmp(name, "hydrology.player_region_x") == 0 ||
        strcmp(name, "hydrology.player_region_z") == 0) {
        if (!WorldIsSurfaceActive() || WorldNetherIsActive()) {
            return Number(outValue, -1.0);
        }
        int mapX = WorldSurfaceMapOriginX() +
            (int)floorf(game->player.position.x);
        int mapZ = WorldSurfaceMapOriginZ() +
            (int)floorf(game->player.position.z);
        SurfaceMapCell canonical = SurfaceCanonicalMapCell(
            (float)mapX, (float)mapZ);
        return Number(outValue,
                      RegionCoordinate(strcmp(name, "hydrology.player_region_x") == 0
                          ? canonical.x : canonical.z));
    }
    return false;
}
