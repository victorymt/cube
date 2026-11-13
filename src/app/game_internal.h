#ifndef VOXELCRAFT_GAME_INTERNAL_H
#define VOXELCRAFT_GAME_INTERNAL_H

#include "app/game_runtime.h"
#include "app/game_ecology.h"
#include "app/game_plugins.h"
#include "gameplay/interaction_types.h"
#include "gameplay/camera.h"
#include "gameplay/camera_world.h"
#include "gameplay/player_actor.h"
#include "gameplay/player_input.h"
#include "gameplay/player_motion.h"
#include "gameplay/player_sensors.h"
#include "gameplay/player_types.h"
#include "gameplay/ship.h"
#include "presentation/environment_presentation.h"
#include "presentation/environment_runtime.h"
#include "presentation/weather_visual.h"
#include "presentation/wildfire_renderer.h"
#include "space/planet_observation.h"
#include "space/space_types.h"
#include "world/terrain.h"
#include "world/world_environment.h"
#include "world/world_lighting.h"
#include "world/tornado.h"

static inline bool GameTryEnterShipAt(GameRuntime *game, int x, int y, int z)
{
    if (!game) return false;
    ActorBody ship = PlayerActorBody(&game->player);
    if (!ShipTryEnter(x, y, z, &ship)) return false;
    PlayerApplyActorBody(&game->player, &ship);
    return true;
}

static inline bool GameBeginDebugShip(GameRuntime *game)
{
    if (!game) return false;
    ActorBody ship = PlayerActorBody(&game->player);
    if (!ShipBeginDebugFlight(&ship)) return false;
    PlayerApplyActorBody(&game->player, &ship);
    return true;
}

static inline bool GameSetShipDebugExhaust(GameRuntime *game, float demand)
{
    if (!game) return false;
    ActorBody ship = PlayerActorBody(&game->player);
    return ShipSetDebugExhaust(&ship, demand);
}

static inline void GameEmitShipDust(GameRuntime *game)
{
    if (!game) return;
    ActorBody ship = PlayerActorBody(&game->player);
    ShipEmitTouchdownDust(&ship);
}

typedef struct GameFrameView {
    HitResult hit;
    ParkedShip hitShip;
    SpaceBodyInfo aimBody;
    PlanetLightState planetLight;
    PlanetObservationState planetObservation;
    WeatherVisualState weatherVisual;
    TornadoState tornado;
    EnvironmentPresentationState environmentPresentation;
    EnvironmentRuntimeSample environmentSample;
    WeatherImpactFireSnapshot wildfires[WILDFIRE_RENDER_MAX_FIRES];
    WeatherImpactExposure wildfireExposure;
    WorldLightingState worldLighting;
    PlayerWaterState playerWater;
    BathymetrySample bathymetry;
    WorldDimension cameraDimension;
    Color skyTop;
    Color skyHorizon;
    Color worldTint;
    double weatherSimulationTime;
    double debugUpdateMainCpuMs;
    double debugRenderMainCpuMs;
    double debugWorldSubmitMainCpuMs;
    double debugShadowMainCpuMs;
    double debugEndDrawingMainCpuMs;
    double debugEndDrawingWallMs;
    double debugSimulationMainCpuMs;
    double debugStreamingMainCpuMs;
    double debugInteractionMainCpuMs;
    double debugEnvironmentMainCpuMs;
    double debugAstronomyMainCpuMs;
    double debugEcologyMainCpuMs;
    double debugEcologyPrepareMainCpuMs;
    double debugEcologyStepMainCpuMs;
    double debugSkyMainCpuMs;
    double debugWaterMainCpuMs;
    double debugEnvironmentSampleMainCpuMs;
    double debugEnvironmentPresentMainCpuMs;
    double debugFloraVisualsMainCpuMs;
    double debugEntitiesMainCpuMs;
    unsigned debugSimulationStepCount;
    float daylight;
    float planetSeasonProgress;
    float spaceFade;
    float skyFade;
    float dt;
    int effectiveRenderDistance;
    unsigned wildfireCount;
    int entityHit;
    int placeX;
    int placeY;
    int placeZ;
    bool hasPlaceAddress;
    WorldBlockAddress placeAddress;
    bool localWorldActive;
    bool underwater;
    bool inNether;
    bool hitParkedShip;
    bool haveAimBody;
    bool canPlace;
} GameFrameView;

bool GameEnvironmentSheltered(Vector3 position);
void GameSyncWorldSurfaceSnapshot(uint32_t homeWorldSeed);
void GameInstallWeatherSurfaceLightProvider(void);
void GameUpdateCamera(GameRuntime *game, float dt);
int GameUpdateWorldStreaming(GameRuntime *game, bool *localWorldActive);
void GameRequestWorldStreaming(Vector3 position, Vector3 velocity,
                               int effectiveRenderDistance,
                               unsigned coverageBudget);
void GameUpdateDebugLeftMeshReady(GameRuntime *game);
void GameRenderInteractionGuides(GameRuntime *game, const GameFrameView *frame);
/* Releases the owned cube generation input on surface teardown. */
void GameUpdateFrameEnvironment(GameRuntime *game, GameFrameView *frame);
void GameCaptureScreenshot(GameRuntime *game, const GameFrameView *frame);
struct ShipHudState;
float GameBuildShipHud(GameRuntime *game, struct ShipHudState *shipHud,
                       char *systemName, size_t systemNameSize);

#endif
