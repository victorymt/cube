#include "presentation/render_pause_menu_internal.h"

#include <math.h>
#include <stdio.h>

static bool DrawTinyButton(Rectangle rect, const char *label, bool primary,
                           bool enabled)
{
    Vector2 mouse = GetMousePosition();
    bool hovered = enabled && CheckCollisionPointRec(mouse, rect);
    Color fill = primary ? (Color){ 68, 142, 90, 255 }
                         : (Color){ 38, 45, 53, 255 };
    Color hoverFill = primary ? (Color){ 83, 164, 104, 255 }
                              : (Color){ 52, 61, 70, 255 };
    if (!enabled) fill = Fade(fill, 0.55f);
    DrawRectangleRounded(rect, 0.08f, 6, hovered ? hoverFill : fill);
    DrawRectangleRoundedLinesEx(
        rect, 0.08f, 6, 1.5f,
        enabled ? (hovered ? WHITE : Fade(WHITE, 0.55f))
                : Fade(WHITE, 0.22f));
    int fontSize = 15;
    int textWidth = UiMeasureText(label, fontSize);
    UiDrawText(label, (int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
               (int)(rect.y + rect.height * 0.5f - fontSize * 0.5f),
               fontSize, enabled ? WHITE : Fade(WHITE, 0.35f));
    return hovered && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
}

static bool DrawTinyValueStepper(Rectangle row, const char *label,
                                 const char *shown, bool enabled,
                                 bool *minusPressed, bool *plusPressed)
{
    UiDrawText(label, (int)row.x, (int)row.y + 7, 14,
               enabled ? Fade(WHITE, 0.84f) : Fade(WHITE, 0.55f));
    Rectangle minus = { row.x + row.width - 104.0f, row.y, 28.0f, 28.0f };
    Rectangle plus = { row.x + row.width - 28.0f, row.y, 28.0f, 28.0f };
    *minusPressed = DrawTinyButton(minus, "-", false, enabled);
    *plusPressed = DrawTinyButton(plus, "+", false, enabled);
    int textWidth = UiMeasureText(shown, 13);
    UiDrawText(shown,
               (int)(row.x + row.width - 52.0f - textWidth * 0.5f),
               (int)row.y + 8, 13,
               enabled ? WHITE : Fade(WHITE, 0.60f));
    return *minusPressed || *plusPressed;
}

static bool DrawTinyVolumeStepper(Rectangle row, const char *label,
                                  float *value)
{
    bool minusPressed = false;
    bool plusPressed = false;
    char shown[8];
    snprintf(shown, sizeof(shown), "%d%%", (int)roundf(*value * 100.0f));
    if (!DrawTinyValueStepper(row, label, shown, true,
                              &minusPressed, &plusPressed)) return false;
    if (minusPressed) *value = fmaxf(0.0f, *value - 0.10f);
    if (plusPressed) *value = fminf(1.0f, *value + 0.10f);
    return true;
}

static bool DrawTinyWorkerStepper(Rectangle row, PauseMenuSettings *settings)
{
    bool minusPressed = false;
    bool plusPressed = false;
    char shown[16];
    if (settings->chunkWorkerCount == 0) {
        snprintf(shown, sizeof(shown), "Auto(%d)",
                 settings->activeChunkWorkerCount);
    } else {
        snprintf(shown, sizeof(shown), "%d", settings->chunkWorkerCount);
    }
    bool changed = DrawTinyValueStepper(
        row, settings->chunkWorkerCountLocked ? "Workers (CLI)" : "Workers",
        shown, !settings->chunkWorkerCountLocked,
        &minusPressed, &plusPressed);
    if (minusPressed) {
        settings->chunkWorkerCount = settings->chunkWorkerCount > 0
            ? settings->chunkWorkerCount - 1 : settings->maxChunkWorkerCount;
    }
    if (plusPressed) {
        settings->chunkWorkerCount = settings->chunkWorkerCount <
            settings->maxChunkWorkerCount
            ? settings->chunkWorkerCount + 1 : 0;
    }
    return changed;
}

void DrawTinyPauseMenu(PauseMenuSettings *settings,
                              PauseMenuActions *actions, int sw, int sh)
{
    static int page = 0;
    float panelWidth = fminf(420.0f, (float)sw - 16.0f);
    Rectangle panel = { ((float)sw - panelWidth) * 0.5f, 6.0f,
                        panelWidth, (float)sh - 12.0f };
    DrawRectangleRounded(panel, 0.04f, 6, (Color){ 30, 38, 45, 248 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 6, 1.5f,
                                Fade(WHITE, 0.45f));
    DrawCenteredText("Paused", (int)panel.y + 8, 22, WHITE);

    float contentX = panel.x + 12.0f;
    float contentWidth = panel.width - 24.0f;
    float tabWidth = (contentWidth - 12.0f) / 3.0f;
    const char *tabs[3] = { "System", "Audio", "Game" };
    for (int index = 0; index < 3; index++) {
        Rectangle tab = { contentX + index * (tabWidth + 6.0f),
                          panel.y + 36.0f, tabWidth, 26.0f };
        if (DrawTinyButton(tab, tabs[index], page == index, true)) page = index;
    }

    float y = panel.y + 70.0f;
    if (page == 0) {
        UiDrawText("Graphics quality", (int)contentX, (int)y, 14,
                   Fade(WHITE, 0.84f));
        y += 19.0f;
        float width = (contentWidth - 8.0f) / 3.0f;
        for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
            Rectangle button = { contentX + quality * (width + 4.0f), y,
                                 width, 28.0f };
            bool selected = settings->graphicsQuality ==
                            (GraphicsQuality)quality;
            if (DrawTinyButton(button,
                               GraphicsQualityName((GraphicsQuality)quality),
                               selected, true) && !selected) {
                settings->graphicsQuality = (GraphicsQuality)quality;
                actions->settingsChanged = true;
                actions->qualityChanged = true;
            }
        }
        Rectangle worker = { contentX, y + 39.0f, contentWidth, 28.0f };
        if (DrawTinyWorkerStepper(worker, settings)) {
            actions->settingsChanged = true;
            actions->workersChanged = true;
        }
        return;
    }

    if (page == 1) {
        Rectangle row = { contentX, y, contentWidth, 28.0f };
        if (DrawTinyVolumeStepper(row, "Master", &settings->masterVolume))
            actions->settingsChanged = true;
        row.y += 30.0f;
        if (DrawTinyVolumeStepper(row, "Environment", &settings->ambientVolume))
            actions->settingsChanged = true;
        row.y += 30.0f;
        if (DrawTinyVolumeStepper(row, "Music volume", &settings->musicVolume))
            actions->settingsChanged = true;
        row.y += 34.0f;
        if (DrawTinyButton(row,
                           settings->musicEnabled ? "Music: On" : "Music: Off",
                           settings->musicEnabled, true)) {
            settings->musicEnabled = !settings->musicEnabled;
            actions->settingsChanged = true;
        }
        row.y += 32.0f;
        if (DrawTinyButton(
                row, settings->weatherDamageEnabled ?
                    "Weather damage: On" : "Weather damage: Off",
                settings->weatherDamageEnabled, true)) {
            settings->weatherDamageEnabled = !settings->weatherDamageEnabled;
            actions->settingsChanged = true;
        }
        return;
    }

    Rectangle action = { contentX, y, contentWidth, 28.0f };
    if (DrawTinyButton(action, "Resume", true, true)) actions->resume = true;
    action.y += 36.0f;
    if (DrawTinyButton(action, "Save World", false, true))
        actions->saveWorld = true;
    action.y += 36.0f;
    if (DrawTinyButton(action, "Return to Menu", false, true))
        actions->returnToMenu = true;
    action.y += 36.0f;
    if (DrawTinyButton(action, "Save & Quit", false, true))
        actions->saveAndQuit = true;
}
