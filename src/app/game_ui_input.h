#ifndef VOXELCRAFT_GAME_UI_INPUT_H
#define VOXELCRAFT_GAME_UI_INPUT_H

#include "presentation/pause_menu_ui.h"

#include <stdbool.h>

typedef struct GameRuntime GameRuntime;

typedef struct GameUiInputResult {
    bool startGame;
    bool pauseMenuSettingsValid;
    PauseMenuSettings pauseMenuSettings;
    PauseMenuActions pauseMenuActions;
} GameUiInputResult;

/* Routes one captured InputFrame through the highest-priority active UI layer.
 * There must be exactly one call per frame; startGame reports the start-page
 * action and pause-menu state is returned for application in the update path. */
bool GameUiInputDispatch(GameRuntime *game, bool suppressPauseOpen,
                         GameUiInputResult *result);

#endif
