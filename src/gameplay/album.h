#ifndef VOXELCRAFT_ALBUM_H
#define VOXELCRAFT_ALBUM_H

#include "world/world_types.h"

#include <stdio.h>

#define ALBUM_MAX_IMAGES 64
#define ALBUM_PATH_MAX 1024
#define ALBUM_COLUMNS 4
#define ALBUM_ROWS 3
#define ALBUM_PER_PAGE (ALBUM_COLUMNS * ALBUM_ROWS)

typedef enum AlbumScreen {
    ALBUM_GRID = 0,
    ALBUM_ADD
} AlbumScreen;

typedef struct AlbumImage {
    bool used;
    char path[ALBUM_PATH_MAX];
} AlbumImage;

typedef struct AlbumFileBrowser {
    bool open;
    char directory[ALBUM_PATH_MAX];
    char *files[1024];
    int fileCount;
    int fileScroll;
    int selected;
    char selectedPath[ALBUM_PATH_MAX];
    bool hasPreview;
    Texture2D preview;
    bool listingError;
    bool hasListed;
} AlbumFileBrowser;

typedef struct Album {
    bool open;
    AlbumScreen screen;
    AlbumImage images[ALBUM_MAX_IMAGES];
    int imageCount;
    int page;
    int selectedIndex;
    Texture2D thumbnails[ALBUM_PER_PAGE];
    bool thumbsLoaded;
    AlbumFileBrowser browser;
} Album;

void AlbumInit(void);
void AlbumReset(void);
void AlbumOpen(void);
void AlbumClose(void);
bool AlbumIsOpen(void);
void AlbumUpdate(void);
void AlbumDraw(void);
bool AlbumSave(FILE *file);
bool AlbumLoad(FILE *file);
void AlbumCleanup(void);

bool AlbumConsumePlaceRequest(void);
const char *AlbumSelectedPath(void);

#endif
