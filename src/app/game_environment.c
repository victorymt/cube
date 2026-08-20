#include "app/game_internal.h"
#include "app/game_debug_trace.h"

#include "core/game_effects.h"
#include "core/perf.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "presentation/audio.h"
#include "presentation/effect_dispatch.h"
#include "presentation/render.h"
#include "space/planet_profile.h"
#include "space/space_state.h"
#include "world/chunks.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/tornado.h"
#include "world/world.h"
#include "world/world_lighting.h"

#include "raymath.h"

#include <math.h>

bool GameEnvironmentSheltered(Vector3 position)
{
    if (!WorldIsSurfaceActive()) return false;
    int x = (int)floorf(position.x);
    int z = (int)floorf(position.z);
    int startY = (int)floorf(position.y) + 1;
    for (int y = startY; y <= startY + 10; y++) {
        BlockType block = GetBlockAt(x, y, z);
        if (block != BLOCK_AIR && !IsLiquidBlock(block)) return true;
    }
    return false;
}

static bool EnvironmentNearWater(Vector3 position)
{
    if (!WorldIsSurfaceActive()) return false;
    int centerX = (int)floorf(position.x);
    int centerY = (int)floorf(position.y);
    int centerZ = (int)floorf(position.z);
    for (int z = centerZ - 5; z <= centerZ + 5; z += 2) {
        for (int x = centerX - 5; x <= centerX + 5; x += 2) {
            for (int y = centerY - 2; y <= centerY + 1; y++) {
                if (IsWaterBlock(GetBlockAt(x, y, z))) return true;
            }
        }
    }
    return false;
}

static float GameSampleFrameAstronomy(GameRuntime *game,
                                      GameFrameView *frame)
{
    float sunset = 0.0f;
    frame->planetLight = (PlanetLightState){ 0 };
    if (!PlanetWorldLightStateAt(game->player.position,
                                 &frame->planetLight)) {
        DayNightFactors(game->dayTime, &frame->daylight, &sunset);
    } else {
        frame->daylight = frame->planetLight.daylight;
        sunset = frame->planetLight.sunset;
    }
    frame->planetObservation =
        PlanetObservationForCamera(&game->camera, &frame->planetLight);
    frame->weatherSimulationTime = SpacePeriodicSimulationTime(
        SpaceElapsedSimulationTime());
    frame->weatherVisual = WeatherVisualStateAtWorld(
        game->camera.position, frame->weatherSimulationTime,
        frame->daylight);
    frame->tornado = TornadoCurrent();
    frame->planetSeasonProgress = -1.0f;
    if (PlanetWorldIsActive()) {
        const PlanetProfile *profile = PlanetWorldProfile();
        float radius = fmaxf(profile->spaceProxyRadius, 24.0f);
        float latitude = game->player.position.z / (radius * 0.82f);
        PlanetSeasonState season = { 0 };
        if (PlanetSeasonEvaluate(
                profile, latitude,
                SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
                &season)) {
            frame->planetSeasonProgress = season.seasonAngle / (2.0f * PI);
        }
    }
    return sunset;
}

static void GameAdvanceFrameSimulation(GameRuntime *game,
                                       GameFrameView *frame)
{
    if (!GameWorldSimulationPaused(game)) {
        double floraStarted = game->debugTraceEnabled
            ? GameDebugTraceMainCpuNowMs() : 0.0;
        ChunksUpdateEcologyVisuals(frame->dt, frame->daylight);
        if (game->debugTraceEnabled) {
            frame->debugFloraVisualsMainCpuMs =
                GameDebugTraceMainCpuNowMs() - floraStarted;
        }
    }
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->importDialog.open &&
        !game->landingTransition.active && frame->localWorldActive) {
        double entitiesStarted = game->debugTraceEnabled
            ? GameDebugTraceMainCpuNowMs() : 0.0;
        EntitiesUpdate(frame->dt, &game->player, frame->daylight);
        if (game->debugTraceEnabled) {
            frame->debugEntitiesMainCpuMs =
                GameDebugTraceMainCpuNowMs() - entitiesStarted;
        }
    }
    EffectDispatchPending();
}

static void GameDeriveFrameSky(GameRuntime *game, GameFrameView *frame,
                               float sunset)
{
    frame->spaceFade = HomeWorldSpaceFade(game->camera.position);
    SkyColorsForLight(frame->daylight, sunset, &frame->skyTop,
                      &frame->skyHorizon);
    frame->worldTint =
        MixWeather(WorldTintForLight(frame->daylight, sunset),
                   frame->daylight, &frame->weatherVisual);
    frame->skyTop = MixWeather(frame->skyTop, frame->daylight,
                               &frame->weatherVisual);
    frame->skyHorizon = MixWeather(frame->skyHorizon, frame->daylight,
                                   &frame->weatherVisual);
    ApplyPlanetWorldPaletteWithObservation(
        &frame->skyTop, &frame->skyHorizon, &frame->worldTint,
        &frame->planetLight, &frame->planetObservation);
    float planetAtmosphereFade =
        PlanetWorldAtmosphereFade(game->camera.position);
    frame->skyFade = fmaxf(frame->spaceFade, planetAtmosphereFade);
    UpdatePlanetSceneExposure(&game->camera);
    frame->skyTop = ColorLerp(frame->skyTop, BLACK, frame->skyFade);
    frame->skyHorizon = ColorLerp(
        frame->skyHorizon, BLACK, frame->skyFade);
    frame->worldTint = ColorLerp(
        frame->worldTint, (Color){ 46, 54, 78, 255 }, frame->skyFade);

    frame->cameraDimension =
        WorldCurrentDimensionAt(game->camera.position.y);
    frame->inNether = frame->cameraDimension == WORLD_DIMENSION_NETHER;
    if (frame->inNether) {
        frame->skyTop = (Color){ 24, 6, 6, 255 };
        frame->skyHorizon = (Color){ 40, 10, 8, 255 };
        frame->worldTint = (Color){ 150, 62, 42, 255 };
        frame->spaceFade = 0.0f;
    }
}

static float GameSampleFrameWater(GameRuntime *game, GameFrameView *frame)
{
    frame->playerWater = PlayerWaterStateAt(game->player.position);
    frame->underwater =
        frame->playerWater.eyesSubmerged &&
        IsWaterBlock(GetBlockAt((int)floorf(game->camera.position.x),
                                (int)floorf(game->camera.position.y),
                                (int)floorf(game->camera.position.z)));
    float underwaterDepth =
        frame->underwater ? frame->playerWater.eyeDepth : 0.0f;
    if (frame->underwater) {
        float deep = Clamp(
            underwaterDepth / UNDERWATER_DEEP_REFERENCE_DEPTH, 0.0f, 1.0f);
        frame->skyHorizon =
            WorldLightingUnderwaterFogColor(underwaterDepth);
        frame->skyTop = ColorLerp(frame->skyHorizon,
                                  (Color){ 3, 18, 30, 255 },
                                  0.28f + deep * 0.42f);
    }
    return underwaterDepth;
}

static void GameBuildFrameEnvironmentSample(GameRuntime *game,
                                            GameFrameView *frame,
                                            float sunset,
                                            float underwaterDepth)
{
    EnvironmentScene environmentScene =
        EnvironmentSceneForDimension(frame->cameraDimension);
    bool forest = false;
    if (environmentScene == ENVIRONMENT_SCENE_HOME) {
        forest = BiomeAt((int)floorf(game->player.position.x),
                         (int)floorf(game->player.position.z)) == BIOME_FOREST;
    } else if (environmentScene == ENVIRONMENT_SCENE_PLANET) {
        forest = PlanetBiomeAt((int)floorf(game->player.position.x),
                               (int)floorf(game->player.position.z)) ==
                 PLANET_BIOME_FOREST;
    }
    bool sheltered = GameEnvironmentSheltered(game->camera.position);
    frame->wildfireCount = 0u;
    frame->wildfireExposure = (WeatherImpactExposure){
        .nearestDistance = INFINITY
    };
    if (frame->localWorldActive && !frame->inNether) {
        frame->wildfireCount = WeatherImpactCollectFires(
            game->camera.position, 190.0f, frame->wildfires,
            WILDFIRE_RENDER_MAX_FIRES);
        frame->wildfireExposure = WeatherImpactExposureAt(
            game->camera.position, sheltered ? 1.0f : 0.0f,
            frame->underwater ? 1.0f : 0.0f);
    }
    frame->environmentSample = (EnvironmentRuntimeSample){
        .dimension = frame->cameraDimension,
        .quality = game->settings.graphicsQuality,
        .weather = frame->weatherVisual,
        .simulationTime = frame->weatherSimulationTime,
        .daylight = frame->daylight,
        .sunset = sunset,
        .atmosphereFade = frame->skyFade,
        .altitude = game->camera.position.y -
                    (float)WorldSurfaceHeightAt(
                        (int)floorf(game->camera.position.x),
                        (int)floorf(game->camera.position.z)),
        .underwaterDepth = underwaterDepth,
        .underwater = frame->underwater,
        .sheltered = sheltered,
        .forest = forest,
        .nearWater = EnvironmentNearWater(game->camera.position),
        .shipInterior = environmentScene == ENVIRONMENT_SCENE_SPACE &&
                        ShipIsDriving(),
        .tornado = frame->tornado,
        .tornadoDistance = TornadoDistanceTo(game->camera.position),
        .tornadoExposure = TornadoForceAt(game->camera.position).exposure,
        .fireHeatExposure = frame->wildfireExposure.heat,
        .fireSmokeExposure = frame->wildfireExposure.smoke,
        .nearestFireDistance = frame->wildfireExposure.nearestDistance
    };
    frame->bathymetry = (BathymetrySample){
        .seaLevel = -1,
        .seabedY = (int)floorf(game->player.position.y),
        .waterDepth = 0,
        .zone = BATHYMETRY_ZONE_LAND,
        .material = BATHYMETRY_MATERIAL_ROCK
    };
    if (PlanetWorldIsActive()) {
        frame->bathymetry = PlanetBathymetryAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z));
    } else if (HomeWorldSurfaceIsActive()) {
        frame->bathymetry = TerrainBathymetryAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), WorldTerrainMode());
    }
}

static void GamePresentFrameEnvironment(GameRuntime *game,
                                        GameFrameView *frame, float sunset)
{
    frame->environmentPresentation = EnvironmentPresentationRuntimeUpdate(
        &game->environment, &frame->environmentSample, frame->dt);
    AudioEnvironmentState audioEnvironment =
        AudioEnvironmentFromPresentation(&frame->environmentPresentation);
    if (game->albumOpen || game->importDialog.open ||
        game->screen != SCREEN_PLAYING) {
        audioEnvironment = (AudioEnvironmentState){ 0 };
    }
    AudioSetEnvironment(&audioEnvironment);
    AudioUpdate(frame->dt);
    frame->worldLighting = WorldLightingForScene(
        &game->camera, game->dayTime, frame->daylight, sunset,
        &frame->planetLight, &frame->weatherVisual, frame->skyHorizon,
        frame->inNether, &frame->environmentPresentation);
    PerfSetMetadata(WorldGetSeed(), frame->effectiveRenderDistance);
    PerfMarkUpdateComplete();
}

void GameUpdateFrameEnvironment(GameRuntime *game, GameFrameView *frame)
{
    double stageStarted = game->debugTraceEnabled
        ? GameDebugTraceMainCpuNowMs() : 0.0;
    float sunset = GameSampleFrameAstronomy(game, frame);
    if (game->debugTraceEnabled) {
        double now = GameDebugTraceMainCpuNowMs();
        frame->debugAstronomyMainCpuMs = now - stageStarted;
        stageStarted = now;
    }
    GameAdvanceFrameSimulation(game, frame);
    if (game->debugTraceEnabled) {
        double now = GameDebugTraceMainCpuNowMs();
        frame->debugEcologyMainCpuMs = now - stageStarted;
        stageStarted = now;
    }
    GameDeriveFrameSky(game, frame, sunset);
    if (game->debugTraceEnabled) {
        double now = GameDebugTraceMainCpuNowMs();
        frame->debugSkyMainCpuMs = now - stageStarted;
        stageStarted = now;
    }
    float underwaterDepth = GameSampleFrameWater(game, frame);
    if (game->debugTraceEnabled) {
        double now = GameDebugTraceMainCpuNowMs();
        frame->debugWaterMainCpuMs = now - stageStarted;
        stageStarted = now;
    }
    GameBuildFrameEnvironmentSample(
        game, frame, sunset, underwaterDepth);
    if (game->debugTraceEnabled) {
        double now = GameDebugTraceMainCpuNowMs();
        frame->debugEnvironmentSampleMainCpuMs = now - stageStarted;
        stageStarted = now;
    }
    GamePresentFrameEnvironment(game, frame, sunset);
    if (game->debugTraceEnabled) {
        frame->debugEnvironmentPresentMainCpuMs =
            GameDebugTraceMainCpuNowMs() - stageStarted;
    }
}
