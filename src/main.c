#include "raylib.h"
#include "raymath.h"

#include "types.h"
#include "terrain.h"
#include "world.h"
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
#include "world_environment.h"
#include "ship.h"
#include "nether.h"
#include "entity.h"
#include "fluid.h"
#include "evolution_catalog.h"
#include "starmap.h"
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

TerrainMode terrainMode = TERRAIN_VARIED;
static bool autoSaveEnabled = true;
static float autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
static float dayTime = 0.30f;
static bool dayCycleEnabled = true;
static bool evolutionScanLocked = false;
static uint32_t evolutionLockedOrganismId = 0u;
static bool biologyAtlasOpen = false;
static int biologyAtlasSlot = -1;
// Tracks whether the quit path already saved ("Save & Quit" menu action), so
// the loop-exit save does not run a second full write+fsync+backup cycle.
static bool quitSaveDone = false;

static const char *ScreenshotDimensionName(WorldDimension dimension)
{
    switch (dimension) {
    case WORLD_DIMENSION_PLANET: return "planet";
    case WORLD_DIMENSION_SPACE: return "space";
    case WORLD_DIMENSION_NETHER: return "nether";
    case WORLD_DIMENSION_HOME:
    default: return "home";
    }
}

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

static bool ObserveEvolutionInfo(const EntityEvolutionDebugInfo *info)
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

static bool CommandLineHasFlag(int argc, char **argv, const char *flag)
{
    for (int index = 1; index < argc; index++) {
        if (strcmp(argv[index], flag) == 0) return true;
    }
    return false;
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

static bool NewWorldSpawnCandidate(int x, int z, TerrainMode mode,
                                   Vector3 *outPosition)
{
    int height = TerrainHeight(x, z, mode);
    int seaLevel = TerrainSeaLevel(mode);
    if ((seaLevel >= 0 && height < seaLevel) || ShouldPlaceTree(x, z, mode)) {
        return false;
    }
    if (height < 0 || height + 3 >= WORLD_HEIGHT) return false;
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

static bool ParsePerfArgs(int argc, char **argv, char *reportPath, size_t reportPathSize,
                         char *baselinePath, size_t baselinePathSize)
{
    bool enabled = false;
    reportPath[0] = '\0';
    baselinePath[0] = '\0';
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--perf") == 0) enabled = true;
        else if (strcmp(argv[i], "--perf-report") == 0 && i + 1 < argc)
            snprintf(reportPath, reportPathSize, "%s", argv[++i]);
        else if (strcmp(argv[i], "--perf-baseline") == 0 && i + 1 < argc)
            snprintf(baselinePath, baselinePathSize, "%s", argv[++i]);
    }
    return enabled;
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
        (float)TerrainHeight((int)floorf(x), (int)floorf(z), terrainMode) + 3.0f, z };
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
    if (!ShipExit(player)) {
        player->floating = true;
        transition->active = false;
        transition->summaryRemaining = 0.0f;
        return;
    }

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

static void DrawEvolutionScanPanel(const EntityEvolutionDebugInfo *info)
{
    if (!info || !info->valid) return;
    int width = 350;
    int x = GetScreenWidth() - width - 18;
    int y = 74;
    float height = evolutionScanLocked ? 320.0f : 150.0f;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y,
                                     (float)width, height },
                         0.04f, 6, Fade((Color){ 10, 18, 24, 255 }, 0.90f));
    DrawRectangleRoundedLinesEx((Rectangle){ (float)x, (float)y,
                                             (float)width, height },
                                0.04f, 6, 1.0f,
                                Fade((Color){ 114, 218, 172, 255 }, 0.72f));
    UiDrawText(TextFormat("%s  SPECIES %08X  //  LINEAGE %08X",
                          evolutionScanLocked ? "LOCKED" : "SCAN",
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
    if (!evolutionScanLocked) return;
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

static int BiologyAtlasFirstSlot(void)
{
    for (int index = 0; index < EVOLUTION_CATALOG_MAX_SPECIES; index++) {
        EvolutionCatalogSpecies species = { 0 };
        if (EvolutionCatalogGetSpecies(index, &species)) return index;
    }
    return -1;
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

static void DrawBiologyAtlas(void)
{
    if (!biologyAtlasOpen) return;
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
        if (slot == biologyAtlasSlot) selectedRank = rank;
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
        bool selected = slot == biologyAtlasSlot;
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
            biologyAtlasSlot = slot;
        }
        visibleIndex++;
    }

    if (biologyAtlasSlot < 0) biologyAtlasSlot = BiologyAtlasFirstSlot();
    EvolutionCatalogSpecies species = { 0 };
    if (!EvolutionCatalogGetSpecies(biologyAtlasSlot, &species)) {
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
    int lockedIndex = evolutionScanLocked
        ? EntityEvolutionFindByOrganism(evolutionLockedOrganismId) : -1;
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
    ShipLocatorReset();
    StarMapClose();
    EntitiesClear();
    ParticlesClear();
    WeatherInit();

    terrainMode = mode;
    player->position = FindNewWorldSpawn(terrainMode);
    player->velocity = Vector3Zero();
    player->yaw = PI;
    player->pitch = -0.25f;
    player->onGround = false;
    player->floating = false;
    PlayerResetRuntimeState(player);

    autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
    dayTime = 0.30f;
    dayCycleEnabled = true;
    UpdateChunks(player->position, EffectiveRenderDistanceForHeight(player->position.y + EYE_HEIGHT));
}

int main(int argc, char **argv)
{
    const int screenWidth = 1280;
    const int screenHeight = 720;

    char perfReportPath[512];
    char perfBaselinePath[512];
    bool perfMode = ParsePerfArgs(argc, argv, perfReportPath, sizeof(perfReportPath),
                                  perfBaselinePath, sizeof(perfBaselinePath));
    bool debugControlEnabled = CommandLineHasFlag(argc, argv, "--debug-stdin");
    GameSettings settings;
    GameSettingsLoad(&settings);
    if (debugControlEnabled) SetTraceLogLevel(LOG_WARNING);
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(screenWidth, screenHeight, "Voxelcraft - raylib");
    if (!IsWindowReady()) {
        fprintf(stderr, "Failed to create a raylib window. Run from a graphical desktop session.\n");
        return 1;
    }
    PerfConfigure(perfMode, perfReportPath, perfBaselinePath);
    SetExitKey(KEY_NULL);
    SetTargetFPS(perfMode ? 0 : 60);
    EnableCursor();
    if (!ChunksStartGenThread()) {
        fprintf(stderr, "Warning: failed to start chunk generation thread; generating synchronously.\n");
    }
    ParticlesInit();
    AudioInit();
    AudioSetVolumes(settings.masterVolume, settings.ambientVolume,
                    settings.musicVolume);
    AudioSetMusicEnabled(settings.musicEnabled);
    WeatherInit();
    WeatherSetParticleScale(
        GraphicsQualityProfileFor(settings.graphicsQuality).precipitationScale);
    AlbumInit();
    SpaceInit();
    NetherInit();
    EntitiesInit();
    blockAtlas = LoadBlockAtlas();
    WorldRendererInit(settings.graphicsQuality);
    cloudModel = LoadCloudModel();
    ShipLoadModel();
    UiFontInit();

    Player player = {
        .position = { 0.5f, 12.0f, 0.5f },
        .velocity = { 0.0f, 0.0f, 0.0f },
        .yaw = PI,
        .pitch = -0.25f,
        .onGround = false,
        .floating = false
    };
    PlayerResetRuntimeState(&player);

    BlockType hotbar[HOTBAR_SIZE] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER, BLOCK_SPACESHIP
    };
    int selectedIndex = 0;
    bool showHelp = true;
    bool showDebug = false;
    bool scannerActive = false;
    bool shipLocatorEnabled = false;
    bool showOrbitTrajectories = true;
    bool screenshotPending = false;
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

    if (perfMode) {
        ChunksResetStreamingStats();
        BeginNewWorld(&player, TERRAIN_VARIED, DEFAULT_WORLD_SEED);
        screen = SCREEN_PLAYING;
        autoSaveEnabled = false;
        showHelp = false;
        DisableCursor();
        PerfSetMetadata(WorldGetSeed(), EffectiveRenderDistanceForHeight(player.position.y + EYE_HEIGHT));
    }

    Camera3D camera = { 0 };
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = CameraFovForHeight(player.position.y + EYE_HEIGHT);
    camera.projection = CAMERA_PERSPECTIVE;
    EnvironmentPresentationRuntime environmentRuntime = { 0 };
    DebugControl debugControl;
    DebugControlInit(&debugControl, debugControlEnabled);
    PlayerInput scriptedPlayerInput = { 0 };
    PlayerInput appliedPlayerInput = { 0 };
    unsigned scriptedInputFrames = 0u;
    bool scriptedInputFirstFrame = false;
    DebugControlReply(
        &debugControl,
        "DEBUG_CONTROL ready commands=start,screenshot,status,teleport,input,"
        "fluid,evolution,quit\n");

    while (!quitRequested && !WindowShouldClose()) {
        PerfBeginFrame();
        float dt = (perfMode || debugControlEnabled) ?
                       (1.0f / 60.0f) : GetFrameTime();
        if (dt > 0.05f) dt = 0.05f;

        bool debugStartRequested = false;
        switch (DebugControlPoll(&debugControl)) {
        case DEBUG_CONTROL_COMMAND_START:
            if (screen == SCREEN_START) {
                debugStartRequested = true;
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL start ignored reason=already_playing\n");
            }
            break;
        case DEBUG_CONTROL_COMMAND_SCREENSHOT:
            if (screen == SCREEN_PLAYING) {
                screenshotPending = true;
                DebugControlReply(&debugControl,
                                  "DEBUG_CONTROL screenshot scheduled\n");
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL screenshot error reason=not_playing\n");
            }
            break;
        case DEBUG_CONTROL_COMMAND_STATUS:
        {
            PlayerWaterState statusWater = PlayerWaterStateAt(player.position);
            FluidSample statusFluid = FluidSampleAt(player.position);
            FluidStats statusFluidStats = FluidGetStats();
            uint64_t statusLoadedVolume = FluidLoadedVolume();
            ChunkWaterRenderDebugInfo statusWaterRender = { 0 };
            ChunksGetWaterRenderDebugInfo(player.position, &statusWaterRender);
            BathymetrySample statusBathymetry = {
                .seaLevel = -1,
                .seabedY = (int)floorf(player.position.y),
                .waterDepth = 0,
                .zone = BATHYMETRY_ZONE_LAND,
                .material = BATHYMETRY_MATERIAL_ROCK
            };
            if (PlanetWorldIsActive()) {
                statusBathymetry = PlanetBathymetryAt(
                    (int)floorf(player.position.x),
                    (int)floorf(player.position.z));
            } else if (HomeWorldSurfaceIsActive()) {
                statusBathymetry = TerrainBathymetryAt(
                    (int)floorf(player.position.x),
                    (int)floorf(player.position.z), terrainMode);
            }
            DebugControlReply(
                &debugControl,
                "DEBUG_CONTROL status screen=%s seed=%u dimension=%s "
                "position=%.6f,%.6f,%.6f velocity=%.6f,%.6f,%.6f "
                "water=%d,%d,%d depth=%.6f surface=%.6f "
                "fluid_volume=%u fluid_surface=%.6f "
                "fluid_flow=%.6f,%.6f,%.6f fluid_queue=%u "
                "fluid_processed=%u fluid_edits=%u fluid_total=%llu "
                "fluid_overflows=%u "
                "bathymetry=%s seabed=%d water_column=%d material=%s "
                "chunk=%d,%d,%d chunk_loaded=%d neighbors=0x%X "
                "water_triangles=%d section_water_triangles=%d "
                "camera_inside_solid=%d\n",
                screen == SCREEN_PLAYING ? "playing" : "start",
                WorldGetSeed(),
                ScreenshotDimensionName(WorldCurrentDimension()),
                player.position.x, player.position.y, player.position.z,
                player.velocity.x, player.velocity.y, player.velocity.z,
                statusWater.feetSubmerged ? 1 : 0,
                statusWater.bodySubmerged ? 1 : 0,
                statusWater.eyesSubmerged ? 1 : 0,
                statusWater.eyeDepth, statusWater.surfaceY,
                (unsigned)statusFluid.volume, statusFluid.surfaceY,
                statusFluid.velocity.x, statusFluid.velocity.y,
                statusFluid.velocity.z, statusFluidStats.activeCells,
                statusFluidStats.lastProcessedCells,
                statusFluidStats.editCount,
                (unsigned long long)statusLoadedVolume,
                statusFluidStats.queueOverflows,
                BathymetryZoneName(statusBathymetry.zone),
                statusBathymetry.seabedY, statusBathymetry.waterDepth,
                BathymetryMaterialName(statusBathymetry.material),
                statusWaterRender.cx, statusWaterRender.cz,
                statusWaterRender.sectionY,
                statusWaterRender.chunkLoaded ? 1 : 0,
                statusWaterRender.neighborLoadedMask,
                statusWaterRender.triangleCount,
                statusWaterRender.sectionTriangleCount,
                PlayerCameraPositionInsideSolid(camera.position) ? 1 : 0);
            break;
        }
        case DEBUG_CONTROL_COMMAND_FLUID_INSPECT:
        {
            int x = debugControl.fluidUsePlayerPosition
                ? (int)floorf(player.position.x) : debugControl.fluidX;
            int y = debugControl.fluidUsePlayerPosition
                ? (int)floorf(player.position.y) : debugControl.fluidY;
            int z = debugControl.fluidUsePlayerPosition
                ? (int)floorf(player.position.z) : debugControl.fluidZ;
            FluidSample sample = FluidSampleAt((Vector3){
                (float)x + 0.5f, (float)y + 0.5f, (float)z + 0.5f
            });
            FluidStats stats = FluidGetStats();
            DebugControlReply(
                &debugControl,
                "DEBUG_CONTROL fluid inspect ok position=%d,%d,%d "
                "volume=%u surface=%.6f flow=%.6f,%.6f,%.6f "
                "queue=%u processed=%u edits=%u total=%llu overflows=%u\n",
                x, y, z, (unsigned)sample.volume, sample.surfaceY,
                sample.velocity.x, sample.velocity.y, sample.velocity.z,
                stats.activeCells, stats.lastProcessedCells, stats.editCount,
                (unsigned long long)FluidLoadedVolume(), stats.queueOverflows);
            break;
        }
        case DEBUG_CONTROL_COMMAND_FLUID_SET:
            if (screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL fluid set error reason=no_active_surface\n");
            } else if (!FluidSetVolumeAt(
                           debugControl.fluidX, debugControl.fluidY,
                           debugControl.fluidZ,
                           (uint8_t)debugControl.fluidVolume)) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL fluid set error reason=cell_unavailable\n");
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL fluid set ok position=%d,%d,%d volume=%u\n",
                    debugControl.fluidX, debugControl.fluidY,
                    debugControl.fluidZ, debugControl.fluidVolume);
            }
            break;
        case DEBUG_CONTROL_COMMAND_FLUID_STEP:
            if (screen != SCREEN_PLAYING || !WorldIsSurfaceActive()) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL fluid step error reason=no_active_surface\n");
            } else {
                FluidStepTicks(debugControl.fluidTicks);
                FluidStats stats = FluidGetStats();
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL fluid step ok ticks=%u queue=%u "
                    "processed=%u total=%llu\n",
                    debugControl.fluidTicks, stats.activeCells,
                    stats.lastProcessedCells,
                    (unsigned long long)FluidLoadedVolume());
            }
            break;
        case DEBUG_CONTROL_COMMAND_TELEPORT:
            if (screen == SCREEN_PLAYING) {
                player.position = (Vector3){ debugControl.teleport.x,
                                              debugControl.teleport.y,
                                              debugControl.teleport.z };
                player.velocity = Vector3Zero();
                player.yaw = debugControl.teleport.yaw;
                player.pitch = debugControl.teleport.pitch;
                player.onGround = false;
                PlayerResetRuntimeState(&player);
                scriptedInputFrames = 0u;
                scriptedInputFirstFrame = false;
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL teleport ok position=%.6f,%.6f,%.6f\n",
                    player.position.x, player.position.y, player.position.z);
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL teleport error reason=not_playing\n");
            }
            break;
        case DEBUG_CONTROL_COMMAND_INPUT:
            if (screen == SCREEN_PLAYING) {
                scriptedPlayerInput = (PlayerInput){
                    .forward = debugControl.playerInput.forward,
                    .strafe = debugControl.playerInput.strafe,
                    .vertical = debugControl.playerInput.vertical,
                    .sprint = debugControl.playerInput.sprint
                };
                scriptedInputFrames = debugControl.playerInput.frames;
                scriptedInputFirstFrame = true;
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL input ok forward=%.3f strafe=%.3f "
                    "vertical=%.3f sprint=%d frames=%u\n",
                    scriptedPlayerInput.forward, scriptedPlayerInput.strafe,
                    scriptedPlayerInput.vertical,
                    scriptedPlayerInput.sprint ? 1 : 0,
                    scriptedInputFrames);
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL input error reason=not_playing\n");
            }
            break;
        case DEBUG_CONTROL_COMMAND_EVOLUTION_INSPECT:
        {
            int index = EntityNearestEvolvable(
                player.position, debugControl.evolutionRadius);
            EntityEvolutionDebugInfo info = { 0 };
            if (index < 0 || !EntityEvolutionInspect(index, &info)) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution inspect none radius=%.3f\n",
                    debugControl.evolutionRadius);
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution inspect ok organism=%u lineage=%u "
                    "species=%u genome=%u generation=%u mutations=%u "
                    "locomotion=%s modules=%u age=%.3f maturity=%.3f "
                    "health=%.3f energy=%.3f diet=%.3f mass=%.3f speed=%.3f "
                    "juvenile=%d pregnant=%d corpse=%d\n",
                    info.organismId, info.lineageId, info.speciesId,
                    info.genomeId, info.generation, info.mutationCount,
                    EvolutionLocomotionName(info.locomotion), info.moduleCount,
                    info.ageDays, info.maturityAgeDays, info.health,
                    info.energy, info.diet, info.mass, info.speed,
                    info.juvenile ? 1 : 0, info.pregnant ? 1 : 0,
                    info.corpse ? 1 : 0);
            }
            break;
        }
        case DEBUG_CONTROL_COMMAND_EVOLUTION_FOCUS:
        {
            int index = EntityNearestEvolvable(
                player.position, debugControl.evolutionRadius);
            EntityEvolutionDebugInfo info = { 0 };
            if (screen != SCREEN_PLAYING ||
                !EntityEvolutionInspect(index, &info)) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution focus none radius=%.3f\n",
                    debugControl.evolutionRadius);
            } else {
                evolutionScanLocked = ObserveEvolutionInfo(&info);
                evolutionLockedOrganismId = evolutionScanLocked
                    ? info.organismId : 0u;
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution focus %s organism=%u species=%u\n",
                    evolutionScanLocked ? "ok" : "error",
                    info.organismId, info.speciesId);
            }
            break;
        }
        case DEBUG_CONTROL_COMMAND_EVOLUTION_REGION:
        case DEBUG_CONTROL_COMMAND_EVOLUTION_BOOTSTRAP:
        {
            PlanetEvolutionRegion evolution = { 0 };
            float evolutionDaylight = 0.0f;
            float evolutionSunset = 0.0f;
            PlanetLightState evolutionLight = { 0 };
            if (PlanetWorldLightStateAt(player.position, &evolutionLight)) {
                evolutionDaylight = evolutionLight.daylight;
            } else {
                DayNightFactors(dayTime, &evolutionDaylight,
                                &evolutionSunset);
            }
            if (!PlanetEcologyEvolutionRegionAt(
                    (int)floorf(player.position.x),
                    (int)floorf(player.position.z), evolutionDaylight,
                    &evolution)) {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution region error reason=no_active_ecology\n");
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution region ok lineages=%u "
                    "bootstrap=%u complete=%d herbivore=%.6f omnivore=%.6f "
                    "carnivore=%.6f\n",
                    evolution.lineageCount, evolution.bootstrapGeneration,
                    evolution.bootstrapComplete ? 1 : 0,
                    evolution.herbivoreDensity, evolution.omnivoreDensity,
                    evolution.carnivoreDensity);
            }
            break;
        }
        case DEBUG_CONTROL_COMMAND_EVOLUTION_ADVANCE:
            if (screen == SCREEN_PLAYING) {
                SpaceAdvanceTime(debugControl.evolutionAdvanceDays);
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution advance ok days=%.3f\n",
                    debugControl.evolutionAdvanceDays);
            } else {
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL evolution advance error reason=not_playing\n");
            }
            break;
        case DEBUG_CONTROL_COMMAND_EVOLUTION_ATLAS:
            if (screen != SCREEN_PLAYING) {
                DebugControlReply(&debugControl,
                                  "DEBUG_CONTROL evolution atlas error reason=not_playing\n");
            } else {
                biologyAtlasOpen = !biologyAtlasOpen;
                biologyAtlasSlot = BiologyAtlasFirstSlot();
                cursorReleased = biologyAtlasOpen;
                if (biologyAtlasOpen) EnableCursor();
                else DisableCursor();
                DebugControlReply(&debugControl,
                                  "DEBUG_CONTROL evolution atlas %s species=%d\n",
                                  biologyAtlasOpen ? "open" : "closed",
                                  EvolutionCatalogSpeciesCount());
            }
            break;
        case DEBUG_CONTROL_COMMAND_EVOLUTION_CATALOG:
            DebugControlReply(&debugControl,
                              "DEBUG_CONTROL evolution catalog ok species=%d individuals=%d surface=%u\n",
                              EvolutionCatalogSpeciesCount(),
                              EvolutionCatalogIndividualCount(),
                              WorldCurrentSurfaceId());
            break;
        case DEBUG_CONTROL_COMMAND_QUIT:
            quitRequested = true;
            DebugControlReply(&debugControl, "DEBUG_CONTROL quit accepted\n");
            break;
        case DEBUG_CONTROL_COMMAND_INVALID:
            DebugControlReply(
                &debugControl,
                "DEBUG_CONTROL error reason=unknown_command\n");
            break;
        case DEBUG_CONTROL_COMMAND_NONE:
        default:
            break;
        }

        if (perfMode) ApplyPerfRoute(&player, PerfFrameIndex());

        bool landingSkipPressed = LandingTransitionUpdate(&landingTransition, &player, dt);

        if (!perfMode && screen == SCREEN_START) {
            AudioSetEnvironment(NULL);
            AudioUpdate(dt);
            bool startGame = false;
            if (IsKeyPressed(KEY_ESCAPE)) quitRequested = true;
            BeginDrawing();
            DrawStartPage(&startGame, &quitRequested, &selectedTerrain, &selectedSeed);
            if (debugStartRequested) startGame = true;
            EndDrawing();

            if (startGame) {
                BeginNewWorld(&player, selectedTerrain, selectedSeed);
                EnvironmentPresentationRuntimeReset(&environmentRuntime);
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
                shipLocatorEnabled = false;
                landingTransition = (LandingTransition){ 0 };
                paused = false;
                screen = SCREEN_PLAYING;
                cursorReleased = false;
                DisableCursor();
                SetImportMessage(terrainMode == TERRAIN_FLAT ?
                                 TextFormat("Flat world seed %u. Press I to import.", WorldGetSeed()) :
                                 TextFormat("World seed %u.", WorldGetSeed()));
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL start ok seed=%u\n", WorldGetSeed());
            }
            continue;
        }

        if (!perfMode && IsKeyPressed(KEY_F10)) screenshotPending = true;

        bool biologyAtlasClosed = false;
        if (biologyAtlasOpen && IsKeyPressed(KEY_ESCAPE)) {
            biologyAtlasOpen = false;
            biologyAtlasClosed = true;
            cursorReleased = false;
            DisableCursor();
        }
        if (!importDialog.open && !albumOpen && !StarMapIsOpen() &&
            !biologyAtlasOpen && !biologyAtlasClosed) {
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

        if (!paused && !albumOpen && !importDialog.open && !landingTransition.active &&
            !biologyAtlasOpen) {
            SpaceAdvanceTime(dt);
        }

        if (biologyAtlasOpen && !biologyAtlasClosed && IsKeyPressed(KEY_B)) {
            biologyAtlasOpen = false;
            cursorReleased = false;
            DisableCursor();
        } else if (!biologyAtlasOpen && !importDialog.open && !paused &&
                   !albumOpen && !landingTransition.active && !cursorReleased &&
                   IsKeyPressed(KEY_B)) {
            biologyAtlasOpen = true;
            biologyAtlasSlot = BiologyAtlasFirstSlot();
            player.velocity = Vector3Zero();
            cursorReleased = true;
            EnableCursor();
        }
        if (biologyAtlasOpen) {
            if (IsKeyPressed(KEY_UP)) {
                biologyAtlasSlot = BiologyAtlasNextSlot(biologyAtlasSlot, -1);
            } else if (IsKeyPressed(KEY_DOWN)) {
                biologyAtlasSlot = BiologyAtlasNextSlot(biologyAtlasSlot, 1);
            }
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

        bool inputBlocked = perfMode || paused || cursorReleased || importDialog.open || albumOpen ||
                            biologyAtlasOpen ||
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
        // Manual save stays reachable while flying the ship: saving does not
        // require parking (ShipSaveState only persists fuel), and a forced
        // exit can fail when no clear 4x4 spot is nearby, which used to
        // silently drop the save (data loss).
        if (IsKeyPressed(KEY_F5) && !paused && !cursorReleased &&
            !importDialog.open && !albumOpen && !landingTransition.active &&
            !StarMapIsOpen()) {
            SaveMap(&player);
        }
        if (!inputBlocked) {
            int hotbarKey = HotbarKeyToIndex();
            if (hotbarKey >= 0 && hotbarKey < HOTBAR_SIZE) selectedIndex = hotbarKey;
            float wheel = GetMouseWheelMove();
            if (wheel > 0.0f) selectedIndex = (selectedIndex + HOTBAR_SIZE - 1) % HOTBAR_SIZE;
            else if (wheel < 0.0f) selectedIndex = (selectedIndex + 1) % HOTBAR_SIZE;
            if (IsKeyPressed(KEY_LEFT_BRACKET)) AdjustRenderDistance(-1);
            if (IsKeyPressed(KEY_RIGHT_BRACKET)) AdjustRenderDistance(1);
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
            if (IsKeyPressed(KEY_L)) {
                shipLocatorEnabled = !shipLocatorEnabled;
                if (shipLocatorEnabled) {
                    SetImportMessage(ShipLocatorHasTarget() ?
                                     "Ship locator online." :
                                     "Ship locator online: deploy or board a ship to establish a signal.");
                } else {
                    SetImportMessage("Ship locator offline.");
                }
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

        if (!perfMode && autoSaveEnabled && screen == SCREEN_PLAYING && !paused &&
            !landingTransition.active) {
            autoSaveTimer -= dt;
            if (autoSaveTimer <= 0.0f) {
                autoSaveTimer = AUTO_SAVE_INTERVAL_SECONDS;
                SaveMap(&player);
            }
        }

        if (!perfMode && dayCycleEnabled && !paused && !albumOpen && !landingTransition.active) {
            dayTime += dt / DAY_LENGTH_SECONDS;
            if (dayTime >= 1.0f) dayTime -= 1.0f;
        }
        if (!paused && !albumOpen && !landingTransition.active &&
            (HomeWorldSurfaceIsActive() || PlanetWorldIsActive())) {
            bool weatherSheltered = EnvironmentSheltered(player.position) ||
                PlayerWaterStateAt(player.position).eyesSubmerged;
            WeatherSetSheltered(weatherSheltered);
            WeatherUpdate(dt, player.position);
        } else if (!HomeWorldSurfaceIsActive() && !PlanetWorldIsActive()) {
            WeatherSetSheltered(false);
            WeatherSuspend();
        }

        if (!perfMode && !landingTransition.active && ShipIsDriving() && !StarMapIsOpen()) {
            ShipUpdate(&player, dt);
            if (PlanetWorldTryLaunch(&player) || HomeWorldTryLaunch(&player)) {
                wasInSpace = true;
            }
        } else if (!perfMode && !inputBlocked) {
            if (scriptedInputFrames > 0u) {
                appliedPlayerInput = scriptedPlayerInput;
                appliedPlayerInput.jumpPressed = scriptedInputFirstFrame &&
                                                 scriptedPlayerInput.vertical > 0.0f;
                scriptedInputFirstFrame = false;
                scriptedInputFrames--;
            } else if (debugControlEnabled) {
                // Debug sessions are driven exclusively by stdin so a
                // desktop key held by the test runner cannot leak into the
                // simulation after a scripted input window expires.
                appliedPlayerInput = (PlayerInput){ 0 };
            } else {
                appliedPlayerInput = PlayerInputFromKeyboard();
            }
            UpdatePlayerWithInput(&player, dt, &appliedPlayerInput);
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
            evolutionScanLocked = false;
            evolutionLockedOrganismId = 0u;
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
            if (HomeWorldSurfaceIsActive() &&
                WorldCurrentDimensionAt(player.position.y + EYE_HEIGHT) == WORLD_DIMENSION_NETHER) {
                UpdateNetherChunks(player.position, effectiveRenderDistance, 4);
            }
            SpaceUpdateSolarGlow(player.position);
        }
        ProcessFinishedMeshJobs(2.0);
        ProcessFinishedChunkJobs();
        if (!paused && !albumOpen && !importDialog.open &&
            !landingTransition.active && !biologyAtlasOpen &&
            localWorldActive) {
            FluidUpdate(dt);
        }
        RebuildDirtyChunkMeshes(player.position);
        ParticlesUpdate(dt);

        UpdatePlayerCamera(&camera, &player, dt, thirdPerson);
        effectiveRenderDistance = EffectiveRenderDistanceForHeight(camera.position.y);

        Vector3 aimEye = { player.position.x, player.position.y + EYE_HEIGHT, player.position.z };
        Vector3 aimDir = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        HitResult hit = RaycastBlocksFiltered(aimEye, aimDir, REACH_DISTANCE,
                                              RAYCAST_BLOCK_SOLID);
        HitResult interactionHit = RaycastBlocksFiltered(
            aimEye, aimDir, REACH_DISTANCE, RAYCAST_BLOCK_ALL);
        int entityHit = EntityRayHit(aimEye, aimDir, REACH_DISTANCE);
        if (!inputBlocked && IsKeyPressed(KEY_N)) {
            if (evolutionScanLocked) {
                evolutionScanLocked = false;
                evolutionLockedOrganismId = 0u;
                SetImportMessage("Evolution scan unlocked.");
            } else {
                EntityEvolutionDebugInfo scanInfo = { 0 };
                if (EntityEvolutionInspect(entityHit, &scanInfo)) {
                    evolutionScanLocked = ObserveEvolutionInfo(&scanInfo);
                    evolutionLockedOrganismId = evolutionScanLocked
                        ? scanInfo.organismId : 0u;
                    SetImportMessage(evolutionScanLocked
                        ? "Evolution scan locked; species added to atlas."
                        : "Evolution scan failed.");
                } else {
                    SetImportMessage("Aim at an evolvable organism to scan.");
                }
            }
        }
        SpaceBodyInfo aimBody = { 0 };
        bool haveAimBody = SpaceBodyPick(aimEye, aimDir, &aimBody);
        ParkedShip hitShip = { 0 };
        bool hitParkedShip = hit.hit &&
                             ShipResolveParkedAt(hit.x, hit.y, hit.z, &hitShip);
        if (!inputBlocked && interactionHit.hit &&
            GetBlockAt(interactionHit.x, interactionHit.y, interactionHit.z) ==
                BLOCK_WATER &&
            IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (InventoryCount(BLOCK_WATER) >= INVENTORY_MAX_PER_BLOCK) {
                SetImportMessage("Inventory full: Water");
            } else if (FluidTryCollectUnit(
                           interactionHit.x, interactionHit.y,
                           interactionHit.z)) {
                InventoryAdd(BLOCK_WATER, 1);
                AudioPlayPick();
                SetImportMessage(TextFormat("Collected Water (%d)",
                                            InventoryCount(BLOCK_WATER)));
            } else {
                SetImportMessage("Need 255 connected water volume to collect.");
            }
        } else if (!inputBlocked && entityHit >= 0 && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            float harvestDaylight = 0.0f;
            float harvestSunset = 0.0f;
            PlanetLightState harvestLight = { 0 };
            if (!PlanetWorldLightStateAt(player.position, &harvestLight)) {
                DayNightFactors(dayTime, &harvestDaylight, &harvestSunset);
            } else {
                harvestDaylight = harvestLight.daylight;
            }
            EntityKill(entityHit, ENTITY_DEATH_PLAYER, harvestDaylight);
        } else if (!inputBlocked && hitParkedShip &&
                   IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            if (InventoryAdd(BLOCK_SPACESHIP, 1) > 0) {
                Vector3 center = {
                    (float)hitShip.coreX + (hitShip.legacy ? 0.5f : 1.0f),
                    (float)hitShip.coreY + 0.5f,
                    (float)hitShip.coreZ + (hitShip.legacy ? 0.5f : 1.0f)
                };
                ShipRemoveParkedAt(hit.x, hit.y, hit.z, true);
                ParticlesEmitBurst(center, BlockBaseColor(BLOCK_SPACESHIP),
                                   20, 3.0f, 0.7f);
                AudioPlayBreak();
            } else {
                SetImportMessage("Inventory full: Spaceship");
            }
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
        ShipDirection placementDirection = ShipDirectionFromYaw(player.yaw);
        if (!inputBlocked && hit.hit) {
            placeX = hit.x + hit.nx;
            placeY = hit.y + hit.ny;
            placeZ = hit.z + hit.nz;
            if (hotbar[selectedIndex] == BLOCK_SPACESHIP) {
                canPlace = InventoryCount(BLOCK_SPACESHIP) > 0 &&
                           ShipCanPlaceParked(placeX, placeY, placeZ,
                                              placementDirection, &player);
            } else {
                BlockType selectedType = hotbar[selectedIndex];
                BlockType targetType = GetBlockAt(placeX, placeY, placeZ);
                bool targetAvailable = targetType == BLOCK_AIR ||
                    (WorldIsSurfaceActive() && targetType == BLOCK_WATER);
                canPlace = InventoryCount(hotbar[selectedIndex]) > 0 &&
                           targetAvailable &&
                           WorldBlockRegionAt(placeY) != WORLD_BLOCK_REGION_NONE &&
                           (selectedType == BLOCK_WATER ||
                            !BlockWouldOverlapPlayer(placeX, placeY, placeZ,
                                                     player.position));
                if (selectedType == BLOCK_WATER) {
                    canPlace = canPlace && WorldIsSurfaceActive() &&
                               WorldBlockRegionAt(placeY) ==
                                   WORLD_BLOCK_REGION_SURFACE;
                }
            }
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
                if (placedType == BLOCK_SPACESHIP) {
                    if (InventoryConsume(placedType, 1)) {
                        if (!ShipPlaceParked(placeX, placeY, placeZ,
                                             placementDirection, true)) {
                            InventoryAdd(placedType, 1);
                            SetImportMessage("Spaceship needs a clear 4x4 area.");
                        } else {
                            ParticlesEmitBurst(
                                (Vector3){ placeX + 1.0f, placeY + 0.5f,
                                           placeZ + 1.0f },
                                BlockBaseColor(placedType), 16, 2.5f, 0.6f);
                            AudioPlayPlace();
                        }
                    }
                } else if (placedType == BLOCK_WATER) {
                    if (InventoryConsume(placedType, 1)) {
                        if (!FluidTryDepositUnit(placeX, placeY, placeZ)) {
                            InventoryAdd(placedType, 1);
                            SetImportMessage(
                                "Water needs 255 free loaded neighbor volume.");
                        } else {
                            ParticlesEmitBurst(
                                (Vector3){ placeX + 0.5f, placeY + 0.5f,
                                           placeZ + 0.5f },
                                BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                            AudioPlayPlace();
                        }
                    }
                } else if (InventoryConsume(placedType, 1)) {
                    if (!SetBlock(placeX, placeY, placeZ, placedType)) {
                        InventoryAdd(placedType, 1);
                        SetImportMessage(
                            "Cannot place block: water has no loaded escape volume.");
                    } else {
                        ParticlesEmitBurst(
                            (Vector3){ placeX + 0.5f, placeY + 0.5f,
                                       placeZ + 0.5f },
                            BlockBaseColor(placedType), 8, 2.0f, 0.5f);
                        AudioPlayPlace();
                    }
                }
            } else if (hotbar[selectedIndex] == BLOCK_SPACESHIP &&
                       InventoryCount(BLOCK_SPACESHIP) > 0) {
                SetImportMessage("Spaceship needs a clear 4x4 area.");
            }
        }
        if (!inputBlocked && hit.hit && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            BlockType picked = GetBlockAt(hit.x, hit.y, hit.z);
            if (ShipResolveParkedAt(hit.x, hit.y, hit.z, NULL)) {
                picked = BLOCK_SPACESHIP;
            }
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
        PlanetObservationState planetObservation =
            PlanetObservationForCamera(&camera, &planetLight);
        double weatherSimulationTime = SpacePeriodicSimulationTime(
            SpaceElapsedSimulationTime());
        WeatherVisualState weatherVisual = WeatherVisualStateAtWorld(
            camera.position, weatherSimulationTime, daylight);
        float planetSeasonProgress = -1.0f;
        if (PlanetWorldIsActive()) {
            const PlanetProfile *profile = PlanetWorldProfile();
            float radius = fmaxf(profile->spaceProxyRadius, 24.0f);
            float latitude = player.position.z / (radius * 0.82f);
            PlanetSeasonState season = { 0 };
            if (PlanetSeasonEvaluate(profile, latitude,
                                     SpacePeriodicSimulationTime(
                                         SpaceElapsedSimulationTime()),
                                     &season)) {
                planetSeasonProgress = season.seasonAngle / (2.0f * PI);
            }
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
        Color worldTint = MixWeather(WorldTintForLight(daylight, sunset), daylight,
                                     &weatherVisual);
        skyTop = MixWeather(skyTop, daylight, &weatherVisual);
        skyHorizon = MixWeather(skyHorizon, daylight, &weatherVisual);
        ApplyPlanetWorldPaletteWithObservation(&skyTop, &skyHorizon, &worldTint,
                                                &planetLight, &planetObservation);
        float planetAtmosphereFade = PlanetWorldAtmosphereFade(camera.position);
        float skyFade = fmaxf(spaceFade, planetAtmosphereFade);
        UpdatePlanetSceneExposure(&camera);
        skyTop = ColorLerp(skyTop, BLACK, skyFade);
        skyHorizon = ColorLerp(skyHorizon, BLACK, skyFade);
        worldTint = ColorLerp(worldTint, (Color){ 46, 54, 78, 255 }, skyFade);
        WorldDimension cameraDimension =
            WorldCurrentDimensionAt(camera.position.y);
        bool inNether = cameraDimension == WORLD_DIMENSION_NETHER;
        if (inNether) {
            skyTop = (Color){ 24, 6, 6, 255 };
            skyHorizon = (Color){ 40, 10, 8, 255 };
            worldTint = (Color){ 150, 62, 42, 255 };
            spaceFade = 0.0f;
        }
        PlayerWaterState playerWater = PlayerWaterStateAt(player.position);
        bool underwater = playerWater.eyesSubmerged && IsWaterBlock(GetBlockAt(
            (int)floorf(camera.position.x), (int)floorf(camera.position.y),
            (int)floorf(camera.position.z)));
        float underwaterDepth = underwater ? playerWater.eyeDepth : 0.0f;
        if (underwater) {
            float deep = Clamp(
                underwaterDepth / UNDERWATER_DEEP_REFERENCE_DEPTH,
                0.0f, 1.0f);
            skyHorizon = WorldLightingUnderwaterFogColor(underwaterDepth);
            skyTop = ColorLerp(skyHorizon, (Color){ 3, 18, 30, 255 },
                               0.28f + deep * 0.42f);
        }
        EnvironmentScene environmentScene =
            EnvironmentSceneForDimension(cameraDimension);
        bool forest = false;
        if (environmentScene == ENVIRONMENT_SCENE_HOME) {
            forest = BiomeAt((int)floorf(player.position.x),
                             (int)floorf(player.position.z)) == BIOME_FOREST;
        } else if (environmentScene == ENVIRONMENT_SCENE_PLANET) {
            forest = PlanetBiomeAt((int)floorf(player.position.x),
                                   (int)floorf(player.position.z)) ==
                     PLANET_BIOME_FOREST;
        }
        EnvironmentRuntimeSample environmentSample = {
            .dimension = cameraDimension,
            .quality = settings.graphicsQuality,
            .weather = weatherVisual,
            .simulationTime = weatherSimulationTime,
            .daylight = daylight,
            .sunset = sunset,
            .atmosphereFade = skyFade,
            .altitude = camera.position.y -
                        (float)WorldSurfaceHeightAt(
                            (int)floorf(camera.position.x),
                            (int)floorf(camera.position.z)),
            .underwaterDepth = underwaterDepth,
            .underwater = underwater,
            .sheltered = EnvironmentSheltered(camera.position),
            .forest = forest,
            .nearWater = EnvironmentNearWater(camera.position),
            .shipInterior = environmentScene == ENVIRONMENT_SCENE_SPACE &&
                            ShipIsDriving()
        };
        BathymetrySample bathymetryForDebug = {
            .seaLevel = -1,
            .seabedY = (int)floorf(player.position.y),
            .waterDepth = 0,
            .zone = BATHYMETRY_ZONE_LAND,
            .material = BATHYMETRY_MATERIAL_ROCK
        };
        if (PlanetWorldIsActive()) {
            bathymetryForDebug = PlanetBathymetryAt(
                (int)floorf(player.position.x),
                (int)floorf(player.position.z));
        } else if (HomeWorldSurfaceIsActive()) {
            bathymetryForDebug = TerrainBathymetryAt(
                (int)floorf(player.position.x),
                (int)floorf(player.position.z), terrainMode);
        }
        EnvironmentPresentationState environmentPresentation =
            EnvironmentPresentationRuntimeUpdate(
                &environmentRuntime, &environmentSample, dt);
        AudioEnvironmentState audioEnvironment =
            AudioEnvironmentFromPresentation(&environmentPresentation);
        if (albumOpen || importDialog.open || screen != SCREEN_PLAYING) {
            audioEnvironment = (AudioEnvironmentState){ 0 };
        }
        AudioSetEnvironment(&audioEnvironment);
        AudioUpdate(dt);
        WorldLightingState worldLighting = WorldLightingForScene(
            &camera, dayTime, daylight, sunset, &planetLight, &weatherVisual,
            skyHorizon, inNether, &environmentPresentation);
        PerfSetMetadata(WorldGetSeed(), effectiveRenderDistance);
        PerfMarkUpdateComplete();

        BeginDrawing();
        ClearBackground(skyTop);
        DrawRectangleGradientV(0, 0, GetScreenWidth(), GetScreenHeight(), skyTop, skyHorizon);
        if (!underwater) {
            DrawPlanetAtmosphereSky(&camera, &planetLight, &planetObservation,
                                    &weatherVisual);
        }

        PerfBeginGpuFrame();
        bool drawSurfaceChunks = PlanetWorldIsActive() ||
                                 (HomeWorldSurfaceIsActive() && !inNether && spaceFade <= 0.05f);
        DrawWorldShadowMap(&camera, effectiveRenderDistance, drawSurfaceChunks,
                           inNether, &worldLighting);
        BeginMode3D(camera);
        DrawWorld(&camera, effectiveRenderDistance, worldTint, drawSurfaceChunks,
                  inNether, &worldLighting);
        if (localWorldActive) EntitiesDraw();
        // Keep the first-person flight view clear. The ship model is only useful
        // as an exterior reference when the camera is in third person.
        if (ShipIsDriving() && thirdPerson) ShipDraw(&player);
        if (!underwater) {
            DrawHomePlanet(&camera, spaceFade);
            if (showOrbitTrajectories) {
                DrawSolarOrbitTrajectories(&camera, spaceFade);
            }
            DrawSolarBodies(&camera, spaceFade);
        }
        bool drawCloudLayer = weatherVisual.active;
        if (skyFade < 0.5f && !inNether && !underwater && drawCloudLayer) {
            DrawClouds(&camera, Fade(worldTint, 1.0f - skyFade * 2.0f),
                       weatherSimulationTime, &weatherVisual,
                       &environmentPresentation, &worldLighting);
        }
        ParticlesDraw();
        if (hit.hit) {
            if (hitParkedShip && !hitShip.legacy) {
                Vector3 center = {
                    hitShip.coreX + 1.0f, hitShip.coreY + 1.0f,
                    hitShip.coreZ + 1.0f
                };
                DrawCubeWires(center, 4.03f, 2.03f, 4.03f, WHITE);
            } else {
                Vector3 center = { hit.x + 0.5f, hit.y + 0.5f, hit.z + 0.5f };
                DrawCubeWires(center, 1.03f, 1.03f, 1.03f, WHITE);
            }
        }
        if (canPlace) {
            if (hotbar[selectedIndex] == BLOCK_SPACESHIP) {
                Vector3 center = { placeX + 1.0f, placeY + 1.0f, placeZ + 1.0f };
                DrawCubeWires(center, 4.02f, 2.02f, 4.02f,
                              Fade(GREEN, 0.9f));
            } else {
                Vector3 center = { placeX + 0.5f, placeY + 0.5f, placeZ + 0.5f };
                DrawCubeWires(center, 1.02f, 1.02f, 1.02f, Fade(GREEN, 0.9f));
            }
        }
        EndMode3D();
        PerfEndGpuFrame();

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

        if (!underwater) {
            DrawStars(&camera, inNether ? 1.0f :
                      1.0f - environmentPresentation.starVisibility,
                      &planetObservation, &weatherVisual);
            DrawSpaceSky(skyFade, daylight, &camera);
            if (spaceFade < 0.5f && !inNether) {
                DrawCelestial(&camera, dayTime, daylight, &planetLight,
                              &planetObservation, &weatherVisual);
            }
            if (!inNether && skyFade < 0.5f) {
                DrawWeatherOverlay(&camera, &weatherVisual);
            }
        }
        DrawEnvironmentPostProcess(&environmentPresentation);
        DrawSolarGuide(&camera, spaceFade);
        if (scannerActive && PlanetWorldIsActive()) PlanetPoiDrawScanner(&camera, player.position);
        ShipLocatorTarget shipLocatorTarget = { 0 };
        if (shipLocatorEnabled && !ShipIsDriving() &&
            ShipLocatorTargetAt(player.position, &shipLocatorTarget)) {
            DrawShipLocator(&camera, &shipLocatorTarget);
        }
        if (ShipIsDriving()) DrawShipHud();
        if (spaceFade > 0.05f && haveAimBody && !StarMapIsOpen()) {
            DrawBodyInfoPanel(&aimBody);
        }
        if (!biologyAtlasOpen) DrawCrosshair(GetScreenWidth(), GetScreenHeight());
        EntityEvolutionDebugInfo aimedEvolution = { 0 };
        int evolutionDisplayEntity = entityHit;
        if (evolutionScanLocked) {
            evolutionDisplayEntity = EntityEvolutionFindByOrganism(
                evolutionLockedOrganismId);
            if (evolutionDisplayEntity < 0) {
                evolutionScanLocked = false;
                evolutionLockedOrganismId = 0u;
            }
        }
        if (EntityEvolutionInspect(evolutionDisplayEntity, &aimedEvolution)) {
            DrawEvolutionScanPanel(&aimedEvolution);
        }
        if (!biologyAtlasOpen) {
            DrawHotbar(hotbar, selectedIndex);
            DrawImportStatus();
            int hour = (int)(dayTime * 24.0f) % 24;
            const char *positionText = TextFormat("XYZ %d %d %d    %02d:00", (int)floorf(player.position.x),
                                                  (int)floorf(player.position.y),
                                                  (int)floorf(player.position.z), hour);
            UiDrawText(positionText, 15, GetScreenHeight() - 32, 17, Fade(BLACK, 0.92f));
            UiDrawText(positionText, 14, GetScreenHeight() - 34, 17, Fade(WHITE, 0.9f));
            const char *saveText = TextFormat("Auto-save: %s", autoSaveEnabled ? "60s" : "off");
            UiDrawText(saveText, 15, GetScreenHeight() - 14, 15, Fade(BLACK, 0.92f));
            UiDrawText(saveText, 14, GetScreenHeight() - 16, 15, Fade(WHITE, 0.65f));
            if (cursorReleased && !importDialog.open) DrawCursorReleasedOverlay();
        }
        if (showHelp && !biologyAtlasOpen) {
            DrawHelpPanel(player.floating, cursorReleased, renderDistanceChunks);
        }
        DrawImportDialog(&importDialog);
        AlbumDraw();
        StarMapDraw();
        if (showDebug && !biologyAtlasOpen) {
            dayTimeForHud = dayTime;
            autoSaveForHud = autoSaveEnabled;
            blockForHud = hit.hit ? GetBlockAt(hit.x, hit.y, hit.z) : BLOCK_AIR;
            SpaceEditCountForHud = GetSpaceEditCount();
            DrawDebugHUD(player.position, player.yaw, player.pitch, daylight,
                         &planetLight, &planetObservation,
                         planetSeasonProgress, &weatherVisual,
                         &bathymetryForDebug);
        }
        DrawBiologyAtlas();
        DrawLandingTransitionOverlay(&landingTransition);
        if (paused) {
            if (IsKeyPressed(KEY_MINUS)) {
                settings.masterVolume = fmaxf(0.0f, settings.masterVolume - 0.1f);
                AudioSetVolumes(settings.masterVolume, settings.ambientVolume,
                                settings.musicVolume);
                GameSettingsSave(&settings);
            }
            if (IsKeyPressed(KEY_EQUAL)) {
                settings.masterVolume = fminf(1.0f, settings.masterVolume + 0.1f);
                AudioSetVolumes(settings.masterVolume, settings.ambientVolume,
                                settings.musicVolume);
                GameSettingsSave(&settings);
            }
            PauseMenuActions pauseActions = { 0 };
            GraphicsQuality previousQuality = settings.graphicsQuality;
            DrawPauseMenu(&settings, &pauseActions);
            if (pauseActions.settingsChanged) {
                if (pauseActions.qualityChanged &&
                    !WorldRendererSetQuality(settings.graphicsQuality)) {
                    settings.graphicsQuality = previousQuality;
                    SetImportMessage("Graphics quality change failed; previous quality restored.");
                }
                WeatherSetParticleScale(
                    GraphicsQualityProfileFor(settings.graphicsQuality).precipitationScale);
                AudioSetVolumes(settings.masterVolume, settings.ambientVolume,
                                settings.musicVolume);
                AudioSetMusicEnabled(settings.musicEnabled);
                GameSettingsSave(&settings);
            }
            if (pauseActions.resume) {
                paused = false;
                DisableCursor();
            }
            if (pauseActions.saveWorld) {
                // Saving is safe while flying: ShipSaveState only persists
                // fuel. Do not gate the save on a successful parking spot
                // (ShipForceExit can fail in a tight dock, which used to
                // silently drop the save).
                SaveMap(&player);
            }
            if (pauseActions.returnToMenu) {
                paused = false;
                cursorReleased = false;
                if (albumOpen) {
                    albumOpen = false;
                    AlbumClose();
                }
                screen = SCREEN_START;
                AudioSetEnvironment(NULL);
                EnableCursor();
            }
            if (pauseActions.saveAndQuit) {
                SaveMap(&player);
                quitSaveDone = true;
                quitRequested = true;
            }
        }

        EndDrawing();
        if (screenshotPending) {
            char screenshotPath[512];
            char debugReportPath[512];
            time_t screenshotTime = time(NULL);
            ChunkStreamingStats streamingStats = ChunksGetStreamingStats();
            ChunkWaterRenderDebugInfo screenshotWaterRender = { 0 };
            ChunksGetWaterRenderDebugInfo(player.position,
                                          &screenshotWaterRender);
            EntityEvolutionDebugInfo screenshotEntity = { 0 };
            int screenshotEntityIndex = evolutionScanLocked
                ? EntityEvolutionFindByOrganism(evolutionLockedOrganismId)
                : EntityNearestEvolvable(player.position, 32.0f);
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
                (int)floorf(player.position.x),
                (int)floorf(player.position.z), daylight, &screenshotRegion);
            FluidSample screenshotFluid = FluidSampleAt(player.position);
            FluidStats screenshotFluidStats = FluidGetStats();
            ScreenshotDebugInfo debugInfo = {
                .world = {
                    .seed = PlanetWorldIsActive() ? PlanetWorldSeed() :
                                                    WorldGetSeed(),
                    .surfaceId = WorldCurrentSurfaceId(),
                    .dimension = ScreenshotDimensionName(cameraDimension),
                    .dayTime = dayTime,
                    .daylight = daylight,
                    .dayCycleEnabled = dayCycleEnabled
                },
                .player = {
                    .position = ScreenshotVector(player.position),
                    .velocity = ScreenshotVector(player.velocity),
                    .yaw = player.yaw,
                    .pitch = player.pitch,
                    .onGround = player.onGround,
                    .floating = player.floating,
                    .driving = ShipIsDriving()
                },
                .camera = {
                    .position = ScreenshotVector(camera.position),
                    .target = ScreenshotVector(camera.target),
                    .fovY = camera.fovy,
                    .thirdPerson = thirdPerson,
                    .insideSolid =
                        PlayerCameraPositionInsideSolid(camera.position)
                },
                .weather = {
                    .name = WeatherName(),
                    .simulationTime = weatherSimulationTime,
                    .active = weatherVisual.active,
                    .atmosphereDensity = weatherVisual.atmosphereDensity,
                    .cloudCover = weatherVisual.cloudCover,
                    .cloudBaseHeight = weatherVisual.cloudBaseHeight,
                    .cloudThickness = weatherVisual.cloudThickness,
                    .cloudOpacity = weatherVisual.cloudOpacity,
                    .fogDensity = weatherVisual.fogDensity,
                    .visibility = weatherVisual.visibility,
                    .precipitationVeil = weatherVisual.precipitationVeil,
                    .stormDarkening = weatherVisual.stormDarkening,
                    .windDrift = weatherVisual.windDrift,
                    .windAngle = weatherVisual.windAngle,
                    .snowFraction = weatherVisual.snowFraction
                },
                .environment = {
                    .altitude = environmentSample.altitude,
                    .atmosphereFade = skyFade,
                    .underwaterDepth = environmentSample.underwaterDepth,
                    .waterSurfaceY = playerWater.surfaceY,
                    .seabedY = bathymetryForDebug.seabedY,
                    .waterColumnDepth = bathymetryForDebug.waterDepth,
                    .bathymetryZone = BathymetryZoneName(bathymetryForDebug.zone),
                    .seabedMaterial = BathymetryMaterialName(
                        bathymetryForDebug.material),
                    .underwater = environmentSample.underwater,
                    .feetSubmerged = playerWater.feetSubmerged,
                    .bodySubmerged = playerWater.bodySubmerged,
                    .eyesSubmerged = playerWater.eyesSubmerged,
                    .sheltered = environmentSample.sheltered,
                    .forest = environmentSample.forest,
                    .nearWater = environmentSample.nearWater,
                    .shipInterior = environmentSample.shipInterior
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
                    .forward = appliedPlayerInput.forward,
                    .strafe = appliedPlayerInput.strafe,
                    .vertical = appliedPlayerInput.vertical,
                    .sprint = appliedPlayerInput.sprint,
                    .remainingFrames = scriptedInputFrames
                },
                .render = {
                    .graphicsQuality = GraphicsQualityName(settings.graphicsQuality),
                    .renderDistanceChunks = effectiveRenderDistance,
                    .fps = GetFPS(),
                    .screenWidth = GetScreenWidth(),
                    .screenHeight = GetScreenHeight(),
                    .frameTimeMs = dt * 1000.0f,
                    .performanceMode = perfMode
                },
                .ui = {
                    .paused = paused,
                    .albumOpen = albumOpen,
                    .starMapOpen = StarMapIsOpen(),
                    .importDialogOpen = importDialog.open,
                    .cursorReleased = cursorReleased,
                    .helpVisible = showHelp,
                    .debugHudVisible = showDebug,
                    .landingTransitionActive = landingTransition.active
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
                    .scanLocked = evolutionScanLocked,
                    .atlasOpen = biologyAtlasOpen,
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
                        &debugControl,
                        "DEBUG_CONTROL capture ok png=%s report=%s\n",
                        screenshotPath, debugReportPath);
                } else {
                    SetImportMessage(TextFormat(
                        "Screenshot saved; %s",
                        ScreenshotResultMessage(reportResult)));
                    DebugControlReply(
                        &debugControl,
                        "DEBUG_CONTROL capture partial png=%s error=%s\n",
                        screenshotPath, ScreenshotResultMessage(reportResult));
                }
            } else {
                SetImportMessage(ScreenshotResultMessage(screenshotResult));
                DebugControlReply(
                    &debugControl,
                    "DEBUG_CONTROL capture error reason=%s\n",
                    ScreenshotResultMessage(screenshotResult));
            }
            screenshotPending = false;
        }
        PerfEndFrame(ChunksGetStreamingStats(), CurrentRenderResourceSnapshot());
        if (perfMode && PerfReportWritten() && !debugControlEnabled) {
            quitRequested = true;
        }
    }

    if (!perfMode && screen == SCREEN_PLAYING) {
        if (landingTransition.active) {
            landingTransition.elapsed = landingTransition.duration;
            LandingTransitionUpdate(&landingTransition, &player, 0.0f);
        }
        // Always save on quit, including while flying (fuel-only state is
        // safe to persist) and when no parking spot is available; the menu's
        // "Save & Quit" already saved, so skip the duplicate cycle.
        if (!quitSaveDone) SaveMap(&player);
    }
    GameSettingsSave(&settings);
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
    AudioShutdown();
    UiFontShutdown();
    bool perfPassed = PerfReportPassed();
    PerfShutdown();
    CloseWindow();
    AlbumCleanup();
    WorldCleanup();
    DebugControlReply(&debugControl, "DEBUG_CONTROL stopped\n");
    return perfMode && !perfPassed ? 2 : 0;
}
