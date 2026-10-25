#include "app/game_debug_weather.h"

#include "raylib.h"
#include "raymath.h"

#include "app/game_internal.h"
#include "app/game_runtime.h"
#include "core/simulation_time.h"
#include "gameplay/player.h"
#include "presentation/weather_presentation.h"
#include "space/space_state.h"
#include "space/planet_atmosphere.h"
#include "world/local_climate.h"
#include "world/radiation_runtime.h"
#include "world/surface_topology.h"
#include "world/tornado.h"
#include "world/tornado_model.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/weather_model.h"
#include "app/game_debug_wildfire.h"
#include "world/wildfire_model.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>
#include <string.h>

static bool WeatherDslBool(DebugDslValue *outValue, bool value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_BOOL,
        .as.boolean = value
    };
    return true;
}

static bool WeatherDslNumber(DebugDslValue *outValue, double value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_NUMBER,
        .as.number = value
    };
    return true;
}

static bool WeatherDslString(DebugDslValue *outValue, const char *value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_STRING,
        .as.string = value ? value : ""
    };
    return true;
}

static bool WeatherDslVec3(DebugDslValue *outValue, Vector3 value)
{
    *outValue = (DebugDslValue){
        .type = DEBUG_DSL_VALUE_VEC3,
        .as.vec3 = { value.x, value.y, value.z }
    };
    return true;
}

typedef struct WeatherRadiationDslState {
    RadiationRuntimeSnapshot snapshot;
    int regionX;
    int regionZ;
    double baselineTemperatureK;
    bool active;
} WeatherRadiationDslState;

static int WeatherRadiationRegionCoordinate(int mapCoordinate)
{
    int64_t value = mapCoordinate;
    if (value >= 0) {
        return (int)(value / RADIATION_RUNTIME_REGION_SIZE);
    }
    return (int)(-((-value + RADIATION_RUNTIME_REGION_SIZE - 1) /
                  RADIATION_RUNTIME_REGION_SIZE));
}

static WeatherRadiationDslState WeatherRadiationState(
    const GameRuntime *game)
{
    WeatherRadiationDslState state = { .regionX = -1, .regionZ = -1 };
    if (!game || !WorldIsSurfaceActive() || WorldNetherIsActive() ||
        !isfinite(game->player.position.x) ||
        !isfinite(game->player.position.z)) {
        return state;
    }

    int originX = WorldSurfaceMapOriginX();
    int originZ = WorldSurfaceMapOriginZ();
    SurfaceMapCell canonical = SurfaceCanonicalMapCell(
        (float)originX + floorf(game->player.position.x),
        (float)originZ + floorf(game->player.position.z));
    state.regionX = WeatherRadiationRegionCoordinate(canonical.x);
    state.regionZ = WeatherRadiationRegionCoordinate(canonical.z);

    RadiationRuntimeSnapshot snapshot = { 0 };
    double offsetK = 0.0;
    if (!RadiationRuntimeGlobalSnapshot(&snapshot) ||
        !RadiationRuntimeGlobalTemperatureOffsetAt(
            WorldCurrentSurfaceId(), canonical.x, canonical.z, &offsetK) ||
        !snapshot.valid || !isfinite(snapshot.column.temperatureK) ||
        !isfinite(offsetK)) {
        return state;
    }

    double baseline = snapshot.column.temperatureK - offsetK;
    if (!isfinite(baseline) || baseline <= 0.0) return state;
    state.snapshot = snapshot;
    state.baselineTemperatureK = baseline;
    state.active = true;
    return state;
}

static bool WeatherRadiationDslResolve(const GameRuntime *game,
                                       const char *name,
                                       DebugDslValue *outValue)
{
    if (!game || !name || !outValue ||
        strncmp(name, "radiation.", 10u) != 0) {
        return false;
    }

    WeatherRadiationDslState state = WeatherRadiationState(game);
    if (strcmp(name, "radiation.active") == 0) {
        return WeatherDslBool(outValue, state.active);
    }
    if (strcmp(name, "radiation.region_x") == 0) {
        return WeatherDslNumber(outValue, state.regionX);
    }
    if (strcmp(name, "radiation.region_z") == 0) {
        return WeatherDslNumber(outValue, state.regionZ);
    }
    if (strcmp(name, "radiation.temperature_k") == 0) {
        return WeatherDslNumber(
            outValue, state.active ? state.snapshot.column.temperatureK : 0.0);
    }
    if (strcmp(name, "radiation.baseline_temperature_k") == 0) {
        return WeatherDslNumber(
            outValue, state.active ? state.baselineTemperatureK : 0.0);
    }
    if (strcmp(name, "radiation.net_flux_wm2") == 0) {
        return WeatherDslNumber(
            outValue, state.active ? state.snapshot.balance.netRadiativeFluxWm2
                                    : 0.0);
    }
    if (strcmp(name, "radiation.equilibrium_temperature_k") == 0) {
        return WeatherDslNumber(
            outValue, state.active
                ? state.snapshot.balance.equilibriumTemperatureK : 0.0);
    }
    if (strcmp(name, "radiation.step_count") == 0) {
        return WeatherDslNumber(
            outValue, state.active ? (double)state.snapshot.stepCount : 0.0);
    }
    return false;
}

static bool WeatherLocalClimate(const GameRuntime *game,
                                LocalClimateState *outClimate)
{
    if (!game || !outClimate || !WorldIsSurfaceActive() ||
        !isfinite(game->player.position.x) ||
        !isfinite(game->player.position.z)) {
        return false;
    }
    return WeatherLocalClimateAtWorldTime(
        (int)floorf(game->player.position.x),
        (int)floorf(game->player.position.z),
        SimulationTimePeriodic(SpaceElapsedSimulationTime()),
        outClimate);
}

static bool WeatherSampleDslResolve(const char *name,
                                    DebugDslValue *outValue)
{
    if (strcmp(name, "weather.phenomenon") == 0) {
        WeatherFieldSample sample = WeatherCurrentSample();
        return WeatherDslString(
            outValue, WeatherPhenomenonName(sample.dominantPhenomenon));
    }
    if (strcmp(name, "weather.cloud_genus") == 0) {
        WeatherFieldSample sample = WeatherCurrentSample();
        return WeatherDslString(
            outValue, WeatherCloudGenusName(sample.dominantCloudGenus));
    }
#define WEATHER_SAMPLE_NUMBER(fieldName, member) \
    if (strcmp(name, fieldName) == 0) { \
        WeatherFieldSample sample = WeatherCurrentSample(); \
        return WeatherDslNumber(outValue, sample.member); \
    }
    WEATHER_SAMPLE_NUMBER("weather.temperature", temperatureK)
    WEATHER_SAMPLE_NUMBER("weather.temperature_k", temperatureK)
    WEATHER_SAMPLE_NUMBER("weather.pressure", pressureAtm)
    WEATHER_SAMPLE_NUMBER("weather.pressure_atm", pressureAtm)
    WEATHER_SAMPLE_NUMBER("weather.humidity", relativeHumidity)
    WEATHER_SAMPLE_NUMBER("weather.relative_humidity", relativeHumidity)
    WEATHER_SAMPLE_NUMBER("weather.dew_point", dewPointK)
    WEATHER_SAMPLE_NUMBER("weather.dew_point_k", dewPointK)
    WEATHER_SAMPLE_NUMBER("weather.wet_bulb", wetBulbK)
    WEATHER_SAMPLE_NUMBER("weather.wet_bulb_k", wetBulbK)
    WEATHER_SAMPLE_NUMBER("weather.wind", wind)
    WEATHER_SAMPLE_NUMBER("weather.gust", gust)
    WEATHER_SAMPLE_NUMBER("weather.visibility", visibility)
    WEATHER_SAMPLE_NUMBER("weather.rain", rain)
    WEATHER_SAMPLE_NUMBER("weather.snow", snow)
    WEATHER_SAMPLE_NUMBER("weather.sleet", sleet)
    WEATHER_SAMPLE_NUMBER("weather.freezing_rain", freezingRain)
    WEATHER_SAMPLE_NUMBER("weather.hail", hail)
    WEATHER_SAMPLE_NUMBER("weather.lightning", lightning)
    WEATHER_SAMPLE_NUMBER("weather.fog", fog)
    WEATHER_SAMPLE_NUMBER("weather.dust", dust)
    WEATHER_SAMPLE_NUMBER("weather.rainbow", rainbow)
    WEATHER_SAMPLE_NUMBER("weather.aurora", aurora)
    WEATHER_SAMPLE_NUMBER("weather.cloud_cover", cloudCover)
    WEATHER_SAMPLE_NUMBER("weather.cloud_flags", cloudGenera)
#undef WEATHER_SAMPLE_NUMBER
    return false;
}

static bool WeatherImpactDslResolve(const GameRuntime *game,
                                    const char *name,
                                    DebugDslValue *outValue)
{
    if (strcmp(name, "weather.damage_enabled") == 0) {
        return WeatherDslBool(outValue, WeatherImpactEnabled());
    }
    if (strcmp(name, "weather.fire_phase") == 0 ||
        strcmp(name, "weather.fire_position") == 0 ||
        strcmp(name, "weather.fire_distance") == 0 ||
        strcmp(name, "weather.fire_intensity") == 0 ||
        strcmp(name, "weather.fire_fuel") == 0 ||
        strcmp(name, "weather.fire_moisture") == 0) {
        WeatherImpactFireSnapshot fire = { 0 };
        float distance = -1.0f;
        bool haveFire = WeatherImpactNearestFire(
            game->player.position, &fire, &distance);
        if (strcmp(name, "weather.fire_phase") == 0) {
            return WeatherDslString(
                outValue,
                haveFire ? WildfirePhaseName(fire.state.phase) : "inactive");
        }
        if (strcmp(name, "weather.fire_position") == 0) {
            Vector3 position = haveFire
                ? (Vector3){ fire.x, fire.y, fire.z } : Vector3Zero();
            return WeatherDslVec3(outValue, position);
        }
        if (strcmp(name, "weather.fire_distance") == 0) {
            return WeatherDslNumber(outValue, distance);
        }
        if (strcmp(name, "weather.fire_intensity") == 0) {
            return WeatherDslNumber(
                outValue, haveFire ? fire.state.intensity : 0.0f);
        }
        if (strcmp(name, "weather.fire_fuel") == 0) {
            return WeatherDslNumber(
                outValue, haveFire ? fire.state.fuel : 0.0f);
        }
        return WeatherDslNumber(
            outValue, haveFire ? fire.state.moisture : 0.0f);
    }
    if (strcmp(name, "weather.fire_local_heat") == 0 ||
        strcmp(name, "weather.fire_local_smoke") == 0) {
        PlayerWaterState water = PlayerWaterStateAt(game->player.position);
        WeatherImpactExposure exposure = WeatherImpactExposureAt(
            game->player.position,
            GameEnvironmentSheltered(game->player.position) ? 1.0f : 0.0f,
            water.bodySubmerged ? 1.0f : 0.0f);
        return WeatherDslNumber(
            outValue, strcmp(name, "weather.fire_local_heat") == 0
                          ? exposure.heat : exposure.smoke);
    }
#define WEATHER_IMPACT_NUMBER(fieldName, member) \
    if (strcmp(name, fieldName) == 0) { \
        WeatherImpactStats stats = WeatherImpactGetStats(); \
        return WeatherDslNumber(outValue, stats.member); \
    }
    WEATHER_IMPACT_NUMBER("weather.surface_count", surfaceCount)
    WEATHER_IMPACT_NUMBER("weather.active_fires", activeFires)
    WEATHER_IMPACT_NUMBER("weather.fire_ignitions", ignitions)
    WEATHER_IMPACT_NUMBER("weather.fire_spread_ignitions", spreadIgnitions)
    WEATHER_IMPACT_NUMBER("weather.fire_extinctions", extinctions)
    WEATHER_IMPACT_NUMBER("weather.fire_suppressions", suppressions)
    WEATHER_IMPACT_NUMBER("weather.fire_burned_blocks", burnedBlocks)
    WEATHER_IMPACT_NUMBER("weather.fire_burn_sites", burnSiteCount)
    WEATHER_IMPACT_NUMBER("weather.fire_recovered_sites", recoveredBurnSites)
    WEATHER_IMPACT_NUMBER("weather.fire_dropped_ignitions", droppedIgnitions)
    WEATHER_IMPACT_NUMBER("weather.fire_dropped_burn_sites", droppedBurnSites)
#undef WEATHER_IMPACT_NUMBER
    return false;
}

static bool WeatherTornadoDslResolve(const GameRuntime *game,
                                     const char *name,
                                     DebugDslValue *outValue)
{
    if (strncmp(name, "weather.tornado_", 16u) != 0) return false;
    if (strcmp(name, "weather.tornado_distance") == 0) {
        return WeatherDslNumber(
            outValue, TornadoDistanceTo(game->player.position));
    }
    if (strcmp(name, "weather.tornado_forced_frames") == 0) {
        return WeatherDslNumber(outValue, TornadoForcedFramesRemaining());
    }
    if (strcmp(name, "weather.tornado_blocks_damaged") == 0 ||
        strcmp(name, "weather.tornado_debris_emitted") == 0 ||
        strcmp(name, "weather.tornado_dropped_effects") == 0) {
        TornadoStats stats = TornadoGetStats();
        if (strcmp(name, "weather.tornado_blocks_damaged") == 0) {
            return WeatherDslNumber(outValue, stats.blockDamageEvents);
        }
        if (strcmp(name, "weather.tornado_debris_emitted") == 0) {
            return WeatherDslNumber(outValue, stats.debrisEmitted);
        }
        return WeatherDslNumber(outValue, stats.droppedEffects);
    }
    TornadoState tornado = TornadoCurrent();
    if (strcmp(name, "weather.tornado_phase") == 0) {
        return WeatherDslString(outValue, TornadoPhaseName(tornado.phase));
    }
    if (strcmp(name, "weather.tornado_center") == 0) {
        return WeatherDslVec3(outValue, tornado.center);
    }
    if (strcmp(name, "weather.tornado_active") == 0) {
        return WeatherDslBool(outValue, tornado.active);
    }
    if (strcmp(name, "weather.tornado_forced") == 0) {
        return WeatherDslBool(outValue, tornado.forced);
    }
    if (strcmp(name, "weather.tornado_intensity") == 0) {
        return WeatherDslNumber(outValue, tornado.intensity);
    }
    if (strcmp(name, "weather.tornado_radius") == 0) {
        return WeatherDslNumber(outValue, tornado.radius);
    }
    if (strcmp(name, "weather.tornado_funnel_height") == 0) {
        return WeatherDslNumber(outValue, tornado.funnelHeight);
    }
    if (strcmp(name, "weather.tornado_wind_mps") == 0) {
        return WeatherDslNumber(outValue, tornado.maximumWindMps);
    }
    return false;
}

static bool WeatherAtmosphereDslResolve(const GameRuntime *game, const char *name,
                                         DebugDslValue *outValue)
{
    if (strncmp(name, "atmosphere.", 11u) != 0) return false;
    AtmosphereSample air = { .valid = true };
    if (!WorldNetherIsActive() && (PlanetWorldIsActive() || HomeWorldSurfaceIsActive())) {
        air = PlanetAtmosphereAtHeight(
            PlanetWorldIsActive() ? PlanetWorldProfile() : NULL,
            game->player.position.y);
    }
    if (strcmp(name, "atmosphere.valid") == 0) return WeatherDslBool(outValue, air.valid);
    if (strcmp(name, "atmosphere.active") == 0)
        return WeatherDslBool(outValue, air.valid && air.layer != ATMOSPHERE_LAYER_VACUUM);
    if (strcmp(name, "atmosphere.layer") == 0)
        return WeatherDslString(outValue, AtmosphereLayerName(air.layer));
#define ATMOSPHERE_NUMBER(fieldName, member) \
    if (strcmp(name, fieldName) == 0) return WeatherDslNumber(outValue, air.member)
    ATMOSPHERE_NUMBER("atmosphere.altitude_m", altitudeM);
    ATMOSPHERE_NUMBER("atmosphere.temperature_k", temperatureK);
    ATMOSPHERE_NUMBER("atmosphere.pressure_atm", pressureAtm);
    ATMOSPHERE_NUMBER("atmosphere.density_kg_m3", densityKgM3);
    ATMOSPHERE_NUMBER("atmosphere.density_ratio", densityRatio);
    ATMOSPHERE_NUMBER("atmosphere.column_fraction", columnFraction);
    ATMOSPHERE_NUMBER("atmosphere.molar_mass_kg_mol", molarMassKgMol);
    ATMOSPHERE_NUMBER("atmosphere.ozone", ozone);
    ATMOSPHERE_NUMBER("atmosphere.ionization", ionization);
#undef ATMOSPHERE_NUMBER
    return false;
}

bool GameDebugWeatherDslResolve(const GameRuntime *game, const char *name,
                                DebugDslValue *outValue)
{
    if (!game || !name || !outValue ||
        (strncmp(name, "weather.", 8u) != 0 &&
         strncmp(name, "radiation.", 10u) != 0 &&
         strncmp(name, "atmosphere.", 11u) != 0)) {
        return false;
    }
    if (WeatherAtmosphereDslResolve(game, name, outValue)) return true;
    if (WeatherRadiationDslResolve(game, name, outValue)) return true;
    if (strcmp(name, "weather.climate") == 0) {
        LocalClimateState climate = { 0 };
        bool haveClimate = WeatherLocalClimate(game, &climate);
        return WeatherDslString(
            outValue,
            haveClimate ? ClimateRegimeName(climate.regime) : "unavailable");
    }
    if (WeatherSampleDslResolve(name, outValue)) return true;
    if (WeatherImpactDslResolve(game, name, outValue)) return true;
    if (WeatherTornadoDslResolve(game, name, outValue)) return true;
    if (strcmp(name, "weather.cloud_layers") == 0 ||
        strcmp(name, "weather.cloud_reference_y") == 0 ||
        strcmp(name, "weather.cloud_bottom_y") == 0) {
        WeatherVisualState visual = WeatherVisualStateAtWorld(
            game->player.position,
            SimulationTimePeriodic(SpaceElapsedSimulationTime()), 0.5f);
        if (strcmp(name, "weather.cloud_reference_y") == 0) {
            return WeatherDslNumber(outValue, visual.cloudAltitudeReference);
        }
        if (strcmp(name, "weather.cloud_bottom_y") == 0) {
            float bottom = visual.cloudBaseHeight;
            for (unsigned i = 0u; i < visual.cloudLayerCount; i++) {
                if (i == 0u || visual.cloudLayers[i].baseHeight < bottom) {
                    bottom = visual.cloudLayers[i].baseHeight;
                }
            }
            return WeatherDslNumber(outValue, visual.active ?
                visual.cloudAltitudeReference + bottom : 0.0f);
        }
        return WeatherDslNumber(outValue, visual.cloudLayerCount);
    }
    if (strcmp(name, "weather.cloud_forced_frames") == 0) {
        return WeatherDslNumber(
            outValue, WeatherForcedCloudFramesRemaining());
    }
    if (strcmp(name, "weather.forced_frames") == 0) {
        return WeatherDslNumber(outValue, WeatherForcedFramesRemaining());
    }
    return false;
}

static void GameDebugInspectWeather(GameRuntime *game)
{
    WeatherFieldSample weather = WeatherCurrentSample();
    WeatherVisualState visual = WeatherVisualStateAtWorld(
        game->player.position,
        SimulationTimePeriodic(SpaceElapsedSimulationTime()), 0.5f);
    WeatherImpactStats impacts = WeatherImpactGetStats();
    WeatherImpactFireSnapshot nearestFire = { 0 };
    float nearestFireDistance = -1.0f;
    bool haveNearestFire = WeatherImpactNearestFire(
        game->player.position, &nearestFire, &nearestFireDistance);
    PlayerWaterState water = PlayerWaterStateAt(game->player.position);
    WeatherImpactExposure fireExposure = WeatherImpactExposureAt(
        game->player.position,
        GameEnvironmentSheltered(game->player.position) ? 1.0f : 0.0f,
        water.bodySubmerged ? 1.0f : 0.0f);
    TornadoState tornado = TornadoCurrent();
    TornadoStats tornadoStats = TornadoGetStats();
    LocalClimateState climate = { 0 };
    bool haveClimate = WeatherLocalClimate(game, &climate);
    char genera[256] = "none";
    size_t generaLength = 0u;
    for (int value = WEATHER_CLOUD_GENUS_CIRRUS;
         value < WEATHER_CLOUD_GENUS_COUNT; value++) {
        WeatherCloudGenus genus = (WeatherCloudGenus)value;
        if (!WeatherSampleHasCloudGenus(weather, genus)) continue;
        int written = snprintf(
            genera + generaLength, sizeof(genera) - generaLength,
            "%s%s", generaLength > 0u ? "," : "", WeatherCloudGenusName(genus));
        if (written < 0 || (size_t)written >= sizeof(genera) - generaLength) {
            genera[sizeof(genera) - 1u] = '\0';
            break;
        }
        generaLength += (size_t)written;
    }
    char layers[384] = "none";
    size_t layerLength = 0u;
    for (unsigned index = 0u; index < visual.cloudLayerCount; index++) {
        WeatherCloudVisualLayer layer = visual.cloudLayers[index];
        int written = snprintf(
            layers + layerLength, sizeof(layers) - layerLength,
            "%s%s:%.3f:%.2f:%.2f", layerLength > 0u ? "," : "",
            WeatherCloudGenusName(layer.genus), layer.coverage,
            layer.baseHeight, layer.thickness);
        if (written < 0 || (size_t)written >= sizeof(layers) - layerLength) {
            layers[sizeof(layers) - 1u] = '\0';
            break;
        }
        layerLength += (size_t)written;
    }
    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL weather inspect ok climate=%s phenomenon=%s "
        "temperature_k=%.3f temperature_c=%.3f pressure_atm=%.6f "
        "humidity=%.6f dew_point_k=%.3f wet_bulb_k=%.3f "
        "wind=%.6f gust=%.6f visibility=%.6f rain=%.6f snow=%.6f "
        "sleet=%.6f freezing_rain=%.6f hail=%.6f lightning=%.6f "
        "fog=%.6f dust=%.6f rainbow=%.6f aurora=%.6f "
        "cloud_genus=%s cloud_genera=%s cloud_layers=%u "
        "cloud_layer_data=%s cloud_reference_y=%.3f cloud_forced_frames=%u "
        "forced_frames=%u damage=%d surfaces=%u fires=%u "
        "fire_phase=%s fire_position=%d,%d,%d fire_distance=%.3f "
        "fire_intensity=%.6f fire_fuel=%.6f fire_moisture=%.6f "
        "fire_local_heat=%.6f fire_local_smoke=%.6f "
        "fire_ignitions=%u fire_spread=%u fire_extinctions=%u "
        "fire_suppressions=%u fire_burned_blocks=%u fire_burn_sites=%u "
        "fire_recovered_sites=%u fire_dropped=%u burn_dropped=%u "
        "tornado_active=%d tornado_forced=%d tornado_phase=%s "
        "tornado_center=%.3f,%.3f,%.3f tornado_distance=%.3f "
        "tornado_intensity=%.6f tornado_radius=%.3f "
        "tornado_funnel_height=%.3f tornado_wind_mps=%.3f "
        "tornado_forced_frames=%u tornado_blocks=%u tornado_debris=%u "
        "tornado_dropped=%u\n",
        haveClimate ? ClimateRegimeName(climate.regime) : "unavailable",
        WeatherPhenomenonName(weather.dominantPhenomenon),
        weather.temperatureK, weather.temperatureK - 273.15f,
        weather.pressureAtm, weather.relativeHumidity, weather.dewPointK,
        weather.wetBulbK, weather.wind, weather.gust, weather.visibility,
        weather.rain, weather.snow, weather.sleet, weather.freezingRain,
        weather.hail, weather.lightning, weather.fog, weather.dust,
        weather.rainbow, weather.aurora,
        WeatherCloudGenusName(weather.dominantCloudGenus), genera,
        visual.cloudLayerCount, layers, visual.cloudAltitudeReference,
        WeatherForcedCloudFramesRemaining(),
        WeatherForcedFramesRemaining(),
        WeatherImpactEnabled() ? 1 : 0, impacts.surfaceCount,
        impacts.activeFires,
        haveNearestFire ? WildfirePhaseName(nearestFire.state.phase) :
                          "inactive",
        haveNearestFire ? nearestFire.x : 0,
        haveNearestFire ? nearestFire.y : 0,
        haveNearestFire ? nearestFire.z : 0,
        haveNearestFire ? nearestFireDistance : -1.0f,
        haveNearestFire ? nearestFire.state.intensity : 0.0f,
        haveNearestFire ? nearestFire.state.fuel : 0.0f,
        haveNearestFire ? nearestFire.state.moisture : 0.0f,
        fireExposure.heat, fireExposure.smoke, impacts.ignitions,
        impacts.spreadIgnitions, impacts.extinctions, impacts.suppressions,
        impacts.burnedBlocks, impacts.burnSiteCount,
        impacts.recoveredBurnSites, impacts.droppedIgnitions,
        impacts.droppedBurnSites, tornado.active ? 1 : 0,
        tornado.forced ? 1 : 0, TornadoPhaseName(tornado.phase),
        tornado.center.x, tornado.center.y, tornado.center.z,
        TornadoDistanceTo(game->player.position), tornado.intensity,
        tornado.radius, tornado.funnelHeight, tornado.maximumWindMps,
        TornadoForcedFramesRemaining(), tornadoStats.blockDamageEvents,
        tornadoStats.debrisEmitted, tornadoStats.droppedEffects);
}

GameDebugDispatchResult GameDebugDispatchWeatherCommand(
    GameRuntime *game, DebugControlCommand command)
{
    if (GameDebugDispatchWildfireCommand(game, command)) {
        return GAME_DEBUG_DISPATCH_HANDLED;
    }

    switch (command) {
    case DEBUG_CONTROL_COMMAND_WEATHER_INSPECT:
        GameDebugInspectWeather(game);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_FORCE: {
        WeatherPhenomenon phenomenon;
        if (!WeatherPhenomenonFromName(
                game->debugControl.weatherPhenomenon, &phenomenon) ||
            !WeatherForcePhenomenon(
                phenomenon, game->debugControl.weatherIntensity,
                game->debugControl.weatherFrames)) {
            GameDebugMarkCommandError(game, "invalid_weather_force");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather force error reason=invalid_arguments\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather force ok phenomenon=%s intensity=%.6f "
            "frames=%u\n",
            WeatherPhenomenonName(phenomenon),
            game->debugControl.weatherIntensity,
            game->debugControl.weatherFrames);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_FORCE: {
        WeatherCloudGenus genus;
        if (!WeatherCloudGenusFromName(
                game->debugControl.weatherCloudGenus, &genus) ||
            genus == WEATHER_CLOUD_GENUS_NONE ||
            !WeatherForceCloudGenus(
                genus, game->debugControl.weatherCloudCoverage,
                game->debugControl.weatherCloudFrames)) {
            GameDebugMarkCommandError(game, "invalid_weather_cloud");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather cloud error reason=invalid_arguments\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather cloud ok genus=%s coverage=%.6f frames=%u\n",
            WeatherCloudGenusName(genus),
            game->debugControl.weatherCloudCoverage,
            game->debugControl.weatherCloudFrames);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    case DEBUG_CONTROL_COMMAND_WEATHER_CLOUD_CLEAR:
        WeatherClearForcedCloud();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather cloud clear ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_FORCE:
        if (!TornadoForce(
                game->player.position,
                game->debugControl.weatherTornadoIntensity,
                game->debugControl.weatherTornadoFrames,
                game->debugControl.weatherTornadoDistance,
                WeatherCurrentSample())) {
            GameDebugMarkCommandError(game, "invalid_weather_tornado");
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL weather tornado error reason=invalid_arguments\n");
            return GAME_DEBUG_DISPATCH_HANDLED;
        }
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather tornado ok intensity=%.6f frames=%u "
            "distance=%.3f\n",
            game->debugControl.weatherTornadoIntensity,
            game->debugControl.weatherTornadoFrames,
            game->debugControl.weatherTornadoDistance);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_TORNADO_CLEAR:
        TornadoClear();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather tornado clear ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_CLEAR:
        WeatherClearForced();
        TornadoClear();
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL weather clear ok\n");
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_DAMAGE:
        game->settings.weatherDamageEnabled =
            game->debugControl.weatherDamageEnabled;
        WeatherImpactSetEnabled(game->settings.weatherDamageEnabled);
        TornadoSetDamageEnabled(game->settings.weatherDamageEnabled);
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather damage enabled=%d\n",
            WeatherImpactEnabled() ? 1 : 0);
        return GAME_DEBUG_DISPATCH_HANDLED;
    case DEBUG_CONTROL_COMMAND_WEATHER_STEP: {
        WeatherImpactStepTicks(game->debugControl.weatherTicks,
                               game->player.position,
                               (WeatherImpactWeather){
                                   .sampleAt = WeatherFieldSampleAtWorldTime,
                                   .simulationTime = SpaceElapsedSimulationTime()
                               });
        TornadoStepTicks(game->debugControl.weatherTicks,
                         game->player.position, WeatherCurrentSample());
        WeatherImpactStats stats = WeatherImpactGetStats();
        DebugControlReply(
            &game->debugControl,
            "DEBUG_CONTROL weather step ok ticks=%u total_ticks=%llu "
            "surfaces=%u fires=%u damage_events=%u\n",
            game->debugControl.weatherTicks,
            (unsigned long long)stats.ticks, stats.surfaceCount,
            stats.activeFires, stats.blockDamageEvents);
        return GAME_DEBUG_DISPATCH_HANDLED;
    }
    default:
        return GAME_DEBUG_DISPATCH_UNHANDLED;
    }
}
