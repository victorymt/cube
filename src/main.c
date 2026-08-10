#include "raylib.h"
#include "raymath.h"

#include "types.h"
#include "terrain.h"
#include "world.h"
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
#include "world_environment.h"
#include "ship.h"
#include "nether.h"
#include "entity.h"
#include "starmap.h"
#include "discovery.h"
#include "ecology.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

TerrainMode terrainMode = TERRAIN_VARIED;
static bool autoSaveEnabled = true;
static float autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
static float dayTime = 0.30f;
static bool dayCycleEnabled = true;

static bool FindLandingSpot(Vector3 start, int minY, int maxY, Vector3 *out)
{
    Vector3 spot = start;
    if (spot.y < (float)minY) spot.y = (float)minY;
    if (spot.y > (float)maxY) spot.y = (float)maxY;

    int safety = 0;
    while (PlayerOverlapsWorld(spot) && safety < 12) {
        spot.y += 1.0f;
        safety++;
        if (spot.y > (float)maxY) spot.y = (float)maxY;
    }
    if (PlayerOverlapsWorld(spot)) return false;

    while (spot.y > (float)minY) {
        Vector3 below = spot;
        below.y -= 1.0f;
        if (below.y < (float)minY || PlayerOverlapsWorld(below)) break;
        spot = below;
    }
    *out = spot;
    return true;
}

typedef struct LandingTransition {
    bool active;
    bool committed;
    bool landed;
    bool homeWorldTarget;
    float elapsed;
    float duration;
    float summaryRemaining;
    float targetGravity;
    float targetRadius;
    float startDistance;
    float targetDistance;
    float startYaw;
    float startPitch;
    float targetYaw;
    float targetPitch;
    float atmosphereDensity;
    PlanetAtmosphereType atmosphereType;
    Vector3 targetCenter;
    Vector3 outward;
    Vector3 atmosphereStart;
    Vector3 landingPosition;
    char targetName[48];
    char landingPoint[64];
    char environment[96];
    char biosphere[96];
} LandingTransition;

#define LANDING_TRANSITION_DURATION 9.6f
#define LANDING_TRANSITION_COMMIT_TIME 3.15f
#define LANDING_TRANSITION_TOUCHDOWN_TIME 8.55f
#define LANDING_APPROACH_CLEARANCE 3.0f
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
        radius = HomeWorldProxyRadius();
    } else if (PlanetWorldLandingTarget(player->position, &body)) {
        center = body.center;
        radius = SolarBodyTerrainProxyRadius(body.spaceProxyRadius);
        gravity = body.profile.surfaceGravity;
    } else {
        return false;
    }

    Vector3 outward = Vector3Subtract(player->position, center);
    float startDistance = Vector3Length(outward);
    if (startDistance < 0.001f) outward = (Vector3){ 0.0f, 1.0f, 0.0f };
    else outward = Vector3Scale(outward, 1.0f / startDistance);
    Vector3 inward = Vector3Negate(outward);
    float targetDistance = radius + LANDING_APPROACH_CLEARANCE;

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
        snprintf(transition->targetName, sizeof(transition->targetName), "HOMEWORLD");
    } else {
        snprintf(transition->targetName, sizeof(transition->targetName), "%s %c", body.name,
                 'a' + (body.index > 0 ? body.index - 1 : 0));
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
    ShipExit(player);

    Vector3 besideShip = transition->landingPosition;
    besideShip.x += cosf(player->yaw) * 2.25f;
    besideShip.z -= sinf(player->yaw) * 2.25f;
    if (FindLandingSpot(besideShip, 0, WORLD_HEIGHT - 1, &besideShip)) {
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
        }
    }

    if (transition->elapsed >= transition->duration) {
        if (!transition->committed && !LandingTransitionCommit(transition, player)) {
            return skipPressed;
        }
        if (!transition->landed) LandingTransitionFinishLanding(transition, player);
        transition->active = false;
        transition->summaryRemaining = LANDING_SUMMARY_DURATION;
        SetImportMessage(TextFormat("Touchdown complete: %s.", transition->targetName));
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
        DrawText(TextFormat("%s  //  %s", stage, transition->targetName), panelX + 16,
                 panelY + 12, 20, RAYWHITE);
        if (transition->landed) {
            DrawText(TextFormat("%s   %03.0f%%", transition->landingPoint,
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
            DrawText(TextFormat("ATMOSPHERIC ALT %.0f blk   %03.0f%%",
                                fmaxf(altitude, 0.0f), progress * 100.0f),
                     panelX + 16, panelY + 42, 16, Fade(RAYWHITE, 0.82f));
        } else {
            float descent = LandingEase(t / LANDING_TRANSITION_COMMIT_TIME);
            float distance = Lerp(transition->startDistance,
                                  transition->targetDistance, descent);
            DrawText(TextFormat("SURFACE RANGE %.1f blk   %03.0f%%",
                                fmaxf(distance - transition->targetRadius, 0.0f),
                                progress * 100.0f),
                     panelX + 16, panelY + 42, 16, Fade(RAYWHITE, 0.82f));
        }
        float gravity = transition->targetGravity *
                        (t < LANDING_TRANSITION_COMMIT_TIME ? 0.0f :
                         LandingEase((t - LANDING_TRANSITION_COMMIT_TIME) /
                                     (LANDING_TRANSITION_TOUCHDOWN_TIME -
                                      LANDING_TRANSITION_COMMIT_TIME)));
        DrawText(TextFormat("GRAVITY %.2fg   |   %s", gravity,
                            transition->landed ? "SCAN LOCKED" : "DESCENT CONTROL"),
                 panelX + 16, panelY + 66, 16, Fade((Color){ 174, 224, 255, 255 }, 0.92f));
        DrawText("E / ESC  skip descent", sw - 190, sh - 26, 14, Fade(WHITE, 0.58f));
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
        DrawText(TextFormat("TOUCHDOWN  //  %s", transition->targetName), panelX + 22,
                 panelY + 18, 22, Fade(RAYWHITE, fade));
        DrawText("LANDING POINT", panelX + 22, panelY + 58, 14, Fade((Color){ 142, 216, 244, 255 }, fade));
        DrawText(transition->landingPoint, panelX + 22, panelY + 78, 17, Fade(RAYWHITE, fade));
        DrawText("ENVIRONMENT", panelX + 22, panelY + 112, 14, Fade((Color){ 142, 216, 244, 255 }, fade));
        DrawText(transition->environment, panelX + 22, panelY + 132, 16, Fade(RAYWHITE, fade));
        DrawText(transition->biosphere, panelX + 22, panelY + 158, 16, Fade(RAYWHITE, fade));
        DrawText(TextFormat("SURFACE GRAVITY %.2fg", transition->targetGravity), panelX + 22,
                 panelY + 183, 14, Fade((Color){ 188, 228, 255, 255 }, fade));
    }
}

static void BeginNewWorld(Player *player, TerrainMode mode, uint32_t seed)
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
    StarMapClose();
    EntitiesClear();
    ParticlesClear();
    WeatherInit();

    terrainMode = mode;
    player->position = (Vector3){ 0.5f, (float)TerrainHeight(0, 0, terrainMode) + 3.0f, 0.5f };
    player->velocity = Vector3Zero();
    player->yaw = PI;
    player->pitch = -0.25f;
    player->onGround = false;
    player->floating = false;

    autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    dayTime = 0.30f;
    dayCycleEnabled = true;
    UpdateChunks(player->position, EffectiveRenderDistanceForHeight(player->position.y + EYE_HEIGHT));
}

int main(void)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Voxelcraft - raylib");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create a raylib window. Run from a graphical desktop session.\n");
        return 1;
    }
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    EnableCursor();
    if (!ChunksStartGenThread()) {
        fprintf(stderr, "Warning: failed to start chunk generation thread; generating synchronously.\n");
    }
    ParticlesInit();
    AudioInit();
    WeatherInit();
    AlbumInit();
    SpaceInit();
    NetherInit();
    EntitiesInit();
    blockAtlas = LoadBlockAtlas();
    cloudModel = LoadCloudModel();
    ShipLoadModel();

    Player player = {
        .position = { 0.5f, 12.0f, 0.5f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .yaw = PI,
        .pitch = -0.25f,
        .onGround = false,
        .floating = false
    };

    BlockType hotbar[HOTBAR_SIZE] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER, BLOCK_SPACESHIP
    };
    int selectedIndex = 0;
    bool showHelp = true;
    bool showDebug = false;
    bool scannerActive = false;
    bool showOrbitTrajectories = true;
    int screenshotCounter = 0;
    bool quitRequested = false;
    bool cursorReleased = false;
    bool paused = false;
    bool albumOpen = false;
    bool albumRainSuspended = false;
    bool wasInSpace = false;
    bool entitiesWorldActive = true;
    uint32_t entitiesWorldDimension = 0u;
    bool thirdPerson = false;
    LandingTransition landingTransition = { 0 };
    ImportDialog importDialog = {
        .relief = true,
        .maxBlocks = IMPORT_DEFAULT_BLOCKS
    };
    GameScreen screen = SCREEN_START;
    TerrainMode selectedTerrain = TERRAIN_VARIED;
    uint32_t selectedSeed = DEFAULT_WORLD_SEED;

    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = CameraFovForHeight(player.position.y + EYE_HEIGHT);
    camera.projection = CAMERA_PERSPECTIVE;

    while (!quitRequested && !WindowShouldClose()) {
        float dt = GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        bool landingSkipPressed = LandingTransitionUpdate(&landingTransition, &player, dt);

        if (screen == SCREEN_START) {
            bool startGame = false;
            if (IsKeyPressed(KEY_ESCAPE)) quitRequested = true;
            BeginDrawing();
            DrawStartPage(&startGame, &quitRequested, &selectedTerrain, &selectedSeed);
            EndDrawing();

            if (startGame) {
                BeginNewWorld(&player, selectedTerrain, selectedSeed);
                importDialog.open = false;
                importDialog.relief = true;
                importDialog.maxBlocks = IMPORT_DEFAULT_BLOCKS;
                importDialog.path[0] = '\0';
                albumOpen = false;
                albumRainSuspended = false;
                wasInSpace = false;
                entitiesWorldActive = true;
                entitiesWorldDimension = 0u;
                thirdPerson = false;
                landingTransition = (LandingTransition){ 0 };
                paused = false;
                screen = SCREEN_PLAYING;
                cursorReleased = false;
                DisableCursor();
                SetImportMessage(terrainMode == TERRAIN_FLAT ?
                                 TextFormat("Flat world seed %u. Press I to import.", WorldGetSeed()) :
                                 TextFormat("World seed %u.", WorldGetSeed()));
            }
            continue;
        }

        if (IsKeyPressed(KEY_F10)) {
            TakeScreenshot(TextFormat("voxelcraft_shot_%03d.png", screenshotCounter));
            SetImportMessage(TextFormat("Screenshot saved: voxelcraft_shot_%03d.png", screenshotCounter));
            screenshotCounter++;
        }

        if (!importDialog.open && !albumOpen && !StarMapIsOpen()) {
            if (!paused && !landingTransition.active && !landingSkipPressed && IsKeyPressed(KEY_ESCAPE)) {
                paused = true;
                player.velocity = Vector3Zero();
                cursorReleased = false;
                EnableCursor();
            } else if (paused && (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_ENTER))) {
                paused = false;
                DisableCursor();
            }
        }

        if (!paused && !albumOpen && !importDialog.open && !landingTransition.active) {
            SpaceAdvanceTime(dt);
        }

        if (!albumOpen && !importDialog.open && !paused && !landingTransition.active &&
            IsKeyPressed(KEY_P)) {
            if (WeatherGetCurrent() == WEATHER_RAIN) {
                albumRainSuspended = true;
                AudioSetRain(false);
            }
            AlbumOpen();
            albumOpen = true;
            player.velocity = Vector3Zero();
            cursorReleased = false;
            EnableCursor();
        }
        AlbumUpdate();
        if (AlbumConsumePlaceRequest()) {
            const char *placedPath = AlbumSelectedPath();
            AlbumClose();
            albumOpen = false;
            if (albumRainSuspended) {
                albumRainSuspended = false;
                AudioSetRain(true);
            }
            if (!paused && !cursorReleased && !importDialog.open) DisableCursor();
            if (placedPath) {
                ImportImageAsBlocks(placedPath, &player, IMPORT_DEFAULT_BLOCKS, false);
            }
        }
        if (!AlbumIsOpen() && albumOpen) {
            albumOpen = false;
            if (albumRainSuspended) {
                albumRainSuspended = false;
                AudioSetRain(true);
            }
            if (!paused && !cursorReleased && !importDialog.open) DisableCursor();
        }

        if (!importDialog.open && !paused && !albumOpen && !landingTransition.active &&
            IsKeyPressed(KEY_TAB)) {
            cursorReleased = !cursorReleased;
            if (cursorReleased) {
                player.velocity = Vector3Zero();
                EnableCursor();
            } else {
                DisableCursor();
            }
        }
        if (!landingTransition.active && ShipIsDriving() && IsKeyPressed(KEY_E)) {
            if (!WorldIsSpaceActive() ||
                !LandingTransitionBegin(&landingTransition, &player)) {
                ShipExit(&player);
            }
        }
        if (!landingTransition.active && WorldCurrentDimension() != WORLD_DIMENSION_PLANET &&
            IsKeyPressed(KEY_M) && !StarMapIsOpen() && !paused && !cursorReleased) {
            StarMapOpen();
            player.velocity = Vector3Zero();
            cursorReleased = true;
            EnableCursor();
        }
        if (StarMapIsOpen()) {
            SolarSystemDef destination = { 0 };
            StarMapUpdate(player.position);
            if (StarMapConsumeTravel(&destination)) {
                ShipBeginSystemWarp(&player, destination.anchorX, destination.anchorZ);
                StarMapClose();
                cursorReleased = false;
                DisableCursor();
            }
            if (!StarMapIsOpen()) {
                cursorReleased = false;
                DisableCursor();
            }
        }
        if (!paused && !albumOpen && !importDialog.open && !landingTransition.active &&
            IsKeyPressed(KEY_F4)) {
            thirdPerson = !thirdPerson;
            SetImportMessage(thirdPerson ? "Third person view." : "First person view.");
        }
        bool openedImportDialog = false;
        if (!importDialog.open && !paused && !landingTransition.active && IsKeyPressed(KEY_I)) {
            OpenImportDialog(&importDialog);
            if (importDialog.open) {
                openedImportDialog = true;
                cursorReleased = true;
                player.velocity = Vector3Zero();
                EnableCursor();
            }
        }
        if (!openedImportDialog) UpdateImportDialog(&importDialog, &player, &cursorReleased);

        bool inputBlocked = paused || cursorReleased || importDialog.open || albumOpen ||
                            landingTransition.active ||
                            ShipIsDriving() || StarMapIsOpen();
        if (!paused && !albumOpen && !importDialog.open && !StarMapIsOpen() &&
            !landingTransition.active &&
            IsKeyPressed(KEY_O)) {
            showOrbitTrajectories = !showOrbitTrajectories;
            SetImportMessage(showOrbitTrajectories ? "Orbit trajectories shown."
                                                   : "Orbit trajectories hidden.");
        }
        if (!inputBlocked && IsKeyPressed(KEY_F1)) showHelp = !showHelp;
        if (!inputBlocked && IsKeyPressed(KEY_F3)) showDebug = !showDebug;
        if (!inputBlocked) {
            int hotbarKey = HotbarKeyToIndex();
            if (hotbarKey >= 0 && hotbarKey < HOTBAR_SIZE) selectedIndex = hotbarKey;
            float wheel = GetMouseWheelMove();
            if (wheel > 0.0f) selectedIndex = (selectedIndex + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
            else if (wheel < 0.0f) selectedIndex = (selectedIndex + 1) % HOTBAR_SIZE;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) AdjustRenderDistance(-1);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) AdjustRenderDistance(1);
            if (IsKeyPressed(KEY_F5)) SaveMap(&player);
            if (IsKeyPressed(KEY_F9)) {
                LoadMap(&player);
                landingTransition = (LandingTransition){ 0 };
                wasInSpace = WorldIsSpaceActive();
                entitiesWorldActive = WorldIsSurfaceActive();
                entitiesWorldDimension = WorldCurrentSurfaceId();
                cursorReleased = false;
                DisableCursor();
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
            }
            if (IsKeyPressed(KEY_F6)) {
                dayCycleEnabled = !dayCycleEnabled;
                SetImportMessage(dayCycleEnabled ? "Day/night cycle enabled." : "Day/night cycle paused.");
            }
            if (IsKeyPressed(KEY_F7)) {
                WeatherCycle();
                SetImportMessage(TextFormat("Weather: %s", WeatherName()));
            }
            if (IsKeyPressed(KEY_F8)) {
                autoSaveEnabled = !autoSaveEnabled;
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
                SetImportMessage(autoSaveEnabled ? "Auto-save enabled (every 60s)." : "Auto-save disabled.");
            }
            if (PlanetWorldIsActive() && IsKeyPressed(KEY_C)) {
                scannerActive = !scannerActive;
                if (scannerActive) {
                    PlanetPoi poi = { 0 };
                    if (PlanetPoiNearest(player.position, &poi)) {
                        SetImportMessage(TextFormat("Scanner online: %s", poi.name));
                    } else {
                        SetImportMessage("Scanner online: no signal found.");
                    }
                } else {
                    SetImportMessage("Scanner offline.");
                }
            }
            bool ctrlHeld = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
            if (ctrlHeld && IsKeyPressed(KEY_Z) && !IsKeyDown(KEY_LEFT_SHIFT)) {
                if (UndoBlockEdit()) SetImportMessage("Undo");
            } else if (ctrlHeld && (IsKeyPressed(KEY_Y) || (IsKeyDown(KEY_LEFT_SHIFT) && IsKeyPressed(KEY_Z)))) {
                if (RedoBlockEdit()) SetImportMessage("Redo");
            }
        }
        WorldTickImportMessage(dt);
        if (!importDialog.open && !paused && !albumOpen) HandleImageDrop(&player, importDialog.maxBlocks, importDialog.relief);

        if (autoSaveEnabled && screen == SCREEN_PLAYING && !paused &&
            !landingTransition.active) {
            autoSaveTimer -= dt;
            if (autoSaveTimer <= 0.0f) {
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
                SaveMap(&player);
            }
        }

        if (dayCycleEnabled && !paused && !albumOpen && !landingTransition.active) {
            dayTime += dt / DAY_LENGTH_SECONDS;
            if (dayTime >= 1.0f) dayTime -= 1.0f;
        }
        if (!paused && !albumOpen && !landingTransition.active &&
            (HomeWorldSurfaceIsActive() || PlanetWorldIsActive())) {
            WeatherUpdate(dt, player.position);
        } else if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) {
            WeatherSuspend();
        }

        AudioUpdate();

        if (!landingTransition.active && ShipIsDriving() && !StarMapIsOpen()) {
            ShipUpdate(&player, dt);
            if (PlanetWorldTryLaunch(&player) || HomeWorldTryLaunch(&player)) {
                wasInSpace = true;
            }
        } else if (!inputBlocked) {
            UpdatePlayer(&player, dt);
            if (PlanetWorldIsActive()) {
                wasInSpace = false;
            } else {
                bool launchedHome = HomeWorldTryLaunch(&player);
                bool inSpaceNow = WorldIsSpaceActive();
                if (inSpaceNow && !wasInSpace) {
                    if (!launchedHome) {
                        SetImportMessage("Entered space - no gravity; follow the sun to the solar system.");
                    }
                } else if (!inSpaceNow && wasInSpace) {
                    SetImportMessage("Back in the atmosphere.");
                }
                wasInSpace = inSpaceNow;
            }
        }
        if (!landingTransition.active && WorldIsSpaceActive() && !StarMapIsOpen() &&
            SpaceRebasePlayer(&player)) {
            // Particles are cosmetic local-frame data; discard the old frame.
            ParticlesClear();
        }
        int effectiveRenderDistance = EffectiveRenderDistanceForHeight(player.position.y + EYE_HEIGHT);
        bool localWorldActive = WorldIsSurfaceActive();
        uint32_t currentEntityDimension = WorldCurrentSurfaceId();
        if (localWorldActive != entitiesWorldActive ||
            (localWorldActive && currentEntityDimension != entitiesWorldDimension)) {
            EntitiesClear();
            entitiesWorldActive = localWorldActive;
            entitiesWorldDimension = currentEntityDimension;
        }
        if (localWorldActive) UpdateChunks(player.position, effectiveRenderDistance);
        if (WorldCurrentDimension() != WORLD_DIMENSION_PLANET) {
            SpaceProcessFinishedGenJobs();
            int spaceGenPerFrame = 2;
            if (ShipIsDriving()) {
                spaceGenPerFrame = ShipIsWarping() ? 16 : (ShipIsCruising() ? 12 : 4);
            }
            UpdateSpaceChunks(player.position, effectiveRenderDistance, spaceGenPerFrame);
            if (HomeWorldSurfaceIsActive()) {
                UpdateNetherChunks(player.position, effectiveRenderDistance, 4);
            }
            SpaceUpdateSolarGlow(player.position);
        }
        ProcessFinishedMeshJobs();
        ProcessFinishedChunkJobs();
        RebuildDirtyChunkMeshes();
        ParticlesUpdate(dt);

        UpdatePlayerCamera(&camera, &player, dt, thirdPerson);
        effectiveRenderDistance = EffectiveRenderDistanceForHeight(camera.position.y);

        Vector3 aimEye = { player.position.x, player.position.y + EYE_HEIGHT, player.position.z };
        Vector3 aimDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        HitResult hit = RaycastBlocks(aimEye, aimDir, REACH_DISTANCE);
        int entityHit = EntityRayHit(aimEye, aimDir, REACH_DISTANCE);
        SpaceBodyInfo aimBody = { 0 };
        bool haveAimBody = SpaceBodyPick(aimEye, aimDir, &aimBody);
        if (!inputBlocked && entityHit >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            EntityKill(entityHit);
        } else if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hit.y >= NETHER_LAYER_Y) {
            BlockType brokenType = GetBlockAt(hit.x, hit.y, hit.z);
            PlanetPoi claimedPoi = { 0 };
            bool poiCore = PlanetPoiIsCore(hit.x, hit.y, hit.z);
            bool poiClaimed = PlanetPoiIsClaimed(hit.x, hit.y, hit.z);
            if (PlanetPoiTryClaim(hit.x, hit.y, hit.z, &claimedPoi)) {
                ParticlesEmitBurst((Vector3){ hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f },
                                   BlockBaseColor(claimedPoi.rewardBlock), 24, 3.8f, 0.85f);
                AudioPlayBreak();
                SetImportMessage(TextFormat("Survey complete: %s, +%d %s", claimedPoi.name,
                                            claimedPoi.rewardAmount,
                                            BlockName(claimedPoi.rewardBlock)));
            } else if (poiClaimed) {
                SetImportMessage("This discovery has already been catalogued.");
            } else if (!poiCore && brokenType != BLOCK_AIR && InventoryAdd(brokenType, 1) > 0) {
                ParticlesEmitBurst((Vector3){ hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f },
                                   BlockBaseColor(brokenType), 16, 3.0f, 0.7f);
                AudioPlayBreak();
                SetBlock(hit.x, hit.y, hit.z, BLOCK_AIR);
            } else if (!poiCore && brokenType != BLOCK_AIR) {
                SetImportMessage(TextFormat("Inventory full: %s", BlockName(brokenType)));
            }
        }
        int placeX = 0;
        int placeY = 0;
        int placeZ = 0;
        bool canPlace = false;
        if (!inputBlocked && hit.hit) {
            placeX = hit.x + hit.nx;
            placeY = hit.y + hit.ny;
            placeZ = hit.z + hit.nz;
            canPlace = InventoryCount(hotbar[selectedIndex]) > 0 &&
                       GetBlockAt(placeX, placeY, placeZ) == BLOCK_AIR &&
                       WorldBlockRegionAt(placeY) != WORLD_BLOCK_REGION_NONE &&
                       !BlockWouldOverlapPlayer(placeX, placeY, placeZ, player.position);
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_ALBUM) {
                if (WeatherGetCurrent() == WEATHER_RAIN) {
                    albumRainSuspended = true;
                    AudioSetRain(false);
                }
                AlbumOpen();
                albumOpen = true;
                player.velocity = Vector3Zero();
                cursorReleased = false;
                EnableCursor();
            } else if (!ShipIsDriving() && ShipTryEnter(hit.x, hit.y, hit.z, &player)) {
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_NETHER_PORTAL) {
                Vector3 landing = player.position;
                if (player.position.y > 0.0f) {
                    landing.y = -46.0f;
                    FindLandingSpot(landing, NETHER_LAYER_Y + 1, NETHER_LAYER_TOP - 1, &landing);
                    SetImportMessage("Entered the Nether.");
                } else {
                    float groundY = (float)TerrainHeight((int)floorf(player.position.x),
                                                         (int)floorf(player.position.z), terrainMode);
                    landing.y = groundY + 3.0f;
                    FindLandingSpot(landing, 0, WORLD_HEIGHT - 1, &landing);
                    SetImportMessage("Back to the surface.");
                }
                player.position = landing;
                player.velocity = Vector3Zero();
                player.floating = false;
                wasInSpace = false;
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR ||
                       GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR_OPEN) {
                BlockType doorType = GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_DOOR ? BLOCK_DOOR_OPEN : BLOCK_DOOR;
                AudioPlayPlace();
                SetBlock(hit.x, hit.y, hit.z, doorType);
            } else if (GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE ||
                       GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE_OPEN) {
                BlockType gateType = GetBlockAt(hit.x, hit.y, hit.z) == BLOCK_FENCE_GATE ? BLOCK_FENCE_GATE_OPEN : BLOCK_FENCE_GATE;
                AudioPlayPlace();
                SetBlock(hit.x, hit.y, hit.z, gateType);
            } else if (canPlace) {
                BlockType placedType = hotbar[selectedIndex];
                if (InventoryConsume(placedType, 1)) {
                    ParticlesEmitBurst((Vector3){ placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f },
                                       BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                    AudioPlayPlace();
                    SetBlock(placeX, placeY, placeZ, placedType);
                }
            }
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            BlockType picked = GetBlockAt(hit.x, hit.y, hit.z);
            if (picked != BLOCK_AIR && IsValidBlockType(picked)) {
                hotbar[selectedIndex] = picked;
                AudioPlayPick();
                SetImportMessage(TextFormat("Picked %s (%d)", BlockName(picked), InventoryCount(picked)));
            }
        }

        float daylight = 0.0f;
        float sunset = 0.0f;
        PlanetLightState planetLight = { 0 };
        if (!PlanetWorldLightStateAt(player.position, &planetLight)) {
            DayNightFactors(dayTime, &daylight, &sunset);
        } else {
            daylight = planetLight.daylight;
            sunset = planetLight.sunset;
        }
        ChunksUpdateEcologyVisuals(dt, daylight);
        if (!paused && !albumOpen && !importDialog.open && !landingTransition.active &&
            localWorldActive) {
            EntitiesUpdate(dt, &player, daylight);
        }
        float spaceFade = HomeWorldSpaceFade(camera.position);
        Color skyTop = { 0 };
        Color skyHorizon = { 0 };
        SkyColorsForLight(daylight, sunset, &skyTop, &skyHorizon);
        Color worldTint = MixWeather(WorldTintForLight(daylight, sunset), daylight);
        skyTop = MixWeather(skyTop, daylight);
        skyHorizon = MixWeather(skyHorizon, daylight);
        ApplyPlanetWorldPaletteWithLight(&skyTop, &skyHorizon, &worldTint,
                                         &planetLight);
        float planetAtmosphereFade = PlanetWorldAtmosphereFade(camera.position);
        float skyFade = fmaxf(spaceFade, planetAtmosphereFade);
        UpdatePlanetSceneExposure(&camera);
        skyTop = ColorLerp(skyTop, BLACK, skyFade);
        skyHorizon = ColorLerp(skyHorizon, BLACK, skyFade);
        worldTint = ColorLerp(worldTint, (Color){ 46, 54, 78, 255 }, skyFade);
        bool inNether = WorldCurrentDimensionAt(camera.position.y) == WORLD_DIMENSION_NETHER;
        if (inNether) {
            skyTop = (Color){ 24, 6, 6, 255 };
            skyHorizon = (Color){ 40, 10, 8, 255 };
            worldTint = (Color){ 150, 62, 42, 255 };
            spaceFade = 0.0f;
        }

        BeginDrawing();
        ClearBackground(skyTop);
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), skyTop, skyHorizon);
        DrawPlanetAtmosphereSky(&camera, &planetLight);

        BeginMode3D(camera);
        bool drawSurfaceChunks = PlanetWorldIsActive() ||
                                 (HomeWorldSurfaceIsActive() && spaceFade <= 0.05f);
        DrawWorld(&camera, effectiveRenderDistance, worldTint, drawSurfaceChunks,
                  HomeWorldSurfaceIsActive());
        if (localWorldActive) EntitiesDraw();
        // Keep the first-person flight view clear. The ship model is only useful
        // as an exterior reference when the camera is in third person.
        if (ShipIsDriving() && thirdPerson) ShipDraw(&player);
        DrawHomePlanet(&camera, spaceFade);
        if (showOrbitTrajectories) DrawSolarOrbitTrajectories(&camera, spaceFade);
        DrawSolarBodies(&camera, spaceFade);
        bool drawCloudLayer = HomeWorldSurfaceIsActive() || PlanetWorldIsActive();
        if (PlanetWorldIsActive()) {
            const PlanetProfile *profile = PlanetWorldProfile();
            drawCloudLayer = profile->atmosphereType != PLANET_ATMOSPHERE_NONE &&
                             profile->atmosphereDensity > 0.28f;
        }
        if (skyFade < 0.5f && !inNether && drawCloudLayer) {
            DrawClouds(&camera, Fade(worldTint, 1.0f - skyFade * 2.0f));
        }
        ParticlesDraw();
        if (hit.hit) {
            Vector3 center = { hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f };
            DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
        }
        if (canPlace) {
            Vector3 center = { placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f };
            DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
        }
        EndMode3D();

        if (IsWaterBlock(GetBlock((int)floorf(camera.position.x),
                                  (int)floorf(camera.position.y),
                                  (int)floorf(camera.position.z)))) {
            DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), (Color){ 16, 64, 128, 130 });
        }

        shipSpeedForHud = Vector3Length(player.velocity);
        if (ShipIsDriving()) {
            shipHudSpeed = shipSpeedForHud;
            shipHudCruising = ShipIsCruising();
            Vector3 gravityDir = Vector3Zero();
            float surfaceDist = 0.0f;
            shipHudAtmosphere = -1.0f;
            if (PlanetWorldIsActive()) {
                shipHudNearPlanet = true;
                shipHudAlt = player.position.y -
                             (float)PlanetTerrainHeight((int)floorf(player.position.x),
                                                        (int)floorf(player.position.z));
                shipHudAtmosphere = (1.0f - PlanetWorldAtmosphereFade(camera.position)) * 100.0f;
            } else if (HomeWorldSurfaceIsActive()) {
                shipHudNearPlanet = true;
                shipHudAlt = player.position.y -
                             (float)TerrainHeight((int)floorf(player.position.x),
                                                  (int)floorf(player.position.z), terrainMode);
                shipHudAtmosphere = (1.0f - HomeWorldSpaceFade(camera.position)) * 100.0f;
            } else if (PlanetSurfaceAt(player.position, &gravityDir, &surfaceDist,
                                       NULL)) {
                shipHudNearPlanet = true;
                shipHudAlt = surfaceDist;
            } else {
                shipHudNearPlanet = false;
                shipHudAlt = player.position.y - (float)SPACE_LAYER_Y;
            }
            shipHudHeading = fmodf(player.yaw * RAD2DEG + 360.0f, 360.0f);
            SolarSystemDef hudSys;
            float hudDist = 0.0f;
            if (PlanetWorldIsActive()) {
                snprintf(shipHudSystem, sizeof(shipHudSystem), "%s surface", PlanetWorldName());
            } else if (FindNearestSystem(player.position, 3000.0f, &hudSys, &hudDist)) {
                snprintf(shipHudSystem, sizeof(shipHudSystem), "%s Prime (%.0f)", hudSys.name, hudDist);
            } else {
                snprintf(shipHudSystem, sizeof(shipHudSystem), "Deep space");
            }
        }

        DrawStars(&camera, inNether ? 1.0f : daylight * (1.0f - skyFade));
        DrawSpaceSky(skyFade, daylight, &camera);
        DrawSolarGuide(&camera, spaceFade);
        if (scannerActive && PlanetWorldIsActive()) PlanetPoiDrawScanner(&camera, player.position);
        if (ShipIsDriving()) DrawShipHud();
        if (spaceFade > 0.05f && haveAimBody && !StarMapIsOpen()) {
            DrawBodyInfoPanel(&aimBody);
        }
        if (spaceFade < 0.5f && !inNether) DrawCelestial(&camera, dayTime, daylight);
        DrawCrosshair(GetScreenWidth(), GetScreenHeight());
        DrawHotbar(hotbar, selectedIndex);
        DrawImportStatus();
        int hour = (int)(dayTime * 24.0f) % 24;
        const char *positionText = TextFormat("XYZ %d %d %d    %02d:00", (int)floorf(player.position.x),
                                              (int)floorf(player.position.y),
                                              (int)floorf(player.position.z), hour);
        DrawText(positionText, 15, GetScreenHeight() - 32, 17, Fade(BLACK, 0.92f));
        DrawText(positionText, 14, GetScreenHeight() - 34, 17, Fade(WHITE, 0.9f));
        const char *saveText = TextFormat("Auto-save: %s", autoSaveEnabled ? "60s" : "off");
        DrawText(saveText, 15, GetScreenHeight() - 14, 15, Fade(BLACK, 0.92f));
        DrawText(saveText, 14, GetScreenHeight() - 16, 15, Fade(WHITE, 0.65f));
        if (cursorReleased && !importDialog.open) DrawCursorReleasedOverlay();
        if (showHelp) DrawHelpPanel(player.floating, cursorReleased, renderDistanceChunks);
        DrawImportDialog(&importDialog);
        AlbumDraw();
        StarMapDraw();
        if (showDebug) {
            dayTimeForHud = dayTime;
            autoSaveForHud = autoSaveEnabled;
            blockForHud = hit.hit ? GetBlockAt(hit.x, hit.y, hit.z) : BLOCK_AIR;
            SpaceEditCountForHud = GetSpaceEditCount();
            DrawDebugHUD(player.position, player.yaw, player.pitch, daylight);
        }
        DrawLandingTransitionOverlay(&landingTransition);
        if (paused) {
            if (IsKeyPressed(KEY_MINUS)) SetMasterVolume(fmaxf(0.0f, GetMasterVolume() - 0.1f));
            if (IsKeyPressed(KEY_EQUAL)) SetMasterVolume(fminf(1.0f, GetMasterVolume() + 0.1f));
            bool resumeGame = false;
            bool saveWorld = false;
            bool saveAndQuit = false;
            bool toggleMusic = false;
            bool returnToMenu = false;
            DrawPauseMenu(&resumeGame, &saveWorld, &saveAndQuit, &toggleMusic, &returnToMenu);
            if (resumeGame) {
                paused = false;
                DisableCursor();
            }
            if (toggleMusic) AudioToggleMusic();
            if (saveWorld) {
                if (ShipIsDriving()) ShipForceExit(&player);
                SaveMap(&player);
            }
            if (returnToMenu) {
                paused = false;
                cursorReleased = false;
                if (albumOpen) {
                    albumOpen = false;
                    AlbumClose();
                }
                screen = SCREEN_START;
                EnableCursor();
            }
            if (saveAndQuit) {
                if (ShipIsDriving()) ShipForceExit(&player);
                SaveMap(&player);
                quitRequested = true;
            }
        }

        EndDrawing();
    }

    if (screen == SCREEN_PLAYING) {
        if (landingTransition.active) {
            landingTransition.elapsed = landingTransition.duration;
            LandingTransitionUpdate(&landingTransition, &player, 0.0f);
        }
        if (ShipIsDriving()) ShipForceExit(&player);
        SaveMap(&player);
    }
    ChunksShutdownGenThread();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    SpaceShutdown();
    UnloadAllNetherChunks();
    UnloadTexture(blockAtlas);
    if (cloudModel.meshCount > 0) UnloadModel(cloudModel);
    UnloadPlanetRenderResources();
    ShipCleanup();
    AudioShutdown();
    CloseWindow();
    AlbumCleanup();
    WorldCleanup();
    return 0;
}
