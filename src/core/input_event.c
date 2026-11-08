#include "core/input_event.h"

#include <string.h>
#include <strings.h>

typedef struct InputKeyNameEntry {
    InputKey key;
    const char *name;
} InputKeyNameEntry;

static const char *const BUTTON_NAMES[INPUT_BUTTON_COUNT] = {
    "left", "right", "middle"
};

static const InputKeyNameEntry KEY_NAMES[] = {
    { INPUT_KEY_ESCAPE, "escape" },
    { INPUT_KEY_ENTER, "enter" },
    { INPUT_KEY_TAB, "tab" },
    { INPUT_KEY_BACKSPACE, "backspace" },
    { INPUT_KEY_DELETE, "delete" },
    { INPUT_KEY_LEFT, "left" },
    { INPUT_KEY_RIGHT, "right" },
    { INPUT_KEY_UP, "up" },
    { INPUT_KEY_DOWN, "down" },
    { INPUT_KEY_PAGE_UP, "page_up" },
    { INPUT_KEY_PAGE_DOWN, "page_down" },
    { INPUT_KEY_SPACE, "space" },
    { INPUT_KEY_LEFT_SHIFT, "left_shift" },
    { INPUT_KEY_RIGHT_SHIFT, "right_shift" },
    { INPUT_KEY_LEFT_CONTROL, "left_control" },
    { INPUT_KEY_RIGHT_CONTROL, "right_control" },
    { INPUT_KEY_LEFT_ALT, "left_alt" },
    { INPUT_KEY_RIGHT_ALT, "right_alt" },
    { INPUT_KEY_LEFT_SUPER, "left_super" },
    { INPUT_KEY_RIGHT_SUPER, "right_super" },
    { INPUT_KEY_LEFT_BRACKET, "left_bracket" },
    { INPUT_KEY_RIGHT_BRACKET, "right_bracket" },
    { INPUT_KEY_MINUS, "minus" },
    { INPUT_KEY_EQUAL, "equal" },
    { INPUT_KEY_KP_SUBTRACT, "kp_subtract" },
    { INPUT_KEY_KP_ADD, "kp_add" },
    { INPUT_KEY_A, "a" }, { INPUT_KEY_B, "b" }, { INPUT_KEY_C, "c" },
    { INPUT_KEY_D, "d" }, { INPUT_KEY_E, "e" }, { INPUT_KEY_F, "f" },
    { INPUT_KEY_G, "g" }, { INPUT_KEY_H, "h" }, { INPUT_KEY_I, "i" },
    { INPUT_KEY_J, "j" }, { INPUT_KEY_K, "k" }, { INPUT_KEY_L, "l" },
    { INPUT_KEY_M, "m" }, { INPUT_KEY_N, "n" }, { INPUT_KEY_O, "o" },
    { INPUT_KEY_P, "p" }, { INPUT_KEY_Q, "q" }, { INPUT_KEY_R, "r" },
    { INPUT_KEY_S, "s" }, { INPUT_KEY_T, "t" }, { INPUT_KEY_U, "u" },
    { INPUT_KEY_V, "v" }, { INPUT_KEY_W, "w" }, { INPUT_KEY_X, "x" },
    { INPUT_KEY_Y, "y" }, { INPUT_KEY_Z, "z" },
    { INPUT_KEY_0, "0" }, { INPUT_KEY_1, "1" }, { INPUT_KEY_2, "2" },
    { INPUT_KEY_3, "3" }, { INPUT_KEY_4, "4" }, { INPUT_KEY_5, "5" },
    { INPUT_KEY_6, "6" }, { INPUT_KEY_7, "7" }, { INPUT_KEY_8, "8" },
    { INPUT_KEY_9, "9" },
    { INPUT_KEY_F1, "f1" }, { INPUT_KEY_F2, "f2" },
    { INPUT_KEY_F3, "f3" }, { INPUT_KEY_F4, "f4" },
    { INPUT_KEY_F5, "f5" }, { INPUT_KEY_F6, "f6" },
    { INPUT_KEY_F7, "f7" }, { INPUT_KEY_F8, "f8" },
    { INPUT_KEY_F9, "f9" }, { INPUT_KEY_F10, "f10" },
    { INPUT_KEY_F11, "f11" }, { INPUT_KEY_F12, "f12" }
};

void InputFrameReset(InputFrame *frame, uint64_t number)
{
    if (!frame) return;
    memset(frame, 0, sizeof(*frame));
    frame->number = number;
}

bool InputFramePush(InputFrame *frame, const InputEvent *event)
{
    if (!frame || !event || event->type == INPUT_EVENT_NONE ||
        frame->eventCount >= INPUT_EVENT_MAX_EVENTS) {
        return false;
    }
    InputEvent copy = *event;
    copy.index = frame->eventCount;
    frame->events[frame->eventCount] = copy;
    frame->eventCount++;
    return true;
}

void InputEventStateReset(InputEventState *state)
{
    if (state) memset(state, 0, sizeof(*state));
}

bool InputEventIsConsumed(const InputEventState *state,
                          const InputFrame *frame, const InputEvent *event)
{
    return !state || !frame || !event || event->index >= frame->eventCount ||
           state->consumed[event->index];
}

bool InputEventConsume(InputEventState *state, const InputFrame *frame,
                       const InputEvent *event)
{
    if (InputEventIsConsumed(state, frame, event)) return false;
    state->consumed[event->index] = true;
    return true;
}

const InputEvent *InputFrameNextEvent(const InputFrame *frame,
                                      const InputEventState *state,
                                      size_t *cursor)
{
    if (!frame || !cursor) return NULL;
    while (*cursor < frame->eventCount) {
        size_t index = (*cursor)++;
        if (!state || !state->consumed[index]) return &frame->events[index];
    }
    return NULL;
}

static bool MatchingEvent(const InputFrame *frame, const InputEventState *state,
                          const InputEvent *event,
                          InputEventType firstType, InputEventType secondType,
                          InputButton button, InputKey key, bool unconsumedOnly)
{
    if (!frame || !event) return false;
    if (event->type != firstType && event->type != secondType) return false;
    if (button != INPUT_BUTTON_COUNT && event->button != button) return false;
    if (key != INPUT_KEY_UNKNOWN && event->key != key) return false;
    return !unconsumedOnly ||
           (state && !state->consumed[event->index]);
}

static bool FindButton(const InputFrame *frame, const InputEventState *state,
                       InputButton button,
                       bool released, bool unconsumedOnly)
{
    if (!frame || button < 0 || button >= INPUT_BUTTON_COUNT) return false;
    for (size_t i = 0; i < frame->eventCount; i++) {
        if (MatchingEvent(frame, state, &frame->events[i],
                          released ? INPUT_EVENT_BUTTON_UP :
                                     INPUT_EVENT_BUTTON_DOWN,
                          INPUT_EVENT_NONE, button, INPUT_KEY_UNKNOWN,
                          unconsumedOnly)) {
            return true;
        }
    }
    return false;
}

bool InputFrameButtonPressed(const InputFrame *frame,
                             const InputEventState *state, InputButton button,
                             bool unconsumedOnly)
{
    return FindButton(frame, state, button, false, unconsumedOnly);
}

bool InputFrameButtonReleased(const InputFrame *frame,
                              const InputEventState *state, InputButton button,
                              bool unconsumedOnly)
{
    return FindButton(frame, state, button, true, unconsumedOnly);
}

static bool FindKey(const InputFrame *frame, const InputEventState *state,
                    InputKey key, bool released,
                    bool unconsumedOnly)
{
    if (!frame || key <= INPUT_KEY_UNKNOWN || key >= INPUT_KEY_COUNT) {
        return false;
    }
    for (size_t i = 0; i < frame->eventCount; i++) {
        if (MatchingEvent(frame, state, &frame->events[i],
                          released ? INPUT_EVENT_KEY_UP : INPUT_EVENT_KEY_DOWN,
                          INPUT_EVENT_NONE, INPUT_BUTTON_COUNT, key,
                          unconsumedOnly)) {
            return true;
        }
    }
    return false;
}

bool InputFrameKeyPressed(const InputFrame *frame,
                          const InputEventState *state, InputKey key,
                          bool unconsumedOnly)
{
    return FindKey(frame, state, key, false, unconsumedOnly);
}

bool InputFrameKeyReleased(const InputFrame *frame,
                           const InputEventState *state, InputKey key,
                           bool unconsumedOnly)
{
    return FindKey(frame, state, key, true, unconsumedOnly);
}

float InputFrameWheel(const InputFrame *frame, const InputEventState *state,
                      bool unconsumedOnly)
{
    if (!frame) return 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i < frame->eventCount; i++) {
        if (frame->events[i].type == INPUT_EVENT_WHEEL &&
            (!unconsumedOnly || (state && !state->consumed[i]))) {
            total += frame->events[i].wheel;
        }
    }
    return total;
}

bool InputFrameKeyDown(const InputFrame *frame, InputKey key)
{
    return frame && key > INPUT_KEY_UNKNOWN && key < INPUT_KEY_COUNT &&
           frame->keysDown[key];
}

const char *InputButtonName(InputButton button)
{
    return button >= 0 && button < INPUT_BUTTON_COUNT
        ? BUTTON_NAMES[button] : "unknown";
}

bool InputButtonFromName(const char *name, InputButton *outButton)
{
    if (!name || !outButton) return false;
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (strcasecmp(name, BUTTON_NAMES[i]) == 0) {
            *outButton = (InputButton)i;
            return true;
        }
    }
    return false;
}

const char *InputKeyName(InputKey key)
{
    for (size_t i = 0; i < sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]); i++) {
        if (KEY_NAMES[i].key == key) return KEY_NAMES[i].name;
    }
    return "unknown";
}

bool InputKeyFromName(const char *name, InputKey *outKey)
{
    if (!name || !outKey) return false;
    for (size_t i = 0; i < sizeof(KEY_NAMES) / sizeof(KEY_NAMES[0]); i++) {
        if (strcasecmp(name, KEY_NAMES[i].name) == 0) {
            *outKey = KEY_NAMES[i].key;
            return true;
        }
    }
    return false;
}
