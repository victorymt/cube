#include "presentation/ui_event.h"

#include <string.h>

bool UiPointInRect(InputPoint point, Rectangle rect)
{
    return point.x >= rect.x && point.y >= rect.y &&
           point.x < rect.x + rect.width && point.y < rect.y + rect.height;
}

UiPointerSnapshot UiPointerSnapshotFromFrame(const InputFrame *frame)
{
    UiPointerSnapshot pointer = { 0 };
    if (!frame) return pointer;
    pointer.position = frame->pointer;
    pointer.delta = frame->pointerDelta;
    pointer.modifiers = frame->modifiers;
    pointer.injected = frame->injected;
    memcpy(pointer.down, frame->buttonsDown, sizeof(pointer.down));
    for (size_t index = 0; index < frame->eventCount; index++) {
        const InputEvent *event = &frame->events[index];
        if (event->type == INPUT_EVENT_BUTTON_DOWN) {
            pointer.pressed[event->button] = true;
        } else if (event->type == INPUT_EVENT_BUTTON_UP) {
            pointer.released[event->button] = true;
        } else if (event->type == INPUT_EVENT_WHEEL) {
            pointer.wheel += event->wheel;
        }
    }
    return pointer;
}

bool UiPointerDown(const UiPointerSnapshot *pointer, InputButton button)
{
    return pointer && button >= 0 && button < INPUT_BUTTON_COUNT &&
           pointer->down[button];
}

bool UiPointerPressed(const UiPointerSnapshot *pointer, InputButton button)
{
    return pointer && button >= 0 && button < INPUT_BUTTON_COUNT &&
           pointer->pressed[button];
}

bool UiPointerReleased(const UiPointerSnapshot *pointer, InputButton button)
{
    return pointer && button >= 0 && button < INPUT_BUTTON_COUNT &&
           pointer->released[button];
}

size_t UiEventDispatchFrame(const InputFrame *frame, InputEventState *state,
                            UiEventHandler handler, void *context)
{
    if (!frame || !state || !handler) return 0u;
    InputEventStateReset(state);
    size_t dispatched = 0u;
    for (size_t index = 0; index < frame->eventCount; index++) {
        const InputEvent *event = &frame->events[index];
        if (InputEventIsConsumed(state, frame, event)) continue;
        UiEventResult result = handler(context, frame, event);
        if (result != UI_EVENT_IGNORED) {
            InputEventConsume(state, frame, event);
            dispatched++;
        }
    }
    return dispatched;
}
