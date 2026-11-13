#include "app/game_player_input.h"

#include "app/game_runtime.h"

#include <math.h>

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

void GamePlayerInputStageFixed(PlayerInput *staged, bool *edgesPending,
                               const PlayerInput *frameInput)
{
    if (!staged || !edgesPending || !frameInput) return;
    staged->forward = frameInput->forward;
    staged->strafe = frameInput->strafe;
    staged->vertical = frameInput->vertical;
    staged->sprint = frameInput->sprint;
    staged->lookDelta.x += frameInput->lookDelta.x;
    staged->lookDelta.y += frameInput->lookDelta.y;
    staged->jumpPressed |= frameInput->jumpPressed;
    staged->toggleFloating |= frameInput->toggleFloating;
    *edgesPending = *edgesPending || frameInput->jumpPressed ||
        frameInput->toggleFloating || fabsf(frameInput->lookDelta.x) > 0.0f ||
        fabsf(frameInput->lookDelta.y) > 0.0f;
}

PlayerInput GamePlayerInputConsumeFixed(PlayerInput *staged,
                                        bool *edgesPending)
{
    if (!staged || !edgesPending) return (PlayerInput){ 0 };
    PlayerInput input = *staged;
    if (!*edgesPending) {
        input.lookDelta = (Vector2){ 0 };
        input.jumpPressed = false;
        input.toggleFloating = false;
    } else {
        staged->lookDelta = (Vector2){ 0 };
        staged->jumpPressed = false;
        staged->toggleFloating = false;
        *edgesPending = false;
    }
    return input;
}
