#include "app/game_save.h"

#include "world/surface_save.h"
#include "world/radiation_runtime.h"
#include "world/hydrology_runtime.h"
#include "world/save_format.h"
#include "world/volcano_runtime.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"
#include "gameplay/inventory.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern bool GameSaveTestWriteRadiationRuntimeState(FILE *file);
extern const char *GameSaveTestLoadRadiationRuntimeState(
    FILE *file, WorldSaveFormat format);

static int radiationResetCalls;
bool WorldSaveFormatHasVolcanoes(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V24 || format == WORLD_SAVE_FORMAT_V25 || format == WORLD_SAVE_FORMAT_V26 || format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29;
}

bool WorldSaveFormatHasLava(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V25 || format == WORLD_SAVE_FORMAT_V26 || format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29; }
bool WorldSaveFormatHasAsh(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29; }
bool WorldSaveFormatHasInventoryRemainders(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29; }
bool IsColorBlock(BlockType type) { return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END; }
bool IsValidBlockType(BlockType type) { return (type >= BLOCK_AIR && type <= BLOCK_NATURAL_END) || IsColorBlock(type); }
static bool RemoveFractionSource(void *user) { (void)user; return true; }
static void TestRemainderPayloadRouting(void)
{
    InventoryReset();
    assert(InventoryCollectVolume(BLOCK_VOLCANIC_ASH, 513, RemoveFractionSource, NULL));
    FILE *file = tmpfile(); assert(file && InventorySaveRemainders(file));
    assert(fputs("TAIL", file) >= 0); rewind(file); InventoryResetRemainders();
    assert(GameSaveTestLoadInventoryRemainders(file, WORLD_SAVE_FORMAT_V28) == NULL);
    assert(InventoryRemainder(BLOCK_VOLCANIC_ASH) == 513);
    char tail[4]; assert(fread(tail, 1, 4, file) == 4 && !memcmp(tail, "TAIL", 4)); fclose(file);
    file = tmpfile(); assert(file); fputs("bad fractions", file); rewind(file);
    assert(GameSaveTestLoadInventoryRemainders(file, WORLD_SAVE_FORMAT_V28) != NULL);
    assert(InventoryRemainder(BLOCK_VOLCANIC_ASH) == 513);
    for (WorldSaveFormat format = WORLD_SAVE_FORMAT_V18; format <= WORLD_SAVE_FORMAT_V27; format++) {
        rewind(file);
        assert(GameSaveTestLoadInventoryRemainders(file, format) == NULL && ftell(file) == 0);
        assert(InventoryRemainder(BLOCK_VOLCANIC_ASH) == 0);
        assert(InventoryCollectVolume(BLOCK_VOLCANIC_ASH, 513, RemoveFractionSource, NULL));
    }
    fclose(file); InventoryReset();
}
static AshBoundary AshOpen(int x, int y, int z, void *user)
{
    (void)x; (void)y; (void)z; (void)user; return ASH_OPEN;
}
static void TestAshPayloadRouting(void)
{
    AshRuntimeReset();
    AshContext ctx = { .valid = true, .bodyId = 7u, .boundary = AshOpen };
    assert(AshRuntimeDeposit(&ctx, 1, 20, 1, 512u) == 512u);
    FILE *file = tmpfile(); assert(file && AshRuntimeSave(file));
    assert(fputs("TAIL", file) >= 0);
    rewind(file); AshRuntimeReset();
    assert(GameSaveTestLoadAshState(file, WORLD_SAVE_FORMAT_V27) == NULL);
    char tail[4]; assert(fread(tail, 1, 4, file) == 4 && !memcmp(tail, "TAIL", 4));
    assert(AshRuntimeStats().volume == 512u); fclose(file);
    file = tmpfile(); assert(file); fputs("bad ash!", file); rewind(file);
    assert(GameSaveTestLoadAshState(file, WORLD_SAVE_FORMAT_V27) != NULL);
    assert(AshRuntimeStats().volume == 512u);
    for (WorldSaveFormat format = WORLD_SAVE_FORMAT_V18; format <= WORLD_SAVE_FORMAT_V26; format++) {
        rewind(file);
        assert(GameSaveTestLoadAshState(file, format) == NULL);
        assert(ftell(file) == 0 && AshRuntimeStats().cells == 0u);
        assert(AshRuntimeDeposit(&ctx, 1, 20, 1, 512u) == 512u);
    }
    fclose(file); AshRuntimeReset();
}
static LavaBoundary LavaOpen(int x, int y, int z, void *user)
{
    (void)x; (void)y; (void)z; (void)user; return LAVA_OPEN;
}
static void TestLavaPayloadRouting(void)
{
    LavaRuntimeReset();
    LavaContext ctx = { .valid = true, .bodyId = 7u, .boundary = LavaOpen };
    assert(LavaRuntimeDeposit(&ctx, 1, 20, 1, 512u, 1450.0) == 512u);
    FILE *file = tmpfile(); assert(file && LavaRuntimeSave(file));
    const uint32_t marker = 0x12345678u;
    assert(fwrite(&marker, sizeof(marker), 1u, file) == 1u);
    rewind(file); LavaRuntimeReset();
    assert(GameSaveTestLoadLavaState(file, WORLD_SAVE_FORMAT_V26) == NULL);
    uint32_t tail = 0; assert(fread(&tail, sizeof(tail), 1u, file) == 1u && tail == marker);
    assert(LavaRuntimeStats().volume == 512u); fclose(file);
    file = tmpfile(); assert(file); fputs("bad lava", file); rewind(file);
    assert(GameSaveTestLoadLavaState(file, WORLD_SAVE_FORMAT_V26) != NULL);
    assert(LavaRuntimeStats().volume == 512u); rewind(file);
    assert(GameSaveTestLoadLavaState(file, WORLD_SAVE_FORMAT_V24) == NULL);
    assert(ftell(file) == 0 && LavaRuntimeStats().records == 0u); fclose(file);
}

static void TestVolcanoPayloadRouting(void)
{
    VolcanoRuntimeReset();
    VolcanoDefinition d = VolcanoDefine(7u, 123u, 24, 20, 0, 0.8f, true);
    assert(VolcanoRuntimeForce(d, 0.8f, 100u));
    FILE *file = tmpfile();
    assert(file && VolcanoRuntimeSave(file));
    const uint32_t sentinel = 0x12345678u;
    assert(fwrite(&sentinel, sizeof(sentinel), 1u, file) == 1u);
    rewind(file);
    VolcanoRuntimeReset();
    assert(GameSaveTestLoadVolcanoState(file, WORLD_SAVE_FORMAT_V24) == NULL);
    uint32_t trailing = 0u;
    assert(fread(&trailing, sizeof(trailing), 1u, file) == 1u && trailing == sentinel);
    assert(VolcanoRuntimeStats().count == 1u);
    fclose(file);
    file = tmpfile();
    assert(file);
    fputs("broken volcano payload", file);
    rewind(file);
    assert(GameSaveTestLoadVolcanoState(file, WORLD_SAVE_FORMAT_V24) != NULL);
    assert(VolcanoRuntimeStats().count == 1u);
    rewind(file);
    assert(GameSaveTestLoadVolcanoState(file, WORLD_SAVE_FORMAT_V23) == NULL);
    assert(ftell(file) == 0 && VolcanoRuntimeStats().count == 0u);
    fclose(file);
}

bool WorldSaveFormatHasRadiationRuntime(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V21 ||
           format == WORLD_SAVE_FORMAT_V22;
}

bool WorldSaveFormatHasHydrologyRuntime(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V22;
}

bool RadiationRuntimeGlobalSaveState(FILE *file)
{
    const uint32_t marker = 0x52545354u; /* "RTST" */
    return file && fwrite(&marker, sizeof(marker), 1, file) == 1;
}

bool RadiationRuntimeGlobalLoadState(FILE *file)
{
    uint32_t marker = 0u;
    const uint32_t expected = 0x52545354u;
    return file && fread(&marker, sizeof(marker), 1, file) == 1 &&
           marker == expected;
}

bool RadiationRuntimeGlobalReset(const RadiationRuntimeConfig *config)
{
    (void)config;
    radiationResetCalls++;
    return true;
}

static uint32_t currentPlanetId = 77u;
static int currentPlanetOriginX = 1000;
static int currentPlanetOriginZ = 4090;

uint32_t PlanetWorldSeed(void)
{
    return currentPlanetId;
}

int PlanetWorldOriginX(void)
{
    return currentPlanetOriginX;
}

int PlanetWorldOriginZ(void)
{
    return currentPlanetOriginZ;
}

bool WorldIsSurfaceDimension(WorldDimension dimension)
{
    return dimension == WORLD_DIMENSION_HOME ||
           dimension == WORLD_DIMENSION_PLANET;
}

static bool Near(float a, float b)
{
    return fabsf(a - b) < 0.01f;
}

static void WriteAddress(FILE *file, SurfaceAddress address)
{
    uint32_t face = (uint32_t)address.face;
    int32_t u = (int32_t)address.u;
    int32_t v = (int32_t)address.v;
    int32_t radial = (int32_t)address.radial;
    assert(fwrite(&address.bodyId, sizeof(address.bodyId), 1, file) == 1);
    assert(fwrite(&face, sizeof(face), 1, file) == 1);
    assert(fwrite(&u, sizeof(u), 1, file) == 1);
    assert(fwrite(&v, sizeof(v), 1, file) == 1);
    assert(fwrite(&radial, sizeof(radial), 1, file) == 1);
}

static FILE *WriteSchemaOneTrailer(
    SurfaceAddress playerAddress, const SurfaceAddress *editAddresses,
    uint32_t editCount)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    uint32_t schema = 1u;
    uint8_t hasPlayer = 1u;
    assert(fwrite(&schema, sizeof(schema), 1, file) == 1);
    assert(fwrite(&hasPlayer, sizeof(hasPlayer), 1, file) == 1);
    WriteAddress(file, playerAddress);
    assert(fwrite(&editCount, sizeof(editCount), 1, file) == 1);
    for (uint32_t index = 0u; index < editCount; index++) {
        WriteAddress(file, editAddresses[index]);
    }
    rewind(file);
    return file;
}

static void TestSchemaOneRejected(void)
{
    currentPlanetId = 77u;
    currentPlanetOriginX = 1000;
    currentPlanetOriginZ = 4090;
    Player player = {
        .position = { 32.25f, 70.8f, 20.75f },
        .yaw = 0.4f
    };
    BlockEdit edits[2] = {
        { 5, 12, -9, BLOCK_STONE },
        { SURFACE_EQUATOR_BLOCKS + 3, 4, -2, BLOCK_GLASS }
    };
    uint32_t dimensions[2] = { 77u, 0u };
    SurfaceAddress editAddresses[2] = {
        SurfaceAddressFromMapCoordinates(
            77u, (float)(currentPlanetOriginX + edits[0].x),
            (float)(currentPlanetOriginZ + edits[0].z), edits[0].y),
        SurfaceAddressFromMapCoordinates(
            0u, (float)edits[1].x, (float)edits[1].z, edits[1].y)
    };
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        77u, (float)currentPlanetOriginX + player.position.x,
        (float)currentPlanetOriginZ + player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaOneTrailer(
        playerAddress, editAddresses, 2u);

    SurfaceAddress *loadedAddresses = NULL;
    SurfaceMapCell *loadedCells = NULL;
    assert(!GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, edits, dimensions, 2,
        &loadedAddresses, &loadedCells));
    assert(Near(player.position.x, 32.25f));
    assert(Near(player.position.z, 20.75f));
    assert(Near(player.yaw, 0.4f));
    assert(edits[0].x == 5 && edits[0].z == -9);
    assert(edits[1].x == SURFACE_EQUATOR_BLOCKS + 3 && edits[1].z == -2);
    assert(!loadedAddresses && !loadedCells);
    fclose(file);
}

static FILE *WriteSchemaTwoTrailer(
    SurfaceAddress playerAddress, const SurfaceAddress *editAddresses,
    const SurfaceMapCell *editCells, uint32_t editCount)
{
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(SurfaceSaveWriteTrailer(
        file, true, playerAddress, editAddresses, editCells, editCount));
    rewind(file);
    return file;
}

static void TestSchemaTwoGlobalCoordinates(void)
{
    currentPlanetId = 91u;
    Player player = {
        .position = {
            (float)SURFACE_EQUATOR_BLOCKS + 100.25f, 50.0f, -200.5f
        },
        .yaw = -0.25f
    };
    BlockEdit edit = {
        SURFACE_EQUATOR_BLOCKS + 73, 8, -211, BLOCK_BRICK
    };
    uint32_t dimension = 91u;
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)edit.x, (float)edit.z);
    SurfaceAddress editAddress = SurfaceAddressFromMapCoordinates(
        91u, (float)cell.x, (float)cell.z, edit.y);
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        91u, player.position.x, player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaTwoTrailer(
        playerAddress, &editAddress, &cell, 1u);

    SurfaceAddress *loadedAddresses = NULL;
    SurfaceMapCell *loadedCells = NULL;
    assert(GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, &edit, &dimension, 1,
        &loadedAddresses, &loadedCells));
    assert(Near(player.position.x, 100.25f));
    assert(Near(player.position.z, -200.5f));
    assert(Near(player.yaw, -0.25f));
    assert(edit.x == 73 && edit.z == -211);
    assert(loadedCells[0].x == 73 && loadedCells[0].z == -211);
    free(loadedAddresses);
    free(loadedCells);
    fclose(file);
}

static void TestRejectsMismatchedEditBodyTransactionally(void)
{
    currentPlanetId = 77u;
    Player player = { .position = { 4.0f, 20.0f, 8.0f } };
    BlockEdit edit = { 9, 3, 12, BLOCK_STONE };
    uint32_t wrongDimension = 88u;
    SurfaceMapCell cell = SurfaceCanonicalMapCell(
        (float)edit.x, (float)edit.z);
    SurfaceAddress editAddress = SurfaceAddressFromMapCoordinates(
        77u, (float)cell.x, (float)cell.z, edit.y);
    SurfaceAddress playerAddress = SurfaceAddressFromMapCoordinates(
        77u, player.position.x, player.position.z,
        (int)floorf(player.position.y));
    FILE *file = WriteSchemaTwoTrailer(
        playerAddress, &editAddress, &cell, 1u);

    SurfaceAddress *loadedAddresses = (SurfaceAddress *)1;
    SurfaceMapCell *loadedCells = (SurfaceMapCell *)1;
    assert(!GameSaveTestLoadSphericalTrailer(
        file, WORLD_DIMENSION_PLANET, &player, &edit, &wrongDimension, 1,
        &loadedAddresses, &loadedCells));
    assert(loadedAddresses == NULL);
    assert(loadedCells == NULL);
    assert(Near(player.position.x, 4.0f));
    assert(Near(player.position.z, 8.0f));
    fclose(file);
}

static void TestRadiationRuntimePayloadRouting(void)
{
    radiationResetCalls = 0;
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(GameSaveTestWriteRadiationRuntimeState(file));
    const unsigned char trailing = 0xa5u;
    assert(fwrite(&trailing, sizeof(trailing), 1, file) == 1);
    rewind(file);
    assert(GameSaveTestLoadRadiationRuntimeState(
               file, WORLD_SAVE_FORMAT_V21) == NULL);
    assert(radiationResetCalls == 0);
    assert(fgetc(file) == trailing);
    assert(fgetc(file) == EOF);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    const unsigned char legacyByte = 0x3cu;
    assert(fwrite(&legacyByte, sizeof(legacyByte), 1, file) == 1);
    rewind(file);
    assert(GameSaveTestLoadRadiationRuntimeState(
               file, WORLD_SAVE_FORMAT_V20) == NULL);
    assert(radiationResetCalls == 1);
    assert(fgetc(file) == legacyByte);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    rewind(file);
    assert(strcmp(GameSaveTestLoadRadiationRuntimeState(
                     file, WORLD_SAVE_FORMAT_V21),
                 "Load failed: radiation state is corrupted.") == 0);
    assert(radiationResetCalls == 1);
    fclose(file);
}

static void TestHydrologyRuntimePayloadRouting(void)
{
    assert(HydrologyRuntimeGlobalReset(NULL));
    assert(HydrologyRuntimeGlobalBind(4u, 0, 0));
    HydrologyRuntimeSnapshot beforeBad = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&beforeBad));
    HydrologyRuntimeCellInput input = {
        .elevationM = 3.0f,
        .soilDepthM = 1.0f,
        .precipitationMPerS = 0.000001f
    };
    assert(HydrologyRuntimeGlobalSetCellInput(0, 0, input));
    unsigned steps = 0u;
    assert(HydrologyRuntimeGlobalAdvance(1.0, &steps));
    assert(steps == HYDROLOGY_RUNTIME_DEFAULT_MAX_CATCH_UP_STEPS);
    HydrologyRuntimeSnapshot before = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&before));

    FILE *file = tmpfile();
    assert(file != NULL);
    assert(GameSaveTestWriteHydrologyRuntimeState(file));
    const unsigned char trailing = 0x6bu;
    assert(fwrite(&trailing, sizeof(trailing), 1, file) == 1);
    rewind(file);
    assert(GameSaveTestLoadHydrologyRuntimeState(
               file, WORLD_SAVE_FORMAT_V22) == NULL);
    HydrologyRuntimeSnapshot after = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&after));
    assert(after.active && after.stepCount == before.stepCount);
    assert(after.location.surfaceId == before.location.surfaceId);
    assert(fgetc(file) == trailing);
    assert(fgetc(file) == EOF);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    const unsigned char legacyByte = 0x3cu;
    assert(fwrite(&legacyByte, sizeof(legacyByte), 1, file) == 1);
    rewind(file);
    assert(GameSaveTestLoadHydrologyRuntimeState(
               file, WORLD_SAVE_FORMAT_V21) == NULL);
    assert(fgetc(file) == legacyByte);
    HydrologyRuntimeSnapshot reset = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&reset));
    assert(!reset.active && reset.stepCount == 0u);
    fclose(file);

    assert(HydrologyRuntimeGlobalBind(4u, 0, 0));
    file = tmpfile();
    assert(file != NULL);
    assert(GameSaveTestWriteHydrologyRuntimeState(file));
    long end = ftell(file);
    assert(end > 1);
    assert(fflush(file) == 0);
    assert(ftruncate(fileno(file), end - 1) == 0);
    rewind(file);
    assert(strcmp(GameSaveTestLoadHydrologyRuntimeState(
                     file, WORLD_SAVE_FORMAT_V22),
                 "Load failed: hydrology state is corrupted.") == 0);
    HydrologyRuntimeSnapshot afterBad = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&afterBad));
    assert(afterBad.active == beforeBad.active &&
           afterBad.stepCount == beforeBad.stepCount &&
           afterBad.location.surfaceId == beforeBad.location.surfaceId);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    const unsigned char wrongMagic = 0xffu;
    assert(fwrite(&wrongMagic, sizeof(wrongMagic), 1, file) == 1);
    rewind(file);
    assert(strcmp(GameSaveTestLoadHydrologyRuntimeState(
                     file, WORLD_SAVE_FORMAT_V22),
                 "Load failed: hydrology state is corrupted.") == 0);
    fclose(file);
}

int main(void)
{
    TestRemainderPayloadRouting();
    TestAshPayloadRouting();
    TestLavaPayloadRouting();
    TestVolcanoPayloadRouting();
    TestSchemaOneRejected();
    TestSchemaTwoGlobalCoordinates();
    TestRejectsMismatchedEditBodyTransactionally();
    TestRadiationRuntimePayloadRouting();
    TestHydrologyRuntimePayloadRouting();
    HydrologyRuntimeGlobalFree();
    puts("game spherical save tests passed");
    return 0;
}
