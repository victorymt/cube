#include "app/input_source.h"

#include "raylib.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct InputKeyBinding {
    InputKey key;
    int rayKey;
} InputKeyBinding;

static const InputKeyBinding KEY_BINDINGS[] = {
    { INPUT_KEY_ESCAPE, KEY_ESCAPE },
    { INPUT_KEY_ENTER, KEY_ENTER },
    { INPUT_KEY_TAB, KEY_TAB },
    { INPUT_KEY_BACKSPACE, KEY_BACKSPACE },
    { INPUT_KEY_DELETE, KEY_DELETE },
    { INPUT_KEY_LEFT, KEY_LEFT },
    { INPUT_KEY_RIGHT, KEY_RIGHT },
    { INPUT_KEY_UP, KEY_UP },
    { INPUT_KEY_DOWN, KEY_DOWN },
    { INPUT_KEY_PAGE_UP, KEY_PAGE_UP },
    { INPUT_KEY_PAGE_DOWN, KEY_PAGE_DOWN },
    { INPUT_KEY_SPACE, KEY_SPACE },
    { INPUT_KEY_LEFT_SHIFT, KEY_LEFT_SHIFT },
    { INPUT_KEY_RIGHT_SHIFT, KEY_RIGHT_SHIFT },
    { INPUT_KEY_LEFT_CONTROL, KEY_LEFT_CONTROL },
    { INPUT_KEY_RIGHT_CONTROL, KEY_RIGHT_CONTROL },
    { INPUT_KEY_LEFT_ALT, KEY_LEFT_ALT },
    { INPUT_KEY_RIGHT_ALT, KEY_RIGHT_ALT },
    { INPUT_KEY_LEFT_SUPER, KEY_LEFT_SUPER },
    { INPUT_KEY_RIGHT_SUPER, KEY_RIGHT_SUPER },
    { INPUT_KEY_LEFT_BRACKET, KEY_LEFT_BRACKET },
    { INPUT_KEY_RIGHT_BRACKET, KEY_RIGHT_BRACKET },
    { INPUT_KEY_MINUS, KEY_MINUS },
    { INPUT_KEY_EQUAL, KEY_EQUAL },
    { INPUT_KEY_KP_SUBTRACT, KEY_KP_SUBTRACT },
    { INPUT_KEY_KP_ADD, KEY_KP_ADD },
    { INPUT_KEY_A, KEY_A }, { INPUT_KEY_B, KEY_B }, { INPUT_KEY_C, KEY_C },
    { INPUT_KEY_D, KEY_D }, { INPUT_KEY_E, KEY_E }, { INPUT_KEY_F, KEY_F },
    { INPUT_KEY_G, KEY_G }, { INPUT_KEY_H, KEY_H }, { INPUT_KEY_I, KEY_I },
    { INPUT_KEY_J, KEY_J }, { INPUT_KEY_K, KEY_K }, { INPUT_KEY_L, KEY_L },
    { INPUT_KEY_M, KEY_M }, { INPUT_KEY_N, KEY_N }, { INPUT_KEY_O, KEY_O },
    { INPUT_KEY_P, KEY_P }, { INPUT_KEY_Q, KEY_Q }, { INPUT_KEY_R, KEY_R },
    { INPUT_KEY_S, KEY_S }, { INPUT_KEY_T, KEY_T }, { INPUT_KEY_U, KEY_U },
    { INPUT_KEY_V, KEY_V }, { INPUT_KEY_W, KEY_W }, { INPUT_KEY_X, KEY_X },
    { INPUT_KEY_Y, KEY_Y }, { INPUT_KEY_Z, KEY_Z },
    { INPUT_KEY_0, KEY_ZERO }, { INPUT_KEY_1, KEY_ONE },
    { INPUT_KEY_2, KEY_TWO }, { INPUT_KEY_3, KEY_THREE },
    { INPUT_KEY_4, KEY_FOUR }, { INPUT_KEY_5, KEY_FIVE },
    { INPUT_KEY_6, KEY_SIX }, { INPUT_KEY_7, KEY_SEVEN },
    { INPUT_KEY_8, KEY_EIGHT }, { INPUT_KEY_9, KEY_NINE },
    { INPUT_KEY_F1, KEY_F1 }, { INPUT_KEY_F2, KEY_F2 },
    { INPUT_KEY_F3, KEY_F3 }, { INPUT_KEY_F4, KEY_F4 },
    { INPUT_KEY_F5, KEY_F5 }, { INPUT_KEY_F6, KEY_F6 },
    { INPUT_KEY_F7, KEY_F7 }, { INPUT_KEY_F8, KEY_F8 },
    { INPUT_KEY_F9, KEY_F9 }, { INPUT_KEY_F10, KEY_F10 },
    { INPUT_KEY_F11, KEY_F11 }, { INPUT_KEY_F12, KEY_F12 }
};

static int RayButton(InputButton button)
{
    switch (button) {
    case INPUT_BUTTON_LEFT: return MOUSE_BUTTON_LEFT;
    case INPUT_BUTTON_RIGHT: return MOUSE_BUTTON_RIGHT;
    case INPUT_BUTTON_MIDDLE: return MOUSE_BUTTON_MIDDLE;
    default: return -1;
    }
}

static int RayKey(InputKey key)
{
    for (size_t i = 0; i < sizeof(KEY_BINDINGS) / sizeof(KEY_BINDINGS[0]); i++) {
        if (KEY_BINDINGS[i].key == key) return KEY_BINDINGS[i].rayKey;
    }
    return KEY_NULL;
}

static bool HasSyntheticDown(const InputSource *source)
{
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        if (source->syntheticButtonsDown[i]) return true;
    }
    for (int i = 0; i < INPUT_KEY_COUNT; i++) {
        if (source->syntheticKeysDown[i]) return true;
    }
    return false;
}

static bool QueueEvent(InputSource *source, const InputEvent *event)
{
    if (!source || !event || source->pendingCount >= INPUT_EVENT_MAX_EVENTS) {
        return false;
    }
    source->pending[source->pendingCount++] = *event;
    return true;
}

void InputSourceInit(InputSource *source)
{
    if (!source) return;
    memset(source, 0, sizeof(*source));
    InputFrameReset(&source->frame, 0);
}

bool InputSourceInjectPointer(InputSource *source, InputPoint point)
{
    if (!source) return false;
    source->syntheticPointerActive = true;
    return QueueEvent(source, &(InputEvent){
        .type = INPUT_EVENT_POINTER_MOVE,
        .point = point
    });
}

bool InputSourceInjectButton(InputSource *source, InputButton button, bool down)
{
    if (!source || button < 0 || button >= INPUT_BUTTON_COUNT) return false;
    return QueueEvent(source, &(InputEvent){
        .type = down ? INPUT_EVENT_BUTTON_DOWN : INPUT_EVENT_BUTTON_UP,
        .button = button
    });
}

bool InputSourceInjectClick(InputSource *source, InputPoint point,
                            InputButton button)
{
    return InputSourceInjectPointer(source, point) &&
           InputSourceInjectButton(source, button, true) &&
           InputSourceInjectButton(source, button, false);
}

bool InputSourceInjectWheel(InputSource *source, float delta)
{
    return isfinite(delta) && QueueEvent(source, &(InputEvent){
        .type = INPUT_EVENT_WHEEL,
        .wheel = delta
    });
}

bool InputSourceInjectKey(InputSource *source, InputKey key, bool down)
{
    if (!source || key <= INPUT_KEY_UNKNOWN || key >= INPUT_KEY_COUNT) {
        return false;
    }
    return QueueEvent(source, &(InputEvent){
        .type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP,
        .key = key
    });
}

bool InputSourceInjectText(InputSource *source, int codepoint)
{
    return codepoint > 0 && QueueEvent(source, &(InputEvent){
        .type = INPUT_EVENT_TEXT_INPUT,
        .codepoint = codepoint
    });
}

bool InputSourceInjectFileDrop(InputSource *source, const char *path)
{
    if (!source || !path || !path[0]) return false;
    InputEvent event = { .type = INPUT_EVENT_FILE_DROP };
    snprintf(event.filePath, sizeof(event.filePath), "%s", path);
    return QueueEvent(source, &event);
}

static InputModifiers SyntheticModifiers(const InputSource *source)
{
    unsigned modifiers = 0u;
    if (source->syntheticKeysDown[INPUT_KEY_LEFT_SHIFT] ||
        source->syntheticKeysDown[INPUT_KEY_RIGHT_SHIFT]) {
        modifiers |= INPUT_MODIFIER_SHIFT;
    }
    if (source->syntheticKeysDown[INPUT_KEY_LEFT_CONTROL] ||
        source->syntheticKeysDown[INPUT_KEY_RIGHT_CONTROL]) {
        modifiers |= INPUT_MODIFIER_CONTROL;
    }
    if (source->syntheticKeysDown[INPUT_KEY_LEFT_ALT] ||
        source->syntheticKeysDown[INPUT_KEY_RIGHT_ALT]) {
        modifiers |= INPUT_MODIFIER_ALT;
    }
    if (source->syntheticKeysDown[INPUT_KEY_LEFT_SUPER] ||
        source->syntheticKeysDown[INPUT_KEY_RIGHT_SUPER]) {
        modifiers |= INPUT_MODIFIER_SUPER;
    }
    return (InputModifiers)modifiers;
}

static InputModifiers PhysicalModifiers(void)
{
    unsigned modifiers = 0u;
    if (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) {
        modifiers |= INPUT_MODIFIER_SHIFT;
    }
    if (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL)) {
        modifiers |= INPUT_MODIFIER_CONTROL;
    }
    if (IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT)) {
        modifiers |= INPUT_MODIFIER_ALT;
    }
    if (IsKeyDown(KEY_LEFT_SUPER) || IsKeyDown(KEY_RIGHT_SUPER)) {
        modifiers |= INPUT_MODIFIER_SUPER;
    }
    return (InputModifiers)modifiers;
}

static void CaptureInjectedFrame(InputSource *source)
{
    source->frame.injected = true;
    if (!source->syntheticPointerActive) {
        source->syntheticPointer = source->lastPointer;
    }
    source->syntheticPointerDelta = (InputPoint){ 0 };
    for (size_t i = 0; i < source->pendingCount; i++) {
        InputEvent event = source->pending[i];
        if (event.type == INPUT_EVENT_POINTER_MOVE) {
            event.delta = (InputPoint){
                event.point.x - source->syntheticPointer.x,
                event.point.y - source->syntheticPointer.y
            };
            source->syntheticPointer = event.point;
            source->syntheticPointerDelta = event.delta;
        } else if (event.type == INPUT_EVENT_BUTTON_DOWN ||
                   event.type == INPUT_EVENT_BUTTON_UP) {
            event.point = source->syntheticPointer;
            source->syntheticButtonsDown[event.button] =
                event.type == INPUT_EVENT_BUTTON_DOWN;
        } else if (event.type == INPUT_EVENT_KEY_DOWN ||
                   event.type == INPUT_EVENT_KEY_UP) {
            source->syntheticKeysDown[event.key] =
                event.type == INPUT_EVENT_KEY_DOWN;
        }
        InputFramePush(&source->frame, &event);
    }
    memcpy(source->frame.buttonsDown, source->syntheticButtonsDown,
           sizeof(source->frame.buttonsDown));
    memcpy(source->frame.keysDown, source->syntheticKeysDown,
           sizeof(source->frame.keysDown));
    source->frame.pointer = source->syntheticPointer;
    source->frame.pointerDelta = source->syntheticPointerDelta;
    source->frame.modifiers = SyntheticModifiers(source);
    source->lastPointer = source->syntheticPointer;
    source->pendingCount = 0;
    source->resyncPhysical = true;
}

static void ResetPhysicalSnapshot(InputSource *source, Vector2 pointer)
{
    source->lastPointer = (InputPoint){ pointer.x, pointer.y };
    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        source->physicalButtonsDown[i] =
            IsMouseButtonDown(RayButton((InputButton)i));
    }
    for (int i = 1; i < INPUT_KEY_COUNT; i++) {
        source->physicalKeysDown[i] =
            IsKeyDown(RayKey((InputKey)i));
    }
    source->physicalCaptured = true;
}

static void CapturePhysicalFrame(InputSource *source)
{
    Vector2 pointer = GetMousePosition();
    Vector2 delta = GetMouseDelta();
    source->frame.pointer = (InputPoint){ pointer.x, pointer.y };
    source->frame.pointerDelta = (InputPoint){ delta.x, delta.y };
    source->frame.modifiers = PhysicalModifiers();

    if (!source->physicalCaptured || source->resyncPhysical) {
        ResetPhysicalSnapshot(source, pointer);
        source->resyncPhysical = false;
    }

    if (delta.x != 0.0f || delta.y != 0.0f ||
        pointer.x != source->lastPointer.x ||
        pointer.y != source->lastPointer.y) {
        InputFramePush(&source->frame, &(InputEvent){
            .type = INPUT_EVENT_POINTER_MOVE,
            .point = { pointer.x, pointer.y },
            .delta = { delta.x, delta.y },
            .modifiers = source->frame.modifiers
        });
    }

    for (int i = 0; i < INPUT_BUTTON_COUNT; i++) {
        bool down = IsMouseButtonDown(RayButton((InputButton)i));
        source->frame.buttonsDown[i] = down;
        if (down != source->physicalButtonsDown[i]) {
            InputFramePush(&source->frame, &(InputEvent){
                .type = down ? INPUT_EVENT_BUTTON_DOWN : INPUT_EVENT_BUTTON_UP,
                .button = (InputButton)i,
                .point = { pointer.x, pointer.y },
                .delta = { delta.x, delta.y },
                .modifiers = source->frame.modifiers
            });
            source->physicalButtonsDown[i] = down;
        }
    }

    for (int i = 1; i < INPUT_KEY_COUNT; i++) {
        bool down = IsKeyDown(RayKey((InputKey)i));
        source->frame.keysDown[i] = down;
        if (down != source->physicalKeysDown[i]) {
            InputFramePush(&source->frame, &(InputEvent){
                .type = down ? INPUT_EVENT_KEY_DOWN : INPUT_EVENT_KEY_UP,
                .key = (InputKey)i,
                .point = { pointer.x, pointer.y },
                .modifiers = source->frame.modifiers
            });
            source->physicalKeysDown[i] = down;
        }
    }

    float wheel = GetMouseWheelMove();
    if (wheel != 0.0f) {
        InputFramePush(&source->frame, &(InputEvent){
            .type = INPUT_EVENT_WHEEL,
            .wheel = wheel,
            .point = { pointer.x, pointer.y },
            .modifiers = source->frame.modifiers
        });
    }

    int codepoint = GetCharPressed();
    while (codepoint > 0) {
        InputFramePush(&source->frame, &(InputEvent){
            .type = INPUT_EVENT_TEXT_INPUT,
            .codepoint = codepoint,
            .point = { pointer.x, pointer.y },
            .modifiers = source->frame.modifiers
        });
        codepoint = GetCharPressed();
    }

    if (IsFileDropped()) {
        FilePathList files = LoadDroppedFiles();
        for (unsigned int i = 0; i < files.count; i++) {
            if (!files.paths || !files.paths[i]) continue;
            InputEvent event = {
                .type = INPUT_EVENT_FILE_DROP,
                .point = { pointer.x, pointer.y },
                .modifiers = source->frame.modifiers
            };
            snprintf(event.filePath, sizeof(event.filePath), "%s",
                     files.paths[i]);
            InputFramePush(&source->frame, &event);
        }
        UnloadDroppedFiles(files);
    }

    source->lastPointer = source->frame.pointer;
}

void InputSourceCaptureFrame(InputSource *source, uint64_t frameNumber)
{
    if (!source) return;
    InputFrameReset(&source->frame, frameNumber);
    bool injected = source->pendingCount > 0 || HasSyntheticDown(source);
    if (injected) {
        CaptureInjectedFrame(source);
    } else {
        CapturePhysicalFrame(source);
    }
}

const InputFrame *InputSourceFrame(const InputSource *source)
{
    return source ? &source->frame : NULL;
}

bool InputSourceHasInjectedInput(const InputSource *source)
{
    return source && (source->pendingCount > 0 || HasSyntheticDown(source));
}
