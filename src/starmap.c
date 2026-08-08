#include "starmap.h"

#include "raylib.h"
#include "space.h"

#include <math.h>
#include <string.h>

#define STAR_MAP_MAX_ENTRIES 40
#define STAR_MAP_VISIBLE_ROWS 10
#define STAR_MAP_RANGE 8000.0f

typedef struct StarMapEntry {
    SolarSystemDef sys;
    float dist;
} StarMapEntry;

static bool open = false;
static StarMapEntry entries[STAR_MAP_MAX_ENTRIES];
static int entryCount = 0;
static int selected = 0;
static int scroll = 0;
static bool travelRequested = false;
static Vector3 travelDestination = { 0 };

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
    SolarSystemDef systems[STAR_MAP_MAX_ENTRIES];
    entryCount = StarSystemsNear(playerPosition, STAR_MAP_RANGE, systems, STAR_MAP_MAX_ENTRIES);
    for (int i = 0; i < entryCount; i++) {
        entries[i].sys = systems[i];
        float dx = systems[i].center.x - playerPosition.x;
        float dz = systems[i].center.z - playerPosition.z;
        entries[i].dist = sqrtf(dx * dx + dz * dz);
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

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
    Rectangle panel = { sw / 2 - 340.0f, sh / 2 - 220.0f, 680.0f, 440.0f };
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 24, 30, 38, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    DrawText("Star Map - nearest systems", (int)panel.x + 26, (int)panel.y + 20, 26, WHITE);
    DrawText("Enter travel   Up/Down select   Esc close", (int)panel.x + 26, (int)panel.y + 56, 16, Fade(WHITE, 0.65f));

    if (entryCount == 0) {
        DrawText("No systems within 8000 blocks.", (int)panel.x + 26, (int)panel.y + 130, 18, Fade(WHITE, 0.6f));
        return;
    }

    int rowH = 36;
    for (int i = scroll; i < scroll + STAR_MAP_VISIBLE_ROWS && i < entryCount; i++) {
        Rectangle row = { panel.x + 22.0f, panel.y + 84.0f + (float)(i - scroll) * rowH,
                          panel.width - 44.0f, (float)rowH - 4.0f };
        bool sel = i == selected;
        if (sel) DrawRectangleRounded(row, 0.06f, 6, (Color){ 98, 160, 115, 95 });

        Color spec = SpectrumColor(entries[i].sys.spectrum);
        DrawCircle((int)row.x + 14, (int)row.y + 14, 6.0f, Fade(spec, 1.0f));
        DrawText(TextFormat("%s Prime", entries[i].sys.name), (int)row.x + 30, (int)row.y + 6, 18,
                 sel ? WHITE : Fade(WHITE, 0.88f));
        DrawText(SpectrumName(entries[i].sys.spectrum), (int)row.x + 250, (int)row.y + 7, 14, Fade(WHITE, 0.6f));
        DrawText(TextFormat("%d planets", entries[i].sys.planetCount), (int)row.x + 420, (int)row.y + 7, 14,
                 Fade(WHITE, 0.55f));
        DrawText(TextFormat("%.0f blocks", entries[i].dist), (int)(row.x + row.width - 140), (int)row.y + 7, 14,
                 Fade(WHITE, 0.7f));
    }

    if (entryCount > STAR_MAP_VISIBLE_ROWS) {
        DrawText(TextFormat("%d of %d systems", selected + 1, entryCount),
                 (int)panel.x + 26, (int)(panel.y + panel.height - 34), 15, Fade(WHITE, 0.6f));
    }
}
