#include "app/game_ui_input.h"

#include "app/game_runtime.h"

#include "raylib.h"

typedef struct GameUiRouteContext {
    GameRuntime *game;
    bool startGame;
} GameUiRouteContext;

static UiEventResult GameUiRouteStartPage(void *context,
                                          const InputFrame *frame,
                                          const InputEvent *event)
{
    GameUiRouteContext *route = context;
    if (!route || !route->game) return UI_EVENT_IGNORED;
    GameRuntime *game = route->game;
    StartPageUiLayout layout = StartPageUiLayoutFor(GetScreenWidth(),
                                                    GetScreenHeight());
    bool quitGame = false;
    UiEventResult result = StartPageUiHandleEvent(
        &game->startPageUi, &route->startGame, &quitGame,
        &game->selectedTerrain, &game->selectedSeed, &layout, frame, event);
    if (quitGame) game->quitRequested = true;
    return result;
}

bool GameUiInputDispatch(GameRuntime *game)
{
    if (!game || game->screen != SCREEN_START) return false;
    GameUiRouteContext route = { .game = game };
    UiEventDispatchFrame(InputSourceFrame(&game->inputSource),
                         &game->inputEvents, GameUiRouteStartPage, &route);
    return route.startGame;
}
