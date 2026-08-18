#include "world/weather_impact.h"

#include "world/fluid.h"
#include "world/world.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_RADIUS 32
#define TEST_SPAN (TEST_RADIUS * 2 + 1)
#define TEST_HEIGHT 32

static BlockType blocks[TEST_SPAN][TEST_HEIGHT][TEST_SPAN];
static uint8_t fluids[TEST_SPAN][TEST_HEIGHT][TEST_SPAN];
static WorldMutationSource mutationSource = WORLD_MUTATION_PLAYER;

static bool Cell(int x, int y, int z, int *ix, int *iy, int *iz)
{
    if (x < -TEST_RADIUS || x > TEST_RADIUS ||
        z < -TEST_RADIUS || z > TEST_RADIUS || y < 0 || y >= TEST_HEIGHT) {
        return false;
    }
    if (ix) *ix = x + TEST_RADIUS;
    if (iy) *iy = y;
    if (iz) *iz = z + TEST_RADIUS;
    return true;
}

static void ResetGrid(BlockType ground)
{
    memset(blocks, 0, sizeof(blocks));
    memset(fluids, 0, sizeof(fluids));
    for (int x = 0; x < TEST_SPAN; x++) {
        for (int z = 0; z < TEST_SPAN; z++) blocks[x][10][z] = ground;
    }
    mutationSource = WORLD_MUTATION_PLAYER;
}

uint32_t WorldGetSeed(void) { return 0x45a19c7du; }
uint32_t WorldCurrentSurfaceId(void) { return 7u; }
bool WorldIsSurfaceActive(void) { return true; }
bool PlanetWorldIsActive(void) { return false; }
int PlanetTerrainHeight(int x, int z) { (void)x; (void)z; return 10; }
int WorldSurfaceHeightAt(int x, int z) { (void)x; (void)z; return 10; }

bool SurfaceBlockReadyAt(int x, int y, int z)
{
    return Cell(x, y, z, NULL, NULL, NULL);
}

BlockType GetBlockAt(int x, int y, int z)
{
    int ix, iy, iz;
    if (!Cell(x, y, z, &ix, &iy, &iz)) return BLOCK_AIR;
    return blocks[ix][iy][iz];
}

WorldMutationSource WorldCurrentMutationSource(void)
{
    return mutationSource;
}

bool SetBlockNoUndoFromSource(int x, int y, int z, BlockType type,
                              WorldMutationSource source)
{
    int ix, iy, iz;
    if (!Cell(x, y, z, &ix, &iy, &iz)) return false;
    WorldMutationSource previous = mutationSource;
    mutationSource = source;
    blocks[ix][iy][iz] = type;
    if (type != BLOCK_WATER) fluids[ix][iy][iz] = 0u;
    WeatherImpactOnBlockChanged(x, y, z);
    mutationSource = previous;
    return true;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

bool IsValidBlockType(BlockType type)
{
    return type >= BLOCK_AIR && type <= BLOCK_NATURAL_END;
}

BlockMaterialResponse BlockMaterialResponseFor(BlockType type)
{
    switch (type) {
    case BLOCK_SAND:
        return (BlockMaterialResponse){ 0.42f, 0.25f, 0.0f, 0.98f };
    case BLOCK_WOOD:
    case BLOCK_PLANK:
        return (BlockMaterialResponse){ 0.72f, 0.60f, 0.95f, 0.10f };
    case BLOCK_STONE:
        return (BlockMaterialResponse){ 0.96f, 0.95f, 0.0f, 0.03f };
    default:
        return (BlockMaterialResponse){ 0.70f, 0.70f, 0.05f, 0.10f };
    }
}

uint8_t FluidGetVolumeAt(int x, int y, int z)
{
    int ix, iy, iz;
    if (!Cell(x, y, z, &ix, &iy, &iz)) return 0u;
    return fluids[ix][iy][iz];
}

bool FluidSetVolumeAt(int x, int y, int z, uint8_t volume)
{
    int ix, iy, iz;
    if (!Cell(x, y, z, &ix, &iy, &iz)) return false;
    fluids[ix][iy][iz] = volume;
    blocks[ix][iy][iz] = volume > 0u ? BLOCK_WATER : BLOCK_AIR;
    return true;
}

static WeatherFieldSample Rain(void)
{
    return (WeatherFieldSample){
        .temperatureK = 289.0f,
        .precipitation = 1.0f,
        .rain = 1.0f,
        .wind = 0.48f,
        .gust = 0.58f,
        .visibility = 0.70f
    };
}

static bool FindSurfaceWithFlag(WeatherSurfaceFlags flag,
                                int *outX, int *outY, int *outZ)
{
    for (int x = -TEST_RADIUS; x <= TEST_RADIUS; x++) {
        for (int z = -TEST_RADIUS; z <= TEST_RADIUS; z++) {
            WeatherSurfaceState state;
            if (WeatherImpactSurfaceAt(x, 10, z, &state) &&
                (state.flags & flag) != 0u) {
                if (outX) *outX = x;
                if (outY) *outY = state.y;
                if (outZ) *outZ = z;
                return true;
            }
        }
    }
    return false;
}

static void TestRainBudgetAndDisable(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    WeatherImpactStepTicks(40u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    WeatherImpactStats stats = WeatherImpactGetStats();
    assert(stats.ticks == 40u);
    assert(stats.surfaceCount > 0u);
    assert(stats.surfaceCount <= WEATHER_IMPACT_MAX_SURFACES);
    assert(stats.depositedWater > 0u);
    assert(FindSurfaceWithFlag(WEATHER_SURFACE_WET, NULL, NULL, NULL));

    WeatherImpactSetEnabled(false);
    assert(!WeatherImpactEnabled());
    assert(WeatherImpactGetStats().surfaceCount == 0u);
    WeatherImpactStepTicks(20u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(WeatherImpactGetStats().ticks == 40u);
}

static void TestFireAndRainInteraction(void)
{
    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    assert(WeatherImpactGetStats().activeFires == 1u);
    float intensity = 0.0f;
    assert(WeatherImpactFireAt(0, 10, 0, &intensity));
    assert(intensity == 1.0f);
    WeatherImpactStepTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(WeatherImpactGetStats().activeFires == 0u);
    assert(GetBlockAt(0, 10, 0) == BLOCK_WOOD);
}

static void TestSnowOwnershipAndPlayerOverride(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    WeatherFieldSample snow = {
        .temperatureK = 264.0f,
        .precipitation = 1.0f,
        .snow = 1.0f,
        .frost = 0.9f,
        .wind = 0.30f,
        .gust = 0.38f,
        .visibility = 0.62f
    };
    WeatherImpactStepTicks(2600u, (Vector3){ 0.5f, 12.0f, 0.5f }, snow);
    int x = 0, y = 0, z = 0;
    assert(FindSurfaceWithFlag(WEATHER_SURFACE_SNOW, &x, &y, &z));
    if (GetBlockAt(x, y + 1, z) == BLOCK_SNOW) {
        assert(SetBlockNoUndoFromSource(x, y + 1, z, BLOCK_PLANK,
                                        WORLD_MUTATION_PLAYER));
        WeatherFieldSample warm = Rain();
        warm.temperatureK = 290.0f;
        WeatherImpactStepTicks(20u, (Vector3){ (float)x, 12.0f, (float)z },
                               warm);
        assert(GetBlockAt(x, y + 1, z) == BLOCK_PLANK);
    }
}

static void TestSaveLoadAndCorruption(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    WeatherImpactStepTicks(30u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    WeatherImpactStats before = WeatherImpactGetStats();
    FILE *file = tmpfile();
    assert(file);
    assert(WeatherImpactSaveState(file));
    rewind(file);
    WeatherImpactReset();
    assert(WeatherImpactLoadState(file));
    fclose(file);
    WeatherImpactStats after = WeatherImpactGetStats();
    assert(after.ticks == before.ticks);
    assert(after.surfaceCount == before.surfaceCount);

    file = tmpfile();
    assert(file);
    fputs("bad weather state", file);
    rewind(file);
    assert(!WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactGetStats().surfaceCount == after.surfaceCount);
}

int main(void)
{
    TestRainBudgetAndDisable();
    TestFireAndRainInteraction();
    TestSnowOwnershipAndPlayerOverride();
    TestSaveLoadAndCorruption();
    puts("weather impact tests passed");
    return 0;
}
