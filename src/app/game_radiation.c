#include "app/game_radiation.h"

#include "app/game_runtime.h"
#include "core/simulation_time.h"
#include "space/planet_profile.h"
#include "space/space_state.h"
#include "world/local_climate.h"
#include "world/radiation_runtime.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>

static PlanetProfile cachedHomeProfile;
static uint32_t cachedHomeSeed;
static bool cachedHomeProfileValid;

static const PlanetProfile *GameHomeRadiationProfile(void)
{
    uint32_t seed = WorldGetSeed();
    if (!cachedHomeProfileValid || cachedHomeSeed != seed) {
        cachedHomeProfile = PlanetProfileGenerateLegacy(
            seed, SOLAR_STYLE_TEMPERATE, 600.0f);
        cachedHomeSeed = seed;
        cachedHomeProfileValid = true;
    }
    return &cachedHomeProfile;
}

void GameAdvanceRadiationSimulationStep(GameRuntime *game,
                                         double simulationSeconds,
                                         double stepSeconds)
{
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive()) {
        RadiationRuntimeGlobalReset(NULL);
        return;
    }

    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + floorf(game->player.position.x),
        (float)originZ + floorf(game->player.position.z));
    int localX = canonical.x - originX;
    int localZ = canonical.z - originZ;
    float longitude = 0.0f;
    float latitude = 0.0f;
    const PlanetProfile *profile = NULL;
    if (PlanetWorldIsActive()) {
        profile = PlanetWorldProfile();
        PlanetSurfaceLatLonAt(localX, localZ, &longitude, &latitude);
    } else {
        profile = GameHomeRadiationProfile();
        HomeSurfaceLatLonAt(localX, localZ, &longitude, &latitude);
    }
    if (!profile) return;

    double periodicTime = SimulationTimePeriodic(simulationSeconds);
    LocalClimateState climate = { 0 };
    double baselineTemperatureK = profile->equilibriumTempK;
    if (WeatherLocalClimateAtWorldTime(
            localX, localZ, periodicTime, &climate) &&
        isfinite(climate.temperatureK) && climate.temperatureK > 0.0f) {
        baselineTemperatureK = climate.temperatureK;
    }
    if (!isfinite(baselineTemperatureK) || baselineTemperatureK <= 0.0) {
        baselineTemperatureK =
            RADIATION_RUNTIME_DEFAULT_INITIAL_TEMPERATURE_K;
    }

    RadiationRuntimeLocationTime location = {
        .latitudeRad = latitude,
        .longitudeRad = longitude,
        .simulationTimeGameTime = periodicTime
    };
    if (!RadiationRuntimeGlobalBind(
            WorldCurrentSurfaceId(), canonical.x, canonical.z,
            baselineTemperatureK)) {
        return;
    }
    (void)RadiationRuntimeGlobalAdvance(
        profile, &location, stepSeconds, NULL);
}
