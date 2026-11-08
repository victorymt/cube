#include "presentation/import_dialog_ui.h"

#include "core/config.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

static int ClampPrecision(int value)
{
    if (value < IMPORT_MIN_BLOCKS) return IMPORT_MIN_BLOCKS;
    if (value > IMPORT_MAX_BLOCKS) return IMPORT_MAX_BLOCKS;
    return value;
}

static int AdjustPrecision(int value, int delta)
{
    int adjusted = ClampPrecision(value) + delta;
    return ClampPrecision(adjusted);
}

static bool HasModifier(const InputFrame *frame, const InputEvent *event,
                        InputModifiers modifier)
{
    unsigned modifiers = (unsigned)frame->modifiers | (unsigned)event->modifiers;
    return (modifiers & (unsigned)modifier) != 0u;
}

static bool CodepointIsPrintable(int codepoint)
{
    return codepoint >= 32 && codepoint != 127 &&
           codepoint <= 0x10ffff &&
           !(codepoint >= 0xd800 && codepoint <= 0xdfff);
}

static size_t EncodeUtf8(int codepoint, char encoded[4])
{
    if (codepoint <= 0x7f) {
        encoded[0] = (char)codepoint;
        return 1u;
    }
    if (codepoint <= 0x7ff) {
        encoded[0] = (char)(0xc0 | (codepoint >> 6));
        encoded[1] = (char)(0x80 | (codepoint & 0x3f));
        return 2u;
    }
    if (codepoint <= 0xffff) {
        encoded[0] = (char)(0xe0 | (codepoint >> 12));
        encoded[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        encoded[2] = (char)(0x80 | (codepoint & 0x3f));
        return 3u;
    }
    encoded[0] = (char)(0xf0 | (codepoint >> 18));
    encoded[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    encoded[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    encoded[3] = (char)(0x80 | (codepoint & 0x3f));
    return 4u;
}

static void AppendBytes(ImportDialog *dialog, const char *text, size_t bytes)
{
    size_t used = strlen(dialog->path);
    if (used >= sizeof(dialog->path) - 1u) return;
    size_t room = sizeof(dialog->path) - used - 1u;
    if (bytes > room) bytes = room;
    memcpy(dialog->path + used, text, bytes);
    dialog->path[used + bytes] = '\0';
}

static void AppendCodepoint(ImportDialog *dialog, int codepoint)
{
    if (!CodepointIsPrintable(codepoint) ||
        codepoint == '[' || codepoint == ']') {
        return;
    }
    char encoded[4];
    size_t bytes = EncodeUtf8(codepoint, encoded);
    AppendBytes(dialog, encoded, bytes);
}

static void DeleteLastCodepoint(ImportDialog *dialog)
{
    size_t length = strlen(dialog->path);
    if (length == 0u) return;
    size_t index = length - 1u;
    while (index > 0u &&
           ((unsigned char)dialog->path[index] & 0xc0u) == 0x80u) {
        index--;
    }
    dialog->path[index] = '\0';
}

static void SetActionPath(ImportDialogActions *actions,
                          ImportDialogAction action, const char *path)
{
    actions->action = action;
    actions->path[0] = '\0';
    if (path && path[0]) {
        snprintf(actions->path, sizeof(actions->path), "%s", path);
    }
}

ImportDialogUiLayout ImportDialogUiLayoutFor(int screenWidth, int screenHeight)
{
    ImportDialogUiLayout layout = { 0 };
    float width = screenWidth > 0 ? (float)screenWidth : 1.0f;
    float height = screenHeight > 0 ? (float)screenHeight : 1.0f;
    float margin = width < 560.0f ? 8.0f : 24.0f;
    float panelWidth = fminf(720.0f, fmaxf(32.0f, width - margin * 2.0f));
    float panelHeight = fminf(256.0f, fmaxf(32.0f, height - margin * 2.0f));
    float padding = fminf(30.0f, fmaxf(12.0f, panelWidth * 0.05f));

    layout.screenWidth = screenWidth;
    layout.screenHeight = screenHeight;
    layout.panel = (Rectangle){
        (width - panelWidth) * 0.5f,
        (height - panelHeight) * 0.5f,
        panelWidth,
        panelHeight
    };
    layout.input = (Rectangle){
        layout.panel.x + padding,
        layout.panel.y + 92.0f,
        layout.panel.width - padding * 2.0f,
        44.0f
    };
    layout.minus = (Rectangle){
        layout.panel.x + padding,
        layout.panel.y + 166.0f,
        44.0f,
        36.0f
    };
    layout.plus = (Rectangle){
        layout.panel.x + layout.panel.width - padding - 44.0f,
        layout.panel.y + 166.0f,
        44.0f,
        36.0f
    };
    return layout;
}

void ImportDialogUiRequestFileImport(ImportDialogActions *actions,
                                     const char *path)
{
    if (!actions || !path || !path[0]) return;
    SetActionPath(actions, IMPORT_DIALOG_ACTION_FILE_IMPORT, path);
}

UiEventResult ImportDialogUiHandleEvent(ImportDialog *dialog,
                                        ImportDialogActions *actions,
                                        const ImportDialogUiLayout *layout,
                                        const InputFrame *frame,
                                        const InputEvent *event,
                                        const char *clipboardText)
{
    if (!dialog || !dialog->open || !actions || !layout || !frame ||
        !event || event->type == INPUT_EVENT_NONE) {
        return UI_EVENT_IGNORED;
    }

    if (event->type == INPUT_EVENT_TEXT_INPUT) {
        if (!HasModifier(frame, event, INPUT_MODIFIER_CONTROL)) {
            AppendCodepoint(dialog, event->codepoint);
        }
        return UI_EVENT_HANDLED;
    }

    if (event->type == INPUT_EVENT_FILE_DROP) {
        if (event->filePath[0]) {
            SetActionPath(actions, IMPORT_DIALOG_ACTION_FILE_IMPORT,
                          event->filePath);
            return UI_EVENT_HANDLED;
        }
        return UI_EVENT_CONSUMED;
    }

    if (event->type == INPUT_EVENT_BUTTON_DOWN &&
        event->button == INPUT_BUTTON_LEFT) {
        if (UiPointInRect(event->point, layout->minus)) {
            dialog->maxBlocks = AdjustPrecision(
                dialog->maxBlocks, -IMPORT_PRECISION_STEP);
            return UI_EVENT_HANDLED;
        }
        if (UiPointInRect(event->point, layout->plus)) {
            dialog->maxBlocks = AdjustPrecision(
                dialog->maxBlocks, IMPORT_PRECISION_STEP);
            return UI_EVENT_HANDLED;
        }
        return UI_EVENT_CONSUMED;
    }

    if (event->type != INPUT_EVENT_KEY_DOWN) return UI_EVENT_CONSUMED;

    if (event->key == INPUT_KEY_ESCAPE) {
        SetActionPath(actions, IMPORT_DIALOG_ACTION_CANCEL, NULL);
        return UI_EVENT_HANDLED;
    }
    if (event->key == INPUT_KEY_ENTER) {
        SetActionPath(actions, IMPORT_DIALOG_ACTION_IMPORT, dialog->path);
        return UI_EVENT_HANDLED;
    }
    if (event->key == INPUT_KEY_BACKSPACE) {
        DeleteLastCodepoint(dialog);
        return UI_EVENT_HANDLED;
    }
    if (event->key == INPUT_KEY_TAB) {
        dialog->relief = !dialog->relief;
        return UI_EVENT_HANDLED;
    }
    if (event->key == INPUT_KEY_V &&
        HasModifier(frame, event, INPUT_MODIFIER_CONTROL)) {
        if (clipboardText && clipboardText[0]) {
            AppendBytes(dialog, clipboardText, strlen(clipboardText));
        }
        return UI_EVENT_HANDLED;
    }

    int step = HasModifier(frame, event, INPUT_MODIFIER_SHIFT)
        ? IMPORT_PRECISION_BIG_STEP : IMPORT_PRECISION_STEP;
    if (event->key == INPUT_KEY_LEFT_BRACKET ||
        event->key == INPUT_KEY_KP_SUBTRACT) {
        dialog->maxBlocks = AdjustPrecision(dialog->maxBlocks, -step);
        return UI_EVENT_HANDLED;
    }
    if (event->key == INPUT_KEY_RIGHT_BRACKET ||
        event->key == INPUT_KEY_KP_ADD) {
        dialog->maxBlocks = AdjustPrecision(dialog->maxBlocks, step);
        return UI_EVENT_HANDLED;
    }
    return UI_EVENT_CONSUMED;
}
