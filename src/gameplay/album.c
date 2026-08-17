#include "gameplay/album.h"

#include "raylib.h"

#include <string.h>

typedef struct AlbumImage {
    bool used;
    char path[ALBUM_PATH_MAX];
} AlbumImage;

typedef struct AlbumModel {
    AlbumImage images[ALBUM_MAX_IMAGES];
    int imageCount;
} AlbumModel;

static AlbumModel album = { 0 };

void AlbumInit(void)
{
    album = (AlbumModel){ 0 };
}

void AlbumReset(void)
{
    AlbumInit();
}

int AlbumImageCount(void)
{
    return album.imageCount;
}

const char *AlbumImagePathAt(int index)
{
    if (index < 0 || index >= album.imageCount) return NULL;
    return album.images[index].path;
}

bool AlbumHasPath(const char *path)
{
    if (!path || !path[0]) return false;
    for (int index = 0; index < album.imageCount; index++) {
        if (strcmp(album.images[index].path, path) == 0) return true;
    }
    return false;
}

AlbumAddResult AlbumAddPath(const char *path)
{
    if (!path || !path[0] || strlen(path) >= ALBUM_PATH_MAX) {
        return ALBUM_ADD_INVALID;
    }
    if (album.imageCount >= ALBUM_MAX_IMAGES) return ALBUM_ADD_FULL;
    if (AlbumHasPath(path)) return ALBUM_ADD_DUPLICATE;

    AlbumImage *image = &album.images[album.imageCount++];
    image->used = true;
    snprintf(image->path, sizeof(image->path), "%s", path);
    return ALBUM_ADD_OK;
}

bool AlbumRemoveAt(int index)
{
    if (index < 0 || index >= album.imageCount) return false;
    for (int current = index; current < album.imageCount - 1; current++) {
        album.images[current] = album.images[current + 1];
    }
    album.imageCount--;
    album.images[album.imageCount] = (AlbumImage){ 0 };
    return true;
}

bool AlbumSave(FILE *file)
{
    if (!file || fprintf(file, "album %d\n", album.imageCount) < 0) {
        return false;
    }
    for (int index = 0; index < album.imageCount; index++) {
        if (fprintf(file, "%s\n", album.images[index].path) < 0) {
            return false;
        }
    }
    return true;
}

bool AlbumLoad(FILE *file)
{
    if (!file) return false;
    AlbumModel loaded = { 0 };
    char label[64] = { 0 };
    int count = 0;
    if (fscanf(file, "%63s %d", label, &count) != 2) {
        album = loaded;
        return true;
    }
    if (strcmp(label, "album") != 0 || count < 0 ||
        count > ALBUM_MAX_IMAGES) {
        return false;
    }

    int separator = 0;
    while ((separator = fgetc(file)) != '\n' && separator != EOF) {}
    for (int index = 0; index < count; index++) {
        char path[ALBUM_PATH_MAX] = { 0 };
        if (!fgets(path, sizeof(path), file)) return false;
        size_t length = strlen(path);
        while (length > 0 &&
               (path[length - 1] == '\n' || path[length - 1] == '\r')) {
            path[--length] = '\0';
        }
        if (!FileExists(path)) continue;

        bool duplicate = false;
        for (int loadedIndex = 0; loadedIndex < loaded.imageCount;
             loadedIndex++) {
            if (strcmp(loaded.images[loadedIndex].path, path) == 0) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        AlbumImage *image = &loaded.images[loaded.imageCount++];
        image->used = true;
        snprintf(image->path, sizeof(image->path), "%s", path);
    }
    album = loaded;
    return true;
}
