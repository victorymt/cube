/* Input source injection contract: queued pointer/button/key/text/file events
 * are captured in order, pointer deltas and modifier snapshots are coherent,
 * and injected frames never call physical Raylib input functions. The stubs
 * intentionally abort if a physical read leaks into an injected frame; window
 * capture itself is out of scope for this deterministic test. This guards the
 * DSL input path needed to reproduce UI clicks without external tools. */
#include "app/input_source.h"

#include "raylib.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static int physicalReads;

static void FailPhysicalRead(void)
{
    physicalReads++;
    assert(!"injected frame read physical input");
}

Vector2 GetMousePosition(void)
{
    FailPhysicalRead();
    return (Vector2){ 0.0f, 0.0f };
}

Vector2 GetMouseDelta(void)
{
    FailPhysicalRead();
    return (Vector2){ 0.0f, 0.0f };
}

bool IsMouseButtonDown(int button)
{
    (void)button;
    FailPhysicalRead();
    return false;
}

bool IsKeyDown(int key)
{
    (void)key;
    FailPhysicalRead();
    return false;
}

float GetMouseWheelMove(void)
{
    FailPhysicalRead();
    return 0.0f;
}

int GetCharPressed(void)
{
    FailPhysicalRead();
    return 0;
}

bool IsFileDropped(void)
{
    FailPhysicalRead();
    return false;
}

FilePathList LoadDroppedFiles(void)
{
    FailPhysicalRead();
    return (FilePathList){ 0 };
}

void UnloadDroppedFiles(FilePathList files)
{
    (void)files;
    FailPhysicalRead();
}

static void TestInjectedClickAndPointerDelta(void)
{
    InputSource source;
    InputSourceInit(&source);
    assert(InputSourceInjectClick(&source, (InputPoint){ 120.0f, 80.0f },
                                  INPUT_BUTTON_LEFT));
    assert(InputSourceHasInjectedInput(&source));
    InputSourceCaptureFrame(&source, 1u);

    const InputFrame *frame = InputSourceFrame(&source);
    assert(frame && frame->number == 1u && frame->injected);
    assert(frame->eventCount == 3u);
    assert(frame->events[0].type == INPUT_EVENT_POINTER_MOVE);
    assert(frame->events[0].point.x == 120.0f && frame->events[0].point.y == 80.0f);
    assert(frame->events[0].delta.x == 120.0f && frame->events[0].delta.y == 80.0f);
    assert(frame->events[1].type == INPUT_EVENT_BUTTON_DOWN);
    assert(frame->events[1].button == INPUT_BUTTON_LEFT);
    assert(frame->events[1].point.x == 120.0f && frame->events[1].point.y == 80.0f);
    assert(frame->events[2].type == INPUT_EVENT_BUTTON_UP);
    assert(frame->pointer.x == 120.0f && frame->pointer.y == 80.0f);
    assert(frame->pointerDelta.x == 120.0f && frame->pointerDelta.y == 80.0f);
    assert(!frame->buttonsDown[INPUT_BUTTON_LEFT]);
    assert(!InputSourceHasInjectedInput(&source));
    assert(physicalReads == 0);
}

static void TestHeldKeyAcrossFrames(void)
{
    InputSource source;
    InputSourceInit(&source);
    assert(InputSourceInjectKey(&source, INPUT_KEY_W, true));
    InputSourceCaptureFrame(&source, 2u);
    const InputFrame *frame = InputSourceFrame(&source);
    assert(frame->injected && frame->eventCount == 1u);
    assert(frame->events[0].type == INPUT_EVENT_KEY_DOWN);
    assert(frame->events[0].key == INPUT_KEY_W);
    assert(frame->keysDown[INPUT_KEY_W]);
    assert(InputSourceHasInjectedInput(&source));

    assert(InputSourceInjectKey(&source, INPUT_KEY_W, false));
    InputSourceCaptureFrame(&source, 3u);
    assert(frame->number == 3u && frame->injected && frame->eventCount == 1u);
    assert(frame->events[0].type == INPUT_EVENT_KEY_UP);
    assert(!frame->keysDown[INPUT_KEY_W]);
    assert(!InputSourceHasInjectedInput(&source));
    assert(physicalReads == 0);
}

static void TestModifiersTextAndFileDrop(void)
{
    InputSource source;
    InputSourceInit(&source);
    assert(InputSourceInjectKey(&source, INPUT_KEY_LEFT_SHIFT, true));
    assert(InputSourceInjectKey(&source, INPUT_KEY_RIGHT_CONTROL, true));
    assert(InputSourceInjectText(&source, 'A'));
    assert(InputSourceInjectFileDrop(&source, "/tmp/album.png"));
    InputSourceCaptureFrame(&source, 4u);

    const InputFrame *frame = InputSourceFrame(&source);
    assert(frame->injected && frame->eventCount == 4u);
    assert(frame->events[0].type == INPUT_EVENT_KEY_DOWN);
    assert(frame->events[1].type == INPUT_EVENT_KEY_DOWN);
    assert(frame->events[2].type == INPUT_EVENT_TEXT_INPUT);
    assert(frame->events[2].codepoint == 'A');
    assert(frame->events[3].type == INPUT_EVENT_FILE_DROP);
    assert(strcmp(frame->events[3].filePath, "/tmp/album.png") == 0);
    assert(frame->modifiers == (INPUT_MODIFIER_SHIFT | INPUT_MODIFIER_CONTROL));
    assert(frame->keysDown[INPUT_KEY_LEFT_SHIFT]);
    assert(frame->keysDown[INPUT_KEY_RIGHT_CONTROL]);

    assert(InputSourceInjectKey(&source, INPUT_KEY_LEFT_SHIFT, false));
    assert(InputSourceInjectKey(&source, INPUT_KEY_RIGHT_CONTROL, false));
    InputSourceCaptureFrame(&source, 5u);
    assert(frame->modifiers == 0u);
    assert(!InputSourceHasInjectedInput(&source));
    assert(physicalReads == 0);
}

static void TestRejectedInjection(void)
{
    InputSource source;
    InputSourceInit(&source);
    assert(!InputSourceInjectPointer(NULL, (InputPoint){ 1.0f, 1.0f }));
    assert(!InputSourceInjectButton(&source, INPUT_BUTTON_COUNT, true));
    assert(!InputSourceInjectKey(&source, INPUT_KEY_UNKNOWN, true));
    assert(!InputSourceInjectWheel(&source, 0.0f / 0.0f));
    assert(!InputSourceInjectText(&source, 0));
    assert(!InputSourceInjectFileDrop(&source, ""));
    assert(!InputSourceHasInjectedInput(&source));
    assert(physicalReads == 0);
}

int main(void)
{
    TestInjectedClickAndPointerDelta();
    TestHeldKeyAcrossFrames();
    TestModifiersTextAndFileDrop();
    TestRejectedInjection();
    puts("input source: exclusive injected events, deltas, modifiers and payloads passed");
    return 0;
}
