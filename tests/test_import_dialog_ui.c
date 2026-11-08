/* Verifies image-import dialog layout ownership, modal event consumption, text
 * editing, precision controls, and deferred import/cancel/file actions. Scope
 * covers keyboard, pointer, clipboard, and file-drop routing. It does not cover
 * drawing or filesystem image loading; source: unified UI event refactor and
 * delayed-window-layout click-offset bug plan. */
#include "presentation/import_dialog_ui.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static InputPoint Center(Rectangle rect)
{
    return (InputPoint){ rect.x + rect.width * 0.5f,
                         rect.y + rect.height * 0.5f };
}

static InputEvent PressEvent(InputPoint point, InputEventType type)
{
    return (InputEvent){
        .type = type,
        .button = INPUT_BUTTON_LEFT,
        .point = point
    };
}

/* Verifies the dialog uses the current window rather than a stale fixed panel. */
static void TestLayoutTracksCurrentWindow(void)
{
    ImportDialogUiLayout full = ImportDialogUiLayoutFor(1280, 720);
    assert(full.panel.x >= 0.0f && full.panel.y >= 0.0f);
    assert(full.panel.x + full.panel.width <= 1280.0f);
    assert(full.panel.y + full.panel.height <= 720.0f);
    assert(full.input.x >= full.panel.x &&
           full.input.x + full.input.width <= full.panel.x + full.panel.width);
    assert(full.minus.x >= full.panel.x && full.plus.x >= full.minus.x);

    ImportDialogUiLayout small = ImportDialogUiLayoutFor(480, 320);
    assert(small.panel.width < full.panel.width);
    assert(small.panel.x >= 0.0f && small.panel.y >= 0.0f);
    assert(small.panel.x + small.panel.width <= 480.0f);
    assert(small.panel.y + small.panel.height <= 320.0f);
}

/* Verifies UTF-8 editing, shortcut suppression, and clipboard append behavior. */
static void TestTextEditingAndClipboard(void)
{
    InputFrame frame = { 0 };
    ImportDialog dialog = { .open = true, .maxBlocks = 64 };
    ImportDialogActions actions = { 0 };
    ImportDialogUiLayout layout = ImportDialogUiLayoutFor(1280, 720);
    InputEvent text = { .type = INPUT_EVENT_TEXT_INPUT, .codepoint = 'a' };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &text, NULL) == UI_EVENT_HANDLED);
    text.codepoint = '[';
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &text, NULL) == UI_EVENT_HANDLED);
    assert(strcmp(dialog.path, "a") == 0);

    text.codepoint = 0x4e2d;
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &text, NULL) == UI_EVENT_HANDLED);
    InputEvent backspace = { .type = INPUT_EVENT_KEY_DOWN,
                             .key = INPUT_KEY_BACKSPACE };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &backspace, NULL) == UI_EVENT_HANDLED);
    assert(strcmp(dialog.path, "a") == 0);

    frame.modifiers = INPUT_MODIFIER_CONTROL;
    InputEvent paste = { .type = INPUT_EVENT_KEY_DOWN,
                         .key = INPUT_KEY_V };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &paste, "/tmp/image.png") ==
           UI_EVENT_HANDLED);
    assert(strcmp(dialog.path, "a/tmp/image.png") == 0);
}

/* Verifies mode/precision keys and that pointer buttons act only on button down. */
static void TestPrecisionAndModeShortcuts(void)
{
    InputFrame frame = { 0 };
    ImportDialog dialog = { .open = true, .maxBlocks = 64 };
    ImportDialogActions actions = { 0 };
    ImportDialogUiLayout layout = ImportDialogUiLayoutFor(1280, 720);
    InputEvent key = { .type = INPUT_EVENT_KEY_DOWN,
                       .key = INPUT_KEY_TAB };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &key, NULL) == UI_EVENT_HANDLED);
    assert(dialog.relief);

    key.key = INPUT_KEY_LEFT_BRACKET;
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &key, NULL) == UI_EVENT_HANDLED);
    assert(dialog.maxBlocks == 48);

    frame.modifiers = INPUT_MODIFIER_SHIFT;
    key.key = INPUT_KEY_RIGHT_BRACKET;
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &key, NULL) == UI_EVENT_HANDLED);
    assert(dialog.maxBlocks == 112);

    frame.modifiers = 0;
    InputEvent plusUp = PressEvent(Center(layout.plus), INPUT_EVENT_BUTTON_UP);
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &plusUp, NULL) == UI_EVENT_CONSUMED);
    assert(dialog.maxBlocks == 112);
    InputEvent plusDown = PressEvent(Center(layout.plus),
                                     INPUT_EVENT_BUTTON_DOWN);
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &plusDown, NULL) == UI_EVENT_HANDLED);
    assert(dialog.maxBlocks == 128);
    InputEvent miss = PressEvent((InputPoint){ 0.0f, 0.0f },
                                 INPUT_EVENT_BUTTON_DOWN);
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &miss, NULL) == UI_EVENT_CONSUMED);
}

/* Verifies modal consumption and deferred import, cancel, and file-drop actions. */
static void TestDeferredActionsAndModalConsumption(void)
{
    InputFrame frame = { 0 };
    ImportDialog dialog = { .open = true, .maxBlocks = 64 };
    ImportDialogActions actions = { 0 };
    ImportDialogUiLayout layout = ImportDialogUiLayoutFor(1280, 720);
    snprintf(dialog.path, sizeof(dialog.path), "/tmp/image.png");

    InputEvent enter = { .type = INPUT_EVENT_KEY_DOWN,
                         .key = INPUT_KEY_ENTER };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &enter, NULL) == UI_EVENT_HANDLED);
    assert(actions.action == IMPORT_DIALOG_ACTION_IMPORT);
    assert(strcmp(actions.path, dialog.path) == 0);
    assert(dialog.open);

    actions = (ImportDialogActions){ 0 };
    InputEvent escape = { .type = INPUT_EVENT_KEY_DOWN,
                          .key = INPUT_KEY_ESCAPE };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &escape, NULL) == UI_EVENT_HANDLED);
    assert(actions.action == IMPORT_DIALOG_ACTION_CANCEL);

    actions = (ImportDialogActions){ 0 };
    InputEvent drop = { .type = INPUT_EVENT_FILE_DROP };
    snprintf(drop.filePath, sizeof(drop.filePath), "/tmp/dropped.png");
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &drop, NULL) == UI_EVENT_HANDLED);
    assert(actions.action == IMPORT_DIALOG_ACTION_FILE_IMPORT);
    assert(strcmp(actions.path, "/tmp/dropped.png") == 0);

    InputEvent keyUp = { .type = INPUT_EVENT_KEY_UP,
                         .key = INPUT_KEY_A };
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &keyUp, NULL) == UI_EVENT_CONSUMED);

    dialog.open = false;
    assert(ImportDialogUiHandleEvent(&dialog, &actions, &layout, &frame,
                                     &enter, NULL) == UI_EVENT_IGNORED);
}

int main(void)
{
    TestLayoutTracksCurrentWindow();
    TestTextEditingAndClipboard();
    TestPrecisionAndModeShortcuts();
    TestDeferredActionsAndModalConsumption();
    puts("import dialog UI events passed");
    return 0;
}
