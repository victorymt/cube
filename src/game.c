#include "raylib.h"
#include "raymath.h"

#include "game.h"
#include "game_debug.h"
#include "game_interaction.h"
#include "game_runtime.h"
#include "types.h"
#include "terrain.h"
#include "world.h"
#include "world_extension.h"
#include "block_atlas.h"
#include "chunks.h"
#include "player.h"
#include "interaction.h"
#include "album.h"
#include "inventory.h"
#include "render.h"
#include "particles.h"
#include "audio.h"
#include "weather.h"
#include "space.h"
#include "space_units.h"
#include "world_environment.h"
#include "ship.h"
#include "nether.h"
#include "entity.h"
#include "fluid.h"
#include "evolution_catalog.h"
#include "starmap.h"
#include "homeworld_map.h"
#include "discovery.h"
#include "ecology.h"
#include "perf.h"
#include "world_renderer.h"
#include "world_lighting.h"
#include "environment_presentation.h"
#include "environment_runtime.h"
#include "game_settings.h"
#include "screenshot.h"
#include "debug_control.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static ScreenshotVector3 ScreenshotVector(Vector3 value)
{
    return (ScreenshotVector3){ value.x, value.y, value.z };
}

static const char *EvolutionModuleName(CreatureModuleType type)
{
    switch (type) {
    case CREATURE_MODULE_TORSO: return "TORSO";
    case CREATURE_MODULE_HEAD: return "HEAD";
    case CREATURE_MODULE_LIMB: return "LIMB";
    case CREATURE_MODULE_FOOT: return "FOOT";
    case CREATURE_MODULE_WING: return "WING";
    case CREATURE_MODULE_FIN: return "FIN";
    case CREATURE_MODULE_TAIL: return "TAIL";
    case CREATURE_MODULE_SENSOR: return "SENSOR";
    case CREATURE_MODULE_ARMOR: return "ARMOR";
    default: return "UNKNOWN";
    }
}

static void EvolutionChildrenText(const EvolutionCatalogIndividual *individual,
                                  unsigned first, unsigned count,
                                  char *text, size_t textSize)
{
    if (!text || textSize == 0u) return;
    text[0] = '\0';
    if (!individual || first >= individual->childCount) {
        snprintf(text, textSize, "none");
        return;
    }
    unsigned end = first + count;
    if (end > individual->childCount) end = individual->childCount;
    size_t used = 0u;
    for (unsigned child = first; child < end && used < textSize; child++) {
        int written = snprintf(text + used, textSize - used, "%s%08X",
                               child == first ? "" : " ",
                               individual->childIds[child]);
        if (written < 0 || (size_t)written >= textSize - used) break;
        used += (size_t)written;
    }
}

static bool EnvironmentSheltered(Vector3 position)
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

static bool GameWorldSimulationPaused(const GameRuntime *game)
{
    return game && (game->paused || HomeWorldMapIsOpen());
}

static bool NewWorldSpawnCandidate(int x, int z, TerrainMode mode,
                                   Vector3 *outPosition)
{
    int height = TerrainHeight(x, z, mode);
    int seaLevel = TerrainSeaLevel(mode);
    if ((seaLevel >= 0 && height < seaLevel) || ShouldPlaceTree(x, z, mode)) {
        return false;
    }
    if (height < SURFACE_MIN_Y ||
        height + 3 >= SURFACE_MAX_Y_EXCLUSIVE) return false;
    if (outPosition) {
        *outPosition = (Vector3){ (float)x + 0.5f, (float)height + 1.01f,
                                  (float)z + 0.5f };
    }
    return true;
}

static Vector3 FindNewWorldSpawn(TerrainMode mode)
{
    const int searchRadius = 512;
    Vector3 candidate = { 0 };
    for (int radius = 0; radius <= searchRadius; radius++) {
        if (radius == 0) {
            if (NewWorldSpawnCandidate(0, 0, mode, &candidate)) return candidate;
            continue;
        }
        for (int offset = -radius; offset <= radius; offset++) {
            if (NewWorldSpawnCandidate(offset, -radius, mode, &candidate) ||
                NewWorldSpawnCandidate(radius, offset, mode, &candidate) ||
                NewWorldSpawnCandidate(-offset, radius, mode, &candidate) ||
                NewWorldSpawnCandidate(-radius, -offset, mode, &candidate)) {
                return candidate;
            }
        }
    }

    int seaLevel = TerrainSeaLevel(mode);
    int platformY = seaLevel >= 0 ? seaLevel + 1 : TerrainHeight(0, 0, mode) + 1;
    for (int z = -1; z <= 1; z++) {
        for (int x = -1; x <= 1; x++) {
            SetBlockNoUndo(x, platformY, z, BLOCK_PLANK);
            SetBlockNoUndo(x, platformY + 1, z, BLOCK_AIR);
            SetBlockNoUndo(x, platformY + 2, z, BLOCK_AIR);
        }
    }
    return (Vector3){ 0.5f, (float)platformY + 1.01f, 0.5f };
}

static void ApplyPerfRoute(Player *player, int frame)
{
    const float dt = 1.0f / 60.0f;
    Vector3 previous = player->position;
    float x = 0.5f;
    float z = 0.5f;
    if (frame >= 120 && !PerfRouteComplete()) {
        int phase = (frame - 120) % 300;
        if (phase < 150) {
            x += (float)phase * 0.78f;
            z += (float)phase * 0.39f;
        } else {
            int returning = phase - 150;
            x += 117.0f - (float)returning * 0.78f;
            z += 58.5f - (float)returning * 0.39f;
        }
    }
    player->position = (Vector3){ x,
        (float)TerrainHeight((int)floorf(x), (int)floorf(z),
                             WorldTerrainMode()) + 3.0f, z };
    Vector3 delta = Vector3Scale(Vector3Subtract(player->position, previous), 1.0f / dt);
    player->velocity = delta;
    if (Vector3LengthSqr(delta) > 0.001f) player->yaw = atan2f(delta.x, delta.z);
    player->pitch = -0.18f;
    player->onGround = true;
    player->floating = false;
}

static RenderResourceSnapshot CurrentRenderResourceSnapshot(void)
{
    RenderResourceSnapshot snapshot = ChunksGetRenderResourceSnapshot();
    RenderResourceSnapshotMerge(&snapshot, SpaceGetRenderResourceSnapshot());
    RenderResourceSnapshotMerge(&snapshot, NetherGetRenderResourceSnapshot());
    snapshot.worldLightingTextureBytes = WorldRendererTextureBytes();
    return snapshot;
}

#define LANDING_TRANSITION_DURATION 9.6f
#define LANDING_TRANSITION_COMMIT_TIME 3.15f
#define LANDING_TRANSITION_TOUCHDOWN_TIME 8.55f
#define LANDING_MIN_APPROACH_DISTANCE 0.012f
#define LANDING_SUMMARY_DURATION 7.0f

static float LandingEase(float value)
{
    float t = Clamp(value, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static const char *LandingHomeBiomeName(Biome biome)
{
    switch (biome) {
    case BIOME_FOREST: return "Forest";
    case BIOME_DESERT: return "Desert";
    case BIOME_SNOW: return "Snowfield";
    case BIOME_MOUNTAIN: return "Mountain";
    case BIOME_PLAINS:
    default: return "Plains";
    }
}

static void LandingTransitionCaptureSummary(LandingTransition *transition,
                                            const Player *player)
{
    int landingX = (int)floorf(player->position.x);
    int landingZ = (int)floorf(player->position.z);
    if (PlanetWorldIsActive()) {
        transition->targetGravity = PlanetWorldGravityScale();
        snprintf(transition->targetName, sizeof(transition->targetName), "%s", PlanetWorldName());
        snprintf(transition->landingPoint, sizeof(transition->landingPoint),
                 "X %+04d   Z %+04d   ALT %d", landingX, landingZ,
                 (int)floorf(player->position.y));
        const PlanetProfile *profile = PlanetWorldProfile();
        snprintf(transition->environment, sizeof(transition->environment),
                 "BIOME %s  |  %s  |  AGE %.1f GYR",
                 PlanetBiomeName(PlanetBiomeAt(landingX, landingZ)),
                 PlanetAtmosphereName(profile->atmosphereType), profile->ageGyr);
        snprintf(transition->biosphere, sizeof(transition->biosphere),
                 "BIOSPHERE %s / %s / %s", PlanetEcologyBiomassName(),
                 PlanetEcologyChemistryName(), PlanetEcologyBodyPlanName());
    } else {
        snprintf(transition->targetName, sizeof(transition->targetName), "HOMEWORLD");
        snprintf(transition->landingPoint, sizeof(transition->landingPoint),
                 "X %+04d   Z %+04d   ALT %d", landingX, landingZ,
                 (int)floorf(player->position.y));
        snprintf(transition->environment, sizeof(transition->environment),
                 "BIOME %s  |  BREATHABLE ATMOSPHERE",
                 LandingHomeBiomeName(BiomeAt(landingX, landingZ)));
        snprintf(transition->biosphere, sizeof(transition->biosphere),
                 "BIOSPHERE NATIVE / CARBON / DIVERSE");
    }
    if (transition->targetGravity < 0.05f) transition->targetGravity = 0.05f;
}

static bool LandingTransitionBegin(LandingTransition *transition, Player *player)
{
    Vector3 center = Vector3Zero();
    float radius = 0.0f;
    float gravity = 1.0f;
    bool homeWorldTarget = HomeWorldCanEnter(player->position);
    SpaceBodyInfo body = { 0 };
    if (homeWorldTarget) {
        center = HomeWorldCenter();
        radius = (float)SpaceUnitsKilometersToGameDistance(
            SPACE_UNITS_EARTH_RADIUS_KM);
    } else if (PlanetWorldLandingTarget(player->position, &body)) {
        center = body.center;
        radius = body.physicalRadiusGame;
        gravity = body.profile.surfaceGravity;
    } else {
        return false;
    }

    Vector3 outward = Vector3Subtract(player->position, center);
    float startDistance = Vector3Length(outward);
    if (startDistance < 0.001f) outward = (Vector3){ 0.0f, 1.0f, 0.0f };
    else outward = Vector3Scale(outward, 1.0f / startDistance);
    Vector3 inward = Vector3Negate(outward);
    float targetDistance = radius +
        fmaxf(radius * 12.0f, LANDING_MIN_APPROACH_DISTANCE);

    *transition = (LandingTransition){
        .active = true,
        .committed = false,
        .landed = false,
        .homeWorldTarget = homeWorldTarget,
        .elapsed = 0.0f,
        .duration = LANDING_TRANSITION_DURATION,
        .summaryRemaining = 0.0f,
        .targetGravity = fmaxf(gravity, 0.05f),
        .targetRadius = radius,
        .startDistance = startDistance,
        .targetDistance = targetDistance,
        .startYaw = player->yaw,
        .startPitch = player->pitch,
        .targetYaw = atan2f(inward.x, inward.z),
        .targetPitch = asinf(Clamp(inward.y, -1.0f, 1.0f)),
        .atmosphereDensity = homeWorldTarget ? 0.85f : body.profile.atmosphereDensity,
        .atmosphereType = homeWorldTarget ? PLANET_ATMOSPHERE_BREATHABLE :
                                            body.profile.atmosphereType,
        .targetCenter = center,
        .outward = outward
    };
    if (homeWorldTarget) {
        snprintf(transition->targetName, sizeof(transition->targetName),
                 "Earth");
    } else {
        snprintf(transition->targetName, sizeof(transition->targetName), "%s",
                 body.name);
    }
    player->velocity = Vector3Zero();
    SetImportMessage(TextFormat("Descent initiated: %s.", transition->targetName));
    return true;
}

static float LandingLerpAngle(float from, float to, float amount)
{
    float delta = fmodf(to - from + PI, 2.0f * PI);
    if (delta < 0.0f) delta += 2.0f * PI;
    delta -= PI;
    return from + delta * amount;
}

static bool LandingTransitionCommit(LandingTransition *transition, Player *player)
{
    if (transition->committed) return true;

    player->position = Vector3Add(transition->targetCenter,
                                  Vector3Scale(transition->outward,
                                               transition->targetDistance));
    player->velocity = Vector3Zero();
    Vector3 landingPosition = Vector3Zero();
    bool enteredAtmosphere = transition->homeWorldTarget ?
                             HomeWorldBeginDescent(player, &landingPosition) :
                             PlanetWorldBeginDescent(player, &landingPosition);
    if (!enteredAtmosphere || !WorldIsSurfaceActive()) {
        transition->active = false;
        transition->summaryRemaining = 0.0f;
        SetImportMessage("Descent aborted: landing target moved out of range.");
        return false;
    }

    transition->committed = true;
    transition->atmosphereStart = player->position;
    transition->landingPosition = landingPosition;
    return true;
}

static void LandingTransitionFinishLanding(LandingTransition *transition, Player *player)
{
    if (transition->landed) return;

    player->position = transition->landingPosition;
    player->velocity = Vector3Zero();
    player->pitch = -0.12f;
    player->floating = false;
    player->onGround = false;
    if (!ShipExit(player)) {
        player->floating = true;
        transition->active = false;
        transition->summaryRemaining = 0.0f;
        return;
    }

    Vector3 besideShip = transition->landingPosition;
    besideShip.x += cosf(player->yaw) * 2.25f;
    besideShip.z -= sinf(player->yaw) * 2.25f;
    int landingMinY = (int)floorf(besideShip.y) -
                      PLAYER_LANDING_SEARCH_DEPTH;
    if (landingMinY < WorldSurfaceMinY()) {
        landingMinY = WorldSurfaceMinY();
    }
    if (PlayerFindLandingSpot(
            besideShip, landingMinY, WorldSurfaceMaxYExclusive() - 1,
            &besideShip)) {
        player->position = besideShip;
    }
    player->velocity = Vector3Zero();
    player->floating = false;
    player->onGround = false;
    transition->landed = true;
    LandingTransitionCaptureSummary(transition, player);
}

static bool LandingTransitionUpdate(LandingTransition *transition, Player *player, float dt)
{
    if (!transition->active) {
        if (transition->summaryRemaining > 0.0f) {
            transition->summaryRemaining = fmaxf(0.0f,
                                                   transition->summaryRemaining - dt);
        }
        return false;
    }

    bool skipPressed = IsKeyPressed(KEY_E) || IsKeyPressed(KEY_ESCAPE);
    if (skipPressed) transition->elapsed = transition->duration;
    else transition->elapsed += dt;

    if (!transition->committed) {
        float linearDescent = Clamp(transition->elapsed /
                                    LANDING_TRANSITION_COMMIT_TIME, 0.0f, 1.0f);
        float descent = LandingEase(linearDescent);
        float distance = Lerp(transition->startDistance,
                              transition->targetDistance, descent);
        player->position = Vector3Add(transition->targetCenter,
                                      Vector3Scale(transition->outward, distance));
        float look = LandingEase(transition->elapsed / 0.85f);
        player->yaw = LandingLerpAngle(transition->startYaw,
                                       transition->targetYaw, look);
        player->pitch = Lerp(transition->startPitch,
                             transition->targetPitch, look);
        float easeRate = 6.0f * linearDescent * (1.0f - linearDescent) /
                         LANDING_TRANSITION_COMMIT_TIME;
        float radialSpeed = (transition->targetDistance - transition->startDistance) *
                            easeRate;
        player->velocity = Vector3Scale(transition->outward, radialSpeed);

        if (transition->elapsed >= LANDING_TRANSITION_COMMIT_TIME &&
            !LandingTransitionCommit(transition, player)) {
            return skipPressed;
        }
    }

    if (transition->committed && !transition->landed) {
        float linearAtmosphere = Clamp(
            (transition->elapsed - LANDING_TRANSITION_COMMIT_TIME) /
            (LANDING_TRANSITION_TOUCHDOWN_TIME - LANDING_TRANSITION_COMMIT_TIME),
            0.0f, 1.0f);
        float descent = LandingEase(linearAtmosphere);
        player->position = Vector3Lerp(transition->atmosphereStart,
                                       transition->landingPosition, descent);
        player->pitch = Lerp(-0.62f, -0.12f, LandingEase(linearAtmosphere));
        float easeRate = 6.0f * linearAtmosphere * (1.0f - linearAtmosphere) /
                         (LANDING_TRANSITION_TOUCHDOWN_TIME -
                          LANDING_TRANSITION_COMMIT_TIME);
        player->velocity = Vector3Scale(
            Vector3Subtract(transition->landingPosition,
                            transition->atmosphereStart),
            easeRate);
        if (transition->elapsed >= LANDING_TRANSITION_TOUCHDOWN_TIME) {
            LandingTransitionFinishLanding(transition, player);
            if (!transition->active) return skipPressed;
        }
    }

    if (transition->elapsed >= transition->duration) {
        if (!transition->committed && !LandingTransitionCommit(transition, player)) {
            return skipPressed;
        }
        if (!transition->landed) LandingTransitionFinishLanding(transition, player);
        if (transition->landed) {
            transition->active = false;
            transition->summaryRemaining = LANDING_SUMMARY_DURATION;
            SetImportMessage(TextFormat("Touchdown complete: %s.", transition->targetName));
        }
    }
    return skipPressed;
}

static void DrawLandingTransitionOverlay(const LandingTransition *transition)
{
    if (!transition->active && transition->summaryRemaining <= 0.0f) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    if (transition->active) {
        float t = transition->elapsed;
        float progress = Clamp(t / transition->duration, 0.0f, 1.0f);
        float switchCover = LandingEase(1.0f - Clamp(fabsf(t - LANDING_TRANSITION_COMMIT_TIME) /
                                                       0.82f, 0.0f, 1.0f));
        float blackout = 0.08f + switchCover * 0.24f;
        DrawRectangle(0, 0, sw, sh, Fade(BLACK, Clamp(blackout, 0.0f, 0.92f)));

        const char *stage = "ORBITAL APPROACH";
        float stageStart = 0.0f;
        float stageEnd = 1.0f;
        if (t >= 1.0f && t < 2.8f) {
            stage = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                    "DEORBIT BURN" : "ENTRY INTERFACE";
            stageStart = 1.0f;
            stageEnd = 2.8f;
        } else if (t >= 2.8f && t < 4.65f) {
            stage = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                    "HIGH ALTITUDE DESCENT" : "UPPER ATMOSPHERE";
            stageStart = 2.8f;
            stageEnd = 4.65f;
        } else if (t >= 4.65f && t < 6.75f) {
            stage = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                    "POWERED DESCENT" : "ATMOSPHERIC FLIGHT";
            stageStart = 4.65f;
            stageEnd = 6.75f;
        } else if (t >= 6.75f && t < LANDING_TRANSITION_TOUCHDOWN_TIME) {
            stage = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                    "FINAL APPROACH" : "CLOUD DECK APPROACH";
            stageStart = 6.75f;
            stageEnd = LANDING_TRANSITION_TOUCHDOWN_TIME;
        } else if (t >= LANDING_TRANSITION_TOUCHDOWN_TIME) {
            stage = "SURFACE SCAN";
            stageStart = LANDING_TRANSITION_TOUCHDOWN_TIME;
            stageEnd = transition->duration;
        }
        float stageProgress = LandingEase((t - stageStart) / fmaxf(stageEnd - stageStart, 0.01f));

        if (transition->atmosphereType != PLANET_ATMOSPHERE_NONE &&
            t >= 1.0f && t < 4.55f) {
            float heat = sinf(Clamp((t - 1.0f) / 3.55f, 0.0f, 1.0f) * PI);
            heat *= 0.45f + Clamp(transition->atmosphereDensity, 0.0f, 1.0f) * 0.55f;
            Color heatColor = (Color){ 255, 96, 38, (unsigned char)(55.0f + heat * 100.0f) };
            DrawRectangle(0, 0, sw, 34, heatColor);
            DrawRectangle(0, sh - 34, sw, 34, heatColor);
            DrawRectangle(0, 0, 34, sh, heatColor);
            DrawRectangle(sw - 34, 0, 34, sh, heatColor);
            DrawCircleGradient(sw / 2, sh / 2, 110.0f + heat * 90.0f,
                               (Color){ 255, 222, 176, (unsigned char)(35.0f + heat * 55.0f) },
                               (Color){ 255, 110, 32, 0 });
        }
        if (t >= 2.45f && t < 4.05f) {
            float cloudAlpha = 0.14f + switchCover * 0.72f;
            int band = (int)(t * 90.0f) % (sw + 260) - 130;
            Color cloudTop = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                             (Color){ 128, 112, 96, 255 } :
                             (Color){ 232, 244, 255, 255 };
            Color cloudBottom = transition->atmosphereType == PLANET_ATMOSPHERE_NONE ?
                                (Color){ 58, 50, 46, 255 } :
                                (Color){ 105, 142, 175, 255 };
            DrawRectangleGradientV(0, 0, sw, sh,
                                   Fade(cloudTop, cloudAlpha * 0.72f),
                                   Fade(cloudBottom, cloudAlpha * 0.48f));
            DrawRectangle(band, sh / 2 + 22, 220, 54,
                          Fade(cloudTop, cloudAlpha * 0.9f));
            DrawRectangle(band - 280, sh / 2 + 84, 330, 36,
                          Fade(cloudBottom, cloudAlpha * 0.65f));
        }
        bool hasCloudDeck = transition->atmosphereType == PLANET_ATMOSPHERE_BREATHABLE ||
                            transition->atmosphereType == PLANET_ATMOSPHERE_DENSE;
        if (t >= 5.85f && t < 8.0f) {
            float cloudPass = sinf(Clamp((t - 5.85f) / 2.15f, 0.0f, 1.0f) * PI);
            Color deckColor = hasCloudDeck ? (Color){ 226, 240, 248, 255 } :
                                             (Color){ 150, 132, 112, 255 };
            float deckAlpha = cloudPass * (hasCloudDeck ? 0.44f : 0.22f);
            DrawRectangleGradientV(0, sh / 3, sw, sh / 2,
                                   Fade(deckColor, deckAlpha * 0.35f),
                                   Fade(deckColor, deckAlpha));
            int drift = (int)(t * 150.0f) % (sw + 360) - 180;
            DrawRectangle(drift, sh / 2 - 48, 300, 96,
                          Fade(deckColor, deckAlpha * 0.72f));
            DrawRectangle(drift - 410, sh / 2 + 38, 420, 62,
                          Fade(deckColor, deckAlpha * 0.56f));
        }
        if (t >= 7.85f) {
            int cx = sw / 2;
            int cy = sh / 2;
            float radius = 34.0f + stageProgress * 160.0f;
            DrawCircleLines(cx, cy, radius, Fade((Color){ 145, 232, 255, 255 }, 0.85f));
            DrawCircleLines(cx, cy, radius * 0.72f, Fade((Color){ 145, 232, 255, 255 }, 0.42f));
            DrawLine(cx - 220, cy, cx + 220, cy, Fade((Color){ 145, 232, 255, 255 }, 0.35f));
            DrawLine(cx, cy - 150, cx, cy + 150, Fade((Color){ 145, 232, 255, 255 }, 0.35f));
            DrawRectangle(cx - 220, cy + 168, (int)(440.0f * stageProgress), 3,
                          (Color){ 145, 232, 255, 220 });
        }

        int panelWidth = fminf(520.0f, (float)sw - 36.0f);
        int panelX = sw / 2 - panelWidth / 2;
        int panelY = sh - 142;
        DrawRectangleRounded((Rectangle){ (float)panelX, (float)panelY,
                                         (float)panelWidth, 94.0f },
                             0.06f, 6, Fade(BLACK, 0.68f));
        DrawRectangleRoundedLinesEx((Rectangle){ (float)panelX, (float)panelY,
                                                 (float)panelWidth, 94.0f },
                                    0.06f, 6, 1.0f, Fade(WHITE, 0.30f));
        UiDrawText(TextFormat("%s  //  %s", stage, transition->targetName), panelX + 16,
                 panelY + 12, 20, RAYWHITE);
        if (transition->landed) {
            UiDrawText(TextFormat("%s   %03.0f%%", transition->landingPoint,
                                progress * 100.0f),
                     panelX + 16, panelY + 42, 16, Fade(RAYWHITE, 0.82f));
        } else if (transition->committed) {
            float atmosphereProgress = Clamp(
                (t - LANDING_TRANSITION_COMMIT_TIME) /
                (LANDING_TRANSITION_TOUCHDOWN_TIME - LANDING_TRANSITION_COMMIT_TIME),
                0.0f, 1.0f);
            float altitude = Lerp(transition->atmosphereStart.y,
                                  transition->landingPosition.y,
                                  LandingEase(atmosphereProgress)) -
                             transition->landingPosition.y;
            UiDrawText(TextFormat("ATMOSPHERIC ALT %.0f blk   %03.0f%%",
                                fmaxf(altitude, 0.0f), progress * 100.0f),
                     panelX + 16, panelY + 42, 16, Fade(RAYWHITE, 0.82f));
        } else {
            float descent = LandingEase(t / LANDING_TRANSITION_COMMIT_TIME);
            float distance = Lerp(transition->startDistance,
                                  transition->targetDistance, descent);
            UiDrawText(TextFormat("SURFACE RANGE %.1f blk   %03.0f%%",
                                fmaxf(distance - transition->targetRadius, 0.0f),
                                progress * 100.0f),
                     panelX + 16, panelY + 42, 16, Fade(RAYWHITE, 0.82f));
        }
        float gravity = transition->targetGravity *
                        (t < LANDING_TRANSITION_COMMIT_TIME ? 0.0f :
                         LandingEase((t - LANDING_TRANSITION_COMMIT_TIME) /
                                     (LANDING_TRANSITION_TOUCHDOWN_TIME -
                                      LANDING_TRANSITION_COMMIT_TIME)));
        UiDrawText(TextFormat("GRAVITY %.2fg   |   %s", gravity,
                            transition->landed ? "SCAN LOCKED" : "DESCENT CONTROL"),
                 panelX + 16, panelY + 66, 16, Fade((Color){ 174, 224, 255, 255 }, 0.92f));
        UiDrawText("E / ESC  skip descent", sw - 190, sh - 26, 14, Fade(WHITE, 0.58f));
    }

    if (!transition->active && transition->summaryRemaining > 0.0f) {
        float fade = Clamp(transition->summaryRemaining < 1.0f ? transition->summaryRemaining : 1.0f,
                           0.0f, 1.0f);
        int panelWidth = fminf(560.0f, (float)sw - 36.0f);
        int panelX = sw / 2 - panelWidth / 2;
        int panelY = sh / 2 - 106;
        DrawRectangleRounded((Rectangle){ (float)panelX, (float)panelY,
                                         (float)panelWidth, 212.0f },
                             0.06f, 6, Fade((Color){ 8, 16, 28, 255 }, 0.90f * fade));
        DrawRectangleRoundedLinesEx((Rectangle){ (float)panelX, (float)panelY,
                                                 (float)panelWidth, 212.0f },
                                    0.06f, 6, 1.5f, Fade((Color){ 150, 224, 255, 255 }, 0.65f * fade));
        UiDrawText(TextFormat("TOUCHDOWN  //  %s", transition->targetName), panelX + 22,
                 panelY + 18, 22, Fade(RAYWHITE, fade));
        UiDrawText("LANDING POINT", panelX + 22, panelY + 58, 14, Fade((Color){ 142, 216, 244, 255 }, fade));
        UiDrawText(transition->landingPoint, panelX + 22, panelY + 78, 17, Fade(RAYWHITE, fade));
        UiDrawText("ENVIRONMENT", panelX + 22, panelY + 112, 14, Fade((Color){ 142, 216, 244, 255 }, fade));
        UiDrawText(transition->environment, panelX + 22, panelY + 132, 16, Fade(RAYWHITE, fade));
        UiDrawText(transition->biosphere, panelX + 22, panelY + 158, 16, Fade(RAYWHITE, fade));
        UiDrawText(TextFormat("SURFACE GRAVITY %.2fg", transition->targetGravity), panelX + 22,
                 panelY + 183, 14, Fade((Color){ 188, 228, 255, 255 }, fade));
    }
}

static void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info,
                                   bool scanLocked)
{
    if (!info || !info->valid) return;
    int width = 350;
    int x = GetScreenWidth() - width - 18;
    int y = 74;
    float height = scanLocked ? 320.0f : 150.0f;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y,
                                     (float)width, height },
                         0.04f, 6, Fade((Color){ 10, 18, 24, 255 }, 0.90f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x, (float)y,
                                             (float)width, height },
                                0.04f, 6, 1.0f,
                                Fade((Color){ 114, 218, 172, 255 }, 0.72f));
    UiDrawText(TextFormat("%s  SPECIES %08X  //  LINEAGE %08X",
                          scanLocked ? "LOCKED" : "SCAN",
                          info->speciesId, info->lineageId),
               x + 14, y + 12, 16, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("%s  GEN %u  MODULES %u  MUT %u",
                          EvolutionLocomotionName(info->locomotion),
                          info->generation, info->moduleCount,
                          info->mutationCount),
               x + 14, y + 39, 15, Fade(RAYWHITE, 0.90f));
    UiDrawText(TextFormat("AGE %.1f / %.1f d   %s%s",
                          info->ageDays, info->maturityAgeDays,
                          info->sex == CREATURE_SEX_FEMALE ? "F" : "M",
                          info->pregnant ? "  GESTATING" : ""),
               x + 14, y + 64, 15, Fade(RAYWHITE, 0.80f));
    UiDrawText(TextFormat("MASS %.2f   SPEED %.2f   DIET %.2f",
                          info->mass, info->speed, info->diet),
               x + 14, y + 89, 15, Fade(RAYWHITE, 0.80f));
    UiDrawText(TextFormat("HEALTH %3.0f%%   ENERGY %3.0f%%  %s",
                          info->health * 100.0f, info->energy * 100.0f,
                          info->corpse ? "CORPSE" :
                          info->juvenile ? "JUVENILE" : "ADULT"),
               x + 14, y + 116, 15, Fade(RAYWHITE, 0.80f));
    if (!scanLocked) return;
    EvolutionCatalogIndividual individual = { 0 };
    bool haveIndividual = EvolutionCatalogGetIndividual(
        PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
        WorldCurrentSurfaceId(), info->organismId, &individual);
    UiDrawText(TextFormat("PARENTS  M:%08X  F:%08X",
                          info->motherId, info->fatherId),
               x + 14, y + 143, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("CHILDREN %u",
                          haveIndividual ? individual.childCount : 0u),
               x + 14, y + 166, 13, (Color){ 142, 216, 244, 255 });
    char firstChildren[96];
    char remainingChildren[96];
    EvolutionChildrenText(haveIndividual ? &individual : NULL, 0u, 4u,
                          firstChildren, sizeof(firstChildren));
    EvolutionChildrenText(haveIndividual ? &individual : NULL, 4u, 4u,
                          remainingChildren, sizeof(remainingChildren));
    UiDrawText(firstChildren, x + 14, y + 184, 12, Fade(RAYWHITE, 0.72f));
    if (haveIndividual && individual.childCount > 4u) {
        UiDrawText(remainingChildren, x + 14, y + 201, 12,
                   Fade(RAYWHITE, 0.72f));
    }
    UiDrawText(TextFormat("BODY %.2f long  %.2f radius  %.2f energy cost",
                          info->phenotype.bodyLength,
                          info->phenotype.bodyRadius,
                          info->phenotype.energyCost),
               x + 14, y + 222, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText("MODULES", x + 14, y + 245, 13,
               (Color){ 142, 216, 244, 255 });
    int moduleY = y + 263;
    unsigned visible = info->phenotype.moduleCount < 4u
        ? info->phenotype.moduleCount : 4u;
    for (unsigned index = 0; index < visible; index++) {
        const CreatureModule *module = &info->phenotype.modules[index];
        UiDrawText(TextFormat("%s  %.2fx%.2fx%.2f  %.2f kg",
                              EvolutionModuleName((CreatureModuleType)module->type),
                              module->length, module->width, module->height,
                              module->mass),
                   x + 14, moduleY, 12, Fade(RAYWHITE, 0.72f));
        moduleY += 13;
    }
}

static int BiologyAtlasNextSlot(int current, int direction)
{
    int start = current < 0 ? (direction > 0 ? -1 : EVOLUTION_CATALOG_MAX_SPECIES) : current;
    for (int step = 0; step < EVOLUTION_CATALOG_MAX_SPECIES; step++) {
        start += direction;
        if (start < 0) start = EVOLUTION_CATALOG_MAX_SPECIES - 1;
        if (start >= EVOLUTION_CATALOG_MAX_SPECIES) start = 0;
        EvolutionCatalogSpecies species = { 0 };
        if (EvolutionCatalogGetSpecies(start, &species)) return start;
    }
    return -1;
}

static void DrawBiologyAtlas(GameRuntime *game)
{
    if (!game || !game->biologyAtlasOpen) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    DrawRectangle(0, 0, sw, sh, Fade((Color){ 5, 12, 18, 255 }, 0.96f));
    UiDrawText("BIOLOGY ATLAS", 30, 24, 26, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("DISCOVERED SPECIES %d   INDIVIDUAL RECORDS %d",
                          EvolutionCatalogSpeciesCount(),
                          EvolutionCatalogIndividualCount()),
               32, 58, 14, Fade(RAYWHITE, 0.68f));
    UiDrawText("B / ESC close   UP/DOWN select", sw - 270, 30, 13,
               Fade(RAYWHITE, 0.62f));

    int listX = 28;
    int listY = 92;
    int listWidth = 250;
    DrawRectangleRounded((Rectangle){ (float)listX, (float)listY,
                                     (float)listWidth, (float)sh - 126.0f },
                         0.03f, 5, Fade((Color){ 12, 27, 34, 255 }, 0.92f));
    int listRows = (sh - 150) / 42;
    if (listRows < 1) listRows = 1;
    int selectedRank = 0;
    int rank = 0;
    for (int slot = 0; slot < EVOLUTION_CATALOG_MAX_SPECIES; slot++) {
        EvolutionCatalogSpecies species = { 0 };
        if (!EvolutionCatalogGetSpecies(slot, &species)) continue;
        if (slot == game->biologyAtlasSlot) selectedRank = rank;
        rank++;
    }
    int listScroll = selectedRank >= listRows ? selectedRank - listRows + 1 : 0;
    int visibleIndex = 0;
    for (int slot = 0; slot < EVOLUTION_CATALOG_MAX_SPECIES; slot++) {
        EvolutionCatalogSpecies species = { 0 };
        if (!EvolutionCatalogGetSpecies(slot, &species)) continue;
        if (visibleIndex < listScroll || visibleIndex >= listScroll + listRows) {
            visibleIndex++;
            continue;
        }
        Rectangle row = { (float)listX + 8.0f,
                          (float)listY + 8.0f + (visibleIndex - listScroll) * 42.0f,
                          (float)listWidth - 16.0f, 36.0f };
        bool selected = slot == game->biologyAtlasSlot;
        if (selected) DrawRectangleRounded(row, 0.12f, 4,
                                           Fade((Color){ 50, 112, 96, 255 }, 0.85f));
        UiDrawText(TextFormat("%08X  GEN %u", species.speciesId,
                              species.representativeGenome.generation),
                   (int)row.x + 8, (int)row.y + 5, 14,
                   selected ? RAYWHITE : Fade(RAYWHITE, 0.80f));
        CreaturePhenotype phenotype = EvolutionDevelop(
            &species.representativeGenome);
        UiDrawText(TextFormat("%s  %u scans", EvolutionLocomotionName(
                              phenotype.locomotion), species.observationCount),
                   (int)row.x + 8, (int)row.y + 21, 11,
                   Fade(RAYWHITE, selected ? 0.82f : 0.58f));
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) &&
            CheckCollisionPointRec(GetMousePosition(), row)) {
            game->biologyAtlasSlot = slot;
        }
        visibleIndex++;
    }

    if (game->biologyAtlasSlot < 0) {
        game->biologyAtlasSlot = EvolutionCatalogFirstSpeciesSlot();
    }
    EvolutionCatalogSpecies species = { 0 };
    if (!EvolutionCatalogGetSpecies(game->biologyAtlasSlot, &species)) {
        UiDrawText("No species discovered. Scan an evolvable organism first.",
                   310, 120, 18, Fade(RAYWHITE, 0.78f));
        return;
    }
    CreaturePhenotype phenotype = EvolutionDevelop(&species.representativeGenome);
    int detailX = 310;
    UiDrawText(TextFormat("SPECIES %08X", species.speciesId), detailX, 100, 22,
               RAYWHITE);
    UiDrawText(TextFormat("LINEAGE %08X   REPRESENTATIVE GENOME %08X",
                          species.lineageId,
                          species.representativeGenome.genomeId),
               detailX, 132, 14, (Color){ 176, 238, 208, 255 });
    UiDrawText(TextFormat("%s  GENERATION %u  MUTATIONS %u",
                          EvolutionLocomotionName(phenotype.locomotion),
                          species.representativeGenome.generation,
                          species.representativeGenome.mutationCount),
               detailX, 158, 16, Fade(RAYWHITE, 0.86f));
    UiDrawText(TextFormat("MASS %.2f  LENGTH %.2f  RADIUS %.2f  SPEED %.2f",
                          phenotype.totalMass, phenotype.bodyLength,
                          phenotype.bodyRadius, phenotype.cruiseSpeed),
               detailX, 184, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("DIET %.2f  ATTACK %.2f  DEFENSE %.2f  ENERGY %.2f",
                          phenotype.diet, phenotype.attack, phenotype.defense,
                          phenotype.energyCost),
               detailX, 208, 14, Fade(RAYWHITE, 0.76f));
    UiDrawText(TextFormat("FIRST SEEN %d, %d   OBSERVATIONS %u",
                          species.firstX, species.firstZ,
                          species.observationCount),
               detailX, 232, 14, Fade(RAYWHITE, 0.68f));
    UiDrawText(TextFormat("WORLD %08X   SURFACE %08X",
                          species.worldSeed, species.surfaceId),
               detailX, 256, 13, Fade(RAYWHITE, 0.62f));
    EntityEvolutionDebugInfo lockedInfo = { 0 };
    EvolutionCatalogIndividual lockedIndividual = { 0 };
    int lockedIndex = game->evolutionScanLocked
        ? EntityEvolutionFindByOrganism(game->evolutionLockedOrganismId) : -1;
    bool haveLockedFamily = EntityEvolutionInspect(lockedIndex, &lockedInfo) &&
        lockedInfo.speciesId == species.speciesId &&
        EvolutionCatalogGetIndividual(
            PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
            WorldCurrentSurfaceId(), lockedInfo.organismId,
            &lockedIndividual);
    UiDrawText(haveLockedFamily
                   ? TextFormat("FAMILY M:%08X F:%08X  CHILDREN %u",
                                lockedInfo.motherId, lockedInfo.fatherId,
                                lockedIndividual.childCount)
                   : "FAMILY lock a living representative to inspect relations",
               detailX, 278, 13, Fade(RAYWHITE, 0.66f));
    if (haveLockedFamily) {
        char familyChildren[192];
        EvolutionChildrenText(&lockedIndividual, 0u,
                              EVOLUTION_CATALOG_MAX_CHILDREN,
                              familyChildren, sizeof(familyChildren));
        UiDrawText(familyChildren, detailX, 298, 12, Fade(RAYWHITE, 0.62f));
    }
    UiDrawText("EXPRESSED MODULES", detailX, 326, 15,
               (Color){ 142, 216, 244, 255 });
    int moduleY = 352;
    unsigned moduleLimit = phenotype.moduleCount < 12u ? phenotype.moduleCount : 12u;
    for (unsigned index = 0; index < moduleLimit; index++) {
        if (moduleY + 14 > sh - 20) break;
        const CreatureModule *module = &phenotype.modules[index];
        UiDrawText(TextFormat("%02u  %-6s  %.2f x %.2f x %.2f  mass %.2f  eff %.2f",
                              index + 1u,
                              EvolutionModuleName((CreatureModuleType)module->type),
                              module->length, module->width, module->height,
                              module->mass, module->efficiency),
                   detailX, moduleY, 13, Fade(RAYWHITE, 0.76f));
        moduleY += 18;
    }
}

static void BeginNewWorld(GameRuntime *game, TerrainMode mode, uint32_t seed)
{
    DrainChunkGen();
    UnloadAllChunks();
    SpaceReset();
    NetherReset();
    AlbumReset();
    WorldReset(seed);
    PlanetEcologyResetState();
    InventoryReset();
    InventoryGrantStarterKit();
    ShipReset();
    ShipLocatorReset();
    StarMapClose();
    EntitiesClear();
    ParticlesClear();
    WeatherInit();

    WorldSetTerrainMode(mode);
    game->player.position = FindNewWorldSpawn(WorldTerrainMode());
    game->player.velocity = Vector3Zero();
    game->player.yaw = PI;
    game->player.pitch = -0.25f;
    game->player.onGround = false;
    game->player.floating = false;
    PlayerResetRuntimeState(&game->player);

    game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    game->dayTime = 0.30f;
    game->dayCycleEnabled = true;
    UpdateChunks(game->player.position,
                 EffectiveRenderDistanceForHeight(
                     game->player.position.y + EYE_HEIGHT));
}

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

static bool GameUpdateStartScreen(GameRuntime *game, float dt,
                                  bool debugStartRequested)
{
    if (game->perfMode || game->screen != SCREEN_START) return false;

    AudioSetEnvironment(NULL);
    AudioUpdate(dt);
    bool startGame = false;
    if (IsKeyPressed(KEY_ESCAPE)) game->quitRequested = true;
    BeginDrawing();
    DrawStartPage(&startGame, &game->quitRequested, &game->selectedTerrain,
                  &game->selectedSeed);
    if (debugStartRequested) startGame = true;
    EndDrawing();

    if (startGame) {
        BeginNewWorld(game, game->selectedTerrain, game->selectedSeed);
        EnvironmentPresentationRuntimeReset(&game->environment);
        game->importDialog.open = false;
        game->importDialog.relief = true;
        game->importDialog.maxBlocks = IMPORT_DEFAULT_BLOCKS;
        game->importDialog.path[0] = '\0';
        game->albumOpen = false;
        game->albumRainSuspended = false;
        game->wasInSpace = false;
        game->entitiesWorldActive = true;
        game->entitiesWorldDimension = 0u;
        game->thirdPerson = false;
        game->shipLocatorEnabled = false;
        game->landingTransition = (LandingTransition){ 0 };
        game->paused = false;
        game->screen = SCREEN_PLAYING;
        game->cursorReleased = false;
        DisableCursor();
        SetImportMessage(
            WorldTerrainMode() == TERRAIN_FLAT
                ? TextFormat("Flat world seed %u. Press I to import.",
                             WorldGetSeed())
                : TextFormat("World seed %u.", WorldGetSeed()));
        DebugControlReply(&game->debugControl,
                          "DEBUG_CONTROL start ok seed=%u\n",
                          WorldGetSeed());
    }
    return true;
}

static void GameUpdateAlbum(GameRuntime *game)
{
    if (!game->albumOpen && !game->importDialog.open && !game->paused &&
        !HomeWorldMapIsOpen() && !game->landingTransition.active &&
        IsKeyPressed(KEY_P)) {
        if (WeatherGetCurrent() == WEATHER_RAIN) {
            game->albumRainSuspended = true;
            AudioSetRain(false);
        }
        AlbumOpen();
        game->albumOpen = true;
        game->player.velocity = Vector3Zero();
        game->cursorReleased = false;
        EnableCursor();
    }

    AlbumUpdate();
    if (AlbumConsumePlaceRequest()) {
        const char *placedPath = AlbumSelectedPath();
        AlbumClose();
        game->albumOpen = false;
        if (game->albumRainSuspended) {
            game->albumRainSuspended = false;
            AudioSetRain(true);
        }
        if (!game->paused && !game->cursorReleased &&
            !game->importDialog.open) {
            DisableCursor();
        }
        if (placedPath) {
            ImportImageAsBlocks(placedPath, &game->player,
                                IMPORT_DEFAULT_BLOCKS, false);
        }
    }
    if (!AlbumIsOpen() && game->albumOpen) {
        game->albumOpen = false;
        if (game->albumRainSuspended) {
            game->albumRainSuspended = false;
            AudioSetRain(true);
        }
        if (!game->paused && !game->cursorReleased &&
            !game->importDialog.open) {
            DisableCursor();
        }
    }
}

static void GameUpdatePauseAndBiologyAtlas(GameRuntime *game, float dt,
                                           bool landingSkipPressed)
{
    bool biologyAtlasClosed = false;
    if (game->biologyAtlasOpen && IsKeyPressed(KEY_ESCAPE)) {
        game->biologyAtlasOpen = false;
        biologyAtlasClosed = true;
        game->cursorReleased = false;
        DisableCursor();
    }
    if (!game->importDialog.open && !game->albumOpen && !StarMapIsOpen() &&
        !HomeWorldMapIsOpen() &&
        !game->biologyAtlasOpen && !biologyAtlasClosed) {
        if (!game->paused && !game->landingTransition.active &&
            !landingSkipPressed && IsKeyPressed(KEY_ESCAPE)) {
            game->paused = true;
            game->player.velocity = Vector3Zero();
            game->cursorReleased = false;
            EnableCursor();
        } else if (game->paused &&
                   (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))) {
            game->paused = false;
            DisableCursor();
        }
    }

    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !game->landingTransition.active && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        SpaceAdvanceTime(dt);
    }

    if (game->biologyAtlasOpen && !biologyAtlasClosed &&
        IsKeyPressed(KEY_B)) {
        game->biologyAtlasOpen = false;
        game->cursorReleased = false;
        DisableCursor();
    } else if (!game->biologyAtlasOpen && !game->importDialog.open &&
               !game->paused && !game->albumOpen &&
               !game->landingTransition.active && !game->cursorReleased &&
               IsKeyPressed(KEY_B)) {
        game->biologyAtlasOpen = true;
        game->biologyAtlasSlot = EvolutionCatalogFirstSpeciesSlot();
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
    }
    if (game->biologyAtlasOpen) {
        if (IsKeyPressed(KEY_UP)) {
            game->biologyAtlasSlot =
                BiologyAtlasNextSlot(game->biologyAtlasSlot, -1);
        } else if (IsKeyPressed(KEY_DOWN)) {
            game->biologyAtlasSlot =
                BiologyAtlasNextSlot(game->biologyAtlasSlot, 1);
        }
    }
}

static float GameSurfaceMapDaylight(const GameRuntime *game)
{
    if (WorldCurrentDimension() == WORLD_DIMENSION_PLANET) {
        return PlanetWorldDaylightAt(game->player.position);
    }
    float daylight = 1.0f;
    float sunset = 0.0f;
    DayNightFactors(game->dayTime, &daylight, &sunset);
    return daylight;
}

static void GameUpdateViewModes(GameRuntime *game)
{
    GameUpdateAlbum(game);

    if (!game->importDialog.open && !game->paused && !game->albumOpen &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_TAB)) {
        game->cursorReleased = !game->cursorReleased;
        if (game->cursorReleased) {
            game->player.velocity = Vector3Zero();
            EnableCursor();
        } else {
            DisableCursor();
        }
    }
    if (!game->landingTransition.active && !HomeWorldMapIsOpen() &&
        ShipIsDriving() &&
        IsKeyPressed(KEY_E)) {
        if (!WorldIsSpaceActive() ||
            !LandingTransitionBegin(&game->landingTransition,
                                    &game->player)) {
            ShipExit(&game->player);
        }
    }
    bool openedHomeMap = false;
    bool homeMapWasOpen = HomeWorldMapIsOpen();
    WorldDimension currentDimension = WorldCurrentDimension();
    bool surfaceMapAvailable = currentDimension == WORLD_DIMENSION_HOME ||
        (currentDimension == WORLD_DIMENSION_PLANET && PlanetWorldIsActive());
    if (!game->landingTransition.active && surfaceMapAvailable &&
        IsKeyPressed(KEY_M) && !HomeWorldMapIsOpen() &&
        !StarMapIsOpen() && !game->paused && !game->cursorReleased) {
        HomeWorldMapOpen(game->player.position,
                         GameSurfaceMapDaylight(game));
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
        openedHomeMap = true;
    }
    if (!openedHomeMap && HomeWorldMapIsOpen()) {
        HomeWorldMapUpdate(game->player.position, game->player.yaw,
                           GameSurfaceMapDaylight(game));
        if (!HomeWorldMapIsOpen()) {
            game->cursorReleased = false;
            DisableCursor();
        }
    }
    if (!homeMapWasOpen && !game->landingTransition.active &&
        !HomeWorldMapIsOpen() &&
        WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
        IsKeyPressed(KEY_M) && !StarMapIsOpen() && !game->paused &&
        !game->cursorReleased) {
        StarMapOpen();
        game->player.velocity = Vector3Zero();
        game->cursorReleased = true;
        EnableCursor();
    }
    if (StarMapIsOpen()) {
        SolarSystemDef destination = { 0 };
        StarMapUpdate(game->player.position);
        if (StarMapConsumeTravel(&destination)) {
            ShipBeginSystemWarp(&game->player, destination.anchorX,
                                destination.anchorZ);
            StarMapClose();
            game->cursorReleased = false;
            DisableCursor();
        }
        if (!StarMapIsOpen()) {
            game->cursorReleased = false;
            DisableCursor();
        }
    }
    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_F4)) {
        game->thirdPerson = !game->thirdPerson;
        SetImportMessage(game->thirdPerson ? "Third person view."
                                           : "First person view.");
    }

    bool openedImportDialog = false;
    if (!game->importDialog.open && !game->paused &&
        !HomeWorldMapIsOpen() &&
        !game->landingTransition.active && IsKeyPressed(KEY_I)) {
        OpenImportDialog(&game->importDialog);
        if (game->importDialog.open) {
            openedImportDialog = true;
            game->cursorReleased = true;
            game->player.velocity = Vector3Zero();
            EnableCursor();
        }
    }
    if (!openedImportDialog) {
        UpdateImportDialog(&game->importDialog, &game->player,
                           &game->cursorReleased);
    }
}

static bool GameInputBlocked(const GameRuntime *game)
{
    return game->perfMode || game->paused || game->cursorReleased ||
           game->importDialog.open || game->albumOpen ||
           game->biologyAtlasOpen || game->landingTransition.active ||
           ShipIsDriving() || StarMapIsOpen() || HomeWorldMapIsOpen();
}

static void GameUpdateGameplayShortcuts(GameRuntime *game,
                                        bool inputBlocked)
{
    if (!game->paused && !game->albumOpen && !game->importDialog.open &&
        !StarMapIsOpen() && !HomeWorldMapIsOpen() &&
        !game->landingTransition.active &&
        IsKeyPressed(KEY_O)) {
        game->showOrbitTrajectories = !game->showOrbitTrajectories;
        SetImportMessage(game->showOrbitTrajectories
                             ? "Orbit trajectories shown."
                             : "Orbit trajectories hidden.");
    }
    if (!inputBlocked && IsKeyPressed(KEY_F1)) {
        game->showHelp = !game->showHelp;
    }
    if (!inputBlocked && IsKeyPressed(KEY_F3)) {
        game->showDebug = !game->showDebug;
    }
    // Saving while flying is valid; ShipSaveState persists the ship state.
    if (IsKeyPressed(KEY_F5) && !game->paused && !game->cursorReleased &&
        !game->importDialog.open && !game->albumOpen &&
        !game->landingTransition.active && !StarMapIsOpen() &&
        !HomeWorldMapIsOpen()) {
        SaveMap(&game->player);
    }
    if (inputBlocked) return;

    int hotbarKey = HotbarKeyToIndex();
    if (hotbarKey >= 0 && hotbarKey < HOTBAR_SIZE) {
        game->selectedIndex = hotbarKey;
    }
    float wheel = GetMouseWheelMove();
    if (wheel > 0.0f) {
        game->selectedIndex =
            (game->selectedIndex + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
    } else if (wheel < 0.0f) {
        game->selectedIndex = (game->selectedIndex + 1) % HOTBAR_SIZE;
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET)) AdjustRenderDistance(-1);
    if (IsKeyPressed(KEY_RIGHT_BRACKET)) AdjustRenderDistance(1);
    if (IsKeyPressed(KEY_F9)) {
        LoadMap(&game->player);
        game->landingTransition = (LandingTransition){ 0 };
        game->wasInSpace = WorldIsSpaceActive();
        game->entitiesWorldActive = WorldIsSurfaceActive();
        game->entitiesWorldDimension = WorldCurrentSurfaceId();
        game->cursorReleased = false;
        DisableCursor();
        game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    }
    if (IsKeyPressed(KEY_F6)) {
        game->dayCycleEnabled = !game->dayCycleEnabled;
        SetImportMessage(game->dayCycleEnabled ? "Day/night cycle enabled."
                                               : "Day/night cycle paused.");
    }
    if (IsKeyPressed(KEY_F7)) {
        WeatherCycle();
        SetImportMessage(TextFormat("Weather: %s", WeatherName()));
    }
    if (IsKeyPressed(KEY_F8)) {
        game->autoSaveEnabled = !game->autoSaveEnabled;
        game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
        SetImportMessage(game->autoSaveEnabled
                             ? "Auto-save enabled (every 60s)."
                             : "Auto-save disabled.");
    }
    if (IsKeyPressed(KEY_L)) {
        game->shipLocatorEnabled = !game->shipLocatorEnabled;
        if (game->shipLocatorEnabled) {
            SetImportMessage(
                ShipLocatorHasTarget()
                    ? "Ship locator online."
                    : "Ship locator online: deploy or board a ship to "
                      "establish a signal.");
        } else {
            SetImportMessage("Ship locator offline.");
        }
    }
    if (PlanetWorldIsActive() && IsKeyPressed(KEY_C)) {
        game->scannerActive = !game->scannerActive;
        if (game->scannerActive) {
            PlanetPoi poi = { 0 };
            if (PlanetPoiNearest(game->player.position, &poi)) {
                SetImportMessage(
                    TextFormat("Scanner online: %s", poi.name));
            } else {
                SetImportMessage("Scanner online: no signal found.");
            }
        } else {
            SetImportMessage("Scanner offline.");
        }
    }
    bool ctrlHeld =
        IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlHeld && IsKeyPressed(KEY_Z) && !IsKeyDown(KEY_LEFT_SHIFT)) {
        if (UndoBlockEdit()) SetImportMessage("Undo");
    } else if (ctrlHeld &&
               (IsKeyPressed(KEY_Y) ||
                (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)))) {
        if (RedoBlockEdit()) SetImportMessage("Redo");
    }
}

static void GameUpdateTemporalState(GameRuntime *game, float dt)
{
    if (!game->perfMode && game->autoSaveEnabled &&
        game->screen == SCREEN_PLAYING && !GameWorldSimulationPaused(game) &&
        !game->landingTransition.active) {
        game->autoSaveTimer -= dt;
        if (game->autoSaveTimer <= 0.0f) {
            game->autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
            SaveMap(&game->player);
        }
    }

    if (!game->perfMode && game->dayCycleEnabled &&
        !GameWorldSimulationPaused(game) &&
        !game->albumOpen && !game->landingTransition.active) {
        game->dayTime += dt / DAY_LENGTH_SECONDS;
        if (game->dayTime >= 1.0f) game->dayTime -= 1.0f;
    }
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->landingTransition.active &&
        (HomeWorldSurfaceIsActive() || PlanetWorldIsActive())) {
        bool weatherSheltered =
            EnvironmentSheltered(game->player.position) ||
            PlayerWaterStateAt(game->player.position).eyesSubmerged;
        WeatherSetSheltered(weatherSheltered);
        WeatherUpdate(dt, game->player.position);
    } else if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) {
        WeatherSetSheltered(false);
        WeatherSuspend();
    }
}

static void GameUpdatePlayerMotion(GameRuntime *game, float dt,
                                   bool inputBlocked)
{
    if (!game->perfMode && !GameWorldSimulationPaused(game) &&
        !game->landingTransition.active &&
        ShipIsDriving() && !StarMapIsOpen()) {
        ShipUpdate(&game->player, dt);
        if (PlanetWorldTryLaunch(&game->player) ||
            HomeWorldTryLaunch(&game->player)) {
            game->wasInSpace = true;
        }
    } else if (!game->perfMode && !inputBlocked) {
        if (game->scriptedInputFrames > 0u) {
            game->appliedPlayerInput = game->scriptedPlayerInput;
            game->appliedPlayerInput.jumpPressed =
                game->scriptedInputFirstFrame &&
                game->scriptedPlayerInput.vertical > 0.0f;
            game->scriptedInputFirstFrame = false;
            game->scriptedInputFrames--;
        } else if (game->debugControlEnabled) {
            // Debug sessions are driven exclusively by stdin so a
            // desktop key held by the test runner cannot leak into the
            // simulation after a scripted input window expires.
            game->appliedPlayerInput = (PlayerInput){ 0 };
        } else {
            game->appliedPlayerInput = PlayerInputFromKeyboard();
        }
        UpdatePlayerWithInput(&game->player, dt, &game->appliedPlayerInput);
        if (PlanetWorldIsActive()) {
            game->wasInSpace = false;
        } else {
            bool launchedHome = HomeWorldTryLaunch(&game->player);
            bool inSpaceNow = WorldIsSpaceActive();
            if (inSpaceNow && !game->wasInSpace) {
                if (!launchedHome) {
                    SetImportMessage(
                        "Entered space - no gravity; follow the sun to "
                        "the solar system.");
                }
            } else if (!inSpaceNow && game->wasInSpace) {
                SetImportMessage("Back in the atmosphere.");
            }
            game->wasInSpace = inSpaceNow;
        }
    }
    if (!game->landingTransition.active && WorldIsSpaceActive() &&
        !StarMapIsOpen() && SpaceRebasePlayer(&game->player)) {
        // Particles are cosmetic local-frame data; discard the old frame.
        ParticlesClear();
    }
}

static int GameUpdateWorldStreaming(GameRuntime *game,
                                    bool *localWorldActive)
{
    int effectiveRenderDistance = EffectiveRenderDistanceForHeight(
        game->player.position.y + EYE_HEIGHT);
    *localWorldActive = WorldIsSurfaceActive();
    uint32_t currentEntityDimension = WorldCurrentSurfaceId();
    if (*localWorldActive != game->entitiesWorldActive ||
        (*localWorldActive &&
         currentEntityDimension != game->entitiesWorldDimension)) {
        EntitiesClear();
        game->evolutionScanLocked = false;
        game->evolutionLockedOrganismId = 0u;
        game->entitiesWorldActive = *localWorldActive;
        game->entitiesWorldDimension = currentEntityDimension;
    }
    if (*localWorldActive) {
        UpdateChunks(game->player.position, effectiveRenderDistance);
    }
    if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
        SpaceProcessFinishedGenJobs();
        int spaceGenPerFrame = 2;
        if (ShipIsDriving()) {
            spaceGenPerFrame = ShipIsWarping()
                                   ? 16
                                   : (ShipIsCruising() ? 12 : 4);
        }
        UpdateSpaceChunks(game->player.position, effectiveRenderDistance,
                          spaceGenPerFrame);
        if (HomeWorldSurfaceIsActive() &&
            WorldCurrentDimensionAt(game->player.position.y + EYE_HEIGHT) ==
                WORLD_DIMENSION_NETHER) {
            UpdateNetherChunks(game->player.position,
                               effectiveRenderDistance, 4);
        }
        SpaceUpdateSolarGlow(game->player.position);
    }
    return effectiveRenderDistance;
}

static void GameUpdateWorldJobs(GameRuntime *game, float dt,
                                bool localWorldActive)
{
    ProcessFinishedMeshJobs(2.0);
    ProcessFinishedChunkJobs();
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->importDialog.open && !game->landingTransition.active &&
        !game->biologyAtlasOpen && localWorldActive) {
        FluidUpdate(dt);
    }
    RebuildDirtyChunkMeshes(game->player.position);
    ParticlesUpdate(dt);
}

static void GameUpdateFrameEnvironment(GameRuntime *game,
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

    if (!GameWorldSimulationPaused(game)) {
        ChunksUpdateEcologyVisuals(frame->dt, frame->daylight);
    }
    if (!GameWorldSimulationPaused(game) && !game->albumOpen &&
        !game->importDialog.open &&
        !game->landingTransition.active && frame->localWorldActive) {
        EntitiesUpdate(frame->dt, &game->player, frame->daylight);
    }

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
    frame->skyHorizon =
        ColorLerp(frame->skyHorizon, BLACK, frame->skyFade);
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
        .sheltered = EnvironmentSheltered(game->camera.position),
        .forest = forest,
        .nearWater = EnvironmentNearWater(game->camera.position),
        .shipInterior = environmentScene == ENVIRONMENT_SCENE_SPACE &&
                        ShipIsDriving()
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

static void GameRenderBackground(GameRuntime *game,
                                 const GameFrameView *frame)
{
    ClearBackground(frame->skyTop);
    DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(),
                           frame->skyTop, frame->skyHorizon);
    if (!frame->underwater) {
        DrawPlanetAtmosphereSky(&game->camera, &frame->planetLight,
                                &frame->planetObservation,
                                &frame->weatherVisual);
    }
}

static void GameRenderInteractionGuides(GameRuntime *game,
                                        const GameFrameView *frame)
{
    if (frame->hit.hit) {
        if (frame->hitParkedShip && !frame->hitShip.legacy) {
            Vector3 center = {
                frame->hitShip.coreX + 1.0f,
                frame->hitShip.coreY + 1.0f,
                frame->hitShip.coreZ + 1.0f
            };
            DrawCubeWires(center, 4.03f, 2.03f, 4.03f, WHITE);
        } else {
            Vector3 center = {
                frame->hit.x + 0.5f,
                frame->hit.y + 0.5f,
                frame->hit.z + 0.5f
            };
            DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
        }
    }
    if (!frame->canPlace) return;

    if (game->hotbar[game->selectedIndex] == BLOCK_SPACESHIP) {
        Vector3 center = {
            frame->placeX + 1.0f,
            frame->placeY + 1.0f,
            frame->placeZ + 1.0f
        };
        DrawCubeWires(center, 4.02f, 2.02f, 4.02f, Fade(GREEN, 0.9f));
    } else {
        Vector3 center = {
            frame->placeX + 0.5f,
            frame->placeY + 0.5f,
            frame->placeZ + 0.5f
        };
        DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
    }
}

static void GameRenderWorldPass(GameRuntime *game,
                                const GameFrameView *frame)
{
    PerfBeginGpuFrame();
    bool drawSurfaceChunks = PlanetWorldIsActive() ||
        (HomeWorldSurfaceIsActive() && !frame->inNether &&
         frame->spaceFade <= 0.05f);
    DrawWorldShadowMap(&game->camera, frame->effectiveRenderDistance,
                       drawSurfaceChunks, frame->inNether,
                       &frame->worldLighting);
    BeginMode3D(game->camera);
    DrawWorld(&game->camera, frame->effectiveRenderDistance, frame->worldTint,
              drawSurfaceChunks, frame->inNether, &frame->worldLighting);
    if (frame->localWorldActive) EntitiesDraw();
    // The ship model is only useful as an exterior reference.
    if (ShipIsDriving() && game->thirdPerson) ShipDraw(&game->player);
    if (!frame->underwater) {
        DrawHomePlanet(&game->camera, frame->spaceFade);
        if (game->showOrbitTrajectories) {
            DrawSolarOrbitTrajectories(&game->camera, frame->spaceFade);
        }
        DrawSolarBodies(&game->camera, frame->spaceFade);
    }
    if (frame->skyFade < 0.5f && !frame->inNether &&
        !frame->underwater && frame->weatherVisual.active) {
        DrawClouds(&game->camera,
                   Fade(frame->worldTint, 1.0f - frame->skyFade * 2.0f),
                   frame->weatherSimulationTime, &frame->weatherVisual,
                   &frame->environmentPresentation, &frame->worldLighting);
    }
    ParticlesDraw();
    GameRenderInteractionGuides(game, frame);
    EndMode3D();
    PerfEndGpuFrame();
}

static float GameBuildShipHud(GameRuntime *game, ShipHudState *shipHud,
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
        .autoCruising = ShipGetDriveMode() == SHIP_DRIVE_AUTO_CRUISE,
        .warping = ShipIsWarping()
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
            (1.0f - PlanetWorldAtmosphereFade(game->camera.position)) * 100.0f;
    } else if (HomeWorldSurfaceIsActive()) {
        shipHud->nearPlanet = true;
        shipHud->altitude = game->player.position.y -
            (float)TerrainHeight(
                (int)floorf(game->player.position.x),
                (int)floorf(game->player.position.z), WorldTerrainMode());
        shipHud->atmosphere =
            (1.0f - HomeWorldSpaceFade(game->camera.position)) * 100.0f;
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
    shipHud->heading = fmodf(
        game->player.yaw * RAD2DEG + 360.0f, 360.0f);
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

static void GameRenderEnvironmentOverlays(GameRuntime *game,
                                          const GameFrameView *frame,
                                          const ShipHudState *shipHud)
{
    if (!frame->underwater) {
        DrawStars(&game->camera,
                  frame->inNether
                      ? 1.0f
                      : 1.0f - frame->environmentPresentation.starVisibility,
                  &frame->planetObservation, &frame->weatherVisual);
        DrawSpaceSky(frame->skyFade, frame->daylight, &game->camera);
        if (frame->spaceFade < 0.5f && !frame->inNether) {
            DrawCelestial(&game->camera, game->dayTime, frame->daylight,
                          &frame->planetLight, &frame->planetObservation,
                          &frame->weatherVisual);
        }
        if (!frame->inNether && frame->skyFade < 0.5f) {
            DrawWeatherOverlay(&game->camera, &frame->weatherVisual);
        }
    }
    DrawEnvironmentPostProcess(&frame->environmentPresentation);
    DrawWarpTunnel(&game->camera, ShipWarpVisualIntensity());
    DrawSolarGuide(&game->camera, frame->spaceFade);
    if (game->scannerActive && PlanetWorldIsActive()) {
        PlanetPoiDrawScanner(&game->camera, game->player.position);
    }
    ShipLocatorTarget shipLocatorTarget = { 0 };
    if (game->shipLocatorEnabled && !ShipIsDriving() &&
        ShipLocatorTargetAt(game->player.position, &shipLocatorTarget)) {
        DrawShipLocator(&game->camera, &shipLocatorTarget);
    }
    if (ShipIsDriving()) DrawShipHud(shipHud);
    if (frame->spaceFade > 0.05f && frame->haveAimBody &&
        !StarMapIsOpen()) {
        DrawBodyInfoPanel(&frame->aimBody);
    }
}

static void GameRenderEvolutionPanel(GameRuntime *game,
                                     const GameFrameView *frame)
{
    EntityEvolutionDebugInfo aimedEvolution = { 0 };
    int evolutionDisplayEntity = frame->entityHit;
    if (game->evolutionScanLocked) {
        evolutionDisplayEntity = EntityEvolutionFindByOrganism(
            game->evolutionLockedOrganismId);
        if (evolutionDisplayEntity < 0) {
            game->evolutionScanLocked = false;
            game->evolutionLockedOrganismId = 0u;
        }
    }
    if (EntityEvolutionInspect(evolutionDisplayEntity, &aimedEvolution)) {
        DrawEvolutionScanPanel(&aimedEvolution, game->evolutionScanLocked);
    }
}

static void GameRenderStatusHud(GameRuntime *game)
{
    DrawHotbar(game->hotbar, game->selectedIndex);
    DrawImportStatus();
    int hour = (int)(game->dayTime * 24.0f) % 24;
    const char *positionText = TextFormat(
        "XYZ %d %d %d    %02d:00",
        (int)floorf(game->player.position.x),
        (int)floorf(game->player.position.y),
        (int)floorf(game->player.position.z), hour);
    UiDrawText(positionText, 15, GetScreenHeight() - 32, 17,
               Fade(BLACK, 0.92f));
    UiDrawText(positionText, 14, GetScreenHeight() - 34, 17,
               Fade(WHITE, 0.9f));
    const char *saveText = TextFormat(
        "Auto-save: %s", game->autoSaveEnabled ? "60s" : "off");
    UiDrawText(saveText, 15, GetScreenHeight() - 14, 15,
               Fade(BLACK, 0.92f));
    UiDrawText(saveText, 14, GetScreenHeight() - 16, 15,
               Fade(WHITE, 0.65f));
    if (game->cursorReleased && !game->importDialog.open) {
        DrawCursorReleasedOverlay();
    }
}

static void GameRenderHud(GameRuntime *game, const GameFrameView *frame,
                          float shipSpeed)
{
    if (!game->biologyAtlasOpen && !HomeWorldMapIsOpen()) {
        DrawCrosshair(GetScreenWidth(), GetScreenHeight());
    }
    if (!HomeWorldMapIsOpen()) GameRenderEvolutionPanel(game, frame);
    if (!game->biologyAtlasOpen && !HomeWorldMapIsOpen()) {
        GameRenderStatusHud(game);
    }
    if (game->showHelp && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        DrawHelpPanel(game->player.floating, game->cursorReleased,
                      renderDistanceChunks);
    }
    DrawImportDialog(&game->importDialog);
    AlbumDraw();
    StarMapDraw();
    if (game->showDebug && !game->biologyAtlasOpen &&
        !HomeWorldMapIsOpen()) {
        const HudFrameState hud = {
            .dayTime = game->dayTime,
            .shipSpeed = shipSpeed,
            .targetedBlock = frame->hit.hit
                ? GetBlockAt(frame->hit.x, frame->hit.y, frame->hit.z)
                : BLOCK_AIR,
            .spaceEditCount = GetSpaceEditCount(),
            .autoSaveEnabled = game->autoSaveEnabled
        };
        DrawDebugHUD(game->player.position, game->player.yaw,
                     game->player.pitch, frame->daylight,
                     &frame->planetLight, &frame->planetObservation,
                     frame->planetSeasonProgress, &frame->weatherVisual,
                     &frame->bathymetry, &hud);
    }
}

static void GameRenderPauseOverlay(GameRuntime *game)
{
    if (!game->paused) return;

    if (IsKeyPressed(KEY_MINUS)) {
        game->settings.masterVolume = fmaxf(
            0.0f, game->settings.masterVolume - 0.1f);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        GameSettingsSave(&game->settings);
    }
    if (IsKeyPressed(KEY_EQUAL)) {
        game->settings.masterVolume = fminf(
            1.0f, game->settings.masterVolume + 0.1f);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        GameSettingsSave(&game->settings);
    }
    PauseMenuActions pauseActions = { 0 };
    GraphicsQuality previousQuality = game->settings.graphicsQuality;
    DrawPauseMenu(&game->settings, &pauseActions);
    if (pauseActions.settingsChanged) {
        if (pauseActions.qualityChanged &&
            !WorldRendererSetQuality(game->settings.graphicsQuality)) {
            game->settings.graphicsQuality = previousQuality;
            SetImportMessage(
                "Graphics quality change failed; previous quality restored.");
        }
        WeatherSetParticleScale(
            GraphicsQualityProfileFor(
                game->settings.graphicsQuality).precipitationScale);
        AudioSetVolumes(game->settings.masterVolume,
                        game->settings.ambientVolume,
                        game->settings.musicVolume);
        AudioSetMusicEnabled(game->settings.musicEnabled);
        GameSettingsSave(&game->settings);
    }
    if (pauseActions.resume) {
        game->paused = false;
        DisableCursor();
    }
    if (pauseActions.saveWorld) {
        // Ship state can be saved even when no parking spot is available.
        SaveMap(&game->player);
    }
    if (pauseActions.returnToMenu) {
        game->paused = false;
        game->cursorReleased = false;
        if (game->albumOpen) {
            game->albumOpen = false;
            AlbumClose();
        }
        game->screen = SCREEN_START;
        AudioSetEnvironment(NULL);
        EnableCursor();
    }
    if (pauseActions.saveAndQuit) {
        SaveMap(&game->player);
        game->quitSaveDone = true;
        game->quitRequested = true;
    }
}

static void GameRenderFrame(GameRuntime *game,
                            const GameFrameView *frame)
{
    BeginDrawing();
    GameRenderBackground(game, frame);
    GameRenderWorldPass(game, frame);

    char shipHudSystem[48] = { 0 };
    ShipHudState shipHud = { 0 };
    float shipSpeed = GameBuildShipHud(
        game, &shipHud, shipHudSystem, sizeof(shipHudSystem));
    GameRenderEnvironmentOverlays(game, frame, &shipHud);
    GameRenderHud(game, frame, shipSpeed);
    DrawBiologyAtlas(game);
    HomeWorldMapDraw();
    DrawLandingTransitionOverlay(&game->landingTransition);
    GameRenderPauseOverlay(game);
    EndDrawing();
}
static void GameCaptureScreenshot(GameRuntime *game,
                                  const GameFrameView *frame)
{
    if (game->screenshotPending) {
        char screenshotPath[512];
        char debugReportPath[512];
        time_t screenshotTime = time(NULL);
        ChunkStreamingStats streamingStats = ChunksGetStreamingStats();
        ChunkWaterRenderDebugInfo screenshotWaterRender = { 0 };
        ChunksGetWaterRenderDebugInfo(game->player.position,
                                      &screenshotWaterRender);
        EntityEvolutionDebugInfo screenshotEntity = { 0 };
        int screenshotEntityIndex = game->evolutionScanLocked
            ? EntityEvolutionFindByOrganism(game->evolutionLockedOrganismId)
            : EntityNearestEvolvable(game->player.position, 32.0f);
        bool haveScreenshotEntity = EntityEvolutionInspect(
            screenshotEntityIndex, &screenshotEntity);
        EvolutionCatalogIndividual screenshotIndividual = { 0 };
        bool haveScreenshotIndividual = haveScreenshotEntity &&
            EvolutionCatalogGetIndividual(
                PlanetWorldIsActive() ? PlanetWorldSeed() : WorldGetSeed(),
                WorldCurrentSurfaceId(), screenshotEntity.organismId,
                &screenshotIndividual);
        PlanetEvolutionRegion screenshotRegion = { 0 };
        bool haveScreenshotRegion = PlanetEcologyEvolutionRegionAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), frame->daylight, &screenshotRegion);
        FluidSample screenshotFluid = FluidSampleAt(game->player.position);
        FluidStats screenshotFluidStats = FluidGetStats();
        ScreenshotDebugInfo debugInfo = {
            .world = {
                .seed = PlanetWorldIsActive() ? PlanetWorldSeed() :
                                                WorldGetSeed(),
                .surfaceId = WorldCurrentSurfaceId(),
                .dimension = WorldDimensionName(frame->cameraDimension),
                .dayTime = game->dayTime,
                .daylight = frame->daylight,
                .dayCycleEnabled = game->dayCycleEnabled
            },
            .player = {
                .position = ScreenshotVector(game->player.position),
                .velocity = ScreenshotVector(game->player.velocity),
                .yaw = game->player.yaw,
                .pitch = game->player.pitch,
                .onGround = game->player.onGround,
                .floating = game->player.floating,
                .driving = ShipIsDriving()
            },
            .camera = {
                .position = ScreenshotVector(game->camera.position),
                .target = ScreenshotVector(game->camera.target),
                .fovY = game->camera.fovy,
                .thirdPerson = game->thirdPerson,
                .insideSolid =
                    PlayerCameraPositionInsideSolid(game->camera.position)
            },
            .weather = {
                .name = WeatherName(),
                .simulationTime = frame->weatherSimulationTime,
                .active = frame->weatherVisual.active,
                .atmosphereDensity = frame->weatherVisual.atmosphereDensity,
                .cloudCover = frame->weatherVisual.cloudCover,
                .cloudBaseHeight = frame->weatherVisual.cloudBaseHeight,
                .cloudThickness = frame->weatherVisual.cloudThickness,
                .cloudOpacity = frame->weatherVisual.cloudOpacity,
                .fogDensity = frame->weatherVisual.fogDensity,
                .visibility = frame->weatherVisual.visibility,
                .precipitationVeil = frame->weatherVisual.precipitationVeil,
                .stormDarkening = frame->weatherVisual.stormDarkening,
                .windDrift = frame->weatherVisual.windDrift,
                .windAngle = frame->weatherVisual.windAngle,
                .snowFraction = frame->weatherVisual.snowFraction
            },
            .environment = {
                .altitude = frame->environmentSample.altitude,
                .atmosphereFade = frame->skyFade,
                .underwaterDepth = frame->environmentSample.underwaterDepth,
                .waterSurfaceY = frame->playerWater.surfaceY,
                .seabedY = frame->bathymetry.seabedY,
                .waterColumnDepth = frame->bathymetry.waterDepth,
                .bathymetryZone = BathymetryZoneName(frame->bathymetry.zone),
                .seabedMaterial = BathymetryMaterialName(
                    frame->bathymetry.material),
                .underwater = frame->environmentSample.underwater,
                .feetSubmerged = frame->playerWater.feetSubmerged,
                .bodySubmerged = frame->playerWater.bodySubmerged,
                .eyesSubmerged = frame->playerWater.eyesSubmerged,
                .sheltered = frame->environmentSample.sheltered,
                .forest = frame->environmentSample.forest,
                .nearWater = frame->environmentSample.nearWater,
                .shipInterior = frame->environmentSample.shipInterior
            },
            .fluid = {
                .volume = screenshotFluid.volume,
                .surfaceY = screenshotFluid.surfaceY,
                .flowVelocity = ScreenshotVector(
                    screenshotFluid.velocity),
                .ticks = screenshotFluidStats.ticks,
                .loadedVolume = FluidLoadedVolume(),
                .activeCells = screenshotFluidStats.activeCells,
                .lastProcessedCells =
                    screenshotFluidStats.lastProcessedCells,
                .editCount = screenshotFluidStats.editCount,
                .queueOverflows = screenshotFluidStats.queueOverflows
            },
            .input = {
                .forward = game->appliedPlayerInput.forward,
                .strafe = game->appliedPlayerInput.strafe,
                .vertical = game->appliedPlayerInput.vertical,
                .sprint = game->appliedPlayerInput.sprint,
                .remainingFrames = game->scriptedInputFrames
            },
            .render = {
                .graphicsQuality = GraphicsQualityName(game->settings.graphicsQuality),
                .renderDistanceChunks = frame->effectiveRenderDistance,
                .fps = GetFPS(),
                .screenWidth = GetScreenWidth(),
                .screenHeight = GetScreenHeight(),
                .frameTimeMs = frame->dt * 1000.0f,
                .performanceMode = game->perfMode
            },
            .ui = {
                .paused = game->paused,
                .albumOpen = game->albumOpen,
                .starMapOpen = StarMapIsOpen(),
                .importDialogOpen = game->importDialog.open,
                .cursorReleased = game->cursorReleased,
                .helpVisible = game->showHelp,
                .debugHudVisible = game->showDebug,
                .landingTransitionActive = game->landingTransition.active
            },
            .streaming = {
                .activeChunks = GetActiveChunkCount(),
                .activeSpaceChunks = GetActiveSpaceChunkCount(),
                .activeNetherChunks = GetActiveNetherChunkCount(),
                .activeEntities = GetActiveEntityCount(),
                .pendingGenerationJobs = GetPendingGenJobCount(),
                .pendingMeshJobs = GetPendingMeshJobCount(),
                .surfaceChunkX = screenshotWaterRender.cx,
                .surfaceChunkZ = screenshotWaterRender.cz,
                .surfaceSectionY = screenshotWaterRender.sectionY,
                .surfaceChunkLoaded = screenshotWaterRender.chunkLoaded,
                .waterNeighborLoadedMask =
                    screenshotWaterRender.neighborLoadedMask,
                .waterTriangleCount = screenshotWaterRender.triangleCount,
                .waterSectionTriangleCount =
                    screenshotWaterRender.sectionTriangleCount,
                .generationSubmitted = streamingStats.generationSubmitted,
                .generationCompleted = streamingStats.generationCompleted,
                .generationCanceled = streamingStats.generationCanceled,
                .meshSubmitted = streamingStats.meshSubmitted,
                .meshCompleted = streamingStats.meshCompleted,
                .meshCanceled = streamingStats.meshCanceled,
                .meshSnapshotBytes = streamingStats.meshSnapshotBytes,
                .syncRebuilds = streamingStats.syncRebuilds,
                .uploadedMeshes = streamingStats.uploadedMeshes,
                .uploadBudgetDeferrals = streamingStats.uploadBudgetDeferrals,
                .generationQueuePeak = streamingStats.generationQueuePeak,
                .meshQueuePeak = streamingStats.meshQueuePeak,
                .pendingMeshSnapshotBytes =
                    streamingStats.pendingMeshSnapshotBytes,
                .pendingMeshSnapshotBytesPeak =
                    streamingStats.pendingMeshSnapshotBytesPeak,
                .generationCpuMs = streamingStats.generationCpuMs,
                .meshCpuMs = streamingStats.meshCpuMs,
                .uploadCpuMs = streamingStats.uploadCpuMs,
                .maxUploadCpuMs = streamingStats.maxUploadCpuMs
            },
            .evolution = {
                .entitySelected = haveScreenshotEntity,
                .scanLocked = game->evolutionScanLocked,
                .atlasOpen = game->biologyAtlasOpen,
                .corpse = screenshotEntity.corpse,
                .juvenile = screenshotEntity.juvenile,
                .pregnant = screenshotEntity.pregnant,
                .regionAvailable = haveScreenshotRegion,
                .bootstrapComplete = screenshotRegion.bootstrapComplete,
                .organismId = screenshotEntity.organismId,
                .lineageId = screenshotEntity.lineageId,
                .speciesId = screenshotEntity.speciesId,
                .genomeId = screenshotEntity.genomeId,
                .generation = screenshotEntity.generation,
                .mutationCount = screenshotEntity.mutationCount,
                .moduleCount = screenshotEntity.moduleCount,
                .motherId = screenshotEntity.motherId,
                .fatherId = screenshotEntity.fatherId,
                .childCount = haveScreenshotIndividual
                    ? screenshotIndividual.childCount : 0u,
                .catalogSpeciesCount =
                    (uint32_t)EvolutionCatalogSpeciesCount(),
                .catalogIndividualCount =
                    (uint32_t)EvolutionCatalogIndividualCount(),
                .regionalLineageCount = screenshotRegion.lineageCount,
                .bootstrapGeneration =
                    screenshotRegion.bootstrapGeneration,
                .sex = !haveScreenshotEntity ? "none" :
                       screenshotEntity.sex == CREATURE_SEX_FEMALE ?
                       "female" : "male",
                .locomotion = haveScreenshotEntity ?
                    EvolutionLocomotionName(screenshotEntity.locomotion) :
                    "none",
                .ageDays = screenshotEntity.ageDays,
                .maturityAgeDays = screenshotEntity.maturityAgeDays,
                .health = screenshotEntity.health,
                .energy = screenshotEntity.energy,
                .diet = screenshotEntity.diet,
                .mass = screenshotEntity.mass,
                .speed = screenshotEntity.speed,
                .herbivoreDensity = screenshotRegion.herbivoreDensity,
                .omnivoreDensity = screenshotRegion.omnivoreDensity,
                .carnivoreDensity = screenshotRegion.carnivoreDensity
            }
        };
        ScreenshotResult screenshotResult = ScreenshotCaptureFrame(
            SCREENSHOT_DIRECTORY, screenshotTime, screenshotPath,
            sizeof(screenshotPath));
        if (screenshotResult == SCREENSHOT_RESULT_OK) {
            ScreenshotResult reportResult = ScreenshotWriteDebugReport(
                screenshotPath, screenshotTime, &debugInfo, debugReportPath,
                sizeof(debugReportPath));
            if (reportResult == SCREENSHOT_RESULT_OK) {
                SetImportMessage(TextFormat(
                    "Debug capture saved: %s (+ .txt)", screenshotPath));
                DebugControlReply(
                    &game->debugControl,
                    "DEBUG_CONTROL capture ok png=%s report=%s\n",
                    screenshotPath, debugReportPath);
            } else {
                SetImportMessage(TextFormat(
                    "Screenshot saved; %s",
                    ScreenshotResultMessage(reportResult)));
                DebugControlReply(
                    &game->debugControl,
                    "DEBUG_CONTROL capture partial png=%s error=%s\n",
                    screenshotPath, ScreenshotResultMessage(reportResult));
            }
        } else {
            SetImportMessage(ScreenshotResultMessage(screenshotResult));
            DebugControlReply(
                &game->debugControl,
                "DEBUG_CONTROL capture error reason=%s\n",
                ScreenshotResultMessage(screenshotResult));
        }
        game->screenshotPending = false;
    }
}

static bool GameUpdateFrame(GameRuntime *game, float dt,
                            bool debugStartRequested)
{
    if (game->perfMode) ApplyPerfRoute(&game->player, PerfFrameIndex());

    bool landingSkipPressed = LandingTransitionUpdate(&game->landingTransition, &game->player, dt);

    if (GameUpdateStartScreen(game, dt, debugStartRequested)) return false;

    if (!game->perfMode && IsKeyPressed(KEY_F10)) game->screenshotPending = true;

    GameUpdatePauseAndBiologyAtlas(game, dt, landingSkipPressed);
    GameUpdateViewModes(game);
    bool inputBlocked = GameInputBlocked(game);
    GameUpdateGameplayShortcuts(game, inputBlocked);
    WorldTickImportMessage(dt);
    if (!game->importDialog.open && !game->paused && !game->albumOpen) HandleImageDrop(&game->player, game->importDialog.maxBlocks, game->importDialog.relief);

    GameUpdateTemporalState(game, dt);
    GameUpdatePlayerMotion(game, dt, inputBlocked);
    bool localWorldActive = false;
    int effectiveRenderDistance =
        GameUpdateWorldStreaming(game, &localWorldActive);
    GameUpdateWorldJobs(game, dt, localWorldActive);

    GameInteractionContext interaction = { 0 };
    effectiveRenderDistance = GameUpdateInteractions(
        game, dt, inputBlocked, &interaction);

    GameFrameView frame = {
        .hit = interaction.hit,
        .hitShip = interaction.hitShip,
        .aimBody = interaction.aimBody,
        .dt = dt,
        .effectiveRenderDistance = effectiveRenderDistance,
        .entityHit = interaction.entityHit,
        .placeX = interaction.placeX,
        .placeY = interaction.placeY,
        .placeZ = interaction.placeZ,
        .localWorldActive = localWorldActive,
        .hitParkedShip = interaction.hitParkedShip,
        .haveAimBody = interaction.haveAimBody,
        .canPlace = interaction.canPlace
    };
    GameUpdateFrameEnvironment(game, &frame);
    GameRenderFrame(game, &frame);
    GameCaptureScreenshot(game, &frame);
    return true;
}

static bool GameStart(GameRuntime *game, int screenWidth, int screenHeight)
{
    const WorldExtensionHooks worldExtensionHooks = {
        .reset = FluidReset,
        .cleanup = FluidCleanup,
        .saveState = FluidSaveState,
        .loadState = FluidLoadState,
        .tryDisplaceBlock = FluidTryDisplaceForBlockTracked,
        .replayBlockDisplacement = FluidReplayBlockDisplacement,
        .onBlockChanged = FluidOnBlockChanged,
        .onChunkLoaded = FluidOnChunkLoaded,
        .onChunkSectionLoaded = FluidOnChunkSectionLoaded,
        .prepareChunkSectionUnload = FluidPrepareChunkSectionUnload
    };
    WorldInstallExtensionHooks(&worldExtensionHooks);
    if (game->debugControlEnabled) SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Voxelcraft - raylib");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create a raylib window. "
                        "Run from a graphical desktop session.\n");
        return false;
    }

    PerfConfigure(game->perfMode, game->perfReportPath,
                  game->perfBaselinePath);
    SetExitKey(KEY_NULL);
    SetTargetFPS(game->perfMode ? 0 : 60);
    EnableCursor();
    if (!ChunksStartGenThread()) {
        fprintf(stderr, "Warning: failed to start chunk generation thread; "
                        "generating synchronously.\n");
    }
    ParticlesInit();
    AudioInit();
    AudioSetVolumes(game->settings.masterVolume,
                    game->settings.ambientVolume,
                    game->settings.musicVolume);
    AudioSetMusicEnabled(game->settings.musicEnabled);
    WeatherInit();
    WeatherSetParticleScale(
        GraphicsQualityProfileFor(
            game->settings.graphicsQuality).precipitationScale);
    AlbumInit();
    SpaceInit();
    NetherInit();
    EntitiesInit();
    blockAtlas = LoadBlockAtlas();
    WorldRendererInit(game->settings.graphicsQuality);
    cloudModel = LoadCloudModel();
    ShipLoadModel();
    UiFontInit();

    if (game->perfMode) {
        ChunksResetStreamingStats();
        BeginNewWorld(game, TERRAIN_VARIED, DEFAULT_WORLD_SEED);
        game->screen = SCREEN_PLAYING;
        game->autoSaveEnabled = false;
        game->showHelp = false;
        DisableCursor();
        PerfSetMetadata(
            WorldGetSeed(), EffectiveRenderDistanceForHeight(
                game->player.position.y + EYE_HEIGHT));
    }

    DebugControlReply(
        &game->debugControl,
        "DEBUG_CONTROL ready commands=start,screenshot,status,teleport,input,"
        "fluid,evolution,quit\n");
    return true;
}

static int GameStop(GameRuntime *game)
{
    if (!game->perfMode && !game->debugControlEnabled &&
        game->screen == SCREEN_PLAYING) {
        if (game->landingTransition.active) {
            game->landingTransition.elapsed = game->landingTransition.duration;
            LandingTransitionUpdate(&game->landingTransition, &game->player,
                                    0.0f);
        }
        if (!game->quitSaveDone) SaveMap(&game->player);
    }
    if (!game->debugControlEnabled) GameSettingsSave(&game->settings);
    ChunksShutdownGenThread();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    SpaceShutdown();
    UnloadAllNetherChunks();
    WorldRendererShutdown();
    UnloadTexture(blockAtlas);
    UnloadCloudRenderResources();
    UnloadPlanetRenderResources();
    ShipCleanup();
    HomeWorldMapUnload();
    AudioShutdown();
    UiFontShutdown();
    bool perfPassed = PerfReportPassed();
    PerfShutdown();
    CloseWindow();
    AlbumCleanup();
    WorldCleanup();
    DebugControlReply(&game->debugControl, "DEBUG_CONTROL stopped\n");
    return game->perfMode && !perfPassed ? 2 : 0;
}

int GameRun(int argc, char **argv)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;
    GameRuntime game;
    GameRuntimeInit(&game, argc, argv);
    if (!GameStart(&game, screenWidth, screenHeight)) return 1;

    while (!game.quitRequested && !WindowShouldClose()) {
        PerfBeginFrame();
        float dt = (game.perfMode || game.debugControlEnabled) ?
                       (1.0f / 60.0f) : GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        bool debugStartRequested = GameDispatchDebugCommand(&game);

        if (!GameUpdateFrame(&game, dt, debugStartRequested)) continue;
        PerfEndFrame(ChunksGetStreamingStats(), CurrentRenderResourceSnapshot());
        if (game.perfMode && PerfReportWritten() && !game.debugControlEnabled) {
            game.quitRequested = true;
        }
    }

    return GameStop(&game);
}
