#include "gameplay/interaction.h"
#include "space/space.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static int solidX = -1;
static int waterX = -1;
static int blockQueries = 0;
static int testFileBytes = 1024;
static int loadImageCalls = 0;
static int unloadImageCalls = 0;
static int undoBeginCalls = 0;
static int undoEndCalls = 0;
static int importedBlockWrites = 0;
static int dirtyChunkCalls = 0;
static unsigned char testImageData = 0u;
static Image nextImage = { 0 };
static char importMessage[160] = { 0 };

TerrainMode terrainMode = TERRAIN_FLAT;

TerrainMode WorldTerrainMode(void)
{
    return terrainMode;
}

void WorldSetTerrainMode(TerrainMode mode)
{
    terrainMode = mode;
}

bool FileExists(const char *fileName)
{
    return fileName && fileName[0] != '\0';
}

bool IsFileExtension(const char *fileName, const char *ext)
{
    (void)fileName;
    (void)ext;
    return true;
}

int GetFileLength(const char *fileName)
{
    (void)fileName;
    return testFileBytes;
}

Image LoadImage(const char *fileName)
{
    (void)fileName;
    loadImageCalls++;
    return nextImage;
}

void UnloadImage(Image image)
{
    assert(image.data != NULL);
    unloadImageCalls++;
}

void ImageResizeNN(Image *image, int newWidth, int newHeight)
{
    assert(image && image->data);
    image->width = newWidth;
    image->height = newHeight;
}

Color GetImageColor(Image image, int x, int y)
{
    (void)image;
    (void)x;
    (void)y;
    return (Color){ 0, 0, 0, 0 };
}

const char *TextFormat(const char *text, ...)
{
    static char formatted[256];
    va_list args;
    va_start(args, text);
    vsnprintf(formatted, sizeof(formatted), text, args);
    va_end(args);
    return formatted;
}

void GameNoticePost(const char *message)
{
    snprintf(importMessage, sizeof(importMessage), "%s",
             message ? message : "");
}

void WorldBeginUndoGroup(void)
{
    undoBeginCalls++;
}

void WorldEndUndoGroup(void)
{
    undoEndCalls++;
}

bool SetBlockForImport(int x, int y, int z, BlockType type)
{
    (void)x;
    (void)y;
    (void)z;
    (void)type;
    importedBlockWrites++;
    return true;
}

BlockType NearestImageBlock(Color color)
{
    (void)color;
    return BLOCK_STONE;
}

int TerrainHeight(int x, int z, TerrainMode mode)
{
    (void)x;
    (void)z;
    (void)mode;
    return 0;
}

bool InHeight(int y)
{
    return y >= 0 && y < WORLD_HEIGHT;
}

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    *cx = x / CHUNK_SIZE;
    *cz = z / CHUNK_SIZE;
    *lx = x % CHUNK_SIZE;
    *lz = z % CHUNK_SIZE;
}

void MarkChunkDirty(int cx, int cz)
{
    (void)cx;
    (void)cz;
    dirtyChunkCalls++;
}

BlockType GetBlockAt(int x, int y, int z)
{
    blockQueries++;
    if (x == waterX && y == 0 && z == 0) return BLOCK_WATER;
    return x == solidX && y == 0 && z == 0 ? BLOCK_STONE : BLOCK_AIR;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

float BlockCollisionHeight(BlockType type)
{
    return type == BLOCK_AIR ? 0.0f : 1.0f;
}

bool IsTranslucentBlock(BlockType type)
{
    (void)type;
    return false;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return true;
}

static void ResetRayWorld(void)
{
    solidX = -1;
    waterX = -1;
    blockQueries = 0;
}

static void ResetImportWorld(void)
{
    testFileBytes = 1024;
    loadImageCalls = 0;
    unloadImageCalls = 0;
    undoBeginCalls = 0;
    undoEndCalls = 0;
    importedBlockWrites = 0;
    dirtyChunkCalls = 0;
    nextImage = (Image){ 0 };
    importMessage[0] = '\0';
}

static void TestRaycastRejectsInvalidInput(void)
{
    ResetRayWorld();
    Vector3 origin = { 0.1f, 0.1f, 0.1f };
    Vector3 zero = { 0.0f, 0.0f, 0.0f };
    Vector3 nanDirection = { NAN, 0.0f, 0.0f };
    Vector3 nanOrigin = { NAN, 0.0f, 0.0f };
    Vector3 hugeOrigin = { (float)INT_MAX, 0.0f, 0.0f };

    assert(!RaycastBlocks(origin, zero, 4.0f).hit);
    assert(RaycastCameraOcclusion(origin, zero, 4.0f) < 0.0f);
    assert(!RaycastBlocks(origin, nanDirection, 4.0f).hit);
    assert(RaycastCameraOcclusion(nanOrigin, (Vector3){ 1.0f, 0.0f, 0.0f },
                                  4.0f) < 0.0f);
    assert(!RaycastBlocks(origin, (Vector3){ 1.0f, 0.0f, 0.0f }, -1.0f).hit);
    assert(RaycastCameraOcclusion(origin, (Vector3){ 1.0f, 0.0f, 0.0f },
                                  INFINITY) < 0.0f);
    assert(!RaycastBlocks(hugeOrigin, (Vector3){ 1.0f, 0.0f, 0.0f },
                          4.0f).hit);
    assert(blockQueries == 0);
}

static void TestRaycastNormalizesDirection(void)
{
    ResetRayWorld();
    solidX = 1;
    Vector3 origin = { 0.1f, 0.1f, 0.1f };
    HitResult hit = RaycastBlocks(
        origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 0.5f);
    assert(!hit.hit);
    assert(RaycastCameraOcclusion(
               origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 0.5f) < 0.0f);

    hit = RaycastBlocks(origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 2.0f);
    assert(hit.hit);
    assert(hit.x == 1 && hit.y == 0 && hit.z == 0);
    assert(hit.nx == -1 && hit.ny == 0 && hit.nz == 0);
    float occlusion = RaycastCameraOcclusion(
        origin, (Vector3){ 2.0f, 0.0f, 0.0f }, 2.0f);
    assert(fabsf(occlusion - 0.9f) < 0.0001f);
}

static void TestRaycastHasIterationLimit(void)
{
    ResetRayWorld();
    HitResult hit = RaycastBlocks(
        (Vector3){ 0.1f, 0.1f, 0.1f },
        (Vector3){ 1.0f, 0.0f, 0.0f }, FLT_MAX);
    assert(!hit.hit);
    assert(blockQueries <= 4096);

    blockQueries = 0;
    assert(RaycastCameraOcclusion(
               (Vector3){ 0.1f, 0.1f, 0.1f },
               (Vector3){ 1.0f, 0.0f, 0.0f }, FLT_MAX) < 0.0f);
    assert(blockQueries <= 4096);
}

static void TestRaycastCanIgnoreLiquids(void)
{
    ResetRayWorld();
    waterX = 1;
    solidX = 2;
    Vector3 origin = { 0.1f, 0.1f, 0.1f };
    Vector3 direction = { 1.0f, 0.0f, 0.0f };

    HitResult all = RaycastBlocks(origin, direction, 4.0f);
    assert(all.hit && all.x == 1);
    HitResult solid = RaycastBlocksFiltered(origin, direction, 4.0f,
                                            RAYCAST_BLOCK_SOLID);
    assert(solid.hit && solid.x == 2);
    assert(solid.nx == -1 && solid.ny == 0 && solid.nz == 0);
}

static void TestPlacementOverlapUsesPlacedBlock(void)
{
    Vector3 player = { 0.5f, 1.0f, 0.5f };
    assert(BlockWouldOverlapPlayer(0, 1, 0, BLOCK_STONE, player));
    assert(!BlockWouldOverlapPlayer(0, 0, 0, BLOCK_STONE, player));

    player.x = 1.0f - PLAYER_RADIUS;
    assert(!BlockWouldOverlapPlayer(1, 1, 0, BLOCK_STONE, player));
}

static void TestImageImportLimits(void)
{
    assert(ClampImportPrecision(INT_MIN) == IMPORT_MIN_BLOCKS);
    assert(ClampImportPrecision(INT_MAX) == IMPORT_MAX_BLOCKS);
    assert(AdjustImportPrecision(IMPORT_MIN_BLOCKS, INT_MIN) ==
           IMPORT_MIN_BLOCKS);
    assert(AdjustImportPrecision(IMPORT_MAX_BLOCKS, INT_MAX) ==
           IMPORT_MAX_BLOCKS);
    assert(AdjustImportPrecision(64, 16) == 80);

    ImageImportPlan plan = { 0 };
    assert(BuildImageImportPlan(1920, 1080, 64, false, &plan));
    assert(plan.targetWidth == 64 && plan.targetHeight == 36);
    assert(plan.sourcePixels == 1920u * 1080u);
    assert(plan.targetPixels == 64u * 36u);
    assert(plan.maximumBlockOperations == plan.targetPixels);

    assert(BuildImageImportPlan(256, 256, 256, false, &plan));
    assert(plan.targetPixels == IMPORT_MAX_TARGET_PIXELS);
    assert(!BuildImageImportPlan(256, 256, 256, true, &plan));
    assert(BuildImageImportPlan(64, 64, 64, true, &plan));
    assert(plan.maximumBlockOperations == IMPORT_MAX_BLOCK_OPERATIONS);

    assert(!BuildImageImportPlan(0, 64, 64, false, &plan));
    assert(!BuildImageImportPlan(INT_MAX, INT_MAX, 64, false, &plan));
    assert(!BuildImageImportPlan(IMPORT_MAX_SOURCE_DIMENSION,
                                 IMPORT_MAX_SOURCE_DIMENSION,
                                 64, false, &plan));
    assert(!BuildImageImportPlan(64, 64, 64, false, NULL));
}

static void TestImageImportFailureLifecycle(void)
{
    Player player = {
        .position = { 0.0f, 2.0f, 0.0f },
        .yaw = 0.0f
    };

    ResetImportWorld();
    testFileBytes = IMPORT_MAX_FILE_BYTES + 1;
    ImportImageAsBlocks("test.png", &player, 64, false);
    assert(loadImageCalls == 0);
    assert(undoBeginCalls == 0 && undoEndCalls == 0);

    ResetImportWorld();
    ImportImageAsBlocks("test.png", &player, 64, false);
    assert(loadImageCalls == 1 && unloadImageCalls == 0);
    assert(undoBeginCalls == 0 && undoEndCalls == 0);

    ResetImportWorld();
    nextImage = (Image){
        .data = &testImageData,
        .width = IMPORT_MAX_SOURCE_DIMENSION + 1,
        .height = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    ImportImageAsBlocks("test.png", &player, 64, false);
    assert(unloadImageCalls == 1);
    assert(undoBeginCalls == 0 && undoEndCalls == 0);
}

static void TestImageImportClosesUndoGroup(void)
{
    ResetImportWorld();
    Player player = {
        .position = { 0.0f, 2.0f, 0.0f },
        .yaw = 0.0f
    };
    nextImage = (Image){
        .data = &testImageData,
        .width = 1,
        .height = 1,
        .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8
    };
    ImportImageAsBlocks("test.png", &player, 64, true);
    assert(loadImageCalls == 1 && unloadImageCalls == 1);
    assert(undoBeginCalls == 1 && undoEndCalls == 1);
    assert(importedBlockWrites == WORLD_HEIGHT - 1);
    assert(dirtyChunkCalls > 0);
}

int main(void)
{
    TestRaycastRejectsInvalidInput();
    TestRaycastNormalizesDirection();
    TestRaycastHasIterationLimit();
    TestRaycastCanIgnoreLiquids();
    TestPlacementOverlapUsesPlacedBlock();
    TestImageImportLimits();
    TestImageImportFailureLifecycle();
    TestImageImportClosesUndoGroup();
    puts("interaction raycast tests passed");
    return 0;
}
