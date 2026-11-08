#ifndef VOXELCRAFT_INPUT_SOURCE_H
#define VOXELCRAFT_INPUT_SOURCE_H

#include "core/input_event.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct InputSource {
    InputFrame frame;
    InputEvent pending[INPUT_EVENT_MAX_EVENTS];
    size_t pendingCount;
    InputPoint lastPointer;
    InputPoint syntheticPointer;
    InputPoint syntheticPointerDelta;
    bool syntheticPointerActive;
    bool syntheticButtonsDown[INPUT_BUTTON_COUNT];
    bool syntheticKeysDown[INPUT_KEY_COUNT];
    bool physicalButtonsDown[INPUT_BUTTON_COUNT];
    bool physicalKeysDown[INPUT_KEY_COUNT];
    bool physicalCaptured;
    bool resyncPhysical;
} InputSource;

void InputSourceInit(InputSource *source);
void InputSourceCaptureFrame(InputSource *source, uint64_t frameNumber);
const InputFrame *InputSourceFrame(const InputSource *source);
bool InputSourceHasInjectedInput(const InputSource *source);

bool InputSourceInjectPointer(InputSource *source, InputPoint point);
bool InputSourceInjectButton(InputSource *source, InputButton button, bool down);
bool InputSourceInjectClick(InputSource *source, InputPoint point,
                            InputButton button);
bool InputSourceInjectWheel(InputSource *source, float delta);
bool InputSourceInjectKey(InputSource *source, InputKey key, bool down);
bool InputSourceInjectText(InputSource *source, int codepoint);
bool InputSourceInjectFileDrop(InputSource *source, const char *path);

#endif
