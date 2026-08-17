#include "app/game_world_transition.h"

#include "core/config.h"
#include "space/space_chunks.h"
#include "space/space_state.h"
#include "space/space_world_transition.h"
#include "world/chunks.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>

static void GameWorldTransitionUnloadSurfaceData(void)
{
    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
}

static void GameWorldTransitionFinishSurfaceActivation(void)
{
    WorldSetNetherActive(false);
    RebuildTorchList();
    ClearUndoHistory();
}

static Vector3 GameWorldTransitionHomeLandingPosition(void)
{
    Vector3 returnPosition = HomeWorldSurfaceReturnPosition();
    int landingX = (int)floorf(returnPosition.x);
    int landingZ = (int)floorf(returnPosition.z);
    int groundY = 0;
    if (!FindSafeSurfaceLanding(landingX, landingZ, 128, 2,
                                &landingX, &landingZ, &groundY)) {
        groundY = TerrainHeight(landingX, landingZ, WorldTerrainMode());
    }
    return (Vector3){
        (float)landingX + 0.5f,
        (float)groundY + 3.0f,
        (float)landingZ + 0.5f
    };
}

static Vector3 GameWorldTransitionPlanetLandingPosition(void)
{
    int shipX = 0;
    int shipZ = 0;
    if (!FindSafeSurfaceLanding(shipX, shipZ, 128, 3,
                                &shipX, &shipZ, NULL)) {
        shipX = 0;
        shipZ = 0;
    }

    int playerX = shipX + 3;
    int playerZ = shipZ;
    int playerGround = 0;
    if (!FindSafeSurfaceLanding(playerX, playerZ, 16, 0,
                                &playerX, &playerZ, &playerGround)) {
        playerGround = PlanetTerrainHeight(playerX, playerZ);
    }
    return (Vector3){
        (float)playerX + 0.5f,
        (float)playerGround + 2.0f,
        (float)playerZ + 0.5f
    };
}

bool GameWorldTransitionBeginDescent(Player *player, bool homeWorldTarget,
                                     Vector3 *outLandingPosition)
{
    if (outLandingPosition) *outLandingPosition = Vector3Zero();
    if (!player) return false;

    Vector3 landing = Vector3Zero();
    if (homeWorldTarget) {
        if (!HomeWorldCanEnter(player->position)) return false;
        landing = GameWorldTransitionHomeLandingPosition();
        GameWorldTransitionUnloadSurfaceData();
        if (!HomeWorldEnterSurface()) return false;
        GameWorldTransitionFinishSurfaceActivation();

        player->position = (Vector3){
            landing.x, SPACE_ENTER_Y - 2.0f, landing.z - 96.0f
        };
        player->yaw = 0.0f;
        SetImportMessage("Crossing Homeworld upper atmosphere.");
    } else {
        if (PlanetWorldIsActive() || HomeWorldSurfaceIsActive()) return false;
        SpaceBodyInfo body;
        if (!PlanetWorldLandingTarget(player->position, &body)) return false;
        Vector3 approachPosition = player->position;

        GameWorldTransitionUnloadSurfaceData();
        if (!PlanetWorldEnterSurface(&body, approachPosition)) return false;
        GameWorldTransitionFinishSurfaceActivation();
        landing = GameWorldTransitionPlanetLandingPosition();

        float entryAngle = (float)(PlanetWorldSeed() % 6283u) * 0.001f;
        Vector3 forward = { sinf(entryAngle), 0.0f, cosf(entryAngle) };
        player->position = Vector3Subtract(
            landing, Vector3Scale(forward, 96.0f));
        player->position.y = PlanetWorldAtmosphereEntryHeight();
        player->yaw = atan2f(forward.x, forward.z);
        SetImportMessage(TextFormat("Crossing %s upper atmosphere.",
                                    PlanetWorldName()));
    }

    player->velocity = Vector3Zero();
    player->pitch = -0.62f;
    player->floating = true;
    player->onGround = false;
    if (outLandingPosition) *outLandingPosition = landing;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    return true;
}

bool GameWorldTransitionTryLaunch(Player *player)
{
    if (!player) return false;
    SpaceTravelPose surfacePose = {
        .position = player->position,
        .velocity = player->velocity,
        .yaw = player->yaw,
        .pitch = player->pitch
    };
    SpaceTravelPose spacePose = { 0 };
    bool fromPlanet = PlanetWorldIsActive();
    char planetName[32] = { 0 };
    bool canLaunch = false;
    if (fromPlanet) {
        snprintf(planetName, sizeof(planetName), "%s", PlanetWorldName());
        canLaunch = PlanetWorldLaunchTarget(&surfacePose, &spacePose);
    } else {
        canLaunch = HomeWorldLaunchTarget(&surfacePose, &spacePose);
    }
    if (!canLaunch) return false;

    DrainChunkGen();
    UnloadAllChunks();
    if (fromPlanet) PlanetWorldLeaveSurface();
    else HomeWorldLeaveSurface(surfacePose.position);
    RebuildTorchList();
    ClearUndoHistory();

    player->position = spacePose.position;
    player->velocity = spacePose.velocity;
    player->yaw = spacePose.yaw;
    player->pitch = spacePose.pitch;
    player->floating = false;
    player->onGround = false;
    if (fromPlanet) {
        SetImportMessage(TextFormat("Left %s atmosphere.", planetName));
    } else {
        SetImportMessage(
            "Left Homeworld atmosphere. Spaceflight is now three-dimensional.");
    }
    return true;
}
