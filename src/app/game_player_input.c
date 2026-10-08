#include "app/game_player_input.h"
#include "app/game_runtime.h"

PlayerInput GameReadPlayerInput(GameRuntime *game)
{
    if (game->scriptedInputFrames > 0u) {
        PlayerInput input = game->scriptedPlayerInput;
        input.jumpPressed = game->scriptedInputFirstFrame && input.vertical > 0;
        game->scriptedInputFirstFrame = false;
        game->scriptedInputFrames--;
        return input;
    }
    if (game->debugControlEnabled && !game->manualInputEnabled)
        return (PlayerInput){0};
    return PlayerInputFromKeyboard();
}
