#ifndef VOXELCRAFT_ALBUM_UI_H
#define VOXELCRAFT_ALBUM_UI_H

#include <stdbool.h>

#define ALBUM_COLUMNS 4
#define ALBUM_ROWS 3
#define ALBUM_PER_PAGE (ALBUM_COLUMNS * ALBUM_ROWS)

typedef enum AlbumUiEvent {
    ALBUM_UI_EVENT_NONE = 0,
    ALBUM_UI_EVENT_IMAGE_ADDED,
    ALBUM_UI_EVENT_ALBUM_FULL,
    ALBUM_UI_EVENT_DUPLICATE_IMAGE,
    ALBUM_UI_EVENT_INVALID_IMAGE
} AlbumUiEvent;

void AlbumUiInit(void);
void AlbumUiReset(void);
void AlbumUiOpen(void);
void AlbumUiClose(void);
bool AlbumUiIsOpen(void);
AlbumUiEvent AlbumUiUpdate(void);
void AlbumUiDraw(void);
void AlbumUiCleanup(void);
bool AlbumUiConsumePlaceRequest(void);
const char *AlbumUiSelectedPath(void);

#endif
