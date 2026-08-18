#include "presentation/render.h"
#include "presentation/render_internal.h"
#include "presentation/render_ui.h"

#include "core/game_notice.h"
#include "raymath.h"
#include "rlgl.h"
#include "world/block_atlas.h"
#include "world/chunks.h"
#include "gameplay/inventory.h"
#include "gameplay/map_markers.h"
#include "world/world.h"
#include "gameplay/interaction.h"
#include "space/planet_material.h"
#include "space/planet_observation.h"
#include "presentation/planet_renderer.h"
#include "space/planet_surface.h"
#include "world/terrain.h"
#include "presentation/particles.h"
#include "space/space_chunks.h"
#include "space/space_query.h"
#include "space/space_state.h"
#include "space/space_units.h"
#include "world/world_environment.h"
#include "world/nether.h"
#include "ecology/entity.h"
#include "gameplay/ship.h"
#include "presentation/audio.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "ecology/ecology.h"
#include "core/perf.h"
#include "presentation/render_sort.h"
#include "world/world_lighting.h"
#include "presentation/world_renderer.h"

#include <math.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define UI_FONT_PATH "assets/fonts/FSEX302-alt.ttf"
#define UI_FONT_BASE_SIZE 32
#define UI_CJK_FONT_PATH "assets/fonts/NotoSansCJKsc-Regular.otf"
#define UI_CJK_CODEPOINTS_PATH "assets/fonts/cjk-common.txt"
#define UI_CJK_FONT_BASE_SIZE 28

static Font uiFont = { 0 };
static bool uiFontReady = false;
static Font uiCjkFont = { 0 };
static bool uiCjkFontReady = false;

void UiFontShutdown(void)
{
    if (uiFontReady) UnloadFont(uiFont);
    if (uiCjkFontReady) UnloadFont(uiCjkFont);
    uiFont = (Font){ 0 };
    uiFontReady = false;
    uiCjkFont = (Font){ 0 };
    uiCjkFontReady = false;
}

static void UiAssetPaths(const char *relative, char *applicationPath,
                         size_t applicationPathSize, const char **paths)
{
    if (!applicationPath || applicationPathSize == 0u || !paths) return;
    applicationPath[0] = '\0';
    const char *applicationDirectory = GetApplicationDirectory();
    if (applicationDirectory) {
        size_t length = strlen(applicationDirectory);
        const char *separator = length > 0 && applicationDirectory[length - 1] == '/'
                                    ? "" : "/";
        snprintf(applicationPath, applicationPathSize, "%s%s%s",
                 applicationDirectory, separator, relative);
    }
    paths[0] = relative;
    paths[1] = applicationPath;
}

static const char *UiFirstExistingPath(const char *relative,
                                       char *applicationPath,
                                       size_t applicationPathSize)
{
    const char *paths[2] = { 0 };
    UiAssetPaths(relative, applicationPath, applicationPathSize, paths);
    for (int i = 0; i < 2; i++) {
        if (paths[i] && paths[i][0] != '\0' && FileExists(paths[i])) {
            return paths[i];
        }
    }
    return NULL;
}

void UiFontInit(void)
{
    UiFontShutdown();

    char applicationPath[512] = { 0 };
    const char *paths[2] = { 0 };
    UiAssetPaths(UI_FONT_PATH, applicationPath, sizeof(applicationPath), paths);
    for (int i = 0; i < 2; i++) {
        if (!paths[i] || paths[i][0] == '\0' || !FileExists(paths[i])) continue;
        Font loaded = LoadFontEx(paths[i], UI_FONT_BASE_SIZE, NULL, 0);
        if (!IsFontValid(loaded) || loaded.texture.id == 0 ||
            loaded.texture.id == GetFontDefault().texture.id) continue;
        uiFont = loaded;
        uiFontReady = true;
        SetTextureFilter(uiFont.texture, TEXTURE_FILTER_POINT);
        break;
    }
    if (!uiFontReady) {
        TraceLog(LOG_WARNING, "UI: Fixedsys font was not found; using default font");
    }

    char cjkFontApplicationPath[512] = { 0 };
    char codepointsApplicationPath[512] = { 0 };
    const char *cjkFontPath = UiFirstExistingPath(
        UI_CJK_FONT_PATH, cjkFontApplicationPath,
        sizeof(cjkFontApplicationPath));
    const char *codepointsPath = UiFirstExistingPath(
        UI_CJK_CODEPOINTS_PATH, codepointsApplicationPath,
        sizeof(codepointsApplicationPath));
    if (cjkFontPath && codepointsPath) {
        char *text = LoadFileText(codepointsPath);
        int codepointCount = 0;
        int *codepoints = text ? LoadCodepoints(text, &codepointCount) : NULL;
        if (codepoints && codepointCount > 0) {
            Font loaded = LoadFontEx(cjkFontPath, UI_CJK_FONT_BASE_SIZE,
                                     codepoints, codepointCount);
            if (IsFontValid(loaded) && loaded.texture.id != 0u &&
                loaded.texture.id != GetFontDefault().texture.id) {
                uiCjkFont = loaded;
                uiCjkFontReady = true;
                SetTextureFilter(uiCjkFont.texture, TEXTURE_FILTER_BILINEAR);
            }
        }
        if (codepoints) UnloadCodepoints(codepoints);
        if (text) UnloadFileText(text);
    }
    if (!uiCjkFontReady) {
        TraceLog(LOG_WARNING, "UI: Noto CJK fallback font was not loaded");
    }
}

static Font UiCodepointFont(int codepoint)
{
    if (codepoint > 127 && uiCjkFontReady) return uiCjkFont;
    return uiFontReady ? uiFont : GetFontDefault();
}

static float UiCodepointAdvance(Font font, int codepoint, float fontSize)
{
    GlyphInfo glyph = GetGlyphInfo(font, codepoint);
    float advance = glyph.advanceX > 0
        ? (float)glyph.advanceX : (float)glyph.image.width;
    if (advance <= 0.0f) advance = (float)font.baseSize * 0.5f;
    return advance * fontSize / (float)font.baseSize;
}

int UiMeasureText(const char *text, int fontSize)
{
    if (!text) return 0;
    float width = 0.0f;
    float maximum = 0.0f;
    for (int offset = 0; text[offset] != '\0';) {
        int bytes = 0;
        int codepoint = GetCodepointNext(text + offset, &bytes);
        if (bytes <= 0) bytes = 1;
        offset += bytes;
        if (codepoint == '\n') {
            maximum = fmaxf(maximum, width);
            width = 0.0f;
            continue;
        }
        width += UiCodepointAdvance(UiCodepointFont(codepoint), codepoint,
                                    (float)fontSize);
    }
    return (int)ceilf(fmaxf(maximum, width));
}

static void DrawUiTextLayer(const char *text, int x, int y, int fontSize,
                            Color color)
{
    if (!text) return;
    Vector2 position = { (float)x, (float)y };
    float lineStart = position.x;
    for (int offset = 0; text[offset] != '\0';) {
        int bytes = 0;
        int codepoint = GetCodepointNext(text + offset, &bytes);
        if (bytes <= 0) bytes = 1;
        offset += bytes;
        if (codepoint == '\n') {
            position.x = lineStart;
            position.y += (float)fontSize * 1.45f;
            continue;
        }
        Font font = UiCodepointFont(codepoint);
        DrawTextCodepoint(font, codepoint, position, (float)fontSize, color);
        position.x += UiCodepointAdvance(font, codepoint, (float)fontSize);
    }
}

void UiDrawText(const char *text, int x, int y, int fontSize, Color color)
{
    DrawUiTextLayer(text, x + 1, y + 2, fontSize, Fade(BLACK, 0.92f));
    DrawUiTextLayer(text, x, y, fontSize, color);
}

static float NormalizeHudHeading(float heading)
{
    if (!isfinite(heading)) return 0.0f;
    heading = fmodf(heading, 360.0f);
    return heading < 0.0f ? heading + 360.0f : heading;
}

float HudHeadingFromYaw(float yawRadians)
{
    return NormalizeHudHeading(180.0f - yawRadians * RAD2DEG);
}

const char *HudHeadingDirection(float headingDegrees)
{
    static const char *directions[] = {
        "N", "NE", "E", "SE", "S", "SW", "W", "NW"
    };
    float heading = NormalizeHudHeading(headingDegrees);
    int octant = (int)floorf((heading + 22.5f) / 45.0f) % 8;
    return directions[octant];
}

static int HudRoundedHeading(float headingDegrees)
{
    return (int)floorf(NormalizeHudHeading(headingDegrees) + 0.5f) % 360;
}

void HudFormatStatusLine(char *buffer, size_t bufferSize,
                         Vector3 playerPosition, float yaw, float pitch,
                         float dayTime)
{
    if (!buffer || bufferSize == 0u) return;
    float heading = HudHeadingFromYaw(yaw);
    int pitchDegrees = isfinite(pitch) ? (int)lroundf(pitch * RAD2DEG) : 0;
    float normalizedTime = isfinite(dayTime) ? fmodf(dayTime, 1.0f) : 0.0f;
    if (normalizedTime < 0.0f) normalizedTime += 1.0f;
    int hour = (int)(normalizedTime * 24.0f) % 24;
    snprintf(buffer, bufferSize,
             "XYZ %d %d %d   %s %03d   P%+03d   %02d:00",
             (int)floorf(playerPosition.x),
             (int)floorf(playerPosition.y),
             (int)floorf(playerPosition.z),
             HudHeadingDirection(heading), HudRoundedHeading(heading),
             pitchDegrees, hour);
}

static void DrawShipHudLamp(int x, int y, Color color, const char *label)
{
    DrawCircle(x, y, 5.0f, Fade(BLACK, 0.90f));
    DrawCircle(x, y, 3.0f, color);
    DrawCircleLines(x, y, 5.0f, Fade(color, 0.55f));
    UiDrawText(label, x + 10, y - 7, 14, Fade(WHITE, 0.88f));
}

static void DrawShipHeadingTape(Rectangle tape, float heading, Color accent)
{
    float centerX = tape.x + tape.width * 0.5f;
    float baseline = tape.y + tape.height - 8.0f;
    DrawLine((int)tape.x, (int)baseline,
             (int)(tape.x + tape.width), (int)baseline,
             Fade(WHITE, 0.24f));

    for (int offset = -45; offset <= 45; offset += 15) {
        float x = centerX + (float)offset * tape.width / 90.0f;
        float tickHeight = offset % 30 == 0 ? 8.0f : 5.0f;
        DrawLine((int)x, (int)(baseline - tickHeight),
                 (int)x, (int)baseline, Fade(WHITE, 0.56f));
    }

    DrawTriangle((Vector2){ centerX, tape.y + 3.0f },
                 (Vector2){ centerX - 5.0f, tape.y + 10.0f },
                 (Vector2){ centerX + 5.0f, tape.y + 10.0f }, accent);
    const char *headingText = TextFormat("HDG %03d", HudRoundedHeading(heading));
    int headingWidth = UiMeasureText(headingText, 15);
    UiDrawText(headingText, (int)(centerX - (float)headingWidth * 0.5f),
               (int)tape.y + 9, 15, accent);
}

void DrawShipHud(const ShipHudState *hud)
{
    if (!hud) return;
    int sw = GetScreenWidth();
    float panelWidth = fminf(500.0f, (float)sw - 24.0f);
    Rectangle panel = {
        (float)sw - panelWidth - 12.0f, 12.0f, panelWidth, 276.0f
    };
    const Color cyan = { 137, 217, 235, 255 };
    const Color amber = { 255, 198, 76, 255 };
    const Color green = { 123, 218, 157, 255 };
    const Color red = { 238, 100, 82, 255 };
    DrawRectangleRounded(panel, 0.035f, 6, (Color){ 12, 18, 22, 224 });
    DrawRectangleRoundedLinesEx(panel, 0.035f, 6, 1.5f,
                                Fade(cyan, 0.42f));

    int left = (int)panel.x + 16;
    int top = (int)panel.y;
    bool assist = ShipFlightAssistEnabled();
    Color modeColor = hud->warping ? cyan :
                      (hud->supercruising ? amber :
                       (hud->approaching ? green :
                        (hud->cruising ? cyan : (assist ? green : amber))));
    const char *driveMode = hud->driveMode ? hud->driveMode : "MANEUVER";

    UiDrawText("FLIGHT COMPUTER", left, top + 10, 15, Fade(cyan, 0.82f));
    int modeWidth = UiMeasureText(driveMode, 14) + 20;
    DrawShipHudLamp((int)(panel.x + panel.width) - modeWidth - 8,
                    top + 19, modeColor, driveMode);
    DrawLine(left, top + 36, (int)(panel.x + panel.width) - 16, top + 36,
             Fade(cyan, 0.24f));

    int rightColumn = (int)(panel.x + panel.width * 0.56f);
    bool physicalUnits = WorldIsSpaceActive();
    double displayedSpeed = physicalUnits
        ? SpaceUnitsGameVelocityToKilometersPerSecond(hud->speed)
        : hud->speed;
    UiDrawText(physicalUnits ? "REL VELOCITY" : "VELOCITY",
               left, top + 45, 13, Fade(WHITE, 0.50f));
    const char *speedText = physicalUnits
        ? TextFormat("%.2f", displayedSpeed)
        : TextFormat("%.0f", displayedSpeed);
    UiDrawText(speedText, left, top + 57, 34, modeColor);
    int speedWidth = UiMeasureText(speedText, 34);
    UiDrawText(physicalUnits ? "KM/S" : "BLK/S",
               left + speedWidth + 8, top + 72, 13,
               Fade(WHITE, 0.52f));

    const char *altitudeLabel = physicalUnits && !hud->nearPlanet
        ? "LOCAL Y"
        : (hud->subsurface ? "SUBSURFACE" :
           (hud->nearPlanet ? "SURFACE ALT" : "ALTITUDE"));
    double displayedAltitude = physicalUnits && hud->nearPlanet
        ? SpaceUnitsGameDistanceToKilometers(hud->altitude)
        : hud->altitude;
    UiDrawText(altitudeLabel,
               rightColumn, top + 45, 13, Fade(WHITE, 0.50f));
    const char *altitudeText = physicalUnits && hud->nearPlanet
        ? TextFormat("%.3g", displayedAltitude)
        : TextFormat("%.0f", displayedAltitude);
    UiDrawText(altitudeText, rightColumn, top + 60, 28,
               Fade(WHITE, 0.94f));
    int altitudeWidth = UiMeasureText(altitudeText, 28);
    UiDrawText(physicalUnits && hud->nearPlanet ? "KM" :
                   (physicalUnits ? "U" : "BLK"),
               rightColumn + altitudeWidth + 7, top + 72, 13,
               Fade(WHITE, 0.52f));

    DrawShipHeadingTape(
        (Rectangle){ panel.x + 16.0f, panel.y + 96.0f,
                     panel.width - 32.0f, 43.0f },
        hud->heading, amber);

    float fuelRatio = Clamp(ShipGetFuel() / SHIP_MAX_FUEL, 0.0f, 1.0f);
    Color fuelColor = fuelRatio > 0.20f ? amber : red;
    UiDrawText("FUEL", left, top + 146, 13, Fade(WHITE, 0.54f));
    UiDrawText(TextFormat("%03.0f%%", fuelRatio * 100.0f), left + 42,
               top + 145, 15, fuelColor);
    Rectangle fuelTrack = {
        panel.x + 16.0f, panel.y + 166.0f,
        panel.width * 0.48f - 22.0f, 7.0f
    };
    DrawRectangleRec(fuelTrack, Fade(WHITE, 0.13f));
    Rectangle fuelFill = fuelTrack;
    fuelFill.width *= fuelRatio;
    DrawRectangleRec(fuelFill, fuelColor);

    char environment[64];
    if (hud->submerged) {
        snprintf(environment, sizeof(environment), "%s",
                 hud->subsurface ? "AQUIFER" : "SUBMERGED");
    } else if (hud->atmosphere >= 0.0f) {
        snprintf(environment, sizeof(environment), "ATM %03.0f%%",
                 Clamp(hud->atmosphere, 0.0f, 100.0f));
    } else {
        snprintf(environment, sizeof(environment), "%s",
                 hud->nearPlanet ? "SURFACE REF" : "VACUUM");
    }
    UiDrawText("ENV", rightColumn, top + 146, 13, Fade(WHITE, 0.54f));
    UiDrawText(environment, rightColumn + 34, top + 145, 15,
               hud->submerged ? cyan :
               (hud->atmosphere > 70.0f ? amber : cyan));

    char gravity[128];
    if (ShipHasGravityPrimary()) {
        snprintf(gravity, sizeof(gravity), "GRAV %s  %.3g/%.3g AU",
                 ShipGravityPrimaryName(),
                 ShipGravityPrimaryDistance() /
                     (float)SPACE_UNITS_GAME_DISTANCE_PER_AU,
                 ShipGravitySphereOfInfluence() /
                     (float)SPACE_UNITS_GAME_DISTANCE_PER_AU);
    } else {
        snprintf(gravity, sizeof(gravity), "GRAV INTERPLANETARY");
    }

    char navigation[160];
    if (ShipHasNavigationTarget()) {
        const char *targetKind = ShipNavigationTargetIsSystem() ? "SYS" :
                                                                  "PLANET";
        const char *route = hud->warping ? "INTERSTELLAR WARP" :
                            (hud->supercruising ? "SUPERCRUISE" :
                             (hud->approaching ? "APPROACH" : "LOCK"));
        snprintf(navigation, sizeof(navigation), "%s // %s // %s",
                 targetKind, ShipNavigationTargetName(), route);
    } else {
        snprintf(navigation, sizeof(navigation), "SYS // %s // NO TARGET",
                 hud->systemName ? hud->systemName : "---");
    }
    double setSpeed = physicalUnits
        ? SpaceUnitsGameVelocityToKilometersPerSecond(hud->targetSpeed)
        : hud->targetSpeed;
    double closingSpeed = physicalUnits
        ? SpaceUnitsGameVelocityToKilometersPerSecond(hud->closingSpeed)
        : hud->closingSpeed;
    char propulsion[160];
    snprintf(propulsion, sizeof(propulsion), "SET %.2f %s  CLOSE %.2f %s",
             setSpeed, physicalUnits ? "KM/S" : "BLK/S",
             closingSpeed, physicalUnits ? "KM/S" : "BLK/S");
    char guidance[160];
    snprintf(guidance, sizeof(guidance), "BRAKE %.3g AU  ETA %s",
             hud->brakingDistance /
                 (float)SPACE_UNITS_GAME_DISTANCE_PER_AU,
             hud->etaSeconds > 0.0f && isfinite(hud->etaSeconds)
                 ? TextFormat("%.0f S", hud->etaSeconds)
                 : "---");

    int statusFont = 14;
    int statusWidth = (int)panel.width - 32;
    while (statusFont > 11 &&
           (UiMeasureText(propulsion, statusFont) > statusWidth ||
            UiMeasureText(guidance, statusFont) > statusWidth ||
            UiMeasureText(gravity, statusFont) > statusWidth ||
            UiMeasureText(navigation, statusFont) > statusWidth)) statusFont--;
    UiDrawText(propulsion, left, top + 180, statusFont, Fade(WHITE, 0.72f));
    UiDrawText(guidance, left, top + 200, statusFont, Fade(WHITE, 0.60f));
    UiDrawText(gravity, left, top + 220, statusFont, Fade(WHITE, 0.60f));
    UiDrawText(navigation, left, top + 242, statusFont,
               ShipHasNavigationTarget() ? modeColor : Fade(WHITE, 0.76f));
}

static const char *ShipLocatorReturnHint(WorldDimension dimension)
{
    switch (dimension) {
    case WORLD_DIMENSION_HOME: return "return to Homeworld";
    case WORLD_DIMENSION_PLANET: return "travel to that planet";
    case WORLD_DIMENSION_SPACE: return "launch into space";
    case WORLD_DIMENSION_NETHER: return "enter the Nether";
    default: return "return to its location";
    }
}

void DrawShipLocator(const Camera3D *camera, const ShipLocatorTarget *target)
{
    if (!camera || !target || target->status == SHIP_LOCATOR_TARGET_NONE) return;

    const Color accent = (Color){ 255, 198, 76, 255 };
    if (target->status == SHIP_LOCATOR_TARGET_REMOTE) {
        int screenWidth = GetScreenWidth();
        if (screenWidth <= 64) return;
        char text[128];
        snprintf(text, sizeof(text), "SHIP  %s  |  %s", target->location,
                 ShipLocatorReturnHint(target->dimension));
        int fontSize = 17;
        int maxWidth = screenWidth - 32;
        while (fontSize > 12 && UiMeasureText(text, fontSize) + 28 > maxWidth) {
            fontSize--;
        }
        if (UiMeasureText(text, fontSize) + 28 > maxWidth) {
            snprintf(text, sizeof(text), "SHIP  %.24s", target->location);
            while (strlen(text) > 4u &&
                   UiMeasureText(text, fontSize) + 28 > maxWidth) {
                text[strlen(text) - 1u] = '\0';
            }
        }
        int width = fminf((float)(UiMeasureText(text, fontSize) + 28),
                          (float)maxWidth);
        Rectangle bar = {
            ((float)screenWidth - (float)width) * 0.5f,
            18.0f,
            (float)width,
            34.0f
        };
        DrawRectangleRounded(bar, 0.08f, 5, Fade(BLACK, 0.76f));
        DrawRectangleRoundedLinesEx(bar, 0.08f, 5, 1.0f, Fade(accent, 0.68f));
        UiDrawText(text, (int)bar.x + 12, (int)bar.y + 8, fontSize, accent);
        return;
    }

    Vector3 cameraForward = Vector3Normalize(
        Vector3Subtract(camera->target, camera->position));
    Vector3 toTarget = Vector3Subtract(target->position, camera->position);
    bool behind = Vector3DotProduct(cameraForward, toTarget) <= 0.0f;
    Vector2 projected = GetWorldToScreen(target->position, *camera);
    ShipLocatorMarkerLayout layout = ShipLocatorMarkerLayoutEvaluate(
        projected, behind, GetScreenWidth(), GetScreenHeight(), 54.0f);
    if (!layout.visible) return;

    if (layout.onScreen) {
        DrawCircleV(layout.position, 13.0f, Fade(BLACK, 0.68f));
        DrawCircleLines((int)layout.position.x, (int)layout.position.y,
                        12.0f, accent);
        DrawTriangle(
            (Vector2){ layout.position.x, layout.position.y - 7.0f },
            (Vector2){ layout.position.x - 6.0f, layout.position.y + 6.0f },
            (Vector2){ layout.position.x + 6.0f, layout.position.y + 6.0f },
            accent);
    } else {
        Vector2 perpendicular = {
            -layout.direction.y,
            layout.direction.x
        };
        DrawTriangle(
            Vector2Add(layout.position, Vector2Scale(layout.direction, 13.0f)),
            Vector2Add(layout.position, Vector2Scale(perpendicular, 7.0f)),
            Vector2Subtract(layout.position, Vector2Scale(perpendicular, 7.0f)),
            accent);
    }

    const char *label = TextFormat("SHIP  %.0f", target->distance);
    int fontSize = 16;
    int labelWidth = UiMeasureText(label, fontSize);
    int labelX = (int)(layout.position.x - (float)labelWidth * 0.5f);
    int labelY = (int)layout.position.y + 17;
    labelX = (int)Clamp((float)labelX, 10.0f,
                        (float)GetScreenWidth() - (float)labelWidth - 10.0f);
    labelY = (int)Clamp((float)labelY, 10.0f,
                        (float)GetScreenHeight() - 24.0f);
    UiDrawText(label, labelX, labelY, fontSize, accent);
}

static Vector2 MapNavigationEdgePosition(Vector2 direction, float margin)
{
    float screenWidth = (float)GetScreenWidth();
    float screenHeight = (float)GetScreenHeight();
    Vector2 center = { screenWidth * 0.5f, screenHeight * 0.5f };
    float halfWidth = fmaxf(20.0f, center.x - margin);
    float halfHeight = fmaxf(20.0f, center.y - margin);
    float horizontal = fabsf(direction.x) > 0.0001f
        ? halfWidth / fabsf(direction.x) : INFINITY;
    float vertical = fabsf(direction.y) > 0.0001f
        ? halfHeight / fabsf(direction.y) : INFINITY;
    return Vector2Add(center, Vector2Scale(direction,
                                            fminf(horizontal, vertical)));
}

void DrawMapNavigation(Vector3 playerPosition, float playerYaw)
{
    if (!WorldIsSurfaceActive()) return;
    MapMarkerSurface surface = {
        .dimension = WorldCurrentDimension(),
        .surfaceId = WorldCurrentSurfaceId()
    };
    MapMarker target;
    if (!MapMarkersTargetOnSurface(surface, &target)) return;

    float playerLongitude = 0.0f;
    float playerLatitude = 0.0f;
    float targetLongitude = 0.0f;
    float targetLatitude = 0.0f;
    if (surface.dimension == WORLD_DIMENSION_PLANET) {
        PlanetSurfaceLatLonAt((int)floorf(playerPosition.x),
                              (int)floorf(playerPosition.z),
                              &playerLongitude, &playerLatitude);
        PlanetSurfaceLatLonAt((int)floorf(target.x), (int)floorf(target.z),
                              &targetLongitude, &targetLatitude);
    } else {
        HomeSurfaceLatLonAt((int)floorf(playerPosition.x),
                            (int)floorf(playerPosition.z),
                            &playerLongitude, &playerLatitude);
        HomeSurfaceLatLonAt((int)floorf(target.x), (int)floorf(target.z),
                            &targetLongitude, &targetLatitude);
    }
    float bearing = 0.0f;
    float distance = 0.0f;
    if (!MapMarkerGreatCircle(playerLongitude, playerLatitude,
                              targetLongitude, targetLatitude,
                              &bearing, &distance)) {
        return;
    }

    float relative = atan2f(sinf(bearing - playerYaw),
                            cosf(bearing - playerYaw));
    Vector2 direction = { sinf(relative), -cosf(relative) };
    Vector2 position = MapNavigationEdgePosition(direction, 88.0f);
    Vector2 perpendicular = { -direction.y, direction.x };
    Color accent = MapMarkerColorValue(target.color);
    DrawCircleV(position, 16.0f, Fade(BLACK, 0.72f));
    DrawTriangle(
        Vector2Add(position, Vector2Scale(direction, 12.0f)),
        Vector2Add(Vector2Subtract(position, Vector2Scale(direction, 6.0f)),
                   Vector2Scale(perpendicular, 8.0f)),
        Vector2Subtract(Vector2Subtract(position, Vector2Scale(direction, 6.0f)),
                        Vector2Scale(perpendicular, 8.0f)),
        accent);
    DrawCircleLines((int)position.x, (int)position.y, 15.0f,
                    Fade(accent, 0.82f));

    char name[MAP_MARKER_NAME_SIZE];
    snprintf(name, sizeof(name), "%s", target.name);
    char label[MAP_MARKER_NAME_SIZE + 32];
    int fontSize = 16;
    snprintf(label, sizeof(label), "%s  %.0f blk", name, distance);
    int maximumWidth = GetScreenWidth() - 32;
    while (fontSize > 12 && UiMeasureText(label, fontSize) > maximumWidth) {
        fontSize--;
    }
    while (name[0] != '\0' && UiMeasureText(label, fontSize) > maximumWidth) {
        MapMarkerNameBackspace(name);
        snprintf(label, sizeof(label), "%s...  %.0f blk", name, distance);
    }
    int labelWidth = UiMeasureText(label, fontSize);
    Vector2 labelCenter = Vector2Subtract(
        position, Vector2Scale(direction, 36.0f));
    int labelX = (int)(labelCenter.x - (float)labelWidth * 0.5f);
    int labelY = (int)labelCenter.y - fontSize / 2;
    labelX = (int)Clamp((float)labelX, 10.0f,
                        (float)GetScreenWidth() - (float)labelWidth - 10.0f);
    labelY = (int)Clamp((float)labelY, 10.0f,
                        (float)GetScreenHeight() - 26.0f);
    Rectangle background = {
        (float)labelX - 7.0f, (float)labelY - 4.0f,
        (float)labelWidth + 14.0f, (float)fontSize + 10.0f
    };
    DrawRectangleRec(background, Fade(BLACK, 0.72f));
    DrawRectangleLinesEx(background, 1.0f, Fade(accent, 0.54f));
    UiDrawText(label, labelX, labelY, fontSize, WHITE);
}

void DrawCrosshair(int screenWidth, int screenHeight)
{
    int cx = screenWidth / 2;
    int cy = screenHeight / 2;
    DrawLine(cx - 9, cy, cx - 3, cy, WHITE);
    DrawLine(cx + 3, cy, cx + 9, cy, WHITE);
    DrawLine(cx, cy - 9, cx, cy - 3, WHITE);
    DrawLine(cx, cy + 3, cx, cy + 9, WHITE);
}

void DrawHotbar(const BlockType *hotbar, int selectedIndex)
{
    int sw = GetScreenWidth();
    int y = GetScreenHeight() - 72;
    int slot = 50;
    int gap = 8;
    int total = HOTBAR_SIZE * slot + (HOTBAR_SIZE - 1) * gap;
    int x0 = sw / 2 - total / 2;

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        BlockType block = hotbar[i];
        Rectangle rect = { (float)(x0 + i * (slot + gap)), (float)y, (float)slot, (float)slot };
        DrawRectangleRounded(rect, 0.12f, 6, Fade(BLACK, i == selectedIndex ? 0.72f : 0.45f));
        DrawRectangleRoundedLinesEx(rect, 0.12f, 6, i == selectedIndex ? 3.0f : 1.0f,
                                    i == selectedIndex ? WHITE : Fade(WHITE, 0.35f));

        BlockTexture texture = TextureForBlockFace(block, 2);
        Rectangle source = AtlasSourceRect(texture);
        Rectangle dest = { rect.x + 14.0f, rect.y + 11.0f, 22.0f, 22.0f };
        DrawTexturePro(ChunksBlockAtlas(), source, dest, Vector2Zero(), 0.0f,
                       WHITE);
        DrawRectangleLines((int)rect.x + 14, (int)rect.y + 11, 22, 22, Fade(BLACK, 0.35f));
        UiDrawText(i == 9 ? "0" : TextFormat("%d", i + 1), (int)rect.x + 6, (int)rect.y + 31, 14, Fade(WHITE, 0.85f));
        int count = InventoryCount(block);
        const char *countText = TextFormat("%d", count);
        int countFont = count >= 100 ? 11 : 13;
        int countWidth = UiMeasureText(countText, countFont);
        UiDrawText(countText, (int)(rect.x + rect.width - 5.0f - (float)countWidth), (int)rect.y + 31, countFont,
                 count > 0 ? WHITE : Fade((Color){ 238, 100, 82, 255 }, 0.9f));
    }

    UiDrawText(TextFormat("%s  x%d", BlockName(hotbar[selectedIndex]), InventoryCount(hotbar[selectedIndex])),
             x0, y - 24, 18, WHITE);
}

void DrawStatusHUD(Vector3 playerPosition, float yaw, float pitch,
                   float dayTime, bool autoSaveEnabled)
{
    char status[128];
    HudFormatStatusLine(status, sizeof(status), playerPosition, yaw, pitch,
                        dayTime);
    UiDrawText(status, 15, GetScreenHeight() - 32, 17, Fade(BLACK, 0.92f));
    UiDrawText(status, 14, GetScreenHeight() - 34, 17, Fade(WHITE, 0.9f));
    const char *saveText = TextFormat(
        "Auto-save: %s", autoSaveEnabled ? "60s" : "off");
    UiDrawText(saveText, 15, GetScreenHeight() - 14, 15,
               Fade(BLACK, 0.92f));
    UiDrawText(saveText, 14, GetScreenHeight() - 16, 15,
               Fade(WHITE, 0.65f));
}

int HotbarKeyToIndex(void)
{
    if (IsKeyPressed(KEY_ZERO)) return 9;
    for (int i = 0; i < 9 && i < HOTBAR_SIZE; i++) {
        if (IsKeyPressed(KEY_ONE + i)) return i;
    }
    return -1;
}

void DrawCenteredText(const char *text, int y, int fontSize, Color color)
{
    int width = UiMeasureText(text, fontSize);
    UiDrawText(text, GetScreenWidth() / 2 - width / 2, y, fontSize, color);
}

bool DrawMenuButton(Rectangle rect, const char *label, bool primary)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect);
    Color fill = primary ? (Color){ 68, 142, 90, 255 } : (Color){ 38, 45, 53, 255 };
    Color hoverFill = primary ? (Color){ 83, 164, 104, 255 } : (Color){ 52, 61, 70, 255 };

    DrawRectangleRounded(rect, 0.08f, 8, hovered ? hoverFill : fill);
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 2.0f, hovered ? WHITE : Fade(WHITE, 0.55f));

    int fontSize = 24;
    int textWidth = UiMeasureText(label, fontSize);
    UiDrawText(label, (int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
             (int)(rect.y + rect.height * 0.5f - fontSize * 0.5f), fontSize, WHITE);

    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

bool DrawTerrainOption(Rectangle rect, const char *title, const char *subtitle, bool selected)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = CheckCollisionPointRec(mouse, rect);
    Color fill = selected ? (Color){ 45, 105, 78, 255 } : (Color){ 35, 44, 52, 255 };
    if (hovered && !selected) fill = (Color){ 45, 56, 66, 255 };

    DrawRectangleRounded(rect, 0.07f, 8, fill);
    DrawRectangleRoundedLinesEx(rect, 0.07f, 8, selected ? 3.0f : 1.5f,
                                selected ? (Color){ 224, 241, 202, 255 } : Fade(WHITE, 0.42f));

    UiDrawText(title, (int)rect.x + 18, (int)rect.y + 13, 22, WHITE);
    UiDrawText(subtitle, (int)rect.x + 18, (int)rect.y + 43, 15, Fade(WHITE, 0.72f));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

void DrawStartPage(bool *startGame, bool *quitGame, TerrainMode *selectedTerrain,
                   uint32_t *selectedSeed)
{
    static char seedText[11] = { 0 };
    static uint32_t displayedSeed = 0;
    static bool seedFocused = false;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    if (seedText[0] == '\0' || (!seedFocused && displayedSeed != *selectedSeed)) {
        snprintf(seedText, sizeof(seedText), "%u", *selectedSeed);
        displayedSeed = *selectedSeed;
    }

    ClearBackground((Color){ 96, 157, 213, 255 });
    DrawRectangleGradientV(0, 0, sw, sh, (Color){ 99, 166, 221, 255 }, (Color){ 58, 91, 78, 255 });

    int tileSize = 54;
    int gap = 10;
    BlockType previewBlocks[HOTBAR_SIZE] = {
        BLOCK_GRASS, BLOCK_DIRT, BLOCK_STONE, BLOCK_WOOD, BLOCK_PLANK,
        BLOCK_BRICK, BLOCK_SAND, BLOCK_SNOW, BLOCK_GLASS, BLOCK_WATER
    };
    int total = HOTBAR_SIZE * tileSize + (HOTBAR_SIZE - 1) * gap;
    int x0 = sw / 2 - total / 2;
    int previewY = sh / 2 - 82;

    DrawCenteredText("Voxelcraft", sh / 2 - 190, 64, WHITE);
    DrawCenteredText("Infinite block world", sh / 2 - 116, 24, Fade(WHITE, 0.86f));

    for (int i = 0; i < HOTBAR_SIZE; i++) {
        Rectangle tile = { (float)(x0 + i * (tileSize + gap)), (float)previewY, (float)tileSize, (float)tileSize };
        BlockTexture texture = TextureForBlockFace(previewBlocks[i], 2);
        Rectangle source = AtlasSourceRect(texture);
        DrawRectangleRounded((Rectangle){ tile.x - 4.0f, tile.y - 4.0f, tile.width + 8.0f, tile.height + 8.0f },
                             0.08f, 6, Fade(BLACK, 0.22f));
        DrawTexturePro(ChunksBlockAtlas(), source, tile, Vector2Zero(), 0.0f,
                       WHITE);
        DrawRectangleLinesEx(tile, 2.0f, Fade(WHITE, 0.48f));
    }

    Rectangle variedRect = { sw / 2 - 252.0f, sh / 2 + 6.0f, 240.0f, 72.0f };
    Rectangle flatRect = { sw / 2 + 12.0f, sh / 2 + 6.0f, 240.0f, 72.0f };
    if (DrawTerrainOption(variedRect, "Varied", "Rolling hills and trees", *selectedTerrain == TERRAIN_VARIED)) {
        *selectedTerrain = TERRAIN_VARIED;
    }
    if (DrawTerrainOption(flatRect, "Flat", "Plain with image import", *selectedTerrain == TERRAIN_FLAT)) {
        *selectedTerrain = TERRAIN_FLAT;
    }

    Rectangle seedRect = { sw / 2 - 252.0f, sh / 2 + 106.0f, 356.0f, 48.0f };
    Rectangle randomRect = { sw / 2 + 116.0f, sh / 2 + 106.0f, 136.0f, 48.0f };
    Vector2 mouse = GetMousePosition();
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        seedFocused = CheckCollisionPointRec(mouse, seedRect);
    }

    if (seedFocused) {
        int codepoint = GetCharPressed();
        while (codepoint > 0) {
            size_t length = strlen(seedText);
            if (codepoint >= '0' && codepoint <= '9' && length < sizeof(seedText) - 1) {
                seedText[length] = (char)codepoint;
                seedText[length + 1] = '\0';
            }
            codepoint = GetCharPressed();
        }
        if (IsKeyPressed(KEY_BACKSPACE)) {
            size_t length = strlen(seedText);
            if (length > 0) seedText[length - 1] = '\0';
        }
    }

    char *seedEnd = NULL;
    unsigned long long parsedSeed = strtoull(seedText, &seedEnd, 10);
    bool validSeed = seedText[0] != '\0' && seedEnd && *seedEnd == '\0' &&
                     parsedSeed > 0 && parsedSeed <= UINT32_MAX;
    if (validSeed) {
        *selectedSeed = (uint32_t)parsedSeed;
        displayedSeed = *selectedSeed;
    }

    UiDrawText("World seed", (int)seedRect.x, (int)seedRect.y - 22, 16, Fade(WHITE, 0.78f));
    DrawRectangleRounded(seedRect, 0.07f, 8, (Color){ 28, 35, 42, 255 });
    DrawRectangleRoundedLinesEx(seedRect, 0.07f, 8, 2.0f,
                                validSeed ? (seedFocused ? WHITE : Fade(WHITE, 0.48f)) : (Color){ 230, 92, 82, 255 });
    UiDrawText(seedText, (int)seedRect.x + 15, (int)seedRect.y + 12, 22, WHITE);
    if (seedFocused) {
        int caretX = (int)seedRect.x + 15 + UiMeasureText(seedText, 22);
        DrawRectangle(caretX + 2, (int)seedRect.y + 11, 2, 25, Fade(WHITE, 0.9f));
    }
    if (DrawMenuButton(randomRect, "Random", false)) {
        uint32_t randomSeed = ((uint32_t)GetRandomValue(0, 0xffff) << 16) |
                              (uint32_t)GetRandomValue(0, 0xffff);
        if (randomSeed == 0) randomSeed = DEFAULT_WORLD_SEED;
        *selectedSeed = randomSeed;
        displayedSeed = randomSeed;
        snprintf(seedText, sizeof(seedText), "%u", randomSeed);
        validSeed = true;
        seedFocused = false;
    }

    Rectangle startRect = { sw / 2 - 130.0f, sh / 2 + 170.0f, 260.0f, 54.0f };
    Rectangle quitRect = { sw / 2 - 130.0f, sh / 2 + 236.0f, 260.0f, 48.0f };
    if (validSeed && (DrawMenuButton(startRect, "Start", true) || IsKeyPressed(KEY_ENTER))) *startGame = true;
    else if (!validSeed) DrawMenuButton(startRect, "Enter a valid seed", false);
    if (DrawMenuButton(quitRect, "Quit", false)) *quitGame = true;

    DrawCenteredText(TextFormat("Seed %u", *selectedSeed), sh - 32, 16, Fade(WHITE, 0.68f));
}

void DrawHelpPanel(bool floating, bool cursorReleased, int viewDistance)
{
    int x = 18;
    int y = 18;
    int w = 430;
    int h = 422;
    DrawRectangleRounded((Rectangle){ (float)x, (float)y, (float)w, (float)h }, 0.05f, 6, Fade(BLACK, 0.68f));
    UiDrawText("Voxelcraft", x + 14, y + 12, 24, WHITE);
    UiDrawText("WASD move    Shift sprint    Space jump/swim", x + 14, y + 48, 17, RAYWHITE);
    UiDrawText("LMB break    RMB place    MMB pick block", x + 14, y + 73, 17, RAYWHITE);
    UiDrawText("F float    Ctrl down (float)    Wheel hotbar", x + 14, y + 98, 17, RAYWHITE);
    UiDrawText("Tab mouse    M star map/warp    L locate ship", x + 14, y + 123, 17, RAYWHITE);
    UiDrawText("N scan organism    B biology atlas", x + 14, y + 148, 17, RAYWHITE);
    UiDrawText("RMB on placed album opens it", x + 14, y + 173, 17, RAYWHITE);
    UiDrawText("Esc pause    F6 day/night    O orbit paths", x + 14, y + 198, 17, RAYWHITE);
    UiDrawText("F4 view    F5 save    F9 load    F10 shot", x + 14, y + 223, 17, RAYWHITE);
    UiDrawText(TextFormat("Fly above y=%.0f to reach space", SPACE_ENTER_Y),
             x + 14, y + 248, 17, RAYWHITE);
    UiDrawText("Break collects; place consumes blocks", x + 14, y + 273, 15, RAYWHITE);
    UiDrawText("Ship: RMB enter, Q lock planet, G navigate/cancel", x + 14, y + 297, 15, RAYWHITE);
    UiDrawText("WASD thrust, X cruise, F assist, E exit", x + 14, y + 321, 15, RAYWHITE);
    UiDrawText("1-0 blocks    [ ] distance    Flat: I import image", x + 14, y + 345, 15, RAYWHITE);
    UiDrawText("Planet: C scanner, break cores for discoveries", x + 14, y + 369, 15, RAYWHITE);
    const char *mode = ShipIsDriving() ? "Ship" : (floating ? "Floating" : "Walking");
    UiDrawText(TextFormat("%s    %s    View %d    FPS %d", mode,
                          cursorReleased ? "Mouse free" : "Mouse locked", viewDistance, GetFPS()),
               x + 14, y + 397, 16, Fade(RAYWHITE, 0.9f));
}

void DrawCursorReleasedOverlay(void)
{
    const char *text = "Mouse released - press Tab to return";
    int fontSize = 20;
    int width = UiMeasureText(text, fontSize) + 28;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = GetScreenHeight() - 132;
    Rectangle rect = { (float)x, (float)y, (float)width, 46.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.58f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.42f));
    UiDrawText(text, x + 14, y + 13, fontSize, WHITE);
}

void DrawImportStatus(void)
{
    float timer = GameNoticeRemaining();
    if (timer <= 0.0f) return;

    const char *message = GameNoticeCurrent();
    int fontSize = 18;
    int padding = 12;
    int width = UiMeasureText(message, fontSize) + padding * 2;
    int x = GetScreenWidth() / 2 - width / 2;
    int y = 18;
    Rectangle rect = { (float)x, (float)y, (float)width, 42.0f };
    DrawRectangleRounded(rect, 0.08f, 8, Fade(BLACK, 0.54f));
    DrawRectangleRoundedLinesEx(rect, 0.08f, 8, 1.5f, Fade(WHITE, 0.38f));
    UiDrawText(message, x + padding, y + 12, fontSize, WHITE);
}

const char *VisiblePathTail(const char *path, int maxWidth, int fontSize)
{
    if (UiMeasureText(path, fontSize) <= maxWidth) return path;

    const char *tail = path;
    int available = maxWidth - UiMeasureText("...", fontSize);
    while (*tail && UiMeasureText(tail, fontSize) > available) tail++;
    return tail;
}

void DrawImportDialog(ImportDialog *dialog)
{
    if (!dialog->open) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Rectangle panel = { sw / 2 - 360.0f, sh / 2 - 128.0f, 720.0f, 256.0f };
    Rectangle input = { panel.x + 30.0f, panel.y + 100.0f, panel.width - 60.0f, 46.0f };

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.42f));
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    UiDrawText("Import image as blocks", (int)panel.x + 30, (int)panel.y + 24, 28, WHITE);
    UiDrawText("Flat mode only. PNG, JPG, BMP, TGA, GIF, QOI, PSD, HDR.",
             (int)panel.x + 30, (int)panel.y + 62, 16, Fade(WHITE, 0.72f));

    DrawRectangleRounded(input, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(input, 0.05f, 8, 2.0f, (Color){ 98, 160, 115, 255 });

    const char *shown = dialog->path[0] ? VisiblePathTail(dialog->path, (int)input.width - 44, 20) : "";
    int textX = (int)input.x + 16;
    if (shown != dialog->path) {
        UiDrawText("...", textX, (int)input.y + 13, 20, Fade(WHITE, 0.85f));
        textX += UiMeasureText("...", 20);
    }
    UiDrawText(shown, textX, (int)input.y + 13, 20, dialog->path[0] ? WHITE : Fade(WHITE, 0.38f));

    if (((int)(GetTime() * 2.0) % 2) == 0) {
        int cursorX = textX + UiMeasureText(shown, 20) + 2;
        DrawLine(cursorX, (int)input.y + 11, cursorX, (int)input.y + 35, WHITE);
    }

    const char *modeText = TextFormat("Mode: %s  (Tab toggles)",
                                      dialog->relief ? "Grayscale relief" : "Flat color");
    int modeWidth = UiMeasureText(modeText, 18);
    UiDrawText(modeText, sw / 2 - modeWidth / 2, (int)panel.y + 140, 18, WHITE);

    Rectangle minusRect = { panel.x + 30.0f, panel.y + 166.0f, 44.0f, 36.0f };
    Rectangle plusRect = { panel.x + panel.width - 74.0f, panel.y + 166.0f, 44.0f, 36.0f };
    if (DrawMenuButton(minusRect, "-", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, -IMPORT_PRECISION_STEP);
    }
    if (DrawMenuButton(plusRect, "+", false)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, IMPORT_PRECISION_STEP);
    }

    const char *precisionText = TextFormat("Precision: max %d blocks per side (min 16)", dialog->maxBlocks);
    int precisionWidth = UiMeasureText(precisionText, 18);
    UiDrawText(precisionText, sw / 2 - precisionWidth / 2, (int)panel.y + 174, 18, WHITE);

    UiDrawText("Type path or Ctrl+V paste. Tab mode. [ ] adjusts. Enter imports. Esc cancels.",
             (int)panel.x + 30, (int)panel.y + 218, 16, Fade(WHITE, 0.76f));
}

void DrawDebugHUD(Vector3 playerPosition, float yaw, float pitch, float daylight,
                  const PlanetLightState *light,
                  const PlanetObservationState *observation,
                  float seasonProgress,
                  const WeatherVisualState *weatherVisual,
                  const BathymetrySample *bathymetry,
                  const HudFrameState *hud)
{
    if (!hud) return;
    int x = 18;
    int y = 76;
    int line = 20;
    int fs = 14;

    UiDrawText(TextFormat("XYZ %.1f %.1f %.1f   yaw %.1f pitch %.1f",
                        playerPosition.x, playerPosition.y, playerPosition.z, yaw * RAD2DEG, pitch * RAD2DEG),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("FPS %d   frame %.2f ms", GetFPS(), GetFrameTime() * 1000.0f), x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Chunks loaded %d   gen queue %d   mesh queue %d",
                        GetActiveChunkCount(), GetPendingGenJobCount(), GetPendingMeshJobCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    ChunkStreamingStats streaming = ChunksGetStreamingStats();
    UiDrawText(TextFormat("Stream gen %.1fms  mesh %.1fms  upload %.1fms (max %.2f)  sync %llu",
                        streaming.generationCpuMs, streaming.meshCpuMs,
                        streaming.uploadCpuMs, streaming.maxUploadCpuMs,
                        (unsigned long long)streaming.syncRebuilds),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Stream peak queues gen %llu  mesh %llu  upload defers %llu",
                        (unsigned long long)streaming.generationQueuePeak,
                        (unsigned long long)streaming.meshQueuePeak,
                        (unsigned long long)streaming.uploadBudgetDeferrals),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Particles %d   edits %d   render dist %d",
                        ParticlesActiveCount(), WorldGetEditCount(),
                        ChunksRenderDistance()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Space %d/%d   nether %d   entities %d",
                        GetActiveSpaceChunkCount(), hud->spaceEditCount,
                        GetActiveNetherChunkCount(), GetActiveEntityCount()),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    UiDrawText(TextFormat("Weather %s   time %02d:00   auto-save %s",
                        WeatherName(), (int)(hud->dayTime * 24.0f) % 24,
                        hud->autoSaveEnabled ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) {
        WeatherFieldSample localWeather = WeatherCurrentSample();
        LocalClimateState localClimate = { 0 };
        bool haveClimate = WeatherLocalClimateAtWorldTime(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
            SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
            &localClimate);
        WeatherImpactStats weatherImpacts = WeatherImpactGetStats();
        UiDrawText(TextFormat("Climate %s   phenomenon %s",
                            haveClimate ? ClimateRegimeName(localClimate.regime) :
                                          "unavailable",
                            WeatherPhenomenonName(
                                localWeather.dominantPhenomenon)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("T %.1f C   P %.3f atm   RH %.0f%%   wind %.2f gust %.2f",
                            localWeather.temperatureK - 273.15f,
                            localWeather.pressureAtm,
                            localWeather.relativeHumidity * 100.0f,
                            localWeather.wind, localWeather.gust),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Cloud %.2f   precip %.2f   storm %.2f   visibility %.2f",
                            localWeather.cloudCover,
                            localWeather.precipitation,
                            localWeather.storm, localWeather.visibility),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Weather effects surfaces %u   fires %u   damage %s (%u)",
                            weatherImpacts.surfaceCount,
                            weatherImpacts.activeFires,
                            WeatherImpactEnabled() ? "on" : "off",
                            weatherImpacts.blockDamageEvents),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        if (weatherVisual && weatherVisual->active) {
            UiDrawText(TextFormat("Visibility %.2f   fog %.2f   veil %.2f   cloud AGL %.1f",
                                weatherVisual->visibility,
                                weatherVisual->fogDensity,
                                weatherVisual->precipitationVeil,
                                weatherVisual->cloudBaseHeight),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    if (bathymetry && bathymetry->seaLevel >= 0) {
        UiDrawText(TextFormat("Bathymetry %s   seabed %d   water %d   material %s",
                            BathymetryZoneName(bathymetry->zone),
                            bathymetry->seabedY,
                            bathymetry->waterDepth,
                            BathymetryMaterialName(bathymetry->material)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SolarSystemDef hudSystem;
    float hudSystemDist = 0.0f;
    if (FindSystemForGuide(playerPosition, &hudSystem, &hudSystemDist)) {
        double distanceAu = SpaceUnitsGameDistanceToKilometers(hudSystemDist) /
                            SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
        UiDrawText(TextFormat("System %s (%.3g AU)", hudSystem.name,
                              distanceAu),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        UiDrawText("Deep space", x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    if (PlanetWorldIsActive()) {
        if (light && observation && observation->valid) {
            UiDrawText(TextFormat(
                         "Sky %s   season %.0f%%   day %.1fh   local flux %.3f   stars %.2f   moon %.2f   eclipse %.2f",
                         PlanetObservationPhaseName(observation->phase),
                         Clamp(seasonProgress, 0.0f, 1.0f) * 100.0f,
                         Clamp(light->dayLengthFraction, 0.0f, 1.0f) * 24.0f,
                         fmaxf(light->incidentIrradiance, 0.0f),
                         observation->starVisibility,
                         observation->moonVisibility,
                         observation->eclipseDarkening),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
        PlanetLocalEcology ecology = PlanetEcologyLocalAt(
            (int)floorf(playerPosition.x), (int)floorf(playerPosition.z), daylight);
        UiDrawText(TextFormat("Ecology capacity %.2f   flora %.2f   fauna %.2f   limit %s",
                            ecology.suitability.carryingCapacity,
                            ecology.suitability.floraCapacity,
                            ecology.suitability.faunaCapacity,
                            PlanetEcologyLimitingFactorName(
                                ecology.suitability.limitingFactor)),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Activity flora %.2f   fauna %.2f   water %.2f   rain %.2f",
                            ecology.suitability.floraActivity,
                            ecology.suitability.faunaActivity,
                            ecology.environment.liquidWaterAccess,
                            ecology.environment.precipitationRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Region (%d,%d)   disturbance %.3f   flora stress %.3f",
                            ecology.diagnostics.regionX,
                            ecology.diagnostics.regionZ,
                            ecology.environment.disturbance,
                            ecology.environment.disturbance * 0.82f),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Fauna harvest %.3f   stress %.3f   net %+.5f/day",
                            ecology.population.faunaHarvestPressure,
                            ecology.diagnostics.faunaStress,
                            ecology.diagnostics.faunaNetRecoveryRate),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Population flora %.2f/%.2f   fauna %.2f/%.2f   seasonal %.2f   radiation memory %.2f",
                            ecology.population.floraDensity,
                            ecology.population.floraCarryingCapacity,
                            ecology.population.faunaDensity,
                            ecology.population.faunaCarryingCapacity,
                            ecology.population.seasonalMemory,
                            ecology.diagnostics.radiationMemory),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Migration net flora %+.3f   fauna %+.3f",
                            ecology.migration.floraNet,
                            ecology.migration.faunaNet),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Flow flora (%+.3f,%+.3f)   fauna (%+.3f,%+.3f)",
                            ecology.migration.floraFlowX,
                            ecology.migration.floraFlowZ,
                            ecology.migration.faunaFlowX,
                            ecology.migration.faunaFlowZ),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Climate %.0f/%.0f K   light %.2f/%.2f   storm %.2f   radiation %.2f   ejecta %.2f",
                            ecology.environment.meanTemperatureK,
                            ecology.environment.currentTemperatureK,
                            ecology.environment.meanUsableLight,
                            ecology.environment.currentUsableLight,
                            ecology.environment.currentStorm,
                            ecology.environment.radiationExposure,
                            ecology.environment.ejectaExposure),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Terrain elevation %.2f   slope %.2f   shelter %.2f",
                            ecology.environment.elevation,
                            ecology.environment.slope,
                            ecology.environment.shelter),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    }
    SpaceScaleDiagnostics scale;
    if (SpaceScaleDiagnosticsAt(playerPosition, &scale)) {
        UiDrawText(TextFormat("Scale %.0f u/AU   1 play s = 1 sim day   error %.3f ppm [%s]",
                            SPACE_UNITS_GAME_DISTANCE_PER_AU,
                            scale.maxRelativeError * 1000000.0,
                            scale.withinErrorBudget ? "OK" : "OUT"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("%s radius %.0f km = %.5f linear u",
                            scale.bodyName, scale.physicalRadiusKm,
                            scale.physicalRadiusGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Proxy visual %.1f u   landing %.1f u   x%.0f",
                            scale.visualRadiusGame, scale.landingRadiusGame,
                            scale.landingRadiusScale),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Gravity %.2f m/s2 (%.2f g)   gameplay %.2f u/s2",
                            scale.physicalGravityMetersPerSecondSquared,
                            scale.physicalGravityEarth,
                            scale.gameplaySurfaceGravity),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Orbit speed %.2f km/s   %.3f u/play-s",
                            scale.orbitalSpeedKilometersPerSecond,
                            scale.orbitalSpeedGame),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("SOI %.0f km (%.3f linear u)   Hill %.0f km",
                            scale.sphereOfInfluenceKm,
                            scale.physicalSphereOfInfluenceGame,
                            scale.hillSphereKm),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Encounter %.1f u   x%.1f [%s]",
                            scale.encounterRadiusGame,
                            scale.encounterRadiusScale,
                            scale.encounterRadiusClamped ? "proxy clamp" : "physical"),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Flux now %.3f Earth   climate mean %.3f Earth",
                            scale.currentIrradianceEarth,
                            scale.climateIrradianceEarth),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
        UiDrawText(TextFormat("Temperature radiative %.0f K   surface %.0f K",
                            scale.radiativeTemperatureK,
                            scale.surfaceTemperatureK),
                 x, y, fs, Fade(WHITE, 0.85f)); y += line;
    } else {
        UiDrawText("Scale target: no planet within 700 u", x, y, fs,
                 Fade(WHITE, 0.68f)); y += line;
    }
    if (!PlanetWorldIsActive() && !HomeWorldSurfaceIsActive()) {
        SpaceSatelliteScaleDiagnostics satelliteScale;
        if (SpaceSatelliteScaleDiagnosticsAt(playerPosition,
                                             &satelliteScale)) {
            UiDrawText(TextFormat("Moon %s radius %.0f km = %.5f linear u",
                                satelliteScale.bodyName,
                                satelliteScale.physicalRadiusKm,
                                satelliteScale.physicalRadiusGame),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon gravity %.2f m/s2   orbit %.2f km/s",
                                satelliteScale.physicalGravityMetersPerSecondSquared,
                                satelliteScale.orbitalSpeedKilometersPerSecond),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon SOI %.0f km   Hill %.0f km",
                                satelliteScale.sphereOfInfluenceKm,
                                satelliteScale.hillSphereKm),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
            UiDrawText(TextFormat("Moon encounter %.2f u [%s]",
                                satelliteScale.encounterRadiusGame,
                                satelliteScale.withinErrorBudget ? "OK" : "OUT"),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
    UiDrawText(TextFormat("Block %s   music %s", BlockName(hud->targetedBlock),
                        AudioIsMusicEnabled() ? "on" : "off"),
             x, y, fs, Fade(WHITE, 0.85f)); y += line;
    if (ShipIsDriving()) {
        if (WorldIsSpaceActive()) {
            UiDrawText(TextFormat("Ship speed %.2f km/s",
                                  SpaceUnitsGameVelocityToKilometersPerSecond(
                                      hud->shipSpeed)),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        } else {
            UiDrawText(TextFormat("Ship speed %.1f blocks/s", hud->shipSpeed),
                     x, y, fs, Fade(WHITE, 0.85f)); y += line;
        }
    }
}

static bool DrawSettingStepper(Rectangle row, const char *label, float *value)
{
    bool changed = false;
    UiDrawText(label, (int)row.x, (int)row.y + 10, 17, Fade(WHITE, 0.84f));
    Rectangle minus = { row.x + row.width - 126.0f, row.y, 38.0f, 38.0f };
    Rectangle plus = { row.x + row.width - 38.0f, row.y, 38.0f, 38.0f };
    if (DrawMenuButton(minus, "-", false)) {
        *value = fmaxf(0.0f, *value - 0.10f);
        changed = true;
    }
    if (DrawMenuButton(plus, "+", false)) {
        *value = fminf(1.0f, *value + 0.10f);
        changed = true;
    }
    const char *percentage = TextFormat("%d%%", (int)roundf(*value * 100.0f));
    int textWidth = UiMeasureText(percentage, 16);
    UiDrawText(percentage, (int)(row.x + row.width - 63.0f - textWidth * 0.5f),
               (int)row.y + 11, 16, WHITE);
    return changed;
}

void DrawPauseMenu(PauseMenuSettings *settings, PauseMenuActions *actions)
{
    if (!settings || !actions) return;
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));

    float panelHeight = fminf(660.0f, (float)sh - 36.0f);
    Rectangle panel = { sw / 2 - 270.0f, ((float)sh - panelHeight) * 0.5f,
                        540.0f, panelHeight };
    DrawRectangleRounded(panel, 0.05f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.05f, 8, 2.0f, Fade(WHITE, 0.45f));

    int left = (int)panel.x + 36;
    int contentWidth = (int)panel.width - 72;
    int y = (int)panel.y + 26;
    DrawCenteredText("Paused", y, 34, WHITE);
    y += 58;

    UiDrawText("Graphics quality", left, y, 17, Fade(WHITE, 0.84f));
    y += 30;
    float segmentWidth = ((float)contentWidth - 12.0f) / 3.0f;
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        Rectangle segment = { (float)left + quality * (segmentWidth + 6.0f),
                              (float)y, segmentWidth, 40.0f };
        bool selected = settings->graphicsQuality == (GraphicsQuality)quality;
        if (DrawMenuButton(segment,
                           GraphicsQualityName((GraphicsQuality)quality), selected) &&
            !selected) {
            settings->graphicsQuality = (GraphicsQuality)quality;
            actions->settingsChanged = true;
            actions->qualityChanged = true;
        }
    }
    y += 58;
    Rectangle volumeRow = { (float)left, (float)y, (float)contentWidth, 38.0f };
    if (DrawSettingStepper(volumeRow, "Master volume", &settings->masterVolume)) {
        actions->settingsChanged = true;
    }
    volumeRow.y += 48.0f;
    if (DrawSettingStepper(volumeRow, "Environment", &settings->ambientVolume)) {
        actions->settingsChanged = true;
    }
    volumeRow.y += 48.0f;
    if (DrawSettingStepper(volumeRow, "Music volume", &settings->musicVolume)) {
        actions->settingsChanged = true;
    }
    y += 160;
    Rectangle musicRect = { (float)left, (float)y, (float)contentWidth, 40.0f };
    if (DrawMenuButton(musicRect,
                       TextFormat("Music: %s", settings->musicEnabled ? "On" : "Off"),
                       settings->musicEnabled)) {
        settings->musicEnabled = !settings->musicEnabled;
        actions->settingsChanged = true;
    }
    y += 58;
    Rectangle weatherDamageRect = {
        (float)left, (float)y, (float)contentWidth, 40.0f
    };
    if (DrawMenuButton(
            weatherDamageRect,
            TextFormat("Weather damage: %s",
                       settings->weatherDamageEnabled ? "On" : "Off"),
            settings->weatherDamageEnabled)) {
        settings->weatherDamageEnabled = !settings->weatherDamageEnabled;
        actions->settingsChanged = true;
    }
    y += 52;
    Rectangle resumeRect = { (float)left, (float)y, (float)contentWidth, 44.0f };
    if (DrawMenuButton(resumeRect, "Resume", true)) actions->resume = true;
    y += 54;
    Rectangle saveRect = { (float)left, (float)y, 226.0f, 42.0f };
    Rectangle menuRect = { (float)left + 242.0f, (float)y, 226.0f, 42.0f };
    if (DrawMenuButton(saveRect, "Save World", false)) actions->saveWorld = true;
    if (DrawMenuButton(menuRect, "Return to Menu", false)) actions->returnToMenu = true;
    y += 52;
    Rectangle quitRect = { (float)left, (float)y, (float)contentWidth, 42.0f };
    if (DrawMenuButton(quitRect, "Save & Quit", false)) actions->saveAndQuit = true;
}
