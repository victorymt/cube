#ifndef VOXELCRAFT_INPUT_EVENT_H
#define VOXELCRAFT_INPUT_EVENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define INPUT_EVENT_MAX_EVENTS 256
#define INPUT_EVENT_FILE_PATH_SIZE 1024

typedef enum InputEventType {
    INPUT_EVENT_NONE = 0,
    INPUT_EVENT_POINTER_MOVE,
    INPUT_EVENT_BUTTON_DOWN,
    INPUT_EVENT_BUTTON_UP,
    INPUT_EVENT_WHEEL,
    INPUT_EVENT_KEY_DOWN,
    INPUT_EVENT_KEY_UP,
    INPUT_EVENT_TEXT_INPUT,
    INPUT_EVENT_FILE_DROP
} InputEventType;

typedef enum InputButton {
    INPUT_BUTTON_LEFT = 0,
    INPUT_BUTTON_RIGHT,
    INPUT_BUTTON_MIDDLE,
    INPUT_BUTTON_COUNT
} InputButton;

typedef enum InputModifiers {
    INPUT_MODIFIER_SHIFT = 1u << 0,
    INPUT_MODIFIER_CONTROL = 1u << 1,
    INPUT_MODIFIER_ALT = 1u << 2,
    INPUT_MODIFIER_SUPER = 1u << 3
} InputModifiers;

typedef enum InputKey {
    INPUT_KEY_UNKNOWN = 0,
    INPUT_KEY_ESCAPE,
    INPUT_KEY_ENTER,
    INPUT_KEY_TAB,
    INPUT_KEY_BACKSPACE,
    INPUT_KEY_DELETE,
    INPUT_KEY_LEFT,
    INPUT_KEY_RIGHT,
    INPUT_KEY_UP,
    INPUT_KEY_DOWN,
    INPUT_KEY_PAGE_UP,
    INPUT_KEY_PAGE_DOWN,
    INPUT_KEY_SPACE,
    INPUT_KEY_LEFT_SHIFT,
    INPUT_KEY_RIGHT_SHIFT,
    INPUT_KEY_LEFT_CONTROL,
    INPUT_KEY_RIGHT_CONTROL,
    INPUT_KEY_LEFT_ALT,
    INPUT_KEY_RIGHT_ALT,
    INPUT_KEY_LEFT_SUPER,
    INPUT_KEY_RIGHT_SUPER,
    INPUT_KEY_LEFT_BRACKET,
    INPUT_KEY_RIGHT_BRACKET,
    INPUT_KEY_MINUS,
    INPUT_KEY_EQUAL,
    INPUT_KEY_KP_SUBTRACT,
    INPUT_KEY_KP_ADD,
    INPUT_KEY_A,
    INPUT_KEY_B,
    INPUT_KEY_C,
    INPUT_KEY_D,
    INPUT_KEY_E,
    INPUT_KEY_F,
    INPUT_KEY_G,
    INPUT_KEY_H,
    INPUT_KEY_I,
    INPUT_KEY_J,
    INPUT_KEY_K,
    INPUT_KEY_L,
    INPUT_KEY_M,
    INPUT_KEY_N,
    INPUT_KEY_O,
    INPUT_KEY_P,
    INPUT_KEY_Q,
    INPUT_KEY_R,
    INPUT_KEY_S,
    INPUT_KEY_T,
    INPUT_KEY_U,
    INPUT_KEY_V,
    INPUT_KEY_W,
    INPUT_KEY_X,
    INPUT_KEY_Y,
    INPUT_KEY_Z,
    INPUT_KEY_0,
    INPUT_KEY_1,
    INPUT_KEY_2,
    INPUT_KEY_3,
    INPUT_KEY_4,
    INPUT_KEY_5,
    INPUT_KEY_6,
    INPUT_KEY_7,
    INPUT_KEY_8,
    INPUT_KEY_9,
    INPUT_KEY_F1,
    INPUT_KEY_F2,
    INPUT_KEY_F3,
    INPUT_KEY_F4,
    INPUT_KEY_F5,
    INPUT_KEY_F6,
    INPUT_KEY_F7,
    INPUT_KEY_F8,
    INPUT_KEY_F9,
    INPUT_KEY_F10,
    INPUT_KEY_F11,
    INPUT_KEY_F12,
    INPUT_KEY_COUNT
} InputKey;

typedef struct InputPoint {
    float x;
    float y;
} InputPoint;

typedef struct InputEvent {
    InputEventType type;
    InputButton button;
    InputKey key;
    InputModifiers modifiers;
    InputPoint point;
    InputPoint delta;
    float wheel;
    int codepoint;
    size_t index;
    char filePath[INPUT_EVENT_FILE_PATH_SIZE];
} InputEvent;

typedef struct InputFrame {
    uint64_t number;
    InputPoint pointer;
    InputPoint pointerDelta;
    InputModifiers modifiers;
    bool buttonsDown[INPUT_BUTTON_COUNT];
    bool keysDown[INPUT_KEY_COUNT];
    bool injected;
    size_t eventCount;
    InputEvent events[INPUT_EVENT_MAX_EVENTS];
} InputFrame;

/* Events are immutable snapshots. Consumers share this separate bitmap so one
 * routing layer can mark what it handled without changing the captured frame. */
typedef struct InputEventState {
    bool consumed[INPUT_EVENT_MAX_EVENTS];
} InputEventState;

void InputFrameReset(InputFrame *frame, uint64_t number);
bool InputFramePush(InputFrame *frame, const InputEvent *event);
void InputEventStateReset(InputEventState *state);
bool InputEventConsume(InputEventState *state, const InputFrame *frame,
                       const InputEvent *event);
bool InputEventIsConsumed(const InputEventState *state,
                          const InputFrame *frame, const InputEvent *event);
const InputEvent *InputFrameNextEvent(const InputFrame *frame,
                                      const InputEventState *state,
                                      size_t *cursor);
bool InputFrameButtonPressed(const InputFrame *frame,
                             const InputEventState *state,
                             InputButton button, bool unconsumedOnly);
bool InputFrameButtonReleased(const InputFrame *frame,
                              const InputEventState *state,
                              InputButton button, bool unconsumedOnly);
bool InputFrameKeyPressed(const InputFrame *frame,
                          const InputEventState *state, InputKey key,
                          bool unconsumedOnly);
bool InputFrameKeyReleased(const InputFrame *frame,
                           const InputEventState *state, InputKey key,
                           bool unconsumedOnly);
float InputFrameWheel(const InputFrame *frame, const InputEventState *state,
                      bool unconsumedOnly);
bool InputFrameKeyDown(const InputFrame *frame, InputKey key);

const char *InputButtonName(InputButton button);
bool InputButtonFromName(const char *name, InputButton *outButton);
const char *InputKeyName(InputKey key);
bool InputKeyFromName(const char *name, InputKey *outKey);

#endif
