#ifndef VOXELCRAFT_IMPORT_DIALOG_UI_H
#define VOXELCRAFT_IMPORT_DIALOG_UI_H

#include "gameplay/ui_types.h"
#include "presentation/ui_event.h"

typedef enum ImportDialogAction {
    IMPORT_DIALOG_ACTION_NONE = 0,
    IMPORT_DIALOG_ACTION_IMPORT,
    IMPORT_DIALOG_ACTION_CANCEL,
    IMPORT_DIALOG_ACTION_FILE_IMPORT
} ImportDialogAction;

typedef struct ImportDialogActions {
    ImportDialogAction action;
    char path[INPUT_EVENT_FILE_PATH_SIZE];
} ImportDialogActions;

typedef struct ImportDialogUiLayout {
    int screenWidth;
    int screenHeight;
    Rectangle panel;
    Rectangle input;
    Rectangle minus;
    Rectangle plus;
} ImportDialogUiLayout;

ImportDialogUiLayout ImportDialogUiLayoutFor(int screenWidth, int screenHeight);
void ImportDialogUiRequestFileImport(ImportDialogActions *actions,
                                     const char *path);

/* Modal routing for the image-import dialog. The dialog state is updated in
 * place, while import/cancel/file actions are deferred to the app update path. */
UiEventResult ImportDialogUiHandleEvent(ImportDialog *dialog,
                                        ImportDialogActions *actions,
                                        const ImportDialogUiLayout *layout,
                                        const InputFrame *frame,
                                        const InputEvent *event,
                                        const char *clipboardText);

#endif
