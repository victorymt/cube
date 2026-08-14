#include "starmap.h"

#include "raylib.h"
#include "raymath.h"
#include "render.h"
#include "space.h"

#include <math.h>
#include <string.h>

#define STAR_MAP_VISIBLE_ROWS 10
#define STAR_MAP_MIN_RANGE STAR_NAVIGATION_RANGE
#define STAR_MAP_MAX_RANGE (STAR_NAVIGATION_RANGE * 4.0f)
#define STAR_MAP_PAN_FRACTION 0.55f

typedef struct StarMapEntry {
    SolarSystemDef sys;
    float dist;
} StarMapEntry;

static bool open = false;
static StarMapEntry entries[STAR_NAVIGATION_MAX_SYSTEMS];
static int entryCount = 0;
static int selected = 0;
static int scroll = 0;
static bool travelRequested = false;
static SolarSystemDef travelSystem = { 0 };
static Vector3 mapPlayerPosition = { 0 };
static Vector3 mapCenterPosition = { 0 };
static float mapRange = STAR_MAP_MIN_RANGE;
static bool mapCentered = false;
static bool entriesDirty = true;

void StarMapOpen(void)
{
    open = true;
    selected = 0;
    scroll = 0;
    travelRequested = false;
    mapRange = STAR_MAP_MIN_RANGE;
    mapCentered = false;
    entriesDirty = true;
}

bool StarMapIsOpen(void)
{
    return open;
}

void StarMapClose(void)
{
    open = false;
    travelRequested = false;
}

static void RefreshEntries(Vector3 playerPosition)
{
    mapPlayerPosition = playerPosition;
    if (!mapCentered) {
        mapCenterPosition = playerPosition;
        mapCentered = true;
        entriesDirty = true;
    }
    if (!entriesDirty) return;

    SolarSystemDef systems[STAR_NAVIGATION_MAX_SYSTEMS];
    entryCount = StarSystemsNear(mapCenterPosition, mapRange, systems,
                                 STAR_NAVIGATION_MAX_SYSTEMS);
    for (int i = 0; i < entryCount; i++) {
        entries[i].sys = systems[i];
        entries[i].dist = Vector3Distance(systems[i].center, mapPlayerPosition);
    }
    if (selected >= entryCount) selected = entryCount - 1;
    if (selected < 0) selected = 0;
    if (scroll > entryCount - STAR_MAP_VISIBLE_ROWS) scroll = entryCount - STAR_MAP_VISIBLE_ROWS;
    if (scroll < 0) scroll = 0;
    entriesDirty = false;
}

static void PanMap(float dx, float dz)
{
    mapCenterPosition.x += dx;
    mapCenterPosition.z += dz;
    selected = 0;
    scroll = 0;
    entriesDirty = true;
}

static void ZoomMap(float factor)
{
    float newRange = Clamp(mapRange * factor, STAR_MAP_MIN_RANGE, STAR_MAP_MAX_RANGE);
    if (fabsf(newRange - mapRange) < 0.1f) return;
    mapRange = newRange;
    selected = 0;
    scroll = 0;
    entriesDirty = true;
}

void StarMapUpdate(Vector3 playerPosition)
{
    if (!open) return;

    RefreshEntries(playerPosition);

    float panDistance = mapRange * STAR_MAP_PAN_FRACTION;
    if (IsKeyPressed(KEY_A)) PanMap(-panDistance, 0.0f);
    if (IsKeyPressed(KEY_D)) PanMap(panDistance, 0.0f);
    if (IsKeyPressed(KEY_W)) PanMap(0.0f, -panDistance);
    if (IsKeyPressed(KEY_S)) PanMap(0.0f, panDistance);
    if (IsKeyPressed(KEY_PAGE_UP)) ZoomMap(2.0f);
    if (IsKeyPressed(KEY_PAGE_DOWN)) ZoomMap(0.5f);
    if (entriesDirty) RefreshEntries(playerPosition);

    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
        if (selected < entryCount - 1) selected++;
        if (selected > scroll + STAR_MAP_VISIBLE_ROWS - 1) scroll++;
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) {
        if (selected > 0) selected--;
        if (selected < scroll) scroll--;
    }

    int wheel = (int)GetMouseWheelMove();
    if (wheel != 0) {
        scroll -= wheel;
        if (scroll > entryCount - STAR_MAP_VISIBLE_ROWS) scroll = entryCount - STAR_MAP_VISIBLE_ROWS;
        if (scroll < 0) scroll = 0;
        if (selected < scroll) selected = scroll;
        if (selected > scroll + STAR_MAP_VISIBLE_ROWS - 1) selected = scroll + STAR_MAP_VISIBLE_ROWS - 1;
    }

    if (IsKeyPressed(KEY_ENTER) && entryCount > 0) {
        const SolarSystemDef *sys = &entries[selected].sys;
        travelSystem = *sys;
        travelRequested = true;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        open = false;
    }
}

bool StarMapConsumeTravel(SolarSystemDef *outSystem)
{
    if (!travelRequested || !outSystem) return false;
    travelRequested = false;
    *outSystem = travelSystem;
    return true;
}

void StarMapDraw(void)
{
    if (!open) return;

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    float panelWidth = fminf(1040.0f, (float)sw - 32.0f);
    float panelHeight = fminf(660.0f, (float)sh - 32.0f);
    Rectangle panel = { (float)sw * 0.5f - panelWidth * 0.5f,
                        (float)sh * 0.5f - panelHeight * 0.5f,
                        panelWidth, panelHeight };
    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 24, 30, 38, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    UiDrawText("Star Map", (int)panel.x + 26, (int)panel.y + 18, 27, WHITE);
    UiDrawText(TextFormat("%d charted systems  |  %.0f block view",
                        entryCount, mapRange),
             (int)panel.x + 26, (int)panel.y + 54, 15, Fade(WHITE, 0.62f));
    UiDrawText("Enter warp   Up/Down select   WASD pan   PgUp/PgDn zoom",
             (int)(panel.x + panel.width - 420.0f), (int)panel.y + 28, 15,
             Fade(WHITE, 0.68f));

    if (entryCount == 0) {
        UiDrawText(TextFormat("No systems within %.0f blocks of this chart position.", mapRange),
                 (int)panel.x + 26, (int)panel.y + 130, 18, Fade(WHITE, 0.6f));
        return;
    }

    float mapWidth = fminf(430.0f, panel.width * 0.43f);
    Rectangle map = { panel.x + 24.0f, panel.y + 88.0f, mapWidth,
                      panel.height - 118.0f };
    float listX = map.x + map.width + 26.0f;
    float listWidth = panel.x + panel.width - listX - 24.0f;
    DrawRectangleRounded(map, 0.025f, 6, (Color){ 10, 18, 28, 235 });
    DrawRectangleLinesEx(map, 1.0f, Fade(WHITE, 0.25f));

    for (int i = 1; i < 4; i++) {
        float x = map.x + map.width * (float)i / 4.0f;
        float y = map.y + map.height * (float)i / 4.0f;
        DrawLine((int)x, (int)map.y, (int)x, (int)(map.y + map.height),
                 Fade(WHITE, 0.08f));
        DrawLine((int)map.x, (int)y, (int)(map.x + map.width), (int)y,
                 Fade(WHITE, 0.08f));
    }
    float centerX = map.x + map.width * 0.5f;
    float centerY = map.y + map.height * 0.5f;
    DrawLine((int)centerX, (int)map.y, (int)centerX, (int)(map.y + map.height),
             Fade(WHITE, 0.18f));
    DrawLine((int)map.x, (int)centerY, (int)(map.x + map.width), (int)centerY,
             Fade(WHITE, 0.18f));
    UiDrawText("X", (int)(map.x + map.width - 18.0f), (int)centerY + 8, 14,
             Fade(WHITE, 0.48f));
    UiDrawText("Z", (int)centerX + 8, (int)map.y + 8, 14, Fade(WHITE, 0.48f));

    for (int i = 0; i < entryCount; i++) {
        float dx = entries[i].sys.center.x - mapCenterPosition.x;
        float dz = entries[i].sys.center.z - mapCenterPosition.z;
        float px = centerX + dx * map.width * 0.5f / mapRange;
        float py = centerY + dz * map.height * 0.5f / mapRange;
        if (px < map.x - 4.0f || px > map.x + map.width + 4.0f ||
            py < map.y - 4.0f || py > map.y + map.height + 4.0f) continue;
        Color dot = SpectrumColor(entries[i].sys.spectrum);
        if (i == selected) {
            DrawCircleLines((int)px, (int)py, 9.0f, Fade(WHITE, 0.9f));
            DrawCircle((int)px, (int)py, 4.0f, dot);
        } else {
            DrawCircle((int)px, (int)py, 2.8f, dot);
        }
    }
    float playerDx = mapPlayerPosition.x - mapCenterPosition.x;
    float playerDz = mapPlayerPosition.z - mapCenterPosition.z;
    float playerX = centerX + playerDx * map.width * 0.5f / mapRange;
    float playerY = centerY + playerDz * map.height * 0.5f / mapRange;
    if (playerX >= map.x && playerX <= map.x + map.width &&
        playerY >= map.y && playerY <= map.y + map.height) {
        DrawTriangle((Vector2){ playerX, playerY - 8.0f },
                     (Vector2){ playerX - 6.0f, playerY + 5.0f },
                     (Vector2){ playerX + 6.0f, playerY + 5.0f }, WHITE);
        UiDrawText("You", (int)playerX + 10, (int)playerY - 7, 14, Fade(WHITE, 0.65f));
    }
    UiDrawText(TextFormat("Chart sector  X %d   Z %d",
                        (int)floorf((mapCenterPosition.x + (float)SpaceOriginX()) /
                                    (float)STAR_SYSTEM_SPACING),
                        (int)floorf((mapCenterPosition.z + (float)SpaceOriginZ()) /
                                    (float)STAR_SYSTEM_SPACING)),
             (int)map.x + 14, (int)(map.y + map.height - 25.0f), 14, Fade(WHITE, 0.6f));

    UiDrawText("SYSTEMS", (int)listX, (int)panel.y + 88, 14, Fade(WHITE, 0.58f));
    int rowH = 35;
    float listTop = panel.y + 110.0f;
    for (int i = scroll; i < scroll + STAR_MAP_VISIBLE_ROWS && i < entryCount; i++) {
        Rectangle row = { listX, listTop + (float)(i - scroll) * rowH,
                          listWidth, (float)rowH - 4.0f };
        bool sel = i == selected;
        if (sel) DrawRectangleRounded(row, 0.06f, 6, (Color){ 98, 160, 115, 95 });

        Color spec = SpectrumColor(entries[i].sys.spectrum);
        DrawCircle((int)row.x + 12, (int)row.y + 13, 5.0f, Fade(spec, 1.0f));
        UiDrawText(entries[i].sys.name, (int)row.x + 26, (int)row.y + 5, 17,
                 sel ? WHITE : Fade(WHITE, 0.88f));
        UiDrawText(TextFormat("%d planets", entries[i].sys.planetCount),
                 (int)(row.x + row.width - 170.0f), (int)row.y + 7, 14,
                 Fade(WHITE, 0.58f));
        UiDrawText(TextFormat("%.0f", entries[i].dist), (int)(row.x + row.width - 48.0f),
                 (int)row.y + 7, 14,
                 Fade(WHITE, 0.7f));
    }

    float infoY = listTop + STAR_MAP_VISIBLE_ROWS * rowH + 12.0f;
    DrawLine((int)listX, (int)infoY, (int)(listX + listWidth), (int)infoY,
             Fade(WHITE, 0.2f));
    if (entryCount > STAR_MAP_VISIBLE_ROWS) {
        UiDrawText(TextFormat("Selected %d / %d", selected + 1, entryCount),
                 (int)listX, (int)infoY + 10, 14, Fade(WHITE, 0.62f));
    }
    const SolarSystemDef *sys = &entries[selected].sys;
    float detailsY = infoY + (entryCount > STAR_MAP_VISIBLE_ROWS ? 34.0f : 10.0f);
    UiDrawText(sys->name, (int)listX, (int)detailsY, 22, WHITE);
    UiDrawText(TextFormat("%s   |   %.0f blocks   |   %d planets",
                        SpectrumName(sys->spectrum), entries[selected].dist,
                        sys->planetCount),
             (int)listX, (int)detailsY + 32, 15, Fade(WHITE, 0.72f));
    UiDrawText(TextFormat("System anchor  [%d, %d]   |   Y %.0f",
                        sys->anchorX, sys->anchorZ, sys->center.y),
             (int)listX, (int)detailsY + 56, 14, Fade(WHITE, 0.58f));
    UiDrawText("Enter: warp ship to this system", (int)listX, (int)detailsY + 86, 15,
             (Color){ 166, 220, 174, 255 });
    UiDrawText("Dots are generated stars; colors show their spectra",
             (int)map.x + 14, (int)map.y + 18, 13, Fade(WHITE, 0.58f));
    UiDrawText("Current position", (int)map.x + 14, (int)map.y + 40, 13,
             Fade(WHITE, 0.48f));
    DrawCircle((int)map.x + 8, (int)map.y + 46, 3.0f, WHITE);
}
