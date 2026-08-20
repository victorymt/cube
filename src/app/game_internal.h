#ifndef VOXELCRAFT_GAME_INTERNAL_H
#define VOXELCRAFT_GAME_INTERNAL_H

#include "app/game_runtime.h"
#include "gameplay/player.h"
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
    double debugSimulationMainCpuMs;
    double debugStreamingMainCpuMs;
    double debugInteractionMainCpuMs;
    double debugEnvironmentMainCpuMs;
    double debugAstronomyMainCpuMs;
    double debugEcologyMainCpuMs;
    double debugSkyMainCpuMs;
    double debugWaterMainCpuMs;
    double debugEnvironmentSampleMainCpuMs;
    double debugEnvironmentPresentMainCpuMs;
    double debugFloraVisualsMainCpuMs;
    double debugEntitiesMainCpuMs;
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
    bool localWorldActive;
    bool underwater;
    bool inNether;
    bool hitParkedShip;
    bool haveAimBody;
    bool canPlace;
} GameFrameView;

bool GameWorldSimulationPaused(const GameRuntime *game);
bool GameEnvironmentSheltered(Vector3 position);
void GameUpdateFrameEnvironment(GameRuntime *game, GameFrameView *frame);
void GameCaptureScreenshot(GameRuntime *game, const GameFrameView *frame);

#endif
