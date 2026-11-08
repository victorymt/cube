/* Debug DSL screen gate contract: UI click and key commands are valid on both
 * the start and playing screens, while gameplay-only commands remain gated.
 * This covers gate decisions only; parsing and event routing belong to
 * test_debug_control, UI event tests, and end-to-end DSL probes. It prevents
 * the start-button probe from being rejected by an inconsistent gate.
 * Source: docs/debug-dsl.md and the start-screen UI click bug. */
#include "app/game_debug_gate.h"
#include "app/game_runtime.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool WorldIsSurfaceActive(void) { return false; }

bool ShipIsDriving(void) { return false; }

static void AssertUiCommandsAllowed(GameRuntime *game)
{
    assert(GameDebugDslCommandBlocked(
               game, DEBUG_CONTROL_COMMAND_UI_CLICK) == NULL);
    assert(GameDebugDslCommandBlocked(
               game, DEBUG_CONTROL_COMMAND_UI_KEY) == NULL);
}

int main(void)
{
    GameRuntime game = { .screen = SCREEN_START };
    AssertUiCommandsAllowed(&game);
    assert(strcmp(GameDebugDslCommandBlocked(
                      &game, DEBUG_CONTROL_COMMAND_PAUSE),
                  "not_playing") == 0);

    game.screen = SCREEN_PLAYING;
    AssertUiCommandsAllowed(&game);
    assert(GameDebugDslCommandBlocked(
               &game, DEBUG_CONTROL_COMMAND_PAUSE) == NULL);

    puts("game debug gate tests passed");
    return 0;
}
