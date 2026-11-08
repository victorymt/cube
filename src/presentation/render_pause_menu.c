#include "presentation/render_pause_menu_internal.h"

#include "presentation/render_ui.h"

#include <math.h>
#include <stdio.h>

static bool PointerInRect(const UiPointerSnapshot *pointer, Rectangle rect)
{
    return pointer && UiPointInRect(pointer->position, rect);
}

static void DrawPauseButton(Rectangle rect, const char *label, bool primary,
                            bool enabled, const UiPointerSnapshot *pointer,
                            int fontSize)
{
    bool hovered = enabled && PointerInRect(pointer, rect);
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
    int textWidth = UiMeasureText(label, fontSize);
    UiDrawText(label, (int)(rect.x + rect.width * 0.5f - textWidth * 0.5f),
               (int)(rect.y + rect.height * 0.5f - fontSize * 0.5f),
               fontSize, enabled ? WHITE : Fade(WHITE, 0.35f));
}

static void DrawPausePanel(const PauseMenuUiLayout *layout)
{
    float roundness = layout->mode == PAUSE_MENU_LAYOUT_TINY ? 0.04f : 0.05f;
    int segments = layout->mode == PAUSE_MENU_LAYOUT_TINY ? 6 : 8;
    float lineWidth = layout->mode == PAUSE_MENU_LAYOUT_TINY ? 1.5f : 2.0f;
    DrawRectangleRounded(layout->panel, roundness, segments,
                         (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(layout->panel, roundness, segments, lineWidth,
                                Fade(WHITE, 0.45f));
}

static void DrawValueStepper(Rectangle row, Rectangle minus, Rectangle plus,
                             const char *label, const char *shown, bool enabled,
                             int fontSize, const UiPointerSnapshot *pointer)
{
    UiDrawText(label, (int)row.x, (int)row.y + 7, fontSize,
               enabled ? Fade(WHITE, 0.84f) : Fade(WHITE, 0.55f));
    DrawPauseButton(minus, "-", false, enabled, pointer, fontSize + 2);
    DrawPauseButton(plus, "+", false, enabled, pointer, fontSize + 2);
    int textWidth = UiMeasureText(shown, fontSize - 1);
    UiDrawText(shown,
               (int)(row.x + row.width - 83.0f - textWidth * 0.5f),
               (int)row.y + 8, fontSize - 1,
               enabled ? WHITE : Fade(WHITE, 0.60f));
}

static void DrawWorkerStepper(const PauseMenuSettings *settings,
                              const PauseMenuUiLayout *layout,
                              const UiPointerSnapshot *pointer)
{
    char shown[16];
    if (settings->chunkWorkerCount == 0) {
        snprintf(shown, sizeof(shown), "Auto (%d)",
                 settings->activeChunkWorkerCount);
    } else {
        snprintf(shown, sizeof(shown), "%d", settings->chunkWorkerCount);
    }
    bool enabled = !settings->chunkWorkerCountLocked;
    const char *label = enabled ? "Chunk workers" : "Chunk workers (CLI)";
    int fontSize = layout->mode == PAUSE_MENU_LAYOUT_TINY ? 14 : 17;
    if (layout->mode == PAUSE_MENU_LAYOUT_TINY) {
        label = enabled ? "Workers" : "Workers (CLI)";
    }
    DrawValueStepper(layout->workerRow, layout->workerMinus,
                     layout->workerPlus, label, shown, enabled, fontSize,
                     pointer);
}

static void DrawVolumeStepper(const PauseMenuSettings *settings,
                              const PauseMenuUiLayout *layout, int index,
                              const char *label,
                              const UiPointerSnapshot *pointer)
{
    const float *values[3] = {
        &settings->masterVolume,
        &settings->ambientVolume,
        &settings->musicVolume
    };
    char shown[8];
    snprintf(shown, sizeof(shown), "%d%%", (int)roundf(*values[index] * 100.0f));
    int fontSize = layout->mode == PAUSE_MENU_LAYOUT_TINY ? 14 : 17;
    DrawValueStepper(layout->volumeRows[index], layout->volumeMinus[index],
                     layout->volumePlus[index], label, shown, true, fontSize,
                     pointer);
}

static void DrawTinyPauseMenu(const PauseMenuSettings *settings,
                              const PauseMenuUiLayout *layout,
                              const UiPointerSnapshot *pointer)
{
    DrawCenteredText("Paused", (int)layout->panel.y + 8, 22, WHITE);
    const char *tabs[PAUSE_MENU_PAGE_COUNT] = { "System", "Audio", "Game" };
    for (int index = 0; index < PAUSE_MENU_PAGE_COUNT; index++) {
        DrawPauseButton(layout->tabs[index], tabs[index],
                        layout->page == (PauseMenuPage)index, true, pointer, 15);
    }

    if (layout->page == PAUSE_MENU_PAGE_SYSTEM) {
        UiDrawText("Graphics quality", (int)layout->workerRow.x,
                   (int)layout->quality[0].y - 19, 14, Fade(WHITE, 0.84f));
        for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
            DrawPauseButton(layout->quality[quality],
                            GraphicsQualityName((GraphicsQuality)quality),
                            settings->graphicsQuality == (GraphicsQuality)quality,
                            true, pointer, 15);
        }
        DrawWorkerStepper(settings, layout, pointer);
        DrawPauseButton(layout->geology,
                        settings->geologyEnabled ? "Volcanoes: On" : "Volcanoes: Off",
                        settings->geologyEnabled, true, pointer, 15);
        return;
    }

    if (layout->page == PAUSE_MENU_PAGE_AUDIO) {
        DrawVolumeStepper(settings, layout, 0, "Master", pointer);
        DrawVolumeStepper(settings, layout, 1, "Environment", pointer);
        DrawVolumeStepper(settings, layout, 2, "Music volume", pointer);
        DrawPauseButton(layout->music,
                        settings->musicEnabled ? "Music: On" : "Music: Off",
                        settings->musicEnabled, true, pointer, 15);
        DrawPauseButton(
            layout->weatherDamage,
            settings->weatherDamageEnabled ?
                "Weather damage: On" : "Weather damage: Off",
            settings->weatherDamageEnabled, true, pointer, 15);
        return;
    }

    DrawPauseButton(layout->resume, "Resume", true, true, pointer, 15);
    DrawPauseButton(layout->saveWorld, "Save World", false, true, pointer, 15);
    DrawPauseButton(layout->returnToMenu, "Return to Menu", false, true,
                    pointer, 15);
    DrawPauseButton(layout->saveAndQuit, "Save & Quit", false, true, pointer,
                    15);
}

static void DrawCompactPauseMenu(const PauseMenuSettings *settings,
                                 const PauseMenuUiLayout *layout,
                                 const UiPointerSnapshot *pointer)
{
    DrawCenteredText("Paused", (int)layout->panel.y + 24, 30, WHITE);
    UiDrawText("Graphics quality", (int)layout->workerRow.x,
               (int)layout->quality[0].y - 25, 16, Fade(WHITE, 0.84f));
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        DrawPauseButton(layout->quality[quality],
                        GraphicsQualityName((GraphicsQuality)quality),
                        settings->graphicsQuality == (GraphicsQuality)quality,
                        true, pointer, 16);
    }
    DrawWorkerStepper(settings, layout, pointer);
    DrawVolumeStepper(settings, layout, 0, "Master volume", pointer);
    DrawVolumeStepper(settings, layout, 1, "Environment", pointer);
    DrawVolumeStepper(settings, layout, 2, "Music volume", pointer);
    DrawPauseButton(layout->music,
                    settings->musicEnabled ? "Music: On" : "Music: Off",
                    settings->musicEnabled, true, pointer, 16);
    DrawPauseButton(
        layout->weatherDamage,
        settings->weatherDamageEnabled ?
            "Weather damage: On" : "Weather damage: Off",
        settings->weatherDamageEnabled, true, pointer, 16);
    DrawPauseButton(layout->geology,
                    settings->geologyEnabled ? "Volcanoes: On" : "Volcanoes: Off",
                    settings->geologyEnabled, true, pointer, 16);
    DrawPauseButton(layout->resume, "Resume", true, true, pointer, 16);
    DrawPauseButton(layout->saveWorld, "Save World", false, true, pointer, 16);
    DrawPauseButton(layout->returnToMenu, "Return to Menu", false, true,
                    pointer, 16);
    DrawPauseButton(layout->saveAndQuit, "Save & Quit", false, true, pointer,
                    16);
}

static void DrawFullPauseMenu(const PauseMenuSettings *settings,
                              const PauseMenuUiLayout *layout,
                              const UiPointerSnapshot *pointer)
{
    DrawCenteredText("Paused", (int)layout->panel.y + 26, 34, WHITE);
    UiDrawText("Graphics quality", (int)layout->workerRow.x,
               (int)layout->quality[0].y - 30, 17, Fade(WHITE, 0.84f));
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        DrawPauseButton(layout->quality[quality],
                        GraphicsQualityName((GraphicsQuality)quality),
                        settings->graphicsQuality == (GraphicsQuality)quality,
                        true, pointer, 17);
    }
    DrawWorkerStepper(settings, layout, pointer);
    DrawVolumeStepper(settings, layout, 0, "Master volume", pointer);
    DrawVolumeStepper(settings, layout, 1, "Environment", pointer);
    DrawVolumeStepper(settings, layout, 2, "Music volume", pointer);
    DrawPauseButton(layout->music,
                    settings->musicEnabled ? "Music: On" : "Music: Off",
                    settings->musicEnabled, true, pointer, 17);
    DrawPauseButton(layout->geology,
                    settings->geologyEnabled ? "Volcanoes: On" : "Volcanoes: Off",
                    settings->geologyEnabled, true, pointer, 17);
    DrawPauseButton(
        layout->weatherDamage,
        settings->weatherDamageEnabled ?
            "Weather damage: On" : "Weather damage: Off",
        settings->weatherDamageEnabled, true, pointer, 17);
    DrawPauseButton(layout->resume, "Resume", true, true, pointer, 17);
    DrawPauseButton(layout->saveWorld, "Save World", false, true, pointer, 17);
    DrawPauseButton(layout->returnToMenu, "Return to Menu", false, true,
                    pointer, 17);
    DrawPauseButton(layout->saveAndQuit, "Save & Quit", false, true, pointer,
                    17);
}

void DrawPauseMenu(const PauseMenuSettings *settings,
                   const PauseMenuUiLayout *layout,
                   const UiPointerSnapshot *pointer)
{
    if (!settings || !layout) return;
    DrawRectangle(0, 0, layout->screenWidth, layout->screenHeight,
                  Fade(BLACK, 0.55f));
    DrawPausePanel(layout);
    if (layout->mode == PAUSE_MENU_LAYOUT_TINY) {
        DrawTinyPauseMenu(settings, layout, pointer);
    } else if (layout->mode == PAUSE_MENU_LAYOUT_COMPACT) {
        DrawCompactPauseMenu(settings, layout, pointer);
    } else {
        DrawFullPauseMenu(settings, layout, pointer);
    }
}
