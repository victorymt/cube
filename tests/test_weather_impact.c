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
static WorldMutationSource lastMutationSource = WORLD_MUTATION_PLAYER;
static bool surfaceLoaded = true;

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
    lastMutationSource = WORLD_MUTATION_PLAYER;
    surfaceLoaded = true;
}

uint32_t WorldGetSeed(void) { return 0x45a19c7du; }
uint32_t WorldCurrentSurfaceId(void) { return 7u; }
bool WorldIsSurfaceActive(void) { return true; }
bool PlanetWorldIsActive(void) { return false; }
int PlanetTerrainHeight(int x, int z) { (void)x; (void)z; return 10; }
int WorldSurfaceHeightAt(int x, int z) { (void)x; (void)z; return 10; }

bool SurfaceBlockReadyAt(int x, int y, int z)
{
    return surfaceLoaded && Cell(x, y, z, NULL, NULL, NULL);
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
    lastMutationSource = source;
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
    case BLOCK_LEAVES:
    case BLOCK_TALL_GRASS:
    case BLOCK_FERN:
        return (BlockMaterialResponse){ 0.25f, 0.20f, 0.88f, 0.18f };
    case BLOCK_STONE:
        return (BlockMaterialResponse){ 0.96f, 0.95f, 0.0f, 0.03f };
    case BLOCK_CHARRED_WOOD:
    case BLOCK_CHARCOAL:
    case BLOCK_FIRE_ASH:
        return (BlockMaterialResponse){ 0.50f, 0.40f, 0.0f, 0.30f };
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

static WeatherFieldSample Dry(void)
{
    return (WeatherFieldSample){
        .temperatureK = 309.0f,
        .relativeHumidity = 0.16f,
        .wind = 0.24f,
        .gust = 0.32f,
        .windAngle = 0.0f,
        .visibility = 1.0f
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
    assert(intensity > 0.5f && intensity <= 1.0f);
    WeatherImpactStepTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(WeatherImpactGetStats().activeFires == 1u);
    WeatherImpactFireSnapshot fire = { 0 };
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(fire.state.moisture > 0.1f);
    assert(WeatherImpactSuppressAt(0, 10, 0, 0.5f, 0.3f) == 1u);
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(fire.state.phase == WILDFIRE_PHASE_SMOLDERING);
    WeatherImpactStepTicks(100u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(WeatherImpactGetStats().activeFires == 0u);
    assert(WeatherImpactGetStats().extinctions == 1u);
    assert(WeatherImpactGetStats().suppressions == 1u);
    assert(GetBlockAt(0, 10, 0) == BLOCK_WOOD);
}

static void TestIgnitionQueriesAndLoadedBounds(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_WOOD;
    surfaceLoaded = false;
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    surfaceLoaded = true;
    assert(FluidSetVolumeAt(1, 10, 0, FLUID_CAPACITY));
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    assert(FluidSetVolumeAt(1, 10, 0, 0u));

    blocks[TEST_RADIUS + 1][10][TEST_RADIUS] = BLOCK_WOOD;
    blocks[TEST_RADIUS + 4][10][TEST_RADIUS] = BLOCK_WOOD;
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    assert(WeatherImpactIgniteAt(1, 10, 0, 0.8f));
    assert(WeatherImpactIgniteAt(4, 10, 0, 0.8f));
    WeatherImpactFireSnapshot fires[2] = { 0 };
    unsigned count = WeatherImpactCollectFires(
        (Vector3){ 0.5f, 10.5f, 0.5f }, 20.0f, fires, 2u);
    assert(count == 2u);
    assert(fires[0].x == 0);
    assert(fires[1].x == 1);
    WeatherImpactFireSnapshot nearest = { 0 };
    float distance = -1.0f;
    assert(WeatherImpactNearestFire(
        (Vector3){ 3.9f, 10.5f, 0.5f }, &nearest, &distance));
    assert(nearest.x == 4);
    assert(distance < 1.0f);
    WeatherImpactExposure exposure = WeatherImpactExposureAt(
        (Vector3){ 0.5f, 10.5f, 0.5f }, 0.0f, 0.0f);
    assert(exposure.heat > 0.0f);
    assert(exposure.smoke > 0.0f);
    WeatherImpactExposure sheltered = WeatherImpactExposureAt(
        (Vector3){ 0.5f, 10.5f, 0.5f }, 1.0f, 1.0f);
    assert(sheltered.heat < exposure.heat);
    assert(sheltered.smoke < exposure.smoke);
    assert(WeatherImpactClearFires() == 3u);
    assert(WeatherImpactGetStats().activeFires == 0u);
}

static void TestFuelConsumptionAndBurnScar(void)
{
    assert(WeatherImpactResidueForFuel(
               BLOCK_TALL_GRASS, 1.0f, 0.02f) == BLOCK_FIRE_ASH);
    assert(WeatherImpactResidueForFuel(
               BLOCK_WOOD, 0.70f, 0.02f) == BLOCK_CHARRED_WOOD);
    assert(WeatherImpactResidueForFuel(
               BLOCK_WOOD, 1.0f, 0.30f) == BLOCK_CHARRED_WOOD);
    assert(WeatherImpactResidueForFuel(
               BLOCK_WOOD, 1.0f, 0.02f) == BLOCK_CHARCOAL);
    assert(WeatherImpactResidueForFuel(
               BLOCK_PLANK, 1.0f, 0.02f) == BLOCK_CHARCOAL);
    assert(WeatherImpactResidueForFuel(
               BLOCK_HUMUS, 0.90f, 0.02f) == BLOCK_CHARCOAL);
    assert(WeatherImpactResidueForFuel(
               BLOCK_HUMUS, 0.90f, 0.50f) == BLOCK_FIRE_ASH);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_TALL_GRASS;
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    WeatherImpactStepTicks(500u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIRE_ASH);
    assert(lastMutationSource == WORLD_MUTATION_ENVIRONMENT);
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    assert(WeatherImpactGetStats().blockDamageEvents == 1u);
    assert(WeatherImpactGetStats().burnedBlocks == 1u);
    assert(WeatherImpactGetStats().burnSiteCount >= 1u);
    WeatherBurnSiteState burn = { 0 };
    assert(WeatherImpactBurnSiteAt(0, 10, 0, &burn));
    assert(burn.severity > 0.95f);
    assert(WeatherImpactGetStats().burnedBlocks == 1u);
    assert(WeatherImpactBurnSeverityAt(0, 10, 0) > 0.9f);
    FILE *file = tmpfile();
    assert(file);
    assert(WeatherImpactSaveState(file));
    rewind(file);
    WeatherImpactReset();
    assert(WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactBurnSiteAt(0, 10, 0, &burn));
    assert(burn.severity > 0.95f);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_WOOD;
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    WeatherImpactStepTicks(
        2000u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
    assert(GetBlockAt(0, 10, 0) == BLOCK_CHARRED_WOOD);
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_PLANK;
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    WeatherImpactStepTicks(2000u,
                           (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
    assert(GetBlockAt(0, 10, 0) == BLOCK_CHARCOAL);
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));
}

static void TestBoundedWorkAndNaturalSources(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    for (int x = -20; x < 0; x++) {
        blocks[x + TEST_RADIUS][10][TEST_RADIUS] = BLOCK_WOOD;
        assert(WeatherImpactIgniteAt(x, 10, 0, 0.8f));
    }
    WeatherImpactStepTicks(1u, (Vector3){ 0.0f, 12.0f, 0.0f }, Dry());
    WeatherImpactFireSnapshot fires[WEATHER_IMPACT_MAX_FIRES] = { 0 };
    unsigned count = WeatherImpactCollectFires(
        (Vector3){ 0.0f, 12.0f, 0.0f }, 100.0f, fires,
        WEATHER_IMPACT_MAX_FIRES);
    unsigned advanced = 0u;
    for (unsigned index = 0u; index < count; index++) {
        if (fires[index].state.ageSeconds > 0.0f) advanced++;
    }
    assert(count == 20u);
    assert(advanced == 16u);

    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    WeatherFieldSample lightning = Dry();
    lightning.lightning = 1.0f;
    WeatherImpactStepTicks(80u, (Vector3){ 0.0f, 12.0f, 0.0f }, lightning);
    assert(WeatherImpactGetStats().ignitions > 0u);

    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    for (int x = 0; x < TEST_SPAN; x++) {
        for (int z = 0; z < TEST_SPAN; z++) {
            blocks[x][11][z] = BLOCK_LAVA;
        }
    }
    WeatherImpactStepTicks(20u, (Vector3){ 0.0f, 12.0f, 0.0f }, Dry());
    assert(WeatherImpactGetStats().ignitions > 0u);
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
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_WOOD;
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 0.9f));
    WeatherImpactStepTicks(30u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
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
    assert(after.activeFires == before.activeFires);
    assert(after.ignitions == before.ignitions);
    assert(after.burnedBlocks == before.burnedBlocks);
    WeatherImpactFireSnapshot fireState = { 0 };
    assert(WeatherImpactFireStateAt(0, 10, 0, &fireState));
    assert(fireState.state.phase != WILDFIRE_PHASE_INACTIVE);

    file = tmpfile();
    assert(file);
    fputs("bad weather state", file);
    rewind(file);
    assert(!WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactGetStats().surfaceCount == after.surfaceCount);
}

typedef struct LegacyDiskHeader {
    uint32_t surfaceCount;
    uint32_t fireCount;
    uint64_t ticks;
} LegacyDiskHeader;

typedef struct LegacyFireRecord {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    float intensity;
    float fuel;
} LegacyFireRecord;

typedef struct CurrentDiskHeader {
    uint32_t surfaceCount;
    uint32_t fireCount;
    uint32_t burnSiteCount;
    uint32_t reserved;
    uint64_t ticks;
    uint64_t processedSurfaces;
    uint64_t depositedWater;
    uint32_t counters[10];
} CurrentDiskHeader;

static void TestLegacyMigrationAndTransactionalFailure(void)
{
    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    FILE *file = tmpfile();
    assert(file);
    const LegacyDiskHeader header = {
        .fireCount = 1u,
        .ticks = 9876u
    };
    const LegacyFireRecord fire = {
        .surfaceId = 7u,
        .x = 2,
        .y = 10,
        .z = 3,
        .intensity = 0.0f,
        .fuel = 0.75f
    };
    const uint32_t sentinel = 0x51a7c0deu;
    assert(fwrite("WXIMPACT1", 1u, 9u, file) == 9u);
    assert(fwrite(&header, sizeof(header), 1u, file) == 1u);
    assert(fwrite(&fire, sizeof(fire), 1u, file) == 1u);
    assert(fwrite(&sentinel, sizeof(sentinel), 1u, file) == 1u);
    rewind(file);
    assert(WeatherImpactLoadState(file));
    uint32_t trailing = 0u;
    assert(fread(&trailing, sizeof(trailing), 1u, file) == 1u);
    assert(trailing == sentinel);
    fclose(file);
    WeatherImpactStats migrated = WeatherImpactGetStats();
    assert(migrated.ticks == 9876u);
    assert(migrated.activeFires == 1u);
    WeatherImpactFireSnapshot migratedFire = { 0 };
    assert(WeatherImpactFireStateAt(2, 10, 3, &migratedFire));
    assert(migratedFire.state.phase == WILDFIRE_PHASE_SMOLDERING);
    assert(migratedFire.state.intensity > 0.0f);
    assert(migratedFire.state.heatOutput > 0.0f);

    file = tmpfile();
    assert(file);
    CurrentDiskHeader corrupt = {
        .fireCount = WEATHER_IMPACT_MAX_FIRES + 1u
    };
    assert(fwrite("WXIMPACT2", 1u, 9u, file) == 9u);
    assert(fwrite(&corrupt, sizeof(corrupt), 1u, file) == 1u);
    rewind(file);
    assert(!WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactGetStats().ticks == migrated.ticks);
    assert(WeatherImpactFireStateAt(2, 10, 3, &migratedFire));
}

int main(void)
{
    TestRainBudgetAndDisable();
    TestFireAndRainInteraction();
    TestIgnitionQueriesAndLoadedBounds();
    TestFuelConsumptionAndBurnScar();
    TestBoundedWorkAndNaturalSources();
    TestSnowOwnershipAndPlayerOverride();
    TestSaveLoadAndCorruption();
    TestLegacyMigrationAndTransactionalFailure();
    puts("weather impact tests passed");
    return 0;
}
