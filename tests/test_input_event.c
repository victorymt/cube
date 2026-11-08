/* Input event contract: immutable frame order, shared consumption state, edge
 * queries, wheel aggregation, held-key snapshots, and stable DSL names. This
 * test only covers the neutral core queue; Raylib capture is exercised by
 * test_input_source. It guards the shared event foundation introduced to stop
 * UI draw code and update code from polling input independently. */
#include "core/input_event.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void TestOrderAndConsumption(void)
{
    InputFrame frame;
    InputEventState state;
    InputFrameReset(&frame, 42u);
    InputEventStateReset(&state);

    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_POINTER_MOVE, .point = { 10.0f, 20.0f }
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_DOWN, .button = INPUT_BUTTON_LEFT,
        .point = { 10.0f, 20.0f }
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_UP, .button = INPUT_BUTTON_LEFT,
        .point = { 10.0f, 20.0f }
    }));

    assert(frame.number == 42u);
    assert(frame.eventCount == 3u);
    assert(frame.events[0].index == 0u);
    assert(frame.events[1].index == 1u);
    assert(frame.events[2].index == 2u);
    assert(InputEventConsume(&state, &frame, &frame.events[1]));
    assert(!InputEventConsume(&state, &frame, &frame.events[1]));
    assert(InputEventIsConsumed(&state, &frame, &frame.events[1]));
    assert(!InputEventIsConsumed(&state, &frame, &frame.events[0]));

    size_t cursor = 0u;
    const InputEvent *event = InputFrameNextEvent(&frame, &state, &cursor);
    assert(event == &frame.events[0]);
    event = InputFrameNextEvent(&frame, &state, &cursor);
    assert(event == &frame.events[2]);
    assert(InputFrameNextEvent(&frame, &state, &cursor) == NULL);

    cursor = 0u;
    assert(InputEventConsume(&state, &frame, &frame.events[0]));
    event = InputFrameNextEvent(&frame, &state, &cursor);
    assert(event == &frame.events[2]);
    assert(InputEventConsume(&state, &frame, &frame.events[2]));
    assert(InputFrameNextEvent(&frame, &state, &cursor) == NULL);
}

static void TestQuerySemantics(void)
{
    InputFrame frame;
    InputEventState state;
    InputFrameReset(&frame, 7u);
    InputEventStateReset(&state);
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_DOWN, .button = INPUT_BUTTON_LEFT
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_UP, .button = INPUT_BUTTON_RIGHT
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_KEY_DOWN, .key = INPUT_KEY_W
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_KEY_UP, .key = INPUT_KEY_ESCAPE
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_WHEEL, .wheel = 1.25f
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_WHEEL, .wheel = -0.25f
    }));

    assert(InputFrameButtonPressed(&frame, &state, INPUT_BUTTON_LEFT, true));
    assert(!InputFrameButtonPressed(&frame, &state, INPUT_BUTTON_MIDDLE, true));
    assert(InputFrameButtonReleased(&frame, &state, INPUT_BUTTON_RIGHT, true));
    assert(InputFrameKeyPressed(&frame, &state, INPUT_KEY_W, true));
    assert(InputFrameKeyReleased(&frame, &state, INPUT_KEY_ESCAPE, true));
    assert(InputFrameWheel(&frame, &state, true) == 1.0f);

    assert(InputEventConsume(&state, &frame, &frame.events[0]));
    assert(InputFrameButtonPressed(&frame, &state, INPUT_BUTTON_LEFT, true) == false);
    assert(InputFrameButtonPressed(&frame, &state, INPUT_BUTTON_LEFT, false));
    assert(InputEventConsume(&state, &frame, &frame.events[4]));
    assert(InputFrameWheel(&frame, &state, true) == -0.25f);
    assert(InputFrameWheel(&frame, &state, false) == 1.0f);

    frame.keysDown[INPUT_KEY_W] = true;
    assert(InputFrameKeyDown(&frame, INPUT_KEY_W));
    assert(!InputFrameKeyDown(&frame, INPUT_KEY_A));
    assert(!InputFrameKeyDown(&frame, INPUT_KEY_UNKNOWN));
}

static void TestNamesAndReset(void)
{
    InputButton button = INPUT_BUTTON_MIDDLE;
    InputKey key = INPUT_KEY_UNKNOWN;
    assert(InputButtonFromName("LEFT", &button));
    assert(button == INPUT_BUTTON_LEFT);
    assert(strcmp(InputButtonName(button), "left") == 0);
    assert(!InputButtonFromName("primary", &button));
    assert(strcmp(InputButtonName(INPUT_BUTTON_COUNT), "unknown") == 0);

    assert(InputKeyFromName("w", &key));
    assert(key == INPUT_KEY_W);
    assert(InputKeyFromName("ESCAPE", &key));
    assert(key == INPUT_KEY_ESCAPE);
    assert(InputKeyFromName("left_super", &key));
    assert(key == INPUT_KEY_LEFT_SUPER);
    assert(strcmp(InputKeyName(INPUT_KEY_PAGE_DOWN), "page_down") == 0);
    assert(!InputKeyFromName("never", &key));
    assert(strcmp(InputKeyName(INPUT_KEY_UNKNOWN), "unknown") == 0);

    InputFrame frame;
    InputFrameReset(&frame, 99u);
    assert(frame.number == 99u);
    assert(frame.eventCount == 0u);
    assert(!InputFramePush(&frame, &(InputEvent){ .type = INPUT_EVENT_NONE }));
    assert(!InputFramePush(NULL, &(InputEvent){ .type = INPUT_EVENT_WHEEL }));
    assert(!InputEventConsume(NULL, &frame, &(InputEvent){ .type = INPUT_EVENT_WHEEL }));
    assert(InputEventIsConsumed(NULL, &frame, &frame.events[0]));
}

int main(void)
{
    TestOrderAndConsumption();
    TestQuerySemantics();
    TestNamesAndReset();
    puts("input events: order, consumption, queries, snapshots and names passed");
    return 0;
}
