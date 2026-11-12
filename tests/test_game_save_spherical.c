#include "app/game_save.h"
#include "world/surface_topology_version.h"
#include "world/surface_cube.h"
#include "world/cube_address_save.h"
#include "world/chunks.h"
#include "world/surface_contract.h"
#include "world/block_edit_types.h"
#include "world/world_cell.h"

/* The production accessor returns the policy installed from the save header.
 * This fixture can switch to a cube policy so the cube section is actually
 * exercised; a permanently-legacy stub hid a write/read ordering bug. */
static bool testCubePolicy = false;

const SurfaceContract *WorldCurrentSurfaceContract(void)
{
    return NULL;
}

WorldSurfaceContext WorldCurrentSurfaceContext(void)
{
    return (WorldSurfaceContext){
        .active = true,
        .kind = WORLD_SURFACE_HOME,
        .topology = SURFACE_TOPOLOGY_CUBE_V1,
        .bodyId = 0u,
        .seed = DEFAULT_WORLD_SEED
    };
}

WorldSurfaceTopologyPolicy WorldCurrentTopologyPolicy(void)
{
    if (testCubePolicy) {
        return (WorldSurfaceTopologyPolicy){
            { SURFACE_TOPOLOGY_CUBE_GRID, SURFACE_CUBE_TOPOLOGY_VERSION },
            { SURFACE_TOPOLOGY_CUBE_GRID, SURFACE_CUBE_TOPOLOGY_VERSION }
        };
    }
    return (WorldSurfaceTopologyPolicy)WORLD_SURFACE_TOPOLOGY_DEFAULT;
}

/* Focused save-fixture stubs: this test exercises ancillary payload routing,
 * not the world edit store or fluid simulation. */
int WorldGetEditCount(void) { return 0; }
const BlockEdit *WorldGetEditAt(int index) { (void)index; return NULL; }
uint32_t WorldGetEditDimensionAt(int index) { (void)index; return 0u; }
bool WorldGetEditSurfaceAddressAt(int index, SurfaceAddress *outAddress)
{ (void)index; (void)outAddress; return false; }
bool WorldGetEditSurfaceMapCellAt(int index, SurfaceMapCell *outCell)
{ (void)index; (void)outCell; return false; }
WorldDimension WorldCurrentDimension(void) { return WORLD_DIMENSION_HOME; }
int WorldSurfaceMapOriginX(void) { return 0; }
int WorldSurfaceMapOriginZ(void) { return 0; }
uint8_t FluidGetVolumeAt(int x, int y, int z)
{ (void)x; (void)y; (void)z; return 0u; }
bool WorldPersistenceInstallEdits(const BlockEdit *edits,
                                  const uint32_t *dimensions,
                                  const SurfaceAddress *addresses,
                                  const SurfaceMapCell *mapCells,
                                  int count)
{ (void)edits; (void)dimensions; (void)addresses; (void)mapCells; (void)count; return true; }

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
#include "world/block_edit_types.h"

extern bool GameSaveTestWriteRadiationRuntimeState(FILE *file);
extern const char *GameSaveTestLoadRadiationRuntimeState(
    FILE *file, WorldSaveFormat format);

static int radiationResetCalls;
bool WorldSaveFormatHasVolcanoes(WorldSaveFormat format)
{
    return format == WORLD_SAVE_FORMAT_V24 || format == WORLD_SAVE_FORMAT_V25 || format == WORLD_SAVE_FORMAT_V26 || format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29 || format == WORLD_SAVE_FORMAT_V30;
}

bool WorldSaveFormatHasLava(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V25 || format == WORLD_SAVE_FORMAT_V26 || format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29 || format == WORLD_SAVE_FORMAT_V30; }
bool WorldSaveFormatHasAsh(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V27 || format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29 || format == WORLD_SAVE_FORMAT_V30; }
bool WorldSaveFormatHasInventoryRemainders(WorldSaveFormat format) { return format == WORLD_SAVE_FORMAT_V28 || format == WORLD_SAVE_FORMAT_V29 || format == WORLD_SAVE_FORMAT_V30; }
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

/* V30 always writes the unified cell block, including an empty store. The
 * codec version and magic are independent of the per-surface topology. */
static void TestCellSectionStartsWithItsMagic(void)
{
    testCubePolicy = true;
    assert(GameSaveTestPolicyUsesCubeGrid());
    FILE *file = tmpfile();
    assert(file);
    assert(GameSaveTestWriteCubeEdits(file));
    assert(ftell(file) == (long)CUBE_ADDRESS_SAVE_HEADER_BYTES);
    rewind(file);
    char magic[8] = { 0 };
    assert(fread(magic, 1, sizeof(magic), file) == sizeof(magic));
    assert(memcmp(magic, CUBE_ADDRESS_SAVE_MAGIC, sizeof(magic)) == 0);
    rewind(file);
    uint32_t version = 0;
    assert(CubeAddressSavePeekVersion(file, &version));
    assert(version == CUBE_ADDRESS_SAVE_VERSION);
    fclose(file);
    testCubePolicy = false;
}

int main(void)
{
    TestRemainderPayloadRouting();
    TestAshPayloadRouting();
    TestLavaPayloadRouting();
    TestVolcanoPayloadRouting();
    TestRadiationRuntimePayloadRouting();
    TestHydrologyRuntimePayloadRouting();
    TestCellSectionStartsWithItsMagic();
    HydrologyRuntimeGlobalFree();
    puts("game spherical save tests passed");
    return 0;
}
