#include "app/game_player_input.h"
#include "app/game_runtime.h"
#include <assert.h>
#include <stdio.h>

static unsigned keyboardReads;
PlayerInput PlayerInputFromKeyboard(void)
{
    keyboardReads++;
    return (PlayerInput){.forward = 1, .vertical = 1, .toggleFloating = true,
                         .lookDelta = {12, -3}};
}
int main(void)
{
    static GameRuntime game;
    game.debugControlEnabled = true;
    /* Reproduce EOF lockout: executor and scripted frames are both absent,
       but a debug session must not read physical keys without handoff. */
    PlayerInput input = GameReadPlayerInput(&game);
    assert(!game.debugDslExecutor && !game.scriptedInputFrames);
    assert(input.forward == 0 && !input.toggleFloating && keyboardReads == 0);
    game.manualInputEnabled = true;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 1 && input.vertical == 1 && input.toggleFloating);
    assert(input.lookDelta.x == 12 && input.lookDelta.y == -3 && keyboardReads == 1);
    game.scriptedInputFrames = 2; game.scriptedInputFirstFrame = true;
    game.scriptedPlayerInput = (PlayerInput){.strafe = -1, .vertical = 1};
    input = GameReadPlayerInput(&game);
    assert(input.strafe == -1 && input.jumpPressed && keyboardReads == 1);
    input = GameReadPlayerInput(&game);
    assert(input.strafe == -1 && !input.jumpPressed && game.scriptedInputFrames == 0);
    input = GameReadPlayerInput(&game);
    assert(input.forward == 1 && keyboardReads == 2);
    game.manualInputEnabled = false;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 0 && keyboardReads == 2);
    game.debugControlEnabled = false;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 1 && keyboardReads == 3);
    puts("player input: EOF lockout reproduced; manual handoff and script isolation passed");
}
