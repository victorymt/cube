#include "raylib.h"
#include "raymath.h"

#include "app/game_interaction.h"
#include "presentation/album_ui.h"
#include "presentation/audio.h"
#include "gameplay/discovery.h"
#include "ecology/entity.h"
#include "ecology/evolution_catalog.h"
#include "world/fluid.h"
#include "app/game_runtime.h"
#include "gameplay/interaction.h"
#include "gameplay/inventory.h"
#include "presentation/particles.h"
#include "gameplay/player.h"
#include "presentation/render.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>

bool GameInteractionObserveEvolutionInfo(const EntityEvolutionDebugInfo *info)
{
    if (!info || !info->valid) return false;
    EvolutionCatalogObservation observation = {
        .worldSeed = PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
        .surfaceId = WorldCurrentSurfaceId(),
        .x = (int)floorf(info->positionX),
        .z = (int)floorf(info->positionZ),
        .organismId = info->organismId,
        .lineageId = info->lineageId,
        .speciesId = info->speciesId,
        .motherId = info->motherId,
        .fatherId = info->fatherId,
        .genome = info->genome,
        .phenotype = info->phenotype
    };
    return EvolutionCatalogObserve(&observation);
}

static int GameUpdateInteractionTargets(GameRuntime *game, float dt,
                                        bool inputBlocked,
                                        GameInteractionContext *context)
{
    *context = (GameInteractionContext){ 0 };
    UpdatePlayerCamera(&game->camera, &game->player, dt, game->thirdPerson);
    int effectiveRenderDistance = EffectiveRenderDistanceForHeight(
        game->camera.position.y);
    Vector3 aimEye = game->camera.position;
    if (!WorldIsSpaceActive()) {
        aimEye = (Vector3){
            game->player.position.x,
            game->player.position.y + EYE_HEIGHT,
            game->player.position.z
        };
    }
    Vector3 aimDir = Vector3Normalize(
        Vector3Subtract(game->camera.target, game->camera.position));
    context->hit = RaycastBlocksFiltered(aimEye, aimDir, REACH_DISTANCE,
                                          RAYCAST_BLOCK_SOLID);
    context->interactionHit = RaycastBlocksFiltered(
        aimEye, aimDir, REACH_DISTANCE, RAYCAST_BLOCK_ALL);
    context->entityHit = EntityRayHit(aimEye, aimDir, REACH_DISTANCE);
    if (!inputBlocked && IsKeyPressed(KEY_N)) {
        if (game->evolutionScanLocked) {
            game->evolutionScanLocked = false;
            game->evolutionLockedOrganismId = 0u;
            SetImportMessage("Evolution scan unlocked.");
        } else {
            EntityEvolutionDebugInfo scanInfo = { 0 };
            if (EntityEvolutionInspect(context->entityHit, &scanInfo)) {
                game->evolutionScanLocked =
                    GameInteractionObserveEvolutionInfo(&scanInfo);
                game->evolutionLockedOrganismId = game->evolutionScanLocked
                    ? scanInfo.organismId : 0u;
                SetImportMessage(game->evolutionScanLocked
                    ? "Evolution scan locked; species added to atlas."
                    : "Evolution scan failed.");
            } else {
                SetImportMessage("Aim at an evolvable organism to scan.");
            }
        }
    }
    if (ShipIsDriving() && WorldIsSpaceActive()) {
        context->haveAimBody = SpacePlanetNavigationPick(
            game->player.position, aimDir, &context->aimBody);
    } else {
        context->haveAimBody = SpaceBodyPick(
            aimEye, aimDir, &context->aimBody);
    }
    context->hitParkedShip =
        context->hit.hit &&
        ShipResolveParkedAt(context->hit.x, context->hit.y,
                            context->hit.z, &context->hitShip);
    return effectiveRenderDistance;
}

static void GameHandleLeftInteraction(
    GameRuntime *game, bool inputBlocked,
    const GameInteractionContext *context)
{
    if (!inputBlocked && context->interactionHit.hit &&
        GetBlockAt(context->interactionHit.x, context->interactionHit.y,
                   context->interactionHit.z) == BLOCK_WATER &&
        IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (InventoryCount(BLOCK_WATER) >= INVENTORY_MAX_PER_BLOCK) {
            SetImportMessage("Inventory full: Water");
        } else if (FluidTryCollectUnit(
                       context->interactionHit.x, context->interactionHit.y,
                       context->interactionHit.z)) {
            InventoryAdd(BLOCK_WATER, 1);
            AudioPlayPick();
            SetImportMessage(TextFormat(
                "Collected Water (%d)", InventoryCount(BLOCK_WATER)));
        } else {
            SetImportMessage("Need 255 connected water volume to collect.");
        }
    } else if (!inputBlocked && context->entityHit >= 0 &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        float harvestDaylight = 0.0f;
        float harvestSunset = 0.0f;
        PlanetLightState harvestLight = { 0 };
        if (!PlanetWorldLightStateAt(game->player.position, &harvestLight)) {
            DayNightFactors(game->dayTime, &harvestDaylight, &harvestSunset);
        } else {
            harvestDaylight = harvestLight.daylight;
        }
        EntityKill(context->entityHit, ENTITY_DEATH_PLAYER,
                   harvestDaylight);
    } else if (!inputBlocked && context->hitParkedShip &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        if (InventoryAdd(BLOCK_SPACESHIP, 1) > 0) {
            Vector3 center = {
                (float)context->hitShip.coreX +
                    (context->hitShip.legacy ? 0.5f : 1.0f),
                (float)context->hitShip.coreY + 0.5f,
                (float)context->hitShip.coreZ +
                    (context->hitShip.legacy ? 0.5f : 1.0f)
            };
            ShipRemoveParkedAt(context->hit.x, context->hit.y,
                               context->hit.z, true);
            ParticlesEmitBurst(center, BlockBaseColor(BLOCK_SPACESHIP),
                               20, 3.0f, 0.7f);
            AudioPlayBreak();
        } else {
            SetImportMessage("Inventory full: Spaceship");
        }
    } else if (!inputBlocked && context->hit.hit &&
               IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
               WorldCanAccessBlockY(context->hit.y)) {
        BlockType brokenType = GetBlockAt(context->hit.x, context->hit.y,
                                          context->hit.z);
        PlanetPoi claimedPoi = { 0 };
        bool poiCore = PlanetPoiIsCore(context->hit.x, context->hit.y,
                                       context->hit.z);
        bool poiClaimed = PlanetPoiIsClaimed(context->hit.x, context->hit.y,
                                             context->hit.z);
        if (PlanetPoiTryClaim(context->hit.x, context->hit.y,
                              context->hit.z, &claimedPoi)) {
            ParticlesEmitBurst(
                (Vector3){ context->hit.x + 0.5f,
                           context->hit.y + 0.5f,
                           context->hit.z + 0.5f },
                BlockBaseColor(claimedPoi.rewardBlock), 24, 3.8f, 0.85f);
            AudioPlayBreak();
            SetImportMessage(TextFormat(
                "Survey complete: %s, +%d %s", claimedPoi.name,
                claimedPoi.rewardAmount,
                BlockName(claimedPoi.rewardBlock)));
        } else if (poiClaimed) {
            SetImportMessage("This discovery has already been catalogued.");
        } else if (!poiCore && brokenType != BLOCK_AIR &&
                   InventoryAdd(brokenType, 1) > 0) {
            ParticlesEmitBurst(
                (Vector3){ context->hit.x + 0.5f,
                           context->hit.y + 0.5f,
                           context->hit.z + 0.5f },
                BlockBaseColor(brokenType), 16, 3.0f, 0.7f);
            AudioPlayBreak();
            SetBlock(context->hit.x, context->hit.y, context->hit.z,
                     BLOCK_AIR);
        } else if (!poiCore && brokenType != BLOCK_AIR) {
            SetImportMessage(
                TextFormat("Inventory full: %s", BlockName(brokenType)));
        }
    }
}

static void GamePreparePlacement(GameRuntime *game, bool inputBlocked,
                                 GameInteractionContext *context)
{
    context->placeX = 0;
    context->placeY = 0;
    context->placeZ = 0;
    context->canPlace = false;
    context->placementDirection = ShipDirectionFromYaw(game->player.yaw);
    if (!inputBlocked && context->hit.hit) {
        context->placeX = context->hit.x + context->hit.nx;
        context->placeY = context->hit.y + context->hit.ny;
        context->placeZ = context->hit.z + context->hit.nz;
        if (game->hotbar[game->selectedIndex] == BLOCK_SPACESHIP) {
            context->canPlace =
                InventoryCount(BLOCK_SPACESHIP) > 0 &&
                ShipCanPlaceParked(
                    context->placeX, context->placeY, context->placeZ,
                    context->placementDirection, &game->player);
        } else {
            BlockType selectedType = game->hotbar[game->selectedIndex];
            BlockType targetType = GetBlockAt(
                context->placeX, context->placeY, context->placeZ);
            bool targetAvailable = targetType == BLOCK_AIR ||
                (WorldIsSurfaceActive() && targetType == BLOCK_WATER);
            context->canPlace =
                InventoryCount(game->hotbar[game->selectedIndex]) > 0 &&
                targetAvailable &&
                WorldBlockRegionAt(context->placeY) !=
                    WORLD_BLOCK_REGION_NONE &&
                (selectedType == BLOCK_WATER ||
                 !BlockWouldOverlapPlayer(
                     context->placeX, context->placeY, context->placeZ,
                     game->player.position));
            if (selectedType == BLOCK_WATER) {
                context->canPlace =
                    context->canPlace && WorldIsSurfaceActive() &&
                    WorldBlockRegionAt(context->placeY) ==
                        WORLD_BLOCK_REGION_SURFACE;
            }
        }
    }
}

static void GameOpenAlbumFromWorld(GameRuntime *game)
{
    if (WeatherGetCurrent() == WEATHER_RAIN) {
        game->albumRainSuspended = true;
        AudioSetRain(false);
    }
    AlbumUiOpen();
    game->albumOpen = true;
    game->player.velocity = Vector3Zero();
    game->cursorReleased = false;
    EnableCursor();
}

static void GameUseNetherPortal(GameRuntime *game)
{
    Vector3 landing = game->player.position;
    bool entering = !WorldNetherIsActive();
    WorldSetNetherActive(entering);
    bool foundLanding = false;
    if (entering) {
        landing.y = -46.0f;
        foundLanding = PlayerFindLandingSpot(
            landing, NETHER_LAYER_Y + 1, NETHER_LAYER_TOP - 1, &landing);
        SetImportMessage("Entered the Nether.");
    } else {
        float groundY = (float)TerrainHeight(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), WorldTerrainMode());
        landing.y = groundY + 3.0f;
        int landingMinY = (int)floorf(landing.y) -
                          PLAYER_LANDING_SEARCH_DEPTH;
        if (landingMinY < WorldSurfaceMinY()) {
            landingMinY = WorldSurfaceMinY();
        }
        foundLanding = PlayerFindLandingSpot(
            landing, landingMinY, WorldSurfaceMaxYExclusive() - 1, &landing);
        SetImportMessage("Back to the surface.");
    }
    if (!foundLanding) {
        WorldSetNetherActive(!entering);
        SetImportMessage("Portal destination is obstructed.");
        return;
    }
    game->player.position = landing;
    game->player.velocity = Vector3Zero();
    game->player.floating = false;
    game->wasInSpace = false;
}

static void GameToggleAccessBlock(const HitResult *hit,
                                  BlockType currentType,
                                  BlockType closedType,
                                  BlockType openType)
{
    BlockType replacement = currentType == closedType ? openType : closedType;
    AudioPlayPlace();
    SetBlock(hit->x, hit->y, hit->z, replacement);
}

static void GamePlaceSelectedBlock(GameRuntime *game,
                                   const GameInteractionContext *context)
{
    BlockType placedType = game->hotbar[game->selectedIndex];
    if (placedType == BLOCK_SPACESHIP) {
        if (InventoryConsume(placedType, 1)) {
            if (!ShipPlaceParked(
                    context->placeX, context->placeY, context->placeZ,
                    context->placementDirection, true)) {
                InventoryAdd(placedType, 1);
                SetImportMessage("Spaceship needs a clear 4x4 area.");
            } else {
                ParticlesEmitBurst(
                    (Vector3){ context->placeX + 1.0f,
                               context->placeY + 0.5f,
                               context->placeZ + 1.0f },
                    BlockBaseColor(placedType), 16, 2.5f, 0.6f);
                AudioPlayPlace();
            }
        }
    } else if (placedType == BLOCK_WATER) {
        if (InventoryConsume(placedType, 1)) {
            if (!FluidTryDepositUnit(context->placeX, context->placeY,
                                     context->placeZ)) {
                InventoryAdd(placedType, 1);
                SetImportMessage(
                    "Water needs 255 free loaded neighbor volume.");
            } else {
                ParticlesEmitBurst(
                    (Vector3){ context->placeX + 0.5f,
                               context->placeY + 0.5f,
                               context->placeZ + 0.5f },
                    BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                AudioPlayPlace();
            }
        }
    } else if (InventoryConsume(placedType, 1)) {
        if (!SetBlock(context->placeX, context->placeY, context->placeZ,
                      placedType)) {
            InventoryAdd(placedType, 1);
            SetImportMessage(
                "Cannot place block: water has no loaded escape volume.");
        } else {
            ParticlesEmitBurst(
                (Vector3){ context->placeX + 0.5f,
                           context->placeY + 0.5f,
                           context->placeZ + 0.5f },
                BlockBaseColor(placedType), 8, 2.0f, 0.5f);
            AudioPlayPlace();
        }
    }
}

static void GameHandleRightInteraction(
    GameRuntime *game, bool inputBlocked,
    const GameInteractionContext *context)
{
    if (inputBlocked || !context->hit.hit ||
        !IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        return;
    }
    BlockType targetType = GetBlockAt(context->hit.x, context->hit.y,
                                      context->hit.z);
    if (targetType == BLOCK_ALBUM) {
        GameOpenAlbumFromWorld(game);
    } else if (!ShipIsDriving() &&
               ShipTryEnter(context->hit.x, context->hit.y,
                            context->hit.z, &game->player)) {
    } else if (targetType == BLOCK_NETHER_PORTAL) {
        GameUseNetherPortal(game);
    } else if (targetType == BLOCK_DOOR || targetType == BLOCK_DOOR_OPEN) {
        GameToggleAccessBlock(&context->hit, targetType, BLOCK_DOOR,
                              BLOCK_DOOR_OPEN);
    } else if (targetType == BLOCK_FENCE_GATE ||
               targetType == BLOCK_FENCE_GATE_OPEN) {
        GameToggleAccessBlock(&context->hit, targetType, BLOCK_FENCE_GATE,
                              BLOCK_FENCE_GATE_OPEN);
    } else if (context->canPlace) {
        GamePlaceSelectedBlock(game, context);
    } else if (game->hotbar[game->selectedIndex] == BLOCK_SPACESHIP &&
               InventoryCount(BLOCK_SPACESHIP) > 0) {
        SetImportMessage("Spaceship needs a clear 4x4 area.");
    }
}

static void GameHandleMiddlePick(GameRuntime *game, bool inputBlocked,
                                 const GameInteractionContext *context)
{
    if (inputBlocked || !context->hit.hit ||
        !IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        return;
    }
    BlockType picked = GetBlockAt(context->hit.x, context->hit.y,
                                  context->hit.z);
    if (ShipResolveParkedAt(context->hit.x, context->hit.y,
                            context->hit.z, NULL)) {
        picked = BLOCK_SPACESHIP;
    }
    if (picked != BLOCK_AIR && IsValidBlockType(picked)) {
        game->hotbar[game->selectedIndex] = picked;
        AudioPlayPick();
        SetImportMessage(TextFormat(
            "Picked %s (%d)", BlockName(picked), InventoryCount(picked)));
    }
}

int GameUpdateInteractions(GameRuntime *game, float dt, bool inputBlocked,
                           GameInteractionContext *context)
{
    int effectiveRenderDistance = GameUpdateInteractionTargets(
        game, dt, inputBlocked, context);
    GameHandleLeftInteraction(game, inputBlocked, context);
    GamePreparePlacement(game, inputBlocked, context);
    GameHandleRightInteraction(game, inputBlocked, context);
    GameHandleMiddlePick(game, inputBlocked, context);
    return effectiveRenderDistance;
}
