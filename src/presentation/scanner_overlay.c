#include "presentation/scanner_overlay.h"

#include "ecology/ecology.h"
#include "gameplay/discovery.h"
#include "presentation/render.h"

#include "raymath.h"

static Color PlanetPoiColor(PlanetPoiType type)
{
    switch (type) {
    case PLANET_POI_RELIC: return (Color){ 255, 190, 86, 255 };
    case PLANET_POI_RESOURCE_CACHE: return (Color){ 88, 214, 255, 255 };
    case PLANET_POI_ANOMALY:
    default: return (Color){ 206, 114, 255, 255 };
    }
}

void DrawPlanetPoiScanner(const Camera3D *camera, Vector3 playerPosition)
{
    if (!camera) return;
    PlanetPoi poi = { 0 };
    if (!PlanetPoiNearest(playerPosition, &poi)) return;

    Vector3 target = { poi.x + 0.5f, poi.y + 0.5f, poi.z + 0.5f };
    float distance = Vector3Distance(playerPosition, target);
    Color color = PlanetPoiColor(poi.type);
    int screenWidth = GetScreenWidth();
    int screenHeight = GetScreenHeight();
    Rectangle panel = {
        screenWidth * 0.5f - 182.0f, 86.0f, 364.0f, 86.0f
    };
    DrawRectangleRounded(panel, 0.06f, 6, Fade(BLACK, 0.68f));
    DrawRectangleRoundedLinesEx(panel, 0.06f, 6, 1.5f,
                                Fade(color, 0.84f));
    DrawCircle((int)panel.x + 20, (int)panel.y + 20, 6.0f, color);
    UiDrawText(TextFormat("SCAN  %s  %.0f m", poi.name, distance),
               (int)panel.x + 36, (int)panel.y + 10, 18, RAYWHITE);
    UiDrawText(TextFormat("BIOSPHERE  %s", PlanetEcologyLifeName()),
               (int)panel.x + 36, (int)panel.y + 36, 15,
               Fade(RAYWHITE, 0.82f));
    UiDrawText(TextFormat("%s  |  %s  |  %s", PlanetEcologyBiomassName(),
                          PlanetEcologyChemistryName(),
                          PlanetEcologyBodyPlanName()),
               (int)panel.x + 36, (int)panel.y + 57, 13,
               Fade(RAYWHITE, 0.68f));

    Vector3 forward = Vector3Normalize(
        Vector3Subtract(camera->target, camera->position));
    Vector3 toTarget = Vector3Subtract(target, camera->position);
    bool ahead = Vector3DotProduct(forward, toTarget) > 0.0f;
    Vector2 screen = GetWorldToScreen(target, *camera);
    if (!ahead || screen.x < 0.0f || screen.x > (float)screenWidth ||
        screen.y < 0.0f || screen.y > (float)screenHeight) {
        return;
    }

    Vector2 center = { screenWidth * 0.5f, screenHeight * 0.5f };
    DrawLineV(center, screen, Fade(color, 0.46f));
    DrawCircleV(screen, 10.0f, Fade(color, 0.20f));
    DrawCircleLines((int)screen.x, (int)screen.y, 7.0f, color);
    UiDrawText(poi.name, (int)screen.x + 12, (int)screen.y - 8, 16, color);
}
