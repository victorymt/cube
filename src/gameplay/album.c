#include "gameplay/album.h"

#include "raymath.h"
#include "presentation/render.h"
#include "gameplay/interaction.h"
#include "world/world.h"

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static Album album = { 0 };
static bool placeRequest = false;

#define ALBUM_FILE_VISIBLE 11

static const char *const ART_GRID = "Album - [ ] / Wheel page   Enter add   Space place in world   Delete remove   Esc close";

static void ClearBrowserFiles(void)
{
    for (int i = 0; i < album.browser.fileCount; i++) {
        free(album.browser.files[i]);
    }
    album.browser.fileCount = 0;
    album.browser.fileScroll = 0;
    album.browser.selected = -1;
    album.browser.selectedPath[0] = '\0';
    album.browser.hasPreview = false;
    if (album.browser.preview.id != 0) {
        UnloadTexture(album.browser.preview);
        album.browser.preview = (Texture2D){ 0 };
    }
}

static void UnloadThumbnails(void)
{
    if (!album.thumbsLoaded) return;
    for (int i = 0; i < ALBUM_PER_PAGE; i++) {
        if (album.thumbnails[i].id != 0) {
            UnloadTexture(album.thumbnails[i]);
            album.thumbnails[i] = (Texture2D){ 0 };
        }
    }
    album.thumbsLoaded = false;
}

static Texture2D LoadScaledTexture(const char *path, int maxSize)
{
    Image image = LoadImage(path);
    if (image.data == NULL) return (Texture2D){ 0 };

    if (image.width > maxSize || image.height > maxSize) {
        float scale = (float)maxSize / (float)(image.width > image.height ? image.width : image.height);
        ImageResize(&image, (int)((float)image.width * scale), (int)((float)image.height * scale));
    }
    Texture2D texture = LoadTextureFromImage(image);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(image);
    return texture;
}

static void LoadThumbnailsForPage(void)
{
    UnloadThumbnails();
    album.thumbsLoaded = true;

    int start = album.page * ALBUM_PER_PAGE;
    for (int i = 0; i < ALBUM_PER_PAGE; i++) {
        int index = start + i;
        if (index < album.imageCount) {
            album.thumbnails[i] = LoadScaledTexture(album.images[index].path, 220);
        }
    }
}

static int PageCount(void)
{
    return (album.imageCount + ALBUM_PER_PAGE - 1) / ALBUM_PER_PAGE;
}

static void ClampPage(void)
{
    int pages = PageCount();
    if (pages == 0) album.page = 0;
    else if (album.page >= pages) album.page = pages - 1;
}

static void SelectIndex(int index)
{
    if (index < 0 || index >= album.imageCount) return;
    album.selectedIndex = index;
}

static void SelectPageItem(int slot)
{
    int index = album.page * ALBUM_PER_PAGE + slot;
    SelectIndex(index);
}

static void AppendDirectoryChar(char c)
{
    size_t len = strlen(album.browser.directory);
    if (len + 2 >= sizeof(album.browser.directory)) return;
    album.browser.directory[len] = c;
    album.browser.directory[len + 1] = '\0';
}

static void ListDirectory(void)
{
    ClearBrowserFiles();
    album.browser.hasListed = true;

    if (album.browser.directory[0] == '\0') {
        snprintf(album.browser.directory, sizeof(album.browser.directory), "%s", GetWorkingDirectory());
    }
    if (!DirectoryExists(album.browser.directory)) {
        album.browser.listingError = true;
        return;
    }
    album.browser.listingError = false;

    FilePathList files = LoadDirectoryFiles(album.browser.directory);
    for (unsigned int i = 0; i < files.count; i++) {
        if (!IsSupportedImageFile(files.paths[i])) continue;
        if (album.browser.fileCount >= (int)(sizeof(album.browser.files) / sizeof(album.browser.files[0]))) break;
        album.browser.files[album.browser.fileCount] = malloc(strlen(files.paths[i]) + 1);
        if (album.browser.files[album.browser.fileCount]) {
            strcpy(album.browser.files[album.browser.fileCount], files.paths[i]);
            album.browser.fileCount++;
        }
    }
    UnloadDirectoryFiles(files);

    if (album.browser.fileCount > 0) album.browser.selected = 0;
}

static void LoadBrowserPreview(void)
{
    if (album.browser.selected < 0 || album.browser.selected >= album.browser.fileCount) {
        album.browser.selectedPath[0] = '\0';
        album.browser.hasPreview = false;
        return;
    }
    snprintf(album.browser.selectedPath, sizeof(album.browser.selectedPath),
             "%s", album.browser.files[album.browser.selected]);
    album.browser.hasPreview = true;
    if (album.browser.preview.id != 0) {
        UnloadTexture(album.browser.preview);
        album.browser.preview = (Texture2D){ 0 };
    }
    album.browser.preview = LoadScaledTexture(album.browser.selectedPath, 420);
}

static bool AlbumHasPath(const char *path)
{
    for (int i = 0; i < album.imageCount; i++) {
        if (album.images[i].used && strcmp(album.images[i].path, path) == 0) return true;
    }
    return false;
}

static void ImportSelectedFile(void)
{
    if (album.browser.selected < 0 || album.browser.selected >= album.browser.fileCount) return;

    const char *path = album.browser.files[album.browser.selected];
    if (album.imageCount >= ALBUM_MAX_IMAGES) {
        SetImportMessage("Album is full (64 images).");
        return;
    }
    if (AlbumHasPath(path)) {
        SetImportMessage("Image already in album.");
        return;
    }
    album.images[album.imageCount].used = true;
    snprintf(album.images[album.imageCount].path, sizeof(album.images[album.imageCount].path), "%s", path);
    album.imageCount++;
    ClampPage();
    LoadThumbnailsForPage();
    SelectIndex(album.imageCount - 1);
    SetImportMessage("Added image to album.");
}

static void DeleteSelected(void)
{
    if (album.selectedIndex < 0 || album.selectedIndex >= album.imageCount) return;

    int target = album.selectedIndex;
    for (int i = target; i < album.imageCount - 1; i++) {
        album.images[i] = album.images[i + 1];
    }
    album.imageCount--;
    album.images[album.imageCount].used = false;
    ClampPage();
    LoadThumbnailsForPage();
    if (album.selectedIndex >= album.imageCount) album.selectedIndex = album.imageCount - 1;
    if (album.selectedIndex < 0) album.selectedIndex = 0;
}

void AlbumInit(void)
{
    memset(&album, 0, sizeof(album));
}

void AlbumReset(void)
{
    AlbumCleanup();
    AlbumInit();
}

void AlbumOpen(void)
{
    album.open = true;
    album.screen = ALBUM_GRID;
    ClampPage();
    if (album.selectedIndex < 0 && album.imageCount > 0) album.selectedIndex = 0;
    LoadThumbnailsForPage();
}

void AlbumClose(void)
{
    if (!album.open) return;
    album.open = false;
    UnloadThumbnails();
    ClearBrowserFiles();
}

bool AlbumIsOpen(void)
{
    return album.open;
}

bool AlbumSave(FILE *file)
{
    if (!file || fprintf(file, "album %d\n", album.imageCount) < 0) return false;
    for (int i = 0; i < album.imageCount; i++) {
        if (fprintf(file, "%s\n", album.images[i].path) < 0) return false;
    }
    return true;
}

bool AlbumLoad(FILE *file)
{
    album.imageCount = 0;

    char label[64] = { 0 };
    int count = 0;
    if (!file) return false;
    if (fscanf(file, "%63s %d", label, &count) != 2) return true;
    if (strcmp(label, "album") != 0 || count < 0 || count > ALBUM_MAX_IMAGES) return false;
    int separator = 0;
    while ((separator = fgetc(file)) != '\n' && separator != EOF) {}
    for (int i = 0; i < count; i++) {
        char path[ALBUM_PATH_MAX] = { 0 };
        if (!fgets(path, sizeof(path), file)) return false;
        size_t len = strlen(path);
        while (len > 0 && (path[len - 1] == '\n' || path[len - 1] == '\r')) {
            path[--len] = '\0';
        }
        if (!FileExists(path)) continue;
        if (AlbumHasPath(path)) continue;
        album.images[album.imageCount].used = true;
        snprintf(album.images[album.imageCount].path, sizeof(album.images[album.imageCount].path), "%s", path);
        album.imageCount++;
    }
    ClampPage();
    if (album.selectedIndex < 0 && album.imageCount > 0) album.selectedIndex = 0;
    return true;
}

void AlbumCleanup(void)
{
    UnloadThumbnails();
    ClearBrowserFiles();
}

static void UpdateGrid(void)
{
    int pages = PageCount();

    if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_RIGHT) ||
        (IsKeyPressed(KEY_ENTER) && IsKeyDown(KEY_LEFT_CONTROL))) {
        if (pages > 0) {
            album.page = (album.page + 1) % pages;
            LoadThumbnailsForPage();
        }
    }
    if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_LEFT)) {
        if (pages > 0) {
            album.page = (album.page + pages - 1) % pages;
            LoadThumbnailsForPage();
        }
    }
    else if (IsKeyPressed(KEY_ENTER) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        album.screen = ALBUM_ADD;
        album.browser.open = true;
        ClearBrowserFiles();
        album.browser.directory[0] = '\0';
        return;
    }
    if (IsKeyPressed(KEY_DELETE)) {
        DeleteSelected();
    }
    if (IsKeyPressed(KEY_SPACE) && album.selectedIndex >= 0 && album.selectedIndex < album.imageCount) {
        placeRequest = true;
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int gridW = ALBUM_COLUMNS * 168 + (ALBUM_COLUMNS - 1) * 16;
    int x0 = sw / 2 - gridW / 2;
    int gridY = sh / 2 - 92;

    for (int slot = 0; slot < ALBUM_PER_PAGE; slot++) {
        int col = slot % ALBUM_COLUMNS;
        int row = slot / ALBUM_COLUMNS;
        Rectangle cell = {
            (float)(x0 + col * 184),
            (float)(gridY + row * 172),
            168.0f, 152.0f
        };
        if (CheckCollisionPointRec(GetMousePosition(), cell) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            SelectPageItem(slot);
        }
    }

    int selPage = album.selectedIndex >= 0 ? album.selectedIndex / ALBUM_PER_PAGE : -1;
    if (selPage >= 0 && selPage != album.page) {
        album.page = selPage;
        LoadThumbnailsForPage();
    }
}

static void UpdateAdd(void)
{
    int c = GetCharPressed();
    while (c > 0) {
        if (c >= 32 && c <= 126) AppendDirectoryChar((char)c);
        c = GetCharPressed();
    }
    if (IsKeyPressed(KEY_BACKSPACE) && strlen(album.browser.directory) > 0) {
        album.browser.directory[strlen(album.browser.directory) - 1] = '\0';
    }
    if (IsKeyPressed(KEY_V) && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) {
        const char *clipboard = GetClipboardText();
        if (clipboard && clipboard[0]) {
            snprintf(album.browser.directory, sizeof(album.browser.directory), "%s", clipboard);
        }
    }
    if (IsKeyPressed(KEY_ENTER) && !IsKeyDown(KEY_LEFT_CONTROL)) {
        if (!album.browser.hasListed || album.browser.fileCount == 0) {
            ListDirectory();
            if (album.browser.fileCount > 0) LoadBrowserPreview();
        } else {
            ImportSelectedFile();
        }
        return;
    }
    if (IsKeyPressed(KEY_DOWN) || IsKeyPressed(KEY_J)) {
        if (album.browser.fileCount > 0) {
            if (album.browser.selected < album.browser.fileCount - 1) {
                album.browser.selected++;
                if (album.browser.selected > album.browser.fileScroll + ALBUM_FILE_VISIBLE - 1) album.browser.fileScroll++;
            }
            LoadBrowserPreview();
        }
    }
    if (IsKeyPressed(KEY_UP) || IsKeyPressed(KEY_K)) {
        if (album.browser.fileCount > 0 && album.browser.selected > 0) {
            album.browser.selected--;
            if (album.browser.selected < album.browser.fileScroll) album.browser.fileScroll--;
            LoadBrowserPreview();
        }
    }
    if (IsKeyPressed(KEY_TAB)) {
        ListDirectory();
        if (album.browser.fileCount > 0) LoadBrowserPreview();
        return;
    }

    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    Rectangle listRect = { sw / 2 - 330.0f, sh / 2 - 40.0f, 440.0f, 380.0f };
    if (CheckCollisionPointRec(GetMousePosition(), listRect)) {
        int wheel = (int)GetMouseWheelMove();
        if (wheel != 0) {
            album.browser.fileScroll -= wheel;
            int maxScroll = album.browser.fileCount - ALBUM_FILE_VISIBLE;
            if (maxScroll < 0) maxScroll = 0;
            if (album.browser.fileScroll < 0) album.browser.fileScroll = 0;
            if (album.browser.fileScroll > maxScroll) album.browser.fileScroll = maxScroll;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && album.browser.fileCount > 0) {
            float itemH = 24.0f;
            for (int i = album.browser.fileScroll; i < album.browser.fileCount; i++) {
                Rectangle item = { listRect.x + 4.0f, listRect.y + 6.0f + (float)(i - album.browser.fileScroll) * itemH,
                                   listRect.width - 8.0f, itemH - 2.0f };
                if (CheckCollisionPointRec(GetMousePosition(), item)) {
                    album.browser.selected = i;
                    LoadBrowserPreview();
                    break;
                }
            }
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        album.screen = ALBUM_GRID;
        ClearBrowserFiles();
    }
}

void AlbumUpdate(void)
{
    if (!album.open) return;

    if (IsKeyPressed(KEY_ESCAPE) && album.screen == ALBUM_GRID) {
        AlbumClose();
        return;
    }

    if (album.screen == ALBUM_GRID) UpdateGrid();
    else UpdateAdd();
}

static void DrawCenteredTextIn(const char *text, const Rectangle *rect, int fontSize)
{
    int width = UiMeasureText(text, fontSize);
    UiDrawText(text, (int)(rect->x + (rect->width - (float)width) / 2.0f),
             (int)(rect->y + (rect->height - (float)fontSize) / 2.0f), fontSize, Fade(WHITE, 0.45f));
}

static void DrawThumbnailCell(const Rectangle *cell, int slot)
{
    int index = album.page * ALBUM_PER_PAGE + slot;
    if (index >= album.imageCount) return;

    bool selected = index == album.selectedIndex;
    DrawRectangleRounded(*cell, 0.06f, 8, (Color){ 22, 28, 34, 245 });
    DrawRectangleRoundedLinesEx(*cell, 0.06f, 8, selected ? 3.0f : 1.5f,
                                selected ? (Color){ 240, 200, 90, 255 } : Fade(WHITE, 0.30f));

    if (album.thumbsLoaded && album.thumbnails[slot].id != 0) {
        Texture2D texture = album.thumbnails[slot];
        float scale = fminf((cell->width - 16.0f) / (float)texture.width,
                            (cell->height - 16.0f) / (float)texture.height);
        if (scale > 0.0f) {
            float w = (float)texture.width * scale;
            float h = (float)texture.height * scale;
            Rectangle dest = { cell->x + (cell->width - w) / 2.0f, cell->y + (cell->height - h) / 2.0f, w, h };
            DrawTexturePro(texture, (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                           dest, Vector2Zero(), 0.0f, WHITE);
        }
    } else {
        DrawCenteredTextIn("missing", cell, 14);
    }

    char name[ALBUM_PATH_MAX];
    snprintf(name, sizeof(name), "%s", GetFileName(album.images[index].path));
    int maxNameWidth = (int)cell->width - 8;
    while (strlen(name) > 0 && UiMeasureText(name, 12) > maxNameWidth) name[strlen(name) - 1] = '\0';
    UiDrawText(name, (int)(cell->x + (cell->width - (float)UiMeasureText(name, 12)) / 2.0f),
             (int)(cell->y + cell->height - 22), 12, Fade(WHITE, 0.65f));
}

static void DrawAlbumGrid(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();
    int pages = PageCount();
    int gridW = ALBUM_COLUMNS * 168 + (ALBUM_COLUMNS - 1) * 16;
    int x0 = sw / 2 - gridW / 2;
    int gridY = sh / 2 - 92;

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
    Rectangle panel = { (float)x0 - 30.0f, (float)(gridY - 70.0f), (float)(gridW + 60.0f), (float)(ALBUM_ROWS * 172 + 150) };
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    UiDrawText("Album", (int)panel.x + 28, (int)panel.y + 18, 32, WHITE);
    UiDrawText(TextFormat("%d images", album.imageCount), (int)panel.x + 28, (int)panel.y + 58, 16, Fade(WHITE, 0.75f));
    if (pages > 0) {
        UiDrawText(TextFormat("Page %d / %d", album.page + 1, pages),
                 (int)(panel.x + panel.width - 150.0f), (int)panel.y + 26, 22, Fade(WHITE, 0.9f));
    }

    Rectangle prevRect = { panel.x + 16.0f, panel.y + 24.0f, 44.0f, 36.0f };
    Rectangle nextRect = { panel.x + panel.width - 60.0f, panel.y + 24.0f, 44.0f, 36.0f };
    if (pages > 0) {
        if (DrawMenuButton(prevRect, "<", false)) {
            album.page = (album.page + pages - 1) % pages;
            LoadThumbnailsForPage();
        }
        if (DrawMenuButton(nextRect, ">", false)) {
            album.page = (album.page + 1) % pages;
            LoadThumbnailsForPage();
        }
    }

    if (album.imageCount == 0) {
        DrawCenteredText("Album is empty. Press Enter to add images.", sh / 2 - 10, 22, Fade(WHITE, 0.85f));
    }

    for (int slot = 0; slot < ALBUM_PER_PAGE; slot++) {
        int col = slot % ALBUM_COLUMNS;
        int row = slot / ALBUM_COLUMNS;
        Rectangle cell = { (float)(x0 + col * 184), (float)(gridY + row * 172), 168.0f, 152.0f };
        DrawThumbnailCell(&cell, slot);
    }


    DrawCenteredText(ART_GRID, sh - 34, 16, Fade(WHITE, 0.72f));
}

static void DrawAdd(void)
{
    int sw = GetScreenWidth();
    int sh = GetScreenHeight();

    DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.60f));
    Rectangle panel = { sw / 2 - 440.0f, sh / 2 - 260.0f, 880.0f, 520.0f };
    DrawRectangleRounded(panel, 0.04f, 8, (Color){ 30, 38, 45, 245 });
    DrawRectangleRoundedLinesEx(panel, 0.04f, 8, 2.0f, Fade(WHITE, 0.45f));

    UiDrawText("Add image to album", (int)panel.x + 30, (int)panel.y + 22, 28, WHITE);
    UiDrawText("Type a folder path (or Ctrl+V paste), then Enter to list images.",
             (int)panel.x + 30, (int)panel.y + 60, 16, Fade(WHITE, 0.72f));

    Rectangle input = { panel.x + 30.0f, panel.y + 96.0f, panel.width - 60.0f, 46.0f };
    DrawRectangleRounded(input, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(input, 0.05f, 8, 2.0f, (Color){ 98, 160, 115, 255 });

    const char *shown = album.browser.directory[0] ? album.browser.directory : "folder path...";
    int textX = (int)input.x + 16;
    UiDrawText(shown, textX, (int)input.y + 13, 20, album.browser.directory[0] ? WHITE : Fade(WHITE, 0.38f));
    if (((int)(GetTime() * 2.0) % 2) == 0) {
        int cursorX = textX + UiMeasureText(shown, 20) + 2;
        DrawLine(cursorX, (int)input.y + 11, cursorX, (int)input.y + 35, WHITE);
    }

    if (album.browser.listingError) {
        UiDrawText("Folder not found. Try an absolute path.", (int)input.x, (int)input.y + 54, 16, (Color){ 240, 130, 110, 255 });
    }

    Rectangle listRect = { panel.x + 30.0f, panel.y + 168.0f, 440.0f, 300.0f };
    DrawRectangleRounded(listRect, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(listRect, 0.05f, 8, 1.5f, Fade(WHITE, 0.25f));

    if (album.browser.fileCount == 0 && !album.browser.listingError) {
        UiDrawText("No image files listed yet.", (int)listRect.x + 14, (int)listRect.y + 12, 16, Fade(WHITE, 0.45f));
    }

    float itemH = 24.0f;
    int visible = (int)(listRect.height / itemH) - 1;
    for (int i = album.browser.fileScroll; i < album.browser.fileCount && i < album.browser.fileScroll + visible; i++) {
        Rectangle item = { listRect.x + 4.0f, listRect.y + 6.0f + (float)(i - album.browser.fileScroll) * itemH,
                           listRect.width - 8.0f, itemH - 2.0f };
        bool selected = i == album.browser.selected;
        if (selected) DrawRectangleRounded(item, 0.05f, 4, (Color){ 98, 160, 115, 80 });
        UiDrawText(TextFormat("%s", GetFileName(album.browser.files[i])),
                 (int)item.x + 8, (int)item.y + 4, 15, selected ? WHITE : Fade(WHITE, 0.82f));
    }

    Rectangle previewRect = { panel.x + 500.0f, panel.y + 168.0f, 350.0f, 300.0f };
    DrawRectangleRounded(previewRect, 0.05f, 8, (Color){ 15, 20, 25, 255 });
    DrawRectangleRoundedLinesEx(previewRect, 0.05f, 8, 1.5f, Fade(WHITE, 0.25f));

    if (album.browser.hasPreview) {
        if (album.browser.preview.id != 0) {
            Texture2D texture = album.browser.preview;
            float scale = fminf((previewRect.width - 20.0f) / (float)texture.width,
                                (previewRect.height - 20.0f) / (float)texture.height);
            if (scale > 0.0f) {
                float w = (float)texture.width * scale;
                float h = (float)texture.height * scale;
                Rectangle dest = { previewRect.x + (previewRect.width - w) / 2.0f,
                                   previewRect.y + (previewRect.height - h) / 2.0f, w, h };
                DrawTexturePro(texture, (Rectangle){ 0, 0, (float)texture.width, (float)texture.height },
                               dest, Vector2Zero(), 0.0f, WHITE);
            }
        }
    } else {
        DrawCenteredTextIn("preview", &previewRect, 16);
    }

    UiDrawText("Enter import    Tab refresh    Esc back", (int)panel.x + 30, (int)panel.y + 486, 16, Fade(WHITE, 0.72f));
}

void AlbumDraw(void)
{
    if (!album.open) return;
    if (album.screen == ALBUM_GRID) DrawAlbumGrid();
    else DrawAdd();
}

bool AlbumConsumePlaceRequest(void)
{
    if (!placeRequest) return false;
    placeRequest = false;
    return album.selectedIndex >= 0 && album.selectedIndex < album.imageCount;
}

const char *AlbumSelectedPath(void)
{
    if (album.selectedIndex < 0 || album.selectedIndex >= album.imageCount) return NULL;
    return album.images[album.selectedIndex].path;
}
