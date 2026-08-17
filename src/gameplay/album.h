#ifndef VOXELCRAFT_ALBUM_H
#define VOXELCRAFT_ALBUM_H

#include <stdbool.h>
#include <stdio.h>

#define ALBUM_MAX_IMAGES 64
#define ALBUM_PATH_MAX 1024

typedef enum AlbumAddResult {
    ALBUM_ADD_OK = 0,
    ALBUM_ADD_FULL,
    ALBUM_ADD_DUPLICATE,
    ALBUM_ADD_INVALID
} AlbumAddResult;

void AlbumInit(void);
void AlbumReset(void);
int AlbumImageCount(void);
const char *AlbumImagePathAt(int index);
bool AlbumHasPath(const char *path);
AlbumAddResult AlbumAddPath(const char *path);
bool AlbumRemoveAt(int index);
bool AlbumSave(FILE *file);
bool AlbumLoad(FILE *file);

#endif
