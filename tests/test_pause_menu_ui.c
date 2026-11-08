/* Verifies pause-menu layout ownership, modal consumption, and one-down event
 * intent. Scope covers tabs, settings, actions, and keyboard shortcuts across
 * responsive modes. It does not cover drawing or applying app side effects;
 * source: unified UI event refactor and click-offset bug plan. */
#include "presentation/pause_menu_ui.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static InputPoint Center(Rectangle rect)
{
    return (InputPoint){ rect.x + rect.width * 0.5f,
                         rect.y + rect.height * 0.5f };
}

static InputEvent PressEvent(InputPoint point, InputEventType type)
{
    return (InputEvent){
        .type = type,
        .button = INPUT_BUTTON_LEFT,
        .point = point
    };
}

static PauseMenuSettings DefaultSettings(void)
{
    return (PauseMenuSettings){
        .graphicsQuality = GRAPHICS_QUALITY_MEDIUM,
        .chunkWorkerCount = 0,
        .activeChunkWorkerCount = 4,
        .maxChunkWorkerCount = 8,
        .masterVolume = 0.5f,
        .ambientVolume = 0.5f,
        .musicVolume = 0.5f,
        .musicEnabled = true,
        .weatherDamageEnabled = true,
        .geologyEnabled = true
    };
}

static void TestResponsiveLayouts(void)
{
    PauseMenuUiLayout tiny = PauseMenuUiLayoutFor(
        500, 400, PAUSE_MENU_PAGE_SYSTEM);
    assert(tiny.mode == PAUSE_MENU_LAYOUT_TINY);
    assert(tiny.tabsVisible && tiny.qualityVisible && tiny.workerVisible);
    assert(!tiny.volumeVisible && !tiny.actionsVisible);
    assert(tiny.panel.x >= 0.0f && tiny.panel.y >= 0.0f);
    assert(tiny.panel.x + tiny.panel.width <= 500.0f);

    PauseMenuUiLayout compact = PauseMenuUiLayoutFor(
        700, 600, PAUSE_MENU_PAGE_AUDIO);
    assert(compact.mode == PAUSE_MENU_LAYOUT_COMPACT);
    assert(!compact.tabsVisible);
    assert(compact.qualityVisible && compact.workerVisible &&
           compact.volumeVisible && compact.actionsVisible);

    PauseMenuUiLayout full = PauseMenuUiLayoutFor(
        1000, 800, PAUSE_MENU_PAGE_GAME);
    assert(full.mode == PAUSE_MENU_LAYOUT_FULL);
    assert(full.panel.width == 540.0f);
    assert(full.actionsVisible && full.geologyVisible &&
           full.weatherDamageVisible);
}

static void TestButtonDownAndModalConsumption(void)
{
    InputFrame frame = { 0 };
    PauseMenuUiState state = { 0 };
    PauseMenuSettings settings = DefaultSettings();
    PauseMenuActions actions = { 0 };
    PauseMenuUiLayout layout = PauseMenuUiLayoutFor(
        1000, 800, PAUSE_MENU_PAGE_SYSTEM);
    InputEvent down = PressEvent(Center(layout.quality[GRAPHICS_QUALITY_HIGH]),
                                 INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &down) == UI_EVENT_HANDLED);
    assert(settings.graphicsQuality == GRAPHICS_QUALITY_HIGH);
    assert(actions.settingsChanged && actions.qualityChanged);

    actions = (PauseMenuActions){ 0 };
    InputEvent up = PressEvent(Center(layout.quality[GRAPHICS_QUALITY_LOW]),
                               INPUT_EVENT_BUTTON_UP);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &up) == UI_EVENT_CONSUMED);
    assert(settings.graphicsQuality == GRAPHICS_QUALITY_HIGH);
    assert(!actions.settingsChanged);

    InputEvent miss = PressEvent((InputPoint){ 0.0f, 0.0f },
                                 INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &miss) == UI_EVENT_CONSUMED);
}

static void TestTinyTabsAndControls(void)
{
    InputFrame frame = { 0 };
    PauseMenuUiState state = { 0 };
    PauseMenuSettings settings = DefaultSettings();
    PauseMenuActions actions = { 0 };
    PauseMenuUiLayout system = PauseMenuUiLayoutFor(
        500, 400, PAUSE_MENU_PAGE_SYSTEM);
    InputEvent audioTab = PressEvent(Center(system.tabs[PAUSE_MENU_PAGE_AUDIO]),
                                     INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &system,
                                  &frame, &audioTab) == UI_EVENT_HANDLED);
    assert(state.page == PAUSE_MENU_PAGE_AUDIO);

    PauseMenuUiLayout audio = PauseMenuUiLayoutFor(500, 400, state.page);
    InputEvent volumeUp = PressEvent(Center(audio.volumePlus[0]),
                                     INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &audio,
                                  &frame, &volumeUp) == UI_EVENT_HANDLED);
    assert(fabsf(settings.masterVolume - 0.6f) < 0.0001f);
    assert(actions.settingsChanged);
}

static void TestSettingsAndActions(void)
{
    InputFrame frame = { 0 };
    PauseMenuUiState state = { 0 };
    PauseMenuSettings settings = DefaultSettings();
    PauseMenuActions actions = { 0 };
    PauseMenuUiLayout layout = PauseMenuUiLayoutFor(
        1000, 800, PAUSE_MENU_PAGE_SYSTEM);

    InputEvent workerPlus = PressEvent(Center(layout.workerPlus),
                                       INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &workerPlus) == UI_EVENT_HANDLED);
    assert(settings.chunkWorkerCount == 1);
    assert(actions.workersChanged);

    InputEvent music = PressEvent(Center(layout.music),
                                  INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &music) == UI_EVENT_HANDLED);
    assert(!settings.musicEnabled);

    InputEvent resume = PressEvent(Center(layout.resume),
                                   INPUT_EVENT_BUTTON_DOWN);
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &resume) == UI_EVENT_HANDLED);
    assert(actions.resume);

    InputEvent escape = { .type = INPUT_EVENT_KEY_DOWN,
                          .key = INPUT_KEY_ESCAPE };
    actions = (PauseMenuActions){ 0 };
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &escape) == UI_EVENT_HANDLED);
    assert(actions.resume);

    InputEvent minus = { .type = INPUT_EVENT_KEY_DOWN,
                         .key = INPUT_KEY_MINUS };
    actions = (PauseMenuActions){ 0 };
    settings.masterVolume = 0.05f;
    assert(PauseMenuUiHandleEvent(&state, &settings, &actions, &layout,
                                  &frame, &minus) == UI_EVENT_HANDLED);
    assert(settings.masterVolume == 0.0f);
    assert(actions.settingsChanged);
}

int main(void)
{
    TestResponsiveLayouts();
    TestButtonDownAndModalConsumption();
    TestTinyTabsAndControls();
    TestSettingsAndActions();
    puts("pause menu UI events passed");
    return 0;
}
