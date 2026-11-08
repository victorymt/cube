#include "presentation/pause_menu_ui.h"

#include <math.h>
#include <stddef.h>

void PauseMenuUiReset(PauseMenuUiState *state)
{
    if (state) state->page = PAUSE_MENU_PAGE_SYSTEM;
}

static void SetValueStepper(Rectangle row, Rectangle *minus, Rectangle *plus)
{
    *minus = (Rectangle){ row.x + row.width - 126.0f, row.y, 38.0f, 38.0f };
    *plus = (Rectangle){ row.x + row.width - 38.0f, row.y, 38.0f, 38.0f };
}

static void BuildTinyLayout(PauseMenuUiLayout *layout, int sw, int sh)
{
    layout->screenWidth = sw;
    layout->screenHeight = sh;
    float panelWidth = fminf(420.0f, (float)sw - 16.0f);
    layout->panel = (Rectangle){ ((float)sw - panelWidth) * 0.5f, 6.0f,
                                 panelWidth, (float)sh - 12.0f };
    float contentX = layout->panel.x + 12.0f;
    float contentWidth = layout->panel.width - 24.0f;
    float tabWidth = (contentWidth - 12.0f) / 3.0f;
    for (int index = 0; index < PAUSE_MENU_PAGE_COUNT; index++) {
        layout->tabs[index] = (Rectangle){
            contentX + index * (tabWidth + 6.0f), layout->panel.y + 36.0f,
            tabWidth, 26.0f
        };
    }
    float y = layout->panel.y + 70.0f;
    layout->tabsVisible = true;

    if (layout->page == PAUSE_MENU_PAGE_SYSTEM) {
        layout->qualityVisible = true;
        layout->workerVisible = true;
        layout->geologyVisible = true;
        float qualityWidth = (contentWidth - 8.0f) / 3.0f;
        for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
            layout->quality[quality] = (Rectangle){
                contentX + quality * (qualityWidth + 4.0f), y + 19.0f,
                qualityWidth, 28.0f
            };
        }
        layout->workerRow = (Rectangle){ contentX, y + 39.0f,
                                         contentWidth, 28.0f };
        layout->workerMinus = (Rectangle){
            layout->workerRow.x + layout->workerRow.width - 104.0f,
            layout->workerRow.y, 28.0f, 28.0f
        };
        layout->workerPlus = (Rectangle){
            layout->workerRow.x + layout->workerRow.width - 28.0f,
            layout->workerRow.y, 28.0f, 28.0f
        };
        layout->geology = (Rectangle){ contentX, layout->workerRow.y + 36.0f,
                                       contentWidth, 28.0f };
    } else if (layout->page == PAUSE_MENU_PAGE_AUDIO) {
        layout->volumeVisible = true;
        layout->musicVisible = true;
        layout->weatherDamageVisible = true;
        for (int index = 0; index < 3; index++) {
            layout->volumeRows[index] = (Rectangle){
                contentX, y + index * 30.0f, contentWidth, 28.0f
            };
            layout->volumeMinus[index] = (Rectangle){
                contentX + contentWidth - 104.0f,
                layout->volumeRows[index].y, 28.0f, 28.0f
            };
            layout->volumePlus[index] = (Rectangle){
                contentX + contentWidth - 28.0f,
                layout->volumeRows[index].y, 28.0f, 28.0f
            };
        }
        layout->music = (Rectangle){ contentX, y + 94.0f, contentWidth, 28.0f };
        layout->weatherDamage = (Rectangle){ contentX, y + 126.0f,
                                             contentWidth, 28.0f };
    } else {
        layout->actionsVisible = true;
        layout->resume = (Rectangle){ contentX, y, contentWidth, 28.0f };
        layout->saveWorld = (Rectangle){ contentX, y + 36.0f,
                                         contentWidth, 28.0f };
        layout->returnToMenu = (Rectangle){ contentX, y + 72.0f,
                                            contentWidth, 28.0f };
        layout->saveAndQuit = (Rectangle){ contentX, y + 108.0f,
                                           contentWidth, 28.0f };
    }
}

static void BuildCompactLayout(PauseMenuUiLayout *layout, int sw, int sh)
{
    layout->screenWidth = sw;
    layout->screenHeight = sh;
    float panelWidth = fminf(720.0f, (float)sw - 28.0f);
    layout->panel = (Rectangle){ ((float)sw - panelWidth) * 0.5f, 12.0f,
                                 panelWidth, (float)sh - 24.0f };
    float contentX = layout->panel.x + 24.0f;
    float contentWidth = layout->panel.width - 48.0f;
    float gap = 20.0f;
    float settingsWidth = floorf((contentWidth - gap) * 0.63f);
    float actionsX = contentX + settingsWidth + gap;
    float actionsWidth = contentWidth - settingsWidth - gap;
    float y = layout->panel.y + 72.0f;

    layout->qualityVisible = true;
    float segmentWidth = (settingsWidth - 10.0f) / 3.0f;
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        layout->quality[quality] = (Rectangle){
            contentX + quality * (segmentWidth + 5.0f), y + 25.0f,
            segmentWidth, 36.0f
        };
    }
    layout->workerVisible = true;
    layout->workerRow = (Rectangle){ contentX, y + 73.0f, settingsWidth, 38.0f };
    SetValueStepper(layout->workerRow, &layout->workerMinus,
                    &layout->workerPlus);
    layout->volumeVisible = true;
    for (int index = 0; index < 3; index++) {
        layout->volumeRows[index] = (Rectangle){
            contentX, y + 121.0f + index * 44.0f, settingsWidth, 38.0f
        };
        SetValueStepper(layout->volumeRows[index], &layout->volumeMinus[index],
                        &layout->volumePlus[index]);
    }
    layout->musicVisible = true;
    layout->music = (Rectangle){ contentX, y + 257.0f, settingsWidth, 36.0f };
    layout->weatherDamageVisible = true;
    layout->weatherDamage = (Rectangle){ contentX, y + 303.0f,
                                         settingsWidth, 36.0f };
    layout->geologyVisible = true;
    layout->geology = (Rectangle){ contentX, y + 349.0f,
                                   settingsWidth, 36.0f };

    layout->actionsVisible = true;
    float actionY = layout->panel.y + 97.0f;
    layout->resume = (Rectangle){ actionsX, actionY, actionsWidth, 42.0f };
    layout->saveWorld = (Rectangle){ actionsX, actionY + 54.0f,
                                     actionsWidth, 42.0f };
    layout->returnToMenu = (Rectangle){ actionsX, actionY + 108.0f,
                                        actionsWidth, 42.0f };
    layout->saveAndQuit = (Rectangle){ actionsX, actionY + 162.0f,
                                       actionsWidth, 42.0f };
}

static void BuildFullLayout(PauseMenuUiLayout *layout, int sw, int sh)
{
    layout->screenWidth = sw;
    layout->screenHeight = sh;
    float panelHeight = fminf(660.0f, (float)sh - 36.0f);
    layout->panel = (Rectangle){ sw / 2 - 270.0f,
                                 ((float)sh - panelHeight) * 0.5f,
                                 540.0f, panelHeight };
    float left = layout->panel.x + 36.0f;
    float contentWidth = layout->panel.width - 72.0f;
    float y = layout->panel.y + 84.0f;

    layout->qualityVisible = true;
    float segmentWidth = (contentWidth - 12.0f) / 3.0f;
    for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
        layout->quality[quality] = (Rectangle){
            left + quality * (segmentWidth + 6.0f), y + 30.0f,
            segmentWidth, 40.0f
        };
    }
    layout->workerVisible = true;
    layout->workerRow = (Rectangle){ left, y + 88.0f, contentWidth, 38.0f };
    SetValueStepper(layout->workerRow, &layout->workerMinus,
                    &layout->workerPlus);
    layout->volumeVisible = true;
    for (int index = 0; index < 3; index++) {
        layout->volumeRows[index] = (Rectangle){
            left, y + 136.0f + index * 48.0f, contentWidth, 38.0f
        };
        SetValueStepper(layout->volumeRows[index], &layout->volumeMinus[index],
                        &layout->volumePlus[index]);
    }
    float toggleY = y + 296.0f;
    layout->musicVisible = true;
    layout->music = (Rectangle){ left, toggleY,
                                 (contentWidth - 12.0f) * 0.5f, 40.0f };
    layout->geologyVisible = true;
    layout->geology = (Rectangle){ left + layout->music.width + 12.0f,
                                   toggleY, layout->music.width, 40.0f };
    layout->weatherDamageVisible = true;
    layout->weatherDamage = (Rectangle){ left, toggleY + 58.0f,
                                         contentWidth, 40.0f };
    layout->actionsVisible = true;
    float actionY = toggleY + 110.0f;
    layout->resume = (Rectangle){ left, actionY, contentWidth, 44.0f };
    layout->saveWorld = (Rectangle){ left, actionY + 54.0f, 226.0f, 42.0f };
    layout->returnToMenu = (Rectangle){ left + 242.0f, actionY + 54.0f,
                                        226.0f, 42.0f };
    layout->saveAndQuit = (Rectangle){ left, actionY + 106.0f,
                                       contentWidth, 42.0f };
}

PauseMenuUiLayout PauseMenuUiLayoutFor(int screenWidth, int screenHeight,
                                       PauseMenuPage page)
{
    PauseMenuUiLayout layout = { 0 };
    if (page < PAUSE_MENU_PAGE_SYSTEM || page >= PAUSE_MENU_PAGE_COUNT) {
        page = PAUSE_MENU_PAGE_SYSTEM;
    }
    layout.page = page;
    if (screenWidth < 640 || screenHeight < 512) {
        layout.mode = PAUSE_MENU_LAYOUT_TINY;
        BuildTinyLayout(&layout, screenWidth, screenHeight);
    } else if (screenWidth < 800 || screenHeight < 696) {
        layout.mode = PAUSE_MENU_LAYOUT_COMPACT;
        BuildCompactLayout(&layout, screenWidth, screenHeight);
    } else {
        layout.mode = PAUSE_MENU_LAYOUT_FULL;
        BuildFullLayout(&layout, screenWidth, screenHeight);
    }
    return layout;
}

static bool PressedIn(const InputEvent *event, Rectangle rect)
{
    return event->type == INPUT_EVENT_BUTTON_DOWN &&
           event->button == INPUT_BUTTON_LEFT &&
           UiPointInRect(event->point, rect);
}

static void AdjustVolume(float *value, float delta, PauseMenuActions *actions)
{
    *value = fminf(1.0f, fmaxf(0.0f, *value + delta));
    actions->settingsChanged = true;
}

UiEventResult PauseMenuUiHandleEvent(PauseMenuUiState *state,
                                     PauseMenuSettings *settings,
                                     PauseMenuActions *actions,
                                     const PauseMenuUiLayout *layout,
                                     const InputFrame *frame,
                                     const InputEvent *event)
{
    if (!state || !settings || !actions || !layout || !frame || !event) {
        return UI_EVENT_IGNORED;
    }
    if (event->type == INPUT_EVENT_NONE) return UI_EVENT_IGNORED;

    if (event->type == INPUT_EVENT_KEY_DOWN) {
        if (event->key == INPUT_KEY_ESCAPE || event->key == INPUT_KEY_ENTER) {
            actions->resume = true;
            return UI_EVENT_HANDLED;
        }
        if (event->key == INPUT_KEY_MINUS) {
            AdjustVolume(&settings->masterVolume, -0.10f, actions);
            return UI_EVENT_HANDLED;
        }
        if (event->key == INPUT_KEY_EQUAL) {
            AdjustVolume(&settings->masterVolume, 0.10f, actions);
            return UI_EVENT_HANDLED;
        }
        return UI_EVENT_CONSUMED;
    }

    if (event->type != INPUT_EVENT_BUTTON_DOWN ||
        event->button != INPUT_BUTTON_LEFT) {
        return UI_EVENT_CONSUMED;
    }

    if (layout->tabsVisible) {
        for (int index = 0; index < PAUSE_MENU_PAGE_COUNT; index++) {
            if (PressedIn(event, layout->tabs[index])) {
                state->page = (PauseMenuPage)index;
                return UI_EVENT_HANDLED;
            }
        }
    }

    if (layout->qualityVisible) {
        for (int quality = 0; quality < GRAPHICS_QUALITY_COUNT; quality++) {
            if (PressedIn(event, layout->quality[quality])) {
                if (settings->graphicsQuality != (GraphicsQuality)quality) {
                    settings->graphicsQuality = (GraphicsQuality)quality;
                    actions->settingsChanged = true;
                    actions->qualityChanged = true;
                }
                return UI_EVENT_HANDLED;
            }
        }
    }

    if (layout->workerVisible && !settings->chunkWorkerCountLocked) {
        if (PressedIn(event, layout->workerMinus)) {
            settings->chunkWorkerCount = settings->chunkWorkerCount > 0
                ? settings->chunkWorkerCount - 1
                : settings->maxChunkWorkerCount;
            actions->settingsChanged = true;
            actions->workersChanged = true;
            return UI_EVENT_HANDLED;
        }
        if (PressedIn(event, layout->workerPlus)) {
            settings->chunkWorkerCount = settings->chunkWorkerCount <
                settings->maxChunkWorkerCount
                ? settings->chunkWorkerCount + 1 : 0;
            actions->settingsChanged = true;
            actions->workersChanged = true;
            return UI_EVENT_HANDLED;
        }
    }

    if (layout->volumeVisible) {
        float *volumes[3] = {
            &settings->masterVolume,
            &settings->ambientVolume,
            &settings->musicVolume
        };
        for (int index = 0; index < 3; index++) {
            if (PressedIn(event, layout->volumeMinus[index])) {
                AdjustVolume(volumes[index], -0.10f, actions);
                return UI_EVENT_HANDLED;
            }
            if (PressedIn(event, layout->volumePlus[index])) {
                AdjustVolume(volumes[index], 0.10f, actions);
                return UI_EVENT_HANDLED;
            }
        }
    }

    if (layout->musicVisible && PressedIn(event, layout->music)) {
        settings->musicEnabled = !settings->musicEnabled;
        actions->settingsChanged = true;
        return UI_EVENT_HANDLED;
    }
    if (layout->weatherDamageVisible &&
        PressedIn(event, layout->weatherDamage)) {
        settings->weatherDamageEnabled = !settings->weatherDamageEnabled;
        actions->settingsChanged = true;
        return UI_EVENT_HANDLED;
    }
    if (layout->geologyVisible && PressedIn(event, layout->geology)) {
        settings->geologyEnabled = !settings->geologyEnabled;
        actions->settingsChanged = true;
        return UI_EVENT_HANDLED;
    }

    if (layout->actionsVisible) {
        if (PressedIn(event, layout->resume)) {
            actions->resume = true;
            return UI_EVENT_HANDLED;
        }
        if (PressedIn(event, layout->saveWorld)) {
            actions->saveWorld = true;
            return UI_EVENT_HANDLED;
        }
        if (PressedIn(event, layout->returnToMenu)) {
            actions->returnToMenu = true;
            return UI_EVENT_HANDLED;
        }
        if (PressedIn(event, layout->saveAndQuit)) {
            actions->saveAndQuit = true;
            return UI_EVENT_HANDLED;
        }
    }

    return UI_EVENT_CONSUMED;
}
