#include "starmap.h"

#include "raylib.h"
#include "raymath.h"
#include "space.h"

#include <math.h>
#include <string.h>

#define STAR_MAP_VISIBLE_ROWS 10

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
static Vector3 travelDestination = { 0 };
static Vector3 mapPlayerPosition = { 0 };

void StarMapOpen(void)
{
    open = true;
    selected = 0;
    scroll = 0;
    travelRequested = false;
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
    SolarSystemDef systems[STAR_NAVIGATION_MAX_SYSTEMS];
    entryCount = StarSystemsNear(playerPosition, STAR_NAVIGATION_RANGE, systems,
                                 STAR_NAVIGATION_MAX_SYSTEMS);
    for (int i = 0; i < entryCount; i++) {
        entries[i].sys = systems[i];
        entries[i].dist = Vector3Distance(systems[i].center, playerPosition);
    }
    if (selected >= entryCount) selected = entryCount - 1;
    if (selected < 0) selected = 0;
    if (scroll > entryCount - STAR_MAP_VISIBLE_ROWS) scroll = entryCount - STAR_MAP_VISIBLE_ROWS;
    if (scroll < 0) scroll = 0;
}

void StarMapUpdate(Vector3 playerPosition)
{
    if (!open) return;

    RefreshEntries(playerPosition);

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
        float destY = sys->center.y + (float)sys->starRadius + 35.0f;
        if (destY > (float)SPACE_LAYER_TOP - 5.0f) destY = (float)SPACE_LAYER_TOP - 5.0f;
        travelDestination = (Vector3){
            sys->center.x,
            destY,
            sys->center.z
        };
        travelRequested = true;
    }
    if (IsKeyPressed(KEY_ESCAPE)) {
        open = false;
    }
}

bool StarMapConsumeTravel(Vector3 *outDestination)
{
    if (!travelRequested) return false;
    travelRequested = false;
    *outDestination = travelDestination;
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

    DrawText("Star Map", (int)panel.x + 26, (int)panel.y + 18, 27, WHITE);
    DrawText(TextFormat("%d reachable systems  |  %.0f block range",
                        entryCount, STAR_NAVIGATION_RANGE),
             (int)panel.x + 26, (int)panel.y + 54, 15, Fade(WHITE, 0.62f));
    DrawText("Enter travel   Up/Down select   Esc close",
             (int)(panel.x + panel.width - 300.0f), (int)panel.y + 28, 15,
             Fade(WHITE, 0.68f));

    if (entryCount == 0) {
        DrawText(TextFormat("No systems within %.0f blocks.", STAR_NAVIGATION_RANGE),
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
    DrawText("X", (int)(map.x + map.width - 18.0f), (int)centerY + 8, 14,
             Fade(WHITE, 0.48f));
    DrawText("Z", (int)centerX + 8, (int)map.y + 8, 14, Fade(WHITE, 0.48f));

    for (int i = 0; i < entryCount; i++) {
        float dx = entries[i].sys.center.x - mapPlayerPosition.x;
        float dz = entries[i].sys.center.z - mapPlayerPosition.z;
        float px = centerX + dx * map.width * 0.5f / STAR_NAVIGATION_RANGE;
        float py = centerY + dz * map.height * 0.5f / STAR_NAVIGATION_RANGE;
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
    DrawTriangle((Vector2){ centerX, centerY - 8.0f },
                 (Vector2){ centerX - 6.0f, centerY + 5.0f },
                 (Vector2){ centerX + 6.0f, centerY + 5.0f },
                 WHITE);
    DrawText("You", (int)centerX + 10, (int)centerY - 7, 14, Fade(WHITE, 0.65f));
    DrawText(TextFormat("X %.0f   Z %.0f", mapPlayerPosition.x, mapPlayerPosition.z),
             (int)map.x + 14, (int)(map.y + map.height - 25.0f), 14, Fade(WHITE, 0.6f));

    DrawText("SYSTEMS", (int)listX, (int)panel.y + 88, 14, Fade(WHITE, 0.58f));
    int rowH = 35;
    float listTop = panel.y + 110.0f;
    for (int i = scroll; i < scroll + STAR_MAP_VISIBLE_ROWS && i < entryCount; i++) {
        Rectangle row = { listX, listTop + (float)(i - scroll) * rowH,
                          listWidth, (float)rowH - 4.0f };
        bool sel = i == selected;
        if (sel) DrawRectangleRounded(row, 0.06f, 6, (Color){ 98, 160, 115, 95 });

        Color spec = SpectrumColor(entries[i].sys.spectrum);
        DrawCircle((int)row.x + 12, (int)row.y + 13, 5.0f, Fade(spec, 1.0f));
        DrawText(entries[i].sys.name, (int)row.x + 26, (int)row.y + 5, 17,
                 sel ? WHITE : Fade(WHITE, 0.88f));
        DrawText(TextFormat("%d planets", entries[i].sys.planetCount),
                 (int)(row.x + row.width - 170.0f), (int)row.y + 7, 14,
                 Fade(WHITE, 0.58f));
        DrawText(TextFormat("%.0f", entries[i].dist), (int)(row.x + row.width - 48.0f),
                 (int)row.y + 7, 14,
                 Fade(WHITE, 0.7f));
    }

    float infoY = listTop + STAR_MAP_VISIBLE_ROWS * rowH + 12.0f;
    DrawLine((int)listX, (int)infoY, (int)(listX + listWidth), (int)infoY,
             Fade(WHITE, 0.2f));
    if (entryCount > STAR_MAP_VISIBLE_ROWS) {
        DrawText(TextFormat("Selected %d / %d", selected + 1, entryCount),
                 (int)listX, (int)infoY + 10, 14, Fade(WHITE, 0.62f));
    }
    const SolarSystemDef *sys = &entries[selected].sys;
    float detailsY = infoY + (entryCount > STAR_MAP_VISIBLE_ROWS ? 34.0f : 10.0f);
    DrawText(sys->name, (int)listX, (int)detailsY, 22, WHITE);
    DrawText(TextFormat("%s   |   %.0f blocks   |   %d planets",
                        SpectrumName(sys->spectrum), entries[selected].dist,
                        sys->planetCount),
             (int)listX, (int)detailsY + 32, 15, Fade(WHITE, 0.72f));
    DrawText(TextFormat("Star center  X %.0f   Y %.0f   Z %.0f",
                        sys->center.x, sys->center.y, sys->center.z),
             (int)listX, (int)detailsY + 56, 14, Fade(WHITE, 0.58f));
    DrawText("Enter: travel to this system", (int)listX, (int)detailsY + 86, 15,
             (Color){ 166, 220, 174, 255 });
    DrawText("Dots are generated stars; colors show their spectra",
             (int)map.x + 14, (int)map.y + 18, 13, Fade(WHITE, 0.58f));
    DrawText("Current position", (int)map.x + 14, (int)map.y + 40, 13,
             Fade(WHITE, 0.48f));
    DrawCircle((int)map.x + 8, (int)map.y + 46, 3.0f, WHITE);
}
