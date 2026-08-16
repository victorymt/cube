
#include "app/game_landing.h"

#include "app/screenshot.h"
#include "ecology/ecology.h"
#include "presentation/render.h"
#include "gameplay/ship.h"
#include "space/space.h"
#include "space/space_units.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include "raylib.h"
#include "raymath.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

bool LandingTransitionBegin(LandingTransition *transition, Player *player)
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
        .targetSystemAnchorX = homeWorldTarget ? 0 : body.systemAnchorX,
        .targetSystemAnchorZ = homeWorldTarget ? 0 : body.systemAnchorZ,
        .targetPlanetIndex = homeWorldTarget ? -1 : body.index - 1,
        .targetCenter = center,
        .targetVelocity = homeWorldTarget ? Vector3Zero() : body.velocity,
        .outward = outward
    };
    if (homeWorldTarget) {
        snprintf(transition->targetName, sizeof(transition->targetName),
                 "Earth");
    } else {
        snprintf(transition->targetName, sizeof(transition->targetName), "%s",
                 body.name);
    }
    ShipResetVisualEffects();
    player->velocity = transition->targetVelocity;
    SetImportMessage(TextFormat("Descent initiated: %s.", transition->targetName));
    return true;
}

static bool LandingTransitionRefreshTarget(LandingTransition *transition,
                                           const Player *player, float dt)
{
    Vector3 previousCenter = transition->targetCenter;
    if (transition->homeWorldTarget) {
        transition->targetCenter = HomeWorldCenter();
        if (dt > 0.0f) {
            transition->targetVelocity = Vector3Scale(
                Vector3Subtract(transition->targetCenter, previousCenter),
                1.0f / dt);
        }
        return true;
    }

    SpaceBodyInfo body;
    if (!SpacePlanetBodyAt(transition->targetSystemAnchorX,
                           transition->targetSystemAnchorZ,
                           transition->targetPlanetIndex,
                           player->position, &body)) {
        return false;
    }
    transition->targetCenter = body.center;
    transition->targetVelocity = body.velocity;
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
        ShipResetVisualEffects();
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
    ShipEmitTouchdownDust(player);
    if (!ShipExit(player)) {
        player->floating = true;
        transition->active = false;
        transition->summaryRemaining = 0.0f;
        ShipResetVisualEffects();
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

bool LandingTransitionUpdate(LandingTransition *transition, Player *player,
                             float dt)
{
    if (!transition->active) {
        if (transition->summaryRemaining > 0.0f) {
            transition->summaryRemaining = fmaxf(0.0f,
                                                   transition->summaryRemaining - dt);
        }
        return false;
    }

    if (!transition->committed &&
        !LandingTransitionRefreshTarget(transition, player, dt)) {
        transition->active = false;
        transition->summaryRemaining = 0.0f;
        ShipResetVisualEffects();
        SetImportMessage("Descent aborted: landing target is unavailable.");
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
        player->velocity = Vector3Add(
            transition->targetVelocity,
            Vector3Scale(transition->outward, radialSpeed));

        if (transition->elapsed >= LANDING_TRANSITION_COMMIT_TIME) {
            if (!LandingTransitionCommit(transition, player)) {
                return skipPressed;
            }
        } else {
            ShipUpdateLandingEffects(player, dt, 0.0f);
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
        ShipUpdateLandingEffects(player, dt, linearAtmosphere);
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

void DrawLandingTransitionOverlay(const LandingTransition *transition)
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
