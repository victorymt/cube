#include "interaction.h"

#include "raymath.h"
#include "chunks.h"
#include "world.h"
#include "terrain.h"
#include "player.h"
#include "space.h"
#include "world_environment.h"

#include <ctype.h>
#include <limits.h>
#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chunks.h"
#include "world.h"
#include "terrain.h"
#include "player.h"

#define RAYCAST_MAX_STEPS 4096u

static bool RaycastPrepare(Vector3 origin, Vector3 *direction,
                           float maxDistance)
{
    if (!direction || !isfinite(origin.x) || !isfinite(origin.y) ||
        !isfinite(origin.z) || !isfinite(maxDistance) ||
        maxDistance < 0.0f) {
        return false;
    }

    double floorX = floor((double)origin.x);
    double floorY = floor((double)origin.y);
    double floorZ = floor((double)origin.z);
    if (floorX < (double)INT_MIN || floorX > (double)INT_MAX ||
        floorY < (double)INT_MIN || floorY > (double)INT_MAX ||
        floorZ < (double)INT_MIN || floorZ > (double)INT_MAX) {
        return false;
    }

    if (!isfinite(direction->x) || !isfinite(direction->y) ||
        !isfinite(direction->z)) {
        return false;
    }
    double lengthSquared = (double)direction->x * (double)direction->x +
                           (double)direction->y * (double)direction->y +
                           (double)direction->z * (double)direction->z;
    if (!isfinite(lengthSquared) || lengthSquared <= 1.0e-12) {
        return false;
    }

    double length = sqrt(lengthSquared);
    direction->x = (float)((double)direction->x / length);
    direction->y = (float)((double)direction->y / length);
    direction->z = (float)((double)direction->z / length);
    return isfinite(direction->x) && isfinite(direction->y) &&
           isfinite(direction->z);
}

static bool RaycastAdvanceCoordinate(int *coordinate, int step)
{
    if (!coordinate || (step > 0 && *coordinate == INT_MAX) ||
        (step < 0 && *coordinate == INT_MIN)) {
        return false;
    }
    *coordinate += step;
    return true;
}

void AdjustRenderDistance(int delta)
{
    int nextDistance = renderDistanceChunks + delta;
    if (nextDistance < MIN_RENDER_DISTANCE_CHUNKS) nextDistance = MIN_RENDER_DISTANCE_CHUNKS;
    if (nextDistance > MAX_RENDER_DISTANCE_CHUNKS) nextDistance = MAX_RENDER_DISTANCE_CHUNKS;
    if (nextDistance == renderDistanceChunks) return;

    renderDistanceChunks = nextDistance;
    SetImportMessage(TextFormat("View distance: %d chunks.", renderDistanceChunks));
}

int HexValue(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

bool IsTrimChar(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

void NormalizeDroppedPath(const char *source, char *dest, size_t destSize)
{
    if (destSize == 0) return;
    dest[0] = '\0';
    if (!source) return;

    const char *read = source;
    while (IsTrimChar(*read)) read++;

    const char *end = read + strlen(read);
    while (end > read && IsTrimChar(end[-1])) end--;
    if (end > read + 1 && ((*read == '"' && end[-1] == '"') || (*read == '\'' && end[-1] == '\''))) {
        read++;
        end--;
    }

    if (strncmp(read, "file://", 7) == 0) {
        read += 7;
        if (strncmp(read, "localhost/", 10) == 0) read += 9;
        else if (*read && *read != '/') {
            const char *slash = strchr(read, '/');
            if (slash) read = slash;
        }
    } else if (strncmp(read, "file:", 5) == 0) {
        read += 5;
    }

    size_t write = 0;
    while (read < end && *read && write + 1 < destSize) {
        if (read[0] == '%' && read[1] && read[2]) {
            int hi = HexValue(read[1]);
            int lo = HexValue(read[2]);
            if (hi >= 0 && lo >= 0) {
                dest[write++] = (char)((hi << 4) | lo);
                read += 3;
                continue;
            }
        }

        dest[write++] = *read++;
    }
    dest[write] = '\0';
}

const char *ResolveDroppedImagePath(const char *path, char *resolved, size_t resolvedSize)
{
    if (!path || resolvedSize == 0) return NULL;

    NormalizeDroppedPath(path, resolved, resolvedSize);
    if (FileExists(resolved)) return resolved;
    if (FileExists(path)) return path;
    return NULL;
}

bool IsSupportedImageFile(const char *path)
{
    return IsFileExtension(path, ".png;.jpg;.jpeg;.bmp;.tga;.gif;.qoi;.psd;.hdr");
}

int ClampImportPrecision(int value)
{
    if (value < IMPORT_MIN_BLOCKS) return IMPORT_MIN_BLOCKS;
    if (value > IMPORT_MAX_BLOCKS) return IMPORT_MAX_BLOCKS;
    return value;
}

int AdjustImportPrecision(int value, int delta)
{
    int64_t adjusted = (int64_t)value + (int64_t)delta;
    if (adjusted < IMPORT_MIN_BLOCKS) return IMPORT_MIN_BLOCKS;
    if (adjusted > IMPORT_MAX_BLOCKS) return IMPORT_MAX_BLOCKS;
    return (int)adjusted;
}

bool BuildImageImportPlan(int imageWidth, int imageHeight, int maxBlocks,
                          bool relief, ImageImportPlan *outPlan)
{
    if (!outPlan || imageWidth <= 0 || imageHeight <= 0 ||
        imageWidth > IMPORT_MAX_SOURCE_DIMENSION ||
        imageHeight > IMPORT_MAX_SOURCE_DIMENSION) {
        return false;
    }

    uint64_t sourcePixels = (uint64_t)(unsigned)imageWidth *
                            (uint64_t)(unsigned)imageHeight;
    if (sourcePixels > IMPORT_MAX_SOURCE_PIXELS) return false;

    int precision = ClampImportPrecision(maxBlocks);
    double scale = fmin((double)precision / (double)imageWidth,
                        (double)precision / (double)imageHeight);
    if (scale > 1.0) scale = 1.0;
    int targetWidth = (int)floor((double)imageWidth * scale);
    int targetHeight = (int)floor((double)imageHeight * scale);
    if (targetWidth < 1) targetWidth = 1;
    if (targetHeight < 1) targetHeight = 1;

    uint64_t targetPixels = (uint64_t)(unsigned)targetWidth *
                            (uint64_t)(unsigned)targetHeight;
    uint64_t operationsPerPixel = relief ? (uint64_t)WORLD_HEIGHT * 2u : 1u;
    uint64_t maximumBlockOperations = targetPixels * operationsPerPixel;
    if (targetPixels > IMPORT_MAX_TARGET_PIXELS ||
        maximumBlockOperations > IMPORT_MAX_BLOCK_OPERATIONS) {
        return false;
    }

    *outPlan = (ImageImportPlan){
        .targetWidth = targetWidth,
        .targetHeight = targetHeight,
        .sourcePixels = sourcePixels,
        .targetPixels = targetPixels,
        .maximumBlockOperations = maximumBlockOperations
    };
    return true;
}

int ImagePixelLuminance(Color pixel)
{
    return (int)roundf(0.2126f * (float)pixel.r +
                       0.7152f * (float)pixel.g +
                       0.0722f * (float)pixel.b);
}

int ReliefHeightForPixel(Color pixel, int baseY)
{
    int maxHeight = WORLD_HEIGHT - baseY;
    if (maxHeight <= 1) return 1;

    float luminance = Clamp((float)ImagePixelLuminance(pixel) / 255.0f, 0.0f, 1.0f);
    return 1 + (int)roundf(luminance * (float)(maxHeight - 1));
}

static bool ImportPlacementBase(const Player *player, int targetWidth,
                                int targetHeight, int rowDx, int rowDz,
                                int colDx, int colDz,
                                int *outBaseX, int *outBaseZ)
{
    if (!player || !outBaseX || !outBaseZ ||
        !isfinite(player->position.x) || !isfinite(player->position.z) ||
        !isfinite(player->yaw)) {
        return false;
    }

    double floorX = floor((double)player->position.x);
    double floorZ = floor((double)player->position.z);
    if (floorX < (double)INT_MIN || floorX > (double)INT_MAX ||
        floorZ < (double)INT_MIN || floorZ > (double)INT_MAX) {
        return false;
    }

    int64_t baseX = (int64_t)floorX + (int64_t)rowDx * 6 -
                    (int64_t)colDx * (targetWidth / 2);
    int64_t baseZ = (int64_t)floorZ + (int64_t)rowDz * 6 -
                    (int64_t)colDz * (targetWidth / 2);
    const int pxValues[2] = { 0, targetWidth - 1 };
    const int pyValues[2] = { 0, targetHeight - 1 };
    for (int pyIndex = 0; pyIndex < 2; pyIndex++) {
        for (int pxIndex = 0; pxIndex < 2; pxIndex++) {
            int64_t worldX = baseX + (int64_t)colDx * pxValues[pxIndex] +
                             (int64_t)rowDx * pyValues[pyIndex];
            int64_t worldZ = baseZ + (int64_t)colDz * pxValues[pxIndex] +
                             (int64_t)rowDz * pyValues[pyIndex];
            if (worldX < INT_MIN || worldX > INT_MAX ||
                worldZ < INT_MIN || worldZ > INT_MAX) {
                return false;
            }
        }
    }

    *outBaseX = (int)baseX;
    *outBaseZ = (int)baseZ;
    return true;
}

void ImportImageAsBlocks(const char *path, const Player *player, int maxBlocks, bool relief)
{
    if (terrainMode != TERRAIN_FLAT) {
        SetImportMessage("Image import is only available in Flat terrain mode.");
        return;
    }
    if (!player || !isfinite(player->position.x) ||
        !isfinite(player->position.z) || !isfinite(player->yaw)) {
        SetImportMessage("Player position is invalid for image import.");
        return;
    }

    maxBlocks = ClampImportPrecision(maxBlocks);

    char localPath[1024] = { 0 };
    const char *imagePath = ResolveDroppedImagePath(path, localPath, sizeof(localPath));

    if (!imagePath) {
        char normalized[96] = { 0 };
        NormalizeDroppedPath(path, normalized, sizeof(normalized));
        SetImportMessage(TextFormat("No image file path found: %.80s", normalized[0] ? normalized : "(empty path)"));
        return;
    }

    if (!IsSupportedImageFile(imagePath)) {
        SetImportMessage("Unsupported image type. Try PNG, JPG, BMP, TGA, GIF, QOI, PSD, or HDR.");
        return;
    }

    int fileBytes = GetFileLength(imagePath);
    if (fileBytes < 0 || fileBytes > IMPORT_MAX_FILE_BYTES) {
        SetImportMessage("Image file is too large to import safely.");
        return;
    }

    Image image = LoadImage(imagePath);
    if (image.data == NULL || image.width <= 0 || image.height <= 0) {
        if (image.data != NULL) UnloadImage(image);
        SetImportMessage("Image decode failed. Try exporting it as a standard PNG or JPG.");
        return;
    }

    ImageImportPlan plan;
    if (!BuildImageImportPlan(image.width, image.height, maxBlocks,
                              relief, &plan)) {
        UnloadImage(image);
        SetImportMessage("Image dimensions exceed the safe import budget.");
        return;
    }

    float forwardX = sinf(player->yaw);
    float forwardZ = cosf(player->yaw);
    int rowDx = 0;
    int rowDz = 0;
    int colDx = 0;
    int colDz = 0;

    if (fabsf(forwardX) > fabsf(forwardZ)) {
        int sx = forwardX >= 0.0f ? 1 : -1;
        rowDx = sx;
        colDz = -sx;
    } else {
        int sz = forwardZ >= 0.0f ? 1 : -1;
        rowDz = sz;
        colDx = sz;
    }

    int baseX = 0;
    int baseZ = 0;
    if (!ImportPlacementBase(player, plan.targetWidth, plan.targetHeight,
                             rowDx, rowDz, colDx, colDz, &baseX, &baseZ)) {
        UnloadImage(image);
        SetImportMessage("Player position is outside the safe import range.");
        return;
    }

    if (image.width != plan.targetWidth || image.height != plan.targetHeight) {
        ImageResizeNN(&image, plan.targetWidth, plan.targetHeight);
        if (image.data == NULL || image.width != plan.targetWidth ||
            image.height != plan.targetHeight) {
            if (image.data != NULL) UnloadImage(image);
            SetImportMessage("Image resize failed.");
            return;
        }
    }

    int targetWidth = plan.targetWidth;
    int targetHeight = plan.targetHeight;
    int placed = 0;
    int minChunkX = 0;
    int maxChunkX = 0;
    int minChunkZ = 0;
    int maxChunkZ = 0;
    bool haveTouchedChunk = false;

    WorldBeginUndoGroup();
    for (int py = 0; py < targetHeight; py++) {
        for (int px = 0; px < targetWidth; px++) {
            Color pixel = GetImageColor(image, px, py);
            int wx = baseX + colDx * px + rowDx * py;
            int wz = baseZ + colDz * px + rowDz * py;
            int wy = TerrainHeight(wx, wz, terrainMode);
            if (relief) {
                int baseY = wy + 1;
                for (int y = baseY; y < WORLD_HEIGHT; y++) {
                    SetBlockForImport(wx, y, wz, BLOCK_AIR);
                }

                if (pixel.a >= 32) {
                    BlockType type = NearestImageBlock(pixel);
                    int columnHeight = ReliefHeightForPixel(pixel, baseY);
                    for (int y = baseY; y < baseY + columnHeight && InHeight(y); y++) {
                        if (SetBlockForImport(wx, y, wz, type)) placed++;
                    }
                }
            } else {
                if (SetBlockForImport(
                        wx, wy, wz,
                        pixel.a < 32 ? BLOCK_GRASS : NearestImageBlock(pixel))) {
                    placed++;
                }
            }

            int cx = 0;
            int cz = 0;
            int lx = 0;
            int lz = 0;
            WorldToChunkLocal(wx, wz, &cx, &cz, &lx, &lz);
            if (!haveTouchedChunk) {
                minChunkX = maxChunkX = cx;
                minChunkZ = maxChunkZ = cz;
                haveTouchedChunk = true;
            } else {
                if (cx < minChunkX) minChunkX = cx;
                if (cx > maxChunkX) maxChunkX = cx;
                if (cz < minChunkZ) minChunkZ = cz;
                if (cz > maxChunkZ) maxChunkZ = cz;
            }
        }
    }
    WorldEndUndoGroup();

    if (haveTouchedChunk) {
        for (int cz = minChunkZ - 1; cz <= maxChunkZ + 1; cz++) {
            for (int cx = minChunkX - 1; cx <= maxChunkX + 1; cx++) {
                MarkChunkDirty(cx, cz);
            }
        }
    }

    UnloadImage(image);
    SetImportMessage(TextFormat("Imported %dx%d %s blocks (%d total), precision %d.",
                                targetWidth, targetHeight, relief ? "relief" : "flat", placed, maxBlocks));
}

void HandleImageDrop(const Player *player, int maxBlocks, bool relief)
{
    if (!IsFileDropped()) return;

    FilePathList dropped = LoadDroppedFiles();
    if (dropped.count > 0) ImportImageAsBlocks(dropped.paths[0], player, maxBlocks, relief);
    UnloadDroppedFiles(dropped);
}

void AppendImportText(ImportDialog *dialog, const char *text)
{
    if (!text || !text[0]) return;

    size_t current = strlen(dialog->path);
    size_t room = sizeof(dialog->path) - current - 1;
    if (room == 0) return;
    strncat(dialog->path, text, room);
}

void OpenImportDialog(ImportDialog *dialog)
{
    if (terrainMode != TERRAIN_FLAT) {
        SetImportMessage("Image import is only available in Flat terrain mode.");
        return;
    }

    dialog->open = true;
    if (dialog->maxBlocks == 0) dialog->maxBlocks = IMPORT_DEFAULT_BLOCKS;
    dialog->maxBlocks = ClampImportPrecision(dialog->maxBlocks);
    dialog->path[0] = '\0';
    SetImportMessage("Type or paste an image path. Tab toggles relief mode.");
}

void UpdateImportDialog(ImportDialog *dialog, const Player *player, bool *cursorReleased)
{
    if (!dialog->open) return;

    int key = GetCharPressed();
    while (key > 0) {
        if (key >= 32 && key <= 126 && key != '[' && key != ']') {
            char text[2] = { (char)key, '\0' };
            AppendImportText(dialog, text);
        }
        key = GetCharPressed();
    }

    bool ctrlDown = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    if (ctrlDown && IsKeyPressed(KEY_V)) {
        AppendImportText(dialog, GetClipboardText());
    }

    if (IsKeyPressed(KEY_BACKSPACE)) {
        size_t length = strlen(dialog->path);
        if (length > 0) dialog->path[length - 1] = '\0';
    }

    int precisionStep = (IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT)) ?
                        IMPORT_PRECISION_BIG_STEP : IMPORT_PRECISION_STEP;
    if (IsKeyPressed(KEY_LEFT_BRACKET) || IsKeyPressed(KEY_KP_SUBTRACT)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, -precisionStep);
    }
    if (IsKeyPressed(KEY_RIGHT_BRACKET) || IsKeyPressed(KEY_KP_ADD)) {
        dialog->maxBlocks = AdjustImportPrecision(dialog->maxBlocks, precisionStep);
    }
    if (IsKeyPressed(KEY_TAB)) dialog->relief = !dialog->relief;

    if (IsKeyPressed(KEY_ENTER)) {
        ImportImageAsBlocks(dialog->path, player, dialog->maxBlocks, dialog->relief);
        dialog->open = false;
        *cursorReleased = false;
        DisableCursor();
    } else if (IsKeyPressed(KEY_ESCAPE)) {
        dialog->open = false;
        SetImportMessage("Image import canceled.");
        *cursorReleased = false;
        DisableCursor();
    }
}

static float RayAABBEnter(Vector3 origin, Vector3 direction, Vector3 min, Vector3 max)
{
    float t0 = 0.0f;
    float t1 = INFINITY;

    if (fabsf(direction.x) < 0.0001f) {
        if (origin.x < min.x || origin.x > max.x) return -1.0f;
    } else {
        float inv = 1.0f / direction.x;
        float ta = (min.x - origin.x) * inv;
        float tb = (max.x - origin.x) * inv;
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        t0 = fmaxf(t0, ta);
        t1 = fminf(t1, tb);
    }
    if (fabsf(direction.y) < 0.0001f) {
        if (origin.y < min.y || origin.y > max.y) return -1.0f;
    } else {
        float inv = 1.0f / direction.y;
        float ta = (min.y - origin.y) * inv;
        float tb = (max.y - origin.y) * inv;
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        t0 = fmaxf(t0, ta);
        t1 = fminf(t1, tb);
    }
    if (fabsf(direction.z) < 0.0001f) {
        if (origin.z < min.z || origin.z > max.z) return -1.0f;
    } else {
        float inv = 1.0f / direction.z;
        float ta = (min.z - origin.z) * inv;
        float tb = (max.z - origin.z) * inv;
        if (ta > tb) { float tmp = ta; ta = tb; tb = tmp; }
        t0 = fmaxf(t0, ta);
        t1 = fminf(t1, tb);
    }

    if (t0 > t1 || t1 < 0.0f) return -1.0f;
    return t0;
}

float RaycastCameraOcclusion(Vector3 origin, Vector3 direction, float maxDistance)
{
    if (!RaycastPrepare(origin, &direction, maxDistance)) return -1.0f;
    Vector3 pos = origin;
    int x = (int)floorf(pos.x);
    int y = (int)floorf(pos.y);
    int z = (int)floorf(pos.z);

    int stepX = (direction.x > 0.0f) ? 1 : -1;
    int stepY = (direction.y > 0.0f) ? 1 : -1;
    int stepZ = (direction.z > 0.0f) ? 1 : -1;

    float nextBoundaryX = (direction.x > 0.0f) ? (float)(x + 1) : (float)x;
    float nextBoundaryY = (direction.y > 0.0f) ? (float)(y + 1) : (float)y;
    float nextBoundaryZ = (direction.z > 0.0f) ? (float)(z + 1) : (float)z;

    float tMaxX = (fabsf(direction.x) < 0.0001f) ? INFINITY : (nextBoundaryX - pos.x) / direction.x;
    float tMaxY = (fabsf(direction.y) < 0.0001f) ? INFINITY : (nextBoundaryY - pos.y) / direction.y;
    float tMaxZ = (fabsf(direction.z) < 0.0001f) ? INFINITY : (nextBoundaryZ - pos.z) / direction.z;

    float tDeltaX = (fabsf(direction.x) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.x);
    float tDeltaY = (fabsf(direction.y) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.y);
    float tDeltaZ = (fabsf(direction.z) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.z);

    float travelled = 0.0f;
    for (unsigned step = 0; step < RAYCAST_MAX_STEPS; step++) {
        BlockType type = GetBlockAt(x, y, z);
        if (type != BLOCK_AIR && !IsTranslucentBlock(type)) {
            float height = BlockCollisionHeight(type);
            if (height > 0.0f) {
                float hitT = RayAABBEnter(origin, direction,
                                          (Vector3){ (float)x, (float)y, (float)z },
                                          (Vector3){ (float)x + 1.0f, (float)y + height, (float)z + 1.0f });
                if (hitT >= 0.0f) {
                    if (hitT > maxDistance) return -1.0f;
                    return hitT;
                }
            }
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            if (!RaycastAdvanceCoordinate(&x, stepX)) break;
            travelled = tMaxX;
            tMaxX += tDeltaX;
        } else if (tMaxY < tMaxZ) {
            if (!RaycastAdvanceCoordinate(&y, stepY)) break;
            travelled = tMaxY;
            tMaxY += tDeltaY;
        } else {
            if (!RaycastAdvanceCoordinate(&z, stepZ)) break;
            travelled = tMaxZ;
            tMaxZ += tDeltaZ;
        }
        if (travelled > maxDistance) break;
    }
    return -1.0f;
}

HitResult RaycastBlocksFiltered(Vector3 origin, Vector3 direction,
                                float maxDistance, unsigned mask)
{
    HitResult result = { 0 };
    if ((mask & RAYCAST_BLOCK_ALL) == 0u) return result;
    if (!RaycastPrepare(origin, &direction, maxDistance)) return result;
    Vector3 pos = origin;
    int x = (int)floorf(pos.x);
    int y = (int)floorf(pos.y);
    int z = (int)floorf(pos.z);

    int stepX = (direction.x > 0.0f) ? 1 : -1;
    int stepY = (direction.y > 0.0f) ? 1 : -1;
    int stepZ = (direction.z > 0.0f) ? 1 : -1;

    float nextBoundaryX = (direction.x > 0.0f) ? (float)(x + 1) : (float)x;
    float nextBoundaryY = (direction.y > 0.0f) ? (float)(y + 1) : (float)y;
    float nextBoundaryZ = (direction.z > 0.0f) ? (float)(z + 1) : (float)z;

    float tMaxX = (fabsf(direction.x) < 0.0001f) ? INFINITY : (nextBoundaryX - pos.x) / direction.x;
    float tMaxY = (fabsf(direction.y) < 0.0001f) ? INFINITY : (nextBoundaryY - pos.y) / direction.y;
    float tMaxZ = (fabsf(direction.z) < 0.0001f) ? INFINITY : (nextBoundaryZ - pos.z) / direction.z;

    float tDeltaX = (fabsf(direction.x) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.x);
    float tDeltaY = (fabsf(direction.y) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.y);
    float tDeltaZ = (fabsf(direction.z) < 0.0001f) ? INFINITY : fabsf(1.0f / direction.z);

    int lastNx = 0;
    int lastNy = 0;
    int lastNz = 0;
    float travelled = 0.0f;

    for (unsigned step = 0;
         step < RAYCAST_MAX_STEPS && travelled <= maxDistance; step++) {
        if (WorldBlockRegionAt(y) == WORLD_BLOCK_REGION_SPACE &&
            !SpaceBlockReadyAt(x, y, z)) {
            return result;
        }
        BlockType block = GetBlockAt(x, y, z);
        bool liquid = IsLiquidBlock(block);
        bool accepted = block != BLOCK_AIR &&
                        ((liquid && (mask & RAYCAST_BLOCK_LIQUID) != 0u) ||
                         (!liquid && (mask & RAYCAST_BLOCK_SOLID) != 0u));
        if (accepted) {
            result.hit = true;
            result.x = x;
            result.y = y;
            result.z = z;
            result.nx = lastNx;
            result.ny = lastNy;
            result.nz = lastNz;
            return result;
        }

        if (tMaxX < tMaxY && tMaxX < tMaxZ) {
            if (!RaycastAdvanceCoordinate(&x, stepX)) break;
            travelled = tMaxX;
            tMaxX += tDeltaX;
            lastNx = -stepX;
            lastNy = 0;
            lastNz = 0;
        } else if (tMaxY < tMaxZ) {
            if (!RaycastAdvanceCoordinate(&y, stepY)) break;
            travelled = tMaxY;
            tMaxY += tDeltaY;
            lastNx = 0;
            lastNy = -stepY;
            lastNz = 0;
        } else {
            if (!RaycastAdvanceCoordinate(&z, stepZ)) break;
            travelled = tMaxZ;
            tMaxZ += tDeltaZ;
            lastNx = 0;
            lastNy = 0;
            lastNz = -stepZ;
        }
    }

    return result;
}

HitResult RaycastBlocks(Vector3 origin, Vector3 direction, float maxDistance)
{
    return RaycastBlocksFiltered(origin, direction, maxDistance,
                                 RAYCAST_BLOCK_ALL);
}

bool BlockWouldOverlapPlayer(int x, int y, int z, Vector3 playerPosition)
{
    WorldBlockRegion region = WorldBlockRegionAt(y);
    bool inSpace = region == WORLD_BLOCK_REGION_SPACE;
    if (region == WORLD_BLOCK_REGION_NONE) return true;
    if (inSpace && !SpaceBlockReadyAt(x, y, z)) return true;

    float minX = (float)x;
    float maxX = (float)x + 1.0f;
    float minY = (float)y;
    float maxY = (float)y + BlockCollisionHeight(GetBlockAt(x, y, z));
    float minZ = (float)z;
    float maxZ = (float)z + 1.0f;

    float playerMinX = playerPosition.x - PLAYER_RADIUS;
    float playerMaxX = playerPosition.x + PLAYER_RADIUS;
    float playerMinY = playerPosition.y;
    float playerMaxY = playerPosition.y + PLAYER_HEIGHT;
    float playerMinZ = playerPosition.z - PLAYER_RADIUS;
    float playerMaxZ = playerPosition.z + PLAYER_RADIUS;

    return playerMaxX > minX && playerMinX < maxX &&
           playerMaxY > minY && playerMinY < maxY &&
           playerMaxZ > minZ && playerMinZ < maxZ;
}
