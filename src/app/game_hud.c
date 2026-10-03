#include "app/game_internal.h"
#include "presentation/render.h"
#include "presentation/render_ui.h"
#include "space/planet_atmosphere.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "space/space_units.h"
#include "world/world.h"

#include "raymath.h"

#include <math.h>
#include <stdio.h>

float GameBuildShipHud(GameRuntime *game, ShipHudState *shipHud,
                              char *systemName, size_t systemNameSize)
{
    float shipSpeed = ShipIsDriving() ? ShipRelativeSpeed() :
                                        Vector3Length(game->player.velocity);
    snprintf(systemName, systemNameSize, "---");
    *shipHud = (ShipHudState){
        .speed = shipSpeed,
        .targetSpeed = ShipTargetSpeed(),
        .closingSpeed = ShipTargetClosingSpeed(),
        .brakingDistance = ShipTargetBrakingDistance(),
        .etaSeconds = ShipTargetEtaSeconds(),
        .atmosphere = -1.0f,
        .systemName = systemName,
        .driveMode = ShipDriveModeName(),
        .approaching = ShipIsApproaching(),
        .supercruising = ShipIsSupercruising(),
        .warping = ShipIsInterstellarWarping()
    };
    if (!ShipIsDriving()) return shipSpeed;

    shipHud->cruising = ShipIsCruising();
    Vector3 gravityDir = Vector3Zero();
    float surfaceDist = 0.0f;
    if (PlanetWorldIsActive()) {
        shipHud->nearPlanet = true;
        shipHud->altitude = game->player.position.y -
            (float)PlanetTerrainHeight(
                (int)floorf(game->player.position.x),
                (int)floorf(game->player.position.z));
        shipHud->atmosphere =
            (float)PlanetAtmosphereAtHeight(PlanetWorldProfile(),
                                           game->player.position.y).densityRatio * 100.0f;
    } else if (HomeWorldSurfaceIsActive()) {
        shipHud->nearPlanet = true;
        shipHud->altitude = game->player.position.y -
            (float)TerrainHeight(
                (int)floorf(game->player.position.x),
                (int)floorf(game->player.position.z), WorldTerrainMode());
        shipHud->atmosphere =
            (float)PlanetAtmosphereAtHeight(NULL,
                                           game->player.position.y).densityRatio * 100.0f;
    } else if (PlanetSurfaceAt(game->player.position, &gravityDir,
                               &surfaceDist, NULL)) {
        shipHud->nearPlanet = true;
        shipHud->altitude = surfaceDist;
    } else {
        shipHud->nearPlanet = false;
        shipHud->altitude = game->player.position.y - (float)SPACE_LAYER_Y;
    }
    if (shipHud->nearPlanet) {
        shipHud->subsurface = shipHud->altitude < -0.5f;
        shipHud->submerged =
            PlayerWaterStateAt(game->player.position).eyesSubmerged;
    }
    shipHud->heading = HudHeadingFromYaw(game->player.yaw);
    SolarSystemDef hudSystem = { 0 };
    float hudDistance = 0.0f;
    if (PlanetWorldIsActive()) {
        snprintf(systemName, systemNameSize, "%s surface", PlanetWorldName());
    } else if (FindNearestSystem(game->player.position,
                                 SOLAR_SYSTEM_QUERY_RADIUS,
                                 &hudSystem, &hudDistance)) {
        double distanceAu = SpaceUnitsGameDistanceToKilometers(hudDistance) /
                            SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
        snprintf(systemName, systemNameSize, "%s (%.3g AU)",
                 hudSystem.name, distanceAu);
    } else {
        snprintf(systemName, systemNameSize, "Deep space");
    }
    return shipSpeed;
}
