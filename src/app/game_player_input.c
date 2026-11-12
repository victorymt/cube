#include "app/game_player_input.h"

#include "app/game_runtime.h"

PlayerInput GamePlayerInputFromFrame(const InputFrame *frame)
{
    if (!frame) return (PlayerInput){ 0 };

    Vector2 lookDelta = { frame->pointerDelta.x, frame->pointerDelta.y };
    if (InputFrameKeyDown(frame, INPUT_KEY_RIGHT))
        lookDelta.x += KEYBOARD_LOOK_SPEED;
    if (InputFrameKeyDown(frame, INPUT_KEY_LEFT))
        lookDelta.x -= KEYBOARD_LOOK_SPEED;
    if (InputFrameKeyDown(frame, INPUT_KEY_DOWN))
        lookDelta.y += KEYBOARD_LOOK_SPEED;
    if (InputFrameKeyDown(frame, INPUT_KEY_UP))
        lookDelta.y -= KEYBOARD_LOOK_SPEED;

    return (PlayerInput){
        .forward = (InputFrameKeyDown(frame, INPUT_KEY_W) ? 1.0f : 0.0f) -
                   (InputFrameKeyDown(frame, INPUT_KEY_S) ? 1.0f : 0.0f),
        .strafe = (InputFrameKeyDown(frame, INPUT_KEY_D) ? 1.0f : 0.0f) -
                  (InputFrameKeyDown(frame, INPUT_KEY_A) ? 1.0f : 0.0f),
        .vertical = (InputFrameKeyDown(frame, INPUT_KEY_SPACE) ? 1.0f : 0.0f) -
                    (InputFrameKeyDown(frame, INPUT_KEY_LEFT_CONTROL) ? 1.0f : 0.0f),
        .sprint = InputFrameKeyDown(frame, INPUT_KEY_LEFT_SHIFT),
        .jumpPressed = InputFrameKeyPressed(
            frame, NULL, INPUT_KEY_SPACE, false),
        .toggleFloating = InputFrameKeyPressed(
            frame, NULL, INPUT_KEY_F, false),
        .lookDelta = lookDelta
    };
}

PlayerInput GameReadPlayerInput(GameRuntime *game)
{
    if (!game) return (PlayerInput){ 0 };
    if (game->scriptedInputFrames > 0u) {
        PlayerInput input = game->scriptedPlayerInput;
        input.jumpPressed = game->scriptedInputFirstFrame && input.vertical > 0;
        game->scriptedInputFirstFrame = false;
        game->scriptedInputFrames--;
        return input;
    }
    if (game->debugControlEnabled && !game->manualInputEnabled)
        return (PlayerInput){0};
    return GamePlayerInputFromFrame(InputSourceFrame(&game->inputSource));
}
