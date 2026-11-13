#include "app/game_player_input.h"
#include "app/game_runtime.h"
#include "core/input_event.h"
#include "gameplay/player_input.h"

#include <assert.h>
#include <stdio.h>

static void SetKeyDown(InputFrame *frame, InputKey key)
{
    assert(frame && key > INPUT_KEY_UNKNOWN && key < INPUT_KEY_COUNT);
    frame->keysDown[key] = true;
    assert(InputFramePush(frame, &(InputEvent){
        .type = INPUT_EVENT_KEY_DOWN,
        .key = key
    }));
}

static void SetPhysicalFrame(GameRuntime *game)
{
    InputFrame *frame = &game->inputSource.frame;
    InputFrameReset(frame, 7u);
    frame->pointerDelta = (InputPoint){ 12.0f, -3.0f };
    SetKeyDown(frame, INPUT_KEY_W);
    SetKeyDown(frame, INPUT_KEY_D);
    SetKeyDown(frame, INPUT_KEY_SPACE);
    SetKeyDown(frame, INPUT_KEY_LEFT_SHIFT);
    SetKeyDown(frame, INPUT_KEY_F);
}

static void TestFixedStepInputStaging(void)
{
    PlayerInput staged = { 0 };
    bool edgesPending = false;
    GamePlayerInputStageFixed(&staged, &edgesPending, &(PlayerInput){
        .forward = 1.0f,
        .lookDelta = { 2.0f, -1.0f },
        .jumpPressed = true,
        .toggleFloating = true
    });
    GamePlayerInputStageFixed(&staged, &edgesPending, &(PlayerInput){
        .forward = 0.5f,
        .strafe = -1.0f,
        .sprint = true,
        .lookDelta = { 1.0f, 3.0f }
    });

    PlayerInput firstTick = GamePlayerInputConsumeFixed(&staged, &edgesPending);
    assert(firstTick.forward == 0.5f && firstTick.strafe == -1.0f);
    assert(firstTick.sprint && firstTick.jumpPressed && firstTick.toggleFloating);
    assert(firstTick.lookDelta.x == 3.0f && firstTick.lookDelta.y == 2.0f);
    assert(!edgesPending);

    PlayerInput secondTick = GamePlayerInputConsumeFixed(&staged, &edgesPending);
    assert(secondTick.forward == 0.5f && secondTick.strafe == -1.0f);
    assert(!secondTick.jumpPressed && !secondTick.toggleFloating);
    assert(secondTick.lookDelta.x == 0.0f && secondTick.lookDelta.y == 0.0f);
}

int main(void)
{
    TestFixedStepInputStaging();
    static GameRuntime game;
    SetPhysicalFrame(&game);

    PlayerInput mapped = GamePlayerInputFromFrame(&game.inputSource.frame);
    assert(mapped.forward == 1.0f && mapped.strafe == 1.0f);
    assert(mapped.vertical == 1.0f && mapped.sprint);
    assert(mapped.jumpPressed && mapped.toggleFloating);
    assert(mapped.lookDelta.x == 12.0f && mapped.lookDelta.y == -3.0f);

    game.debugControlEnabled = true;
    /* Reproduce EOF lockout: executor and scripted frames are both absent,
       but a debug session must not consume the captured physical input without
       an explicit manual handoff. */
    PlayerInput input = GameReadPlayerInput(&game);
    assert(!game.debugDslExecutor && !game.scriptedInputFrames);
    assert(input.forward == 0 && input.strafe == 0 && !input.toggleFloating);

    game.manualInputEnabled = true;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 1.0f && input.strafe == 1.0f);
    assert(input.vertical == 1.0f && input.sprint);
    assert(input.jumpPressed && input.toggleFloating);

    game.scriptedInputFrames = 2;
    game.scriptedInputFirstFrame = true;
    game.scriptedPlayerInput = (PlayerInput){ .strafe = -1.0f, .vertical = 1.0f };
    input = GameReadPlayerInput(&game);
    assert(input.strafe == -1.0f && input.jumpPressed);
    input = GameReadPlayerInput(&game);
    assert(input.strafe == -1.0f && !input.jumpPressed);
    assert(game.scriptedInputFrames == 0u);

    input = GameReadPlayerInput(&game);
    assert(input.forward == 1.0f && input.strafe == 1.0f);

    game.manualInputEnabled = false;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 0.0f && !input.jumpPressed);

    game.debugControlEnabled = false;
    input = GameReadPlayerInput(&game);
    assert(input.forward == 1.0f && input.jumpPressed && input.toggleFloating);

    InputFrame *frame = &game.inputSource.frame;
    InputFrameReset(frame, 8u);
    frame->pointerDelta = (InputPoint){ 0.0f, 0.0f };
    SetKeyDown(frame, INPUT_KEY_RIGHT);
    SetKeyDown(frame, INPUT_KEY_DOWN);
    input = GamePlayerInputFromFrame(frame);
    assert(input.lookDelta.x == KEYBOARD_LOOK_SPEED);
    assert(input.lookDelta.y == KEYBOARD_LOOK_SPEED);

    input = GamePlayerInputFromFrame(NULL);
    assert(input.forward == 0.0f && input.strafe == 0.0f && input.vertical == 0.0f);
    puts("player input: app frame mapping, debug handoff and script priority passed");
}
