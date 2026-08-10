#include "ecology_test_fixture.h"

#include "ecology.h"
#include "terrain.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t propertyWorldSeed = DEFAULT_WORLD_SEED;
static BlockEdit propertyBlockEdits[64];
static int propertyBlockEditCount = 0;
static uint64_t propertyBlockEditRevision = 1u;
static int propertyBlockEditReadCount = 0;
TerrainMode terrainMode = TERRAIN_VARIED;

uint32_t WorldGetSeed(void)
{
    return propertyWorldSeed;
}

int WorldSurfaceHeightAt(int x, int z)
{
    return PlanetTerrainHeight(x, z);
}

int WorldGetEditCount(void)
{
    return propertyBlockEditCount;
}

uint64_t WorldGetEditRevision(void)
{
    return propertyBlockEditRevision;
}

bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit)
{
    propertyBlockEditReadCount++;
    if (!outEdit || index < 0 || index >= propertyBlockEditCount) {
        return false;
    }
    *outEdit = propertyBlockEdits[index];
    return true;
}

float TorchLightAtBlockNearby(int x, int y, int z,
                              const int *indices, int count)
{
    (void)x;
    (void)y;
    (void)z;
    (void)indices;
    (void)count;
    return 0.0f;
}

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

int ColorBlockIndex(BlockType type)
{
    return IsColorBlock(type) ? (int)type - BLOCK_COLOR_START : -1;
}

Color ColorPalette256(int index)
{
    unsigned char value = (unsigned char)(index & 0xff);
    return (Color){ value, value, value, 255 };
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_AIR || type == BLOCK_GLASS ||
           type == BLOCK_WATER || type == BLOCK_ICE ||
           type == BLOCK_FLOWER || type == BLOCK_MUSHROOM ||
           type == BLOCK_GLASS_PANE || type == BLOCK_NETHER_PORTAL;
}

BlockType GetBlockAt(int x, int y, int z)
{
    return GetBlock(x, y, z);
}

void PlanetPoiApplyToChunk(Chunk *chunk, int cx, int cz)
{
    (void)chunk;
    (void)cx;
    (void)cz;
}

void UnloadModel(Model model)
{
    (void)model;
}

void EcologyTestSetSeed(uint32_t seed)
{
    propertyWorldSeed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
}

static void BumpPropertyBlockEditRevision(void)
{
    propertyBlockEditRevision++;
    if (propertyBlockEditRevision == 0u) propertyBlockEditRevision = 1u;
}

void EcologyTestClearBlockEdits(void)
{
    propertyBlockEditCount = 0;
    BumpPropertyBlockEditRevision();
    memset(propertyBlockEdits, 0, sizeof(propertyBlockEdits));
}

void EcologyTestAddBlockEdit(int x, int y, int z, BlockType type)
{
    assert(propertyBlockEditCount <
           (int)(sizeof(propertyBlockEdits) / sizeof(propertyBlockEdits[0])));
    propertyBlockEdits[propertyBlockEditCount++] = (BlockEdit){
        .x = x,
        .y = y,
        .z = z,
        .type = type
    };
    BumpPropertyBlockEditRevision();
}

void EcologyTestSetBlockEditType(int index, BlockType type)
{
    assert(index >= 0 && index < propertyBlockEditCount);
    if (propertyBlockEdits[index].type == type) return;
    propertyBlockEdits[index].type = type;
    BumpPropertyBlockEditRevision();
}

int EcologyTestBlockEditCount(void)
{
    return propertyBlockEditCount;
}

uint64_t EcologyTestBlockEditRevision(void)
{
    return propertyBlockEditRevision;
}

int EcologyTestBlockEditReadCount(void)
{
    return propertyBlockEditReadCount;
}

void EcologyTestResetBlockEditReadCount(void)
{
    propertyBlockEditReadCount = 0;
}

static void WritePlanetWorldFixture(FILE *file, uint32_t seed,
                                    int originX, int originZ,
                                    SolarBodyStyle planetStyle)
{
    uint8_t active = 1u;
    uint32_t style = (uint32_t)planetStyle;
    int32_t savedOriginX = (int32_t)originX;
    int32_t savedOriginZ = (int32_t)originZ;
    int32_t planetIndex = 1;
    float bodyCenter[3] = { 420.0f, -18.0f, 75.0f };
    float returnPosition[3] = { 486.0f, -18.0f, 75.0f };
    float proxyRadius = 62.0f;
    char name[32] = "Ecology Replay";

    assert(fwrite(&active, sizeof(active), 1, file) == 1);
    assert(fwrite(&seed, sizeof(seed), 1, file) == 1);
    assert(fwrite(&style, sizeof(style), 1, file) == 1);
    assert(fwrite(&savedOriginX, sizeof(savedOriginX), 1, file) == 1);
    assert(fwrite(&savedOriginZ, sizeof(savedOriginZ), 1, file) == 1);
    assert(fwrite(&planetIndex, sizeof(planetIndex), 1, file) == 1);
    assert(fwrite(bodyCenter, sizeof(bodyCenter), 1, file) == 1);
    assert(fwrite(returnPosition, sizeof(returnPosition), 1, file) == 1);
    assert(fwrite(&proxyRadius, sizeof(proxyRadius), 1, file) == 1);
    assert(fwrite(name, sizeof(name), 1, file) == 1);
}

void EcologyTestActivatePlanetStyle(uint32_t seed, int originX, int originZ,
                                       SolarBodyStyle style)
{
    FILE *file = tmpfile();
    assert(file);
    WritePlanetWorldFixture(file, seed, originX, originZ, style);
    rewind(file);
    assert(PlanetWorldLoadState(file));
    fclose(file);
    assert(PlanetWorldIsActive());
    assert(PlanetWorldSeed() == seed);
    assert(PlanetWorldOriginX() == originX);
    assert(PlanetWorldOriginZ() == originZ);
}

void EcologyTestActivatePlanet(uint32_t seed, int originX, int originZ)
{
    EcologyTestActivatePlanetStyle(seed, originX, originZ,
                               SOLAR_STYLE_TEMPERATE);
}

void EcologyTestSaveSimulationState(FILE *file)
{
    assert(file);
    assert(SpaceSaveState(file));
    assert(PlanetWorldSaveState(file));
    assert(PlanetEcologySaveState(file));
}

void EcologyTestLoadSimulationState(FILE *file)
{
    assert(file);
    rewind(file);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));
}
