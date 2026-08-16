#ifndef VOXELCRAFT_GAME_INTERNAL_H
#define VOXELCRAFT_GAME_INTERNAL_H

#include "app/game_runtime.h"
#include "gameplay/player.h"
#include "gameplay/ship.h"
#include "presentation/environment_presentation.h"
#include "presentation/environment_runtime.h"
#include "presentation/weather_visual.h"
#include "space/planet_observation.h"
#include "space/space.h"
#include "world/terrain.h"
#include "world/world_environment.h"
#include "world/world_lighting.h"

typedef struct GameFrameView {
    HitResult hit;
    ParkedShip hitShip;
    SpaceBodyInfo aimBody;
    PlanetLightState planetLight;
    PlanetObservationState planetObservation;
    WeatherVisualState weatherVisual;
    EnvironmentPresentationState environmentPresentation;
    EnvironmentRuntimeSample environmentSample;
    WorldLightingState worldLighting;
    PlayerWaterState playerWater;
    BathymetrySample bathymetry;
    WorldDimension cameraDimension;
    Color skyTop;
    Color skyHorizon;
    Color worldTint;
    double weatherSimulationTime;
    float daylight;
    float planetSeasonProgress;
    float spaceFade;
    float skyFade;
    float dt;
    int effectiveRenderDistance;
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

void GameCaptureScreenshot(GameRuntime *game, const GameFrameView *frame);

#endif
