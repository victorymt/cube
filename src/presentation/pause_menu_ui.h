#ifndef VOXELCRAFT_PAUSE_MENU_UI_H
#define VOXELCRAFT_PAUSE_MENU_UI_H

#include "presentation/render_quality.h"
#include "presentation/ui_event.h"

#include <stdbool.h>

typedef enum PauseMenuPage {
    PAUSE_MENU_PAGE_SYSTEM = 0,
    PAUSE_MENU_PAGE_AUDIO,
    PAUSE_MENU_PAGE_GAME,
    PAUSE_MENU_PAGE_COUNT
} PauseMenuPage;

typedef enum PauseMenuLayoutMode {
    PAUSE_MENU_LAYOUT_TINY = 0,
    PAUSE_MENU_LAYOUT_COMPACT,
    PAUSE_MENU_LAYOUT_FULL
} PauseMenuLayoutMode;

typedef struct PauseMenuUiState {
    PauseMenuPage page;
} PauseMenuUiState;

typedef struct PauseMenuActions {
    bool resume;
    bool saveWorld;
    bool saveAndQuit;
    bool returnToMenu;
    bool settingsChanged;
    bool qualityChanged;
    bool workersChanged;
} PauseMenuActions;

typedef struct PauseMenuSettings {
    GraphicsQuality graphicsQuality;
    int chunkWorkerCount;
    int activeChunkWorkerCount;
    int maxChunkWorkerCount;
    bool chunkWorkerCountLocked;
    float masterVolume;
    float ambientVolume;
    float musicVolume;
    bool musicEnabled;
    bool weatherDamageEnabled;
    bool geologyEnabled;
} PauseMenuSettings;

typedef struct PauseMenuUiLayout {
    PauseMenuLayoutMode mode;
    PauseMenuPage page;
    int screenWidth;
    int screenHeight;
    Rectangle panel;
    Rectangle tabs[PAUSE_MENU_PAGE_COUNT];
    Rectangle quality[GRAPHICS_QUALITY_COUNT];
    Rectangle workerRow;
    Rectangle workerMinus;
    Rectangle workerPlus;
    Rectangle geology;
    Rectangle volumeRows[3];
    Rectangle volumeMinus[3];
    Rectangle volumePlus[3];
    Rectangle music;
    Rectangle weatherDamage;
    Rectangle resume;
    Rectangle saveWorld;
    Rectangle returnToMenu;
    Rectangle saveAndQuit;
    bool tabsVisible;
    bool qualityVisible;
    bool workerVisible;
    bool geologyVisible;
    bool volumeVisible;
    bool musicVisible;
    bool weatherDamageVisible;
    bool actionsVisible;
} PauseMenuUiLayout;

void PauseMenuUiReset(PauseMenuUiState *state);
PauseMenuUiLayout PauseMenuUiLayoutFor(int screenWidth, int screenHeight,
                                       PauseMenuPage page);

/* Modal pause-menu routing. Every event is consumed while the menu is open;
 * only BUTTON_DOWN events mutate controls, so a full click never fires twice. */
UiEventResult PauseMenuUiHandleEvent(PauseMenuUiState *state,
                                     PauseMenuSettings *settings,
                                     PauseMenuActions *actions,
                                     const PauseMenuUiLayout *layout,
                                     const InputFrame *frame,
                                     const InputEvent *event);

#endif
