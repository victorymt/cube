/* UI event routing contract: shared hit testing, pointer snapshots, and
 * first-layer consumption over one immutable InputFrame. This covers the
 * presentation adapter only; Raylib capture and every concrete screen's hit
 * layout are exercised by test_input_source and UI tests. It exists so drawing
 * code cannot silently reintroduce a second click poll. */
#include "presentation/ui_event.h"

#include "core/input_event.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

typedef struct DispatchProbe {
    const InputFrame *expectedFrame;
    UiEventResult results[8];
    InputEvent seen[8];
    size_t seenCount;
} DispatchProbe;

static UiEventResult ProbeHandler(void *context, const InputFrame *frame,
                                  const InputEvent *event)
{
    DispatchProbe *probe = context;
    assert(probe);
    assert(frame == probe->expectedFrame);
    assert(event);
    assert(probe->seenCount < 8u);
    probe->seen[probe->seenCount] = *event;
    return probe->results[probe->seenCount++];
}

static void TestPointInRect(void)
{
    Rectangle rect = { 10.0f, 20.0f, 30.0f, 40.0f };
    assert(UiPointInRect((InputPoint){ 10.0f, 20.0f }, rect));
    assert(UiPointInRect((InputPoint){ 39.999f, 59.999f }, rect));
    assert(!UiPointInRect((InputPoint){ 40.0f, 30.0f }, rect));
    assert(!UiPointInRect((InputPoint){ 20.0f, 60.0f }, rect));
    assert(!UiPointInRect((InputPoint){ 9.999f, 30.0f }, rect));
    assert(!UiPointInRect((InputPoint){ 20.0f, 19.999f }, rect));
    assert(!UiPointInRect((InputPoint){ 20.0f, 30.0f },
                          (Rectangle){ 10.0f, 20.0f, -1.0f, 40.0f }));
}

static void TestPointerSnapshot(void)
{
    InputFrame frame;
    InputFrameReset(&frame, 12u);
    frame.pointer = (InputPoint){ 100.0f, 200.0f };
    frame.pointerDelta = (InputPoint){ 3.0f, -4.0f };
    frame.modifiers = INPUT_MODIFIER_SHIFT | INPUT_MODIFIER_CONTROL;
    frame.injected = true;
    frame.buttonsDown[INPUT_BUTTON_RIGHT] = true;
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_POINTER_MOVE,
        .point = frame.pointer,
        .delta = frame.pointerDelta
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_DOWN, .button = INPUT_BUTTON_LEFT
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_BUTTON_UP, .button = INPUT_BUTTON_RIGHT
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_WHEEL, .wheel = 1.25f
    }));
    assert(InputFramePush(&frame, &(InputEvent){
        .type = INPUT_EVENT_WHEEL, .wheel = -0.25f
    }));

    UiPointerSnapshot pointer = UiPointerSnapshotFromFrame(&frame);
    assert(pointer.position.x == 100.0f);
    assert(pointer.position.y == 200.0f);
    assert(pointer.delta.x == 3.0f);
    assert(pointer.delta.y == -4.0f);
    assert(pointer.modifiers == frame.modifiers);
    assert(pointer.injected);
    assert(UiPointerDown(&pointer, INPUT_BUTTON_RIGHT));
    assert(!UiPointerDown(&pointer, INPUT_BUTTON_LEFT));
    assert(UiPointerPressed(&pointer, INPUT_BUTTON_LEFT));
    assert(UiPointerReleased(&pointer, INPUT_BUTTON_RIGHT));
    assert(fabsf(pointer.wheel - 1.0f) < 0.0001f);
    assert(!UiPointerPressed(&pointer, INPUT_BUTTON_COUNT));
    assert(!UiPointerPressed(NULL, INPUT_BUTTON_LEFT));

    UiPointerSnapshot empty = UiPointerSnapshotFromFrame(NULL);
    assert(empty.position.x == 0.0f && empty.position.y == 0.0f);
}

static void TestDispatchConsumption(void)
{
    InputFrame frame;
    InputFrameReset(&frame, 33u);
    for (int index = 0; index < 4; index++) {
        assert(InputFramePush(&frame, &(InputEvent){
            .type = INPUT_EVENT_KEY_DOWN,
            .key = (InputKey)(INPUT_KEY_A + index)
        }));
    }
    DispatchProbe probe = {
        .expectedFrame = &frame,
        .results = {
            UI_EVENT_IGNORED,
            UI_EVENT_HANDLED,
            UI_EVENT_CONSUMED,
            UI_EVENT_IGNORED
        }
    };
    InputEventState state;
    state.consumed[0] = true;
    size_t dispatched = UiEventDispatchFrame(&frame, &state, ProbeHandler,
                                             &probe);
    assert(dispatched == 2u);
    assert(probe.seenCount == 4u);
    for (size_t index = 0; index < 4u; index++) {
        assert(probe.seen[index].index == index);
    }
    assert(!InputEventIsConsumed(&state, &frame, &frame.events[0]));
    assert(InputEventIsConsumed(&state, &frame, &frame.events[1]));
    assert(InputEventIsConsumed(&state, &frame, &frame.events[2]));
    assert(!InputEventIsConsumed(&state, &frame, &frame.events[3]));

    assert(UiEventDispatchFrame(NULL, &state, ProbeHandler, &probe) == 0u);
    assert(UiEventDispatchFrame(&frame, NULL, ProbeHandler, &probe) == 0u);
    assert(UiEventDispatchFrame(&frame, &state, NULL, &probe) == 0u);
}

int main(void)
{
    TestPointInRect();
    TestPointerSnapshot();
    TestDispatchConsumption();
    puts("ui events: hit testing, snapshots and routed consumption passed");
    return 0;
}
