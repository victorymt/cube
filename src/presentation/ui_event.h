#ifndef VOXELCRAFT_UI_EVENT_H
#define VOXELCRAFT_UI_EVENT_H

#include "core/input_event.h"

#include "raylib.h"

#include <stdbool.h>
#include <stddef.h>

typedef enum UiEventResult {
    UI_EVENT_IGNORED = 0,
    UI_EVENT_HANDLED,
    UI_EVENT_CONSUMED
} UiEventResult;

typedef struct UiPointerSnapshot {
    InputPoint position;
    InputPoint delta;
    InputModifiers modifiers;
    bool down[INPUT_BUTTON_COUNT];
    bool pressed[INPUT_BUTTON_COUNT];
    bool released[INPUT_BUTTON_COUNT];
    float wheel;
    bool injected;
} UiPointerSnapshot;

typedef UiEventResult (*UiEventHandler)(void *context,
                                        const InputFrame *frame,
                                        const InputEvent *event);

bool UiPointInRect(InputPoint point, Rectangle rect);
UiPointerSnapshot UiPointerSnapshotFromFrame(const InputFrame *frame);
bool UiPointerDown(const UiPointerSnapshot *pointer, InputButton button);
bool UiPointerPressed(const UiPointerSnapshot *pointer, InputButton button);
bool UiPointerReleased(const UiPointerSnapshot *pointer, InputButton button);

/* Dispatches each unconsumed event to one top-level router. HANDLED and
 * CONSUMED results both mark the event consumed; only IGNORED lets a lower
 * layer see it. */
size_t UiEventDispatchFrame(const InputFrame *frame, InputEventState *state,
                            UiEventHandler handler, void *context);

#endif
