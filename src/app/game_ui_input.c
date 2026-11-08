#include "app/game_ui_input.h"

#include "app/game_runtime.h"

#include "gameplay/interaction.h"
#include "presentation/homeworld_map.h"
#include "presentation/starmap.h"
#include "raylib.h"
#include "raymath.h"

typedef struct GameUiRouteContext {
    GameRuntime *game;
    GameUiInputResult *result;
    bool suppressPauseOpen;
} GameUiRouteContext;

static PauseMenuSettings PauseMenuSettingsSnapshot(const GameRuntime *game)
{
    return (PauseMenuSettings){
        .graphicsQuality = game->settings.graphicsQuality,
        .chunkWorkerCount = game->chunkWorkerCountOverridden
            ? game->chunkWorkerCount
            : game->settings.chunkWorkerCount,
        .activeChunkWorkerCount = ChunksStartedWorkerCount(),
        .maxChunkWorkerCount = MAX_CHUNK_WORKER_THREADS,
        .chunkWorkerCountLocked = game->chunkWorkerCountOverridden,
        .masterVolume = game->settings.masterVolume,
        .ambientVolume = game->settings.ambientVolume,
        .musicVolume = game->settings.musicVolume,
        .musicEnabled = game->settings.musicEnabled,
        .weatherDamageEnabled = game->settings.weatherDamageEnabled,
        .geologyEnabled = game->settings.geologyEnabled
    };
}

static bool GameCanOpenPauseMenu(const GameRuntime *game,
                                 bool suppressPauseOpen)
{
    return !game->paused && !game->inventoryOpen && !game->importDialog.open &&
           !game->albumOpen && !StarMapIsOpen() && !HomeWorldMapIsOpen() &&
           !game->biologyAtlasOpen && !game->landingTransition.active &&
           !suppressPauseOpen;
}

static bool GameCanOpenImportDialog(const GameRuntime *game)
{
    return !game->paused && !game->inventoryOpen &&
           !game->importDialog.open && !game->albumOpen &&
           !StarMapIsOpen() && !HomeWorldMapIsOpen() &&
           !game->biologyAtlasOpen && !game->landingTransition.active &&
           !game->ecologyCatchupPending;
}

static UiEventResult GameUiRouteStartPage(void *context,
                                          const InputFrame *frame,
                                          const InputEvent *event)
{
    GameUiRouteContext *route = context;
    if (!route || !route->game || !route->result) return UI_EVENT_IGNORED;
    GameRuntime *game = route->game;
    StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                    GetScreenHeight());
    bool quitGame = false;
    UiEventResult handled = StartPageUiHandleEvent(
        &game->startPageUi, &route->result->startGame, &quitGame,
        &game->selectedTerrain, &game->selectedSeed, &layout, frame, event);
    if (quitGame) game->quitRequested = true;
    return handled;
}

static UiEventResult GameUiRoutePlaying(void *context,
                                        const InputFrame *frame,
                                        const InputEvent *event)
{
    GameUiRouteContext *route = context;
    if (!route || !route->game || !route->result) return UI_EVENT_IGNORED;
    GameRuntime *game = route->game;

    if (game->importDialog.open) {
        ImportDialogUiLayout layout = ImportDialogUiLayoutFor(
            GetScreenWidth(), GetScreenHeight());
        return ImportDialogUiHandleEvent(
            &game->importDialog, &route->result->importDialogActions,
            &layout, frame, event, GetClipboardText());
    }

    if (game->paused) {
        PauseMenuUiLayout layout = PauseMenuUiLayoutFor(
            GetScreenWidth(), GetScreenHeight(), game->pauseMenuUi.page);
        route->result->pauseMenuSettingsValid = true;
        return PauseMenuUiHandleEvent(
            &game->pauseMenuUi, &route->result->pauseMenuSettings,
            &route->result->pauseMenuActions, &layout, frame, event);
    }

    if (event->type == INPUT_EVENT_KEY_DOWN &&
        event->key == INPUT_KEY_ESCAPE &&
        GameCanOpenPauseMenu(game, route->suppressPauseOpen)) {
        game->paused = true;
        game->player.velocity = Vector3Zero();
        game->cursorReleased = false;
        EnableCursor();
        route->result->pauseMenuSettings = PauseMenuSettingsSnapshot(game);
        route->result->pauseMenuSettingsValid = true;
        return UI_EVENT_HANDLED;
    }

    if (event->type == INPUT_EVENT_FILE_DROP) {
        if (GameCanOpenImportDialog(game)) {
            ImportDialogUiRequestFileImport(
                &route->result->importDialogActions, event->filePath);
        }
        return UI_EVENT_CONSUMED;
    }

    if (event->type == INPUT_EVENT_KEY_DOWN &&
        event->key == INPUT_KEY_I && GameCanOpenImportDialog(game)) {
        OpenImportDialog(&game->importDialog);
        if (game->importDialog.open) {
            game->cursorReleased = true;
            game->player.velocity = Vector3Zero();
            EnableCursor();
        }
        return UI_EVENT_HANDLED;
    }

    return UI_EVENT_IGNORED;
}

static UiEventResult GameUiRouteFrame(void *context, const InputFrame *frame,
                                      const InputEvent *event)
{
    GameUiRouteContext *route = context;
    if (!route || !route->game) return UI_EVENT_IGNORED;
    if (route->game->screen == SCREEN_START) {
        return GameUiRouteStartPage(context, frame, event);
    }
    if (route->game->screen == SCREEN_PLAYING) {
        return GameUiRoutePlaying(context, frame, event);
    }
    return UI_EVENT_IGNORED;
}

bool GameUiInputDispatch(GameRuntime *game, bool suppressPauseOpen,
                         GameUiInputResult *result)
{
    GameUiInputResult localResult = { 0 };
    if (!result) result = &localResult;
    if (!game) return false;
    GameUiRouteContext route = {
        .game = game,
        .result = result,
        .suppressPauseOpen = suppressPauseOpen
    };
    UiEventDispatchFrame(InputSourceFrame(&game->inputSource),
                         &game->inputEvents, GameUiRouteFrame, &route);
    return result->startGame;
}
