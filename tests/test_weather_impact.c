#include "world/weather_impact.h"

#include "world/fluid.h"
#include "world/surface_topology.h"
#include "world/world.h"
#include "world/world_surface_snapshot.h"

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

static void StepUniformTicks(unsigned ticks, Vector3 position,
                             WeatherFieldSample weather)
{
    WeatherImpactStepTicks(ticks, position,
                          (WeatherImpactWeather){ .uniform = weather });
}

static void UpdateUniformImpacts(float dt, Vector3 position,
                                 WeatherFieldSample weather)
{
    WeatherImpactUpdate(dt, position,
                        (WeatherImpactWeather){ .uniform = weather });
}

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

uint32_t WorldGetSeed(void)
{
    return WorldCurrentSurfaceSnapshot()->context.seed;
}
uint32_t WorldCurrentSurfaceId(void)
{
    return WorldCurrentSurfaceSnapshot()->context.bodyId;
}
bool WorldIsSurfaceActive(void)
{
    return WorldCurrentSurfaceSnapshot()->context.active;
}
WorldSurfaceContext WorldCurrentSurfaceContext(void)
{
    return WorldCurrentSurfaceSnapshot()->context;
}
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

BlockType GetBlockBaseAt(int x, int y, int z) { return GetBlockAt(x, y, z); }

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
    case BLOCK_OAK_LOG:
    case BLOCK_BIRCH_LOG:
    case BLOCK_ASPEN_LOG:
    case BLOCK_SPRUCE_LOG:
    case BLOCK_PINE_LOG:
    case BLOCK_WILLOW_LOG:
        return (BlockMaterialResponse){ 0.70f, 0.58f, 0.92f, 0.10f };
    case BLOCK_OAK_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_ASPEN_LEAVES:
    case BLOCK_SPRUCE_NEEDLES:
    case BLOCK_PINE_NEEDLES:
    case BLOCK_WILLOW_LEAVES:
        return (BlockMaterialResponse){ 0.24f, 0.18f, 0.94f, 0.20f };
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
    StepUniformTicks(40u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    WeatherImpactStats stats = WeatherImpactGetStats();
    assert(stats.ticks == 40u);
    assert(stats.surfaceCount > 0u);
    assert(stats.surfaceCount <= WEATHER_IMPACT_MAX_SURFACES);
    assert(stats.depositedWater > 0u);
    assert(FindSurfaceWithFlag(WEATHER_SURFACE_WET, NULL, NULL, NULL));

    WeatherImpactSetEnabled(false);
    assert(!WeatherImpactEnabled());
    assert(WeatherImpactGetStats().surfaceCount == 0u);
    StepUniformTicks(20u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
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
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(WeatherImpactGetStats().activeFires == 1u);
    WeatherImpactFireSnapshot fire = { 0 };
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(fire.state.moisture > 0.1f);
    assert(WeatherImpactSuppressAt(0, 10, 0, 0.5f, 0.3f) == 1u);
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(fire.state.phase == WILDFIRE_PHASE_SMOLDERING);
    StepUniformTicks(100u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
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
    StepUniformTicks(500u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
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
    StepUniformTicks(
        2000u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
    assert(GetBlockAt(0, 10, 0) == BLOCK_CHARRED_WOOD);
    assert(!WeatherImpactIgniteAt(0, 10, 0, 1.0f));

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_PLANK;
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    StepUniformTicks(2000u,
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
    StepUniformTicks(1u, (Vector3){ 0.0f, 12.0f, 0.0f }, Dry());
    WeatherImpactFireSnapshot fires[WEATHER_IMPACT_MAX_FIRES] = { 0 };
    unsigned count = WeatherImpactCollectFires(
        (Vector3){ 0.0f, 12.0f, 0.0f }, 100.0f, fires,
        WEATHER_IMPACT_MAX_FIRES);
    unsigned advanced = 0u;
    for (unsigned index = 0u; index < count; index++) {
        if (fires[index].state.ageSeconds > 0.0f) advanced++;
    }
    assert(count == 20u);
    assert(advanced == count);
    for (unsigned index = 0u; index < count; index++) {
        assert(fires[index].state.ageSeconds == 0.5f);
    }

    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    WeatherFieldSample lightning = Dry();
    lightning.lightning = 1.0f;
    StepUniformTicks(80u, (Vector3){ 0.0f, 12.0f, 0.0f }, lightning);
    assert(WeatherImpactGetStats().ignitions > 0u);

    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    for (int x = 0; x < TEST_SPAN; x++) {
        for (int z = 0; z < TEST_SPAN; z++) {
            blocks[x][11][z] = BLOCK_LAVA;
        }
    }
    StepUniformTicks(20u, (Vector3){ 0.0f, 12.0f, 0.0f }, Dry());
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
    StepUniformTicks(2600u, (Vector3){ 0.5f, 12.0f, 0.5f }, snow);
    int x = 0, y = 0, z = 0;
    assert(FindSurfaceWithFlag(WEATHER_SURFACE_SNOW, &x, &y, &z));
    if (GetBlockAt(x, y + 1, z) == BLOCK_SNOW) {
        assert(SetBlockNoUndoFromSource(x, y + 1, z, BLOCK_PLANK,
                                        WORLD_MUTATION_PLAYER));
        WeatherFieldSample warm = Rain();
        warm.temperatureK = 290.0f;
        StepUniformTicks(20u, (Vector3){ (float)x, 12.0f, (float)z },
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
    StepUniformTicks(30u, (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
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

typedef struct CurrentBurnRecord {
    uint32_t surfaceId;
    int32_t x;
    int32_t y;
    int32_t z;
    float severity;
    float recovery;
    float ageSeconds;
} CurrentBurnRecord;

typedef struct CurrentFireRecord {
    uint32_t surfaceId;
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t fuelBlock;
    uint32_t phase;
    float intensity;
    float fuel;
    float moisture;
    float ageSeconds;
    float initialFuel;
} CurrentFireRecord;

typedef struct CurrentSurfaceRecord {
    uint32_t surfaceId;
    int32_t x, y, z;
    uint32_t flags;
    float wetness, snowDepth, iceAmount;
    float erosionExposure, windExposure, impactExposure;
    uint32_t originalBlock, expectedBlock;
    int32_t effectY;
    uint8_t originalFluidVolume, ownedEffect, reserved[2];
} CurrentSurfaceRecord;

static void LoadTimingFixture(unsigned surfaces, unsigned fires,
                              unsigned burns)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    FILE *file = tmpfile();
    assert(file);
    CurrentDiskHeader header = {
        .surfaceCount = surfaces, .fireCount = fires, .burnSiteCount = burns
    };
    assert(fwrite("WXIMPACT2", 1u, 9u, file) == 9u);
    assert(fwrite(&header, sizeof(header), 1u, file) == 1u);
    for (unsigned index = 0u; index < surfaces; index++) {
        CurrentSurfaceRecord record = {
            .surfaceId = 7u, .x = 2000 + (int)index, .y = 10, .z = 2000,
            .flags = WEATHER_SURFACE_WET | WEATHER_SURFACE_SNOW |
                     WEATHER_SURFACE_ICE,
            .wetness = 1.0f, .snowDepth = 1.0f, .iceAmount = 1.0f,
            .erosionExposure = 0.25f, .effectY = 11
        };
        assert(fwrite(&record, sizeof(record), 1u, file) == 1u);
    }
    for (unsigned index = 0u; index < fires; index++) {
        /* Isolated fuel keeps spreading and local water out of this clock
           comparison, even when every fire slot is occupied. */
        int x = -30 + (int)(index % 16u) * 4;
        int z = -30 + (int)(index / 16u) * 4;
        blocks[x + TEST_RADIUS][10][z + TEST_RADIUS] = BLOCK_WOOD;
        CurrentFireRecord record = {
            .surfaceId = 7u, .x = x, .y = 10, .z = z,
            .fuelBlock = BLOCK_WOOD, .phase = WILDFIRE_PHASE_FLAMING,
            .intensity = 0.8f, .fuel = 3.0f, .initialFuel = 3.0f,
            .moisture = 0.15f
        };
        assert(fwrite(&record, sizeof(record), 1u, file) == 1u);
    }
    for (unsigned index = 0u; index < burns; index++) {
        CurrentBurnRecord record = {
            .surfaceId = 7u, .x = 4000 + (int)index, .y = 10, .z = 4000,
            .severity = 1.0f, .recovery = 0.1f, .ageSeconds = 2000.0f
        };
        assert(fwrite(&record, sizeof(record), 1u, file) == 1u);
    }
    rewind(file);
    assert(WeatherImpactLoadState(file));
    fclose(file);
}

typedef struct TimingSnapshot {
    WeatherSurfaceState surface;
    WeatherImpactFireSnapshot fire;
    WeatherBurnSiteState burn;
    bool fireActive;
} TimingSnapshot;

static TimingSnapshot CaptureTimingSnapshot(void)
{
    TimingSnapshot result = { 0 };
    assert(WeatherImpactSurfaceAt(2000, 10, 2000, &result.surface));
    result.fireActive = WeatherImpactFireStateAt(-30, 10, -30, &result.fire);
    assert(WeatherImpactBurnSiteAt(4000, 10, 4000, &result.burn));
    return result;
}

static void AssertTimingEqual(TimingSnapshot actual, TimingSnapshot expected)
{
    assert(actual.surface.flags == expected.surface.flags);
    assert(actual.surface.wetness == expected.surface.wetness);
    assert(actual.surface.snowDepth == expected.surface.snowDepth);
    assert(actual.surface.iceAmount == expected.surface.iceAmount);
    assert(actual.burn.ageSeconds == expected.burn.ageSeconds);
    assert(actual.burn.recovery == expected.burn.recovery);
    assert(actual.fireActive == expected.fireActive);
    if (actual.fireActive) {
        assert(actual.fire.state.phase == expected.fire.state.phase);
        assert(actual.fire.state.ageSeconds == expected.fire.state.ageSeconds);
        assert(actual.fire.state.fuel == expected.fire.state.fuel);
        assert(actual.fire.state.intensity == expected.fire.state.intensity);
        assert(actual.fire.state.moisture == expected.fire.state.moisture);
        assert(actual.fire.state.heatOutput == expected.fire.state.heatOutput);
        assert(actual.fire.state.smokeOutput == expected.fire.state.smokeOutput);
    }
}

static void TestElapsedTimeIndependentOfRecordCount(void)
{
    enum { TICKS = 40 };
    TimingSnapshot baseline[TICKS];
    const unsigned populations[][3] = {
        { 1u, 1u, 1u },
        { 65u, 17u, 33u },
        { WEATHER_IMPACT_MAX_SURFACES, WEATHER_IMPACT_MAX_FIRES,
          WEATHER_IMPACT_MAX_BURN_SITES }
    };
    for (unsigned run = 0u; run < 5u; run++) {
        unsigned population = run < 3u ? run : 2u;
        LoadTimingFixture(populations[population][0],
                          populations[population][1],
                          populations[population][2]);
        for (unsigned tick = 0u; tick < TICKS; tick++) {
            /* Crossing wet/dry and freezing boundaries catches attempts to
               apply today's weather retroactively to missed ticks. */
            WeatherFieldSample weather = tick < 20u ? Dry() : Rain();
            if (tick < 4u) weather.temperatureK = 264.0f;
            Vector3 position = { 10000.0f, 12.0f, 10000.0f };
            if (run == 4u) {
                for (unsigned frame = 0u; frame < 4u; frame++) {
                    UpdateUniformImpacts(0.125f, position, weather);
                }
            } else {
                StepUniformTicks(1u, position, weather);
            }
            TimingSnapshot actual = CaptureTimingSnapshot();
            if (run == 0u) baseline[tick] = actual;
            else AssertTimingEqual(actual, baseline[tick]);
            if (run == 3u && tick == 6u) {
                FILE *file = tmpfile();
                assert(file && WeatherImpactSaveState(file));
                rewind(file);
                WeatherImpactReset();
                assert(WeatherImpactLoadState(file));
                fclose(file);
                AssertTimingEqual(CaptureTimingSnapshot(), actual);
            }
        }
    }
    assert(baseline[1].fire.state.ageSeconds == 1.0f);
    assert(baseline[TICKS - 1].surface.snowDepth == 0.0f);
    assert(baseline[TICKS - 1].burn.ageSeconds == 2020.0f);
    assert(baseline[TICKS - 1].burn.recovery > baseline[0].burn.recovery);
}

static void TestInactiveSurfaceAndUnloadedFireClocks(void)
{
    LoadTimingFixture(WEATHER_IMPACT_MAX_SURFACES, WEATHER_IMPACT_MAX_FIRES,
                      WEATHER_IMPACT_MAX_BURN_SITES);
    TimingSnapshot before = CaptureTimingSnapshot();
    WorldSurfaceSnapshot home = *WorldCurrentSurfaceSnapshot();
    WorldSurfaceSnapshot other = home;
    other.context.bodyId = home.context.bodyId + 1u;
    assert(WorldInstallSurfaceSnapshot(&other));
    StepUniformTicks(20u, (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
    assert(WorldInstallSurfaceSnapshot(&home));
    AssertTimingEqual(CaptureTimingSnapshot(), before);

    surfaceLoaded = false;
    StepUniformTicks(4u, (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
    TimingSnapshot unloaded = CaptureTimingSnapshot();
    assert(unloaded.fire.state.ageSeconds == before.fire.state.ageSeconds);
    assert(unloaded.fire.state.fuel == before.fire.state.fuel);
    assert(unloaded.burn.ageSeconds == before.burn.ageSeconds + 2.0f);
    surfaceLoaded = true;
    StepUniformTicks(1u, (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
    assert(CaptureTimingSnapshot().fire.state.ageSeconds == 0.5f);

    WeatherBurnSiteState burn;
    assert(WeatherImpactBurnSiteAt(4000, 10, 4000, &burn));
    float age = burn.ageSeconds;
    WeatherImpactSetEnabled(false);
    StepUniformTicks(20u, (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
    assert(WeatherImpactBurnSiteAt(4000, 10, 4000, &burn));
    assert(burn.ageSeconds == age);
    WeatherImpactSetEnabled(true);
    StepUniformTicks(1u, (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
    assert(WeatherImpactBurnSiteAt(4000, 10, 4000, &burn));
    assert(burn.ageSeconds == age + 0.5f);
}

static void TestOwnedMeltAtCapacity(void)
{
    for (unsigned frozenWater = 0u; frozenWater < 2u; frozenWater++) {
        for (unsigned run = 0u; run < 4u; run++) {
            ResetGrid(BLOCK_STONE);
            WeatherImpactInit(true);
            unsigned count = run == 0u ? 1u : WEATHER_IMPACT_MAX_SURFACES;
            FILE *file = tmpfile();
            assert(file);
            CurrentDiskHeader header = { .surfaceCount = count };
            assert(fwrite("WXIMPACT2", 1u, 9u, file) == 9u);
            assert(fwrite(&header, sizeof(header), 1u, file) == 1u);
            for (unsigned index = 0u; index < count; index++) {
                int x = (int)(index % 32u);
                int z = (int)(index / 32u);
                CurrentSurfaceRecord record = {
                    .surfaceId = 7u, .x = x, .y = 10, .z = z,
                    .flags = frozenWater ? WEATHER_SURFACE_ICE : WEATHER_SURFACE_SNOW,
                    .snowDepth = frozenWater ? 0.0f : 1.0f,
                    .iceAmount = frozenWater ? 1.0f : 0.0f,
                    .originalBlock = frozenWater ? BLOCK_WATER : BLOCK_AIR,
                    .expectedBlock = frozenWater ? BLOCK_ICE : BLOCK_SNOW,
                    .effectY = 11,
                    .originalFluidVolume = frozenWater ? FLUID_CAPACITY / 2u : 0u,
                    .ownedEffect = frozenWater ? 2u : 1u
                };
                blocks[x + TEST_RADIUS][11][z + TEST_RADIUS] =
                    (BlockType)record.expectedBlock;
                assert(fwrite(&record, sizeof(record), 1u, file) == 1u);
            }
            rewind(file);
            assert(WeatherImpactLoadState(file));
            fclose(file);
            if (run == 3u) {
                assert(SetBlockNoUndoFromSource(
                    0, 11, 0, BLOCK_PLANK, WORLD_MUTATION_PLAYER));
            }
            surfaceLoaded = run != 2u;
            StepUniformTicks(20u,
                (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
            if (!surfaceLoaded) {
                assert(GetBlockAt(0, 11, 0) ==
                       (frozenWater ? BLOCK_ICE : BLOCK_SNOW));
                assert(WeatherImpactGetStats().surfaceCount == count);
                surfaceLoaded = true;
                StepUniformTicks(1u,
                    (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
            }
            for (unsigned index = 0u; index < count; index++) {
                int x = (int)(index % 32u);
                int z = (int)(index / 32u);
                if (run == 3u && index == 0u) {
                    assert(GetBlockAt(x, 11, z) == BLOCK_PLANK);
                    continue;
                }
                assert(GetBlockAt(x, 11, z) ==
                       (frozenWater ? BLOCK_WATER : BLOCK_AIR));
                if (frozenWater) {
                    assert(FluidGetVolumeAt(x, 11, z) == FLUID_CAPACITY / 2u);
                }
            }
            assert(WeatherImpactGetStats().surfaceCount == 0u);
        }
    }
}

static void TestSpreadStartsOnNextTick(void)
{
    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
    bool spread = false;
    for (unsigned tick = 0u; tick < 100u && !spread; tick++) {
        StepUniformTicks(1u,
            (Vector3){ 10000.0f, 12.0f, 10000.0f }, Dry());
        WeatherImpactFireSnapshot fires[WEATHER_IMPACT_MAX_FIRES];
        unsigned count = WeatherImpactCollectFires(
            (Vector3){ 0.0f, 10.0f, 0.0f }, 100.0f,
            fires, WEATHER_IMPACT_MAX_FIRES);
        for (unsigned index = 0u; index < count; index++) {
            if (fires[index].x == 0 && fires[index].y == 10 && fires[index].z == 0) {
                continue;
            }
            assert(fires[index].state.ageSeconds == 0.0f);
            spread = true;
        }
    }
    assert(spread);
}

static unsigned localWeatherCalls;
static double localWeatherTime;

static WeatherFieldSample SpatialWeather(int x, int z, double time)
{
    (void)z;
    assert(time == localWeatherTime);
    localWeatherCalls++;
    if (time < 20.0 && (x == -30 || x == 2000 || x == 4000)) {
        WeatherFieldSample cold = Dry();
        cold.temperatureK = 264.0f;
        return cold;
    }
    return Rain();
}

static void TestImpactsUseTheirOwnWeather(void)
{
    TimingSnapshot baseline[8];
    for (unsigned run = 0u; run < 2u; run++) {
        LoadTimingFixture(2u, 2u, 2u);
        for (unsigned tick = 0u; tick < 8u; tick++) {
            localWeatherTime = 10.0 + (double)tick * 0.5;
            localWeatherCalls = 0u;
            WeatherImpactStepTicks(1u,
                (Vector3){ run ? -20000.0f : 10000.0f, 12.0f, 10000.0f },
                (WeatherImpactWeather){ .sampleAt = SpatialWeather,
                                        .simulationTime = localWeatherTime });
            assert(localWeatherCalls >= 5u && localWeatherCalls <= 8u);
            TimingSnapshot cold = CaptureTimingSnapshot();
            WeatherSurfaceState warm;
            WeatherBurnSiteState wetBurn;
            assert(WeatherImpactSurfaceAt(2001, 10, 2000, &warm));
            assert(WeatherImpactBurnSiteAt(4001, 10, 4000, &wetBurn));
            assert(cold.surface.snowDepth == 1.0f);
            assert(cold.surface.iceAmount == 1.0f);
            assert(warm.snowDepth < cold.surface.snowDepth);
            assert(warm.wetness < cold.surface.wetness);
            assert(wetBurn.recovery > cold.burn.recovery);
            assert(cold.fireActive);
            WeatherImpactFireSnapshot rainyFire;
            if (WeatherImpactFireStateAt(-26, 10, -30, &rainyFire)) {
                assert(rainyFire.state.moisture > cold.fire.state.moisture);
                assert(rainyFire.state.intensity < cold.fire.state.intensity);
            }
            if (run == 0u) baseline[tick] = cold;
            else AssertTimingEqual(cold, baseline[tick]);
        }
        /* A new time invalidates all cached columns, including when stepping
           via the DSL with a fixed player position. */
        localWeatherTime = 20.0;
        WeatherImpactStepTicks(1u, (Vector3){ 10000.0f, 12.0f, 10000.0f },
            (WeatherImpactWeather){ .sampleAt = SpatialWeather,
                                    .simulationTime = localWeatherTime });
        assert(CaptureTimingSnapshot().surface.snowDepth < 1.0f);
        uint64_t ticks = WeatherImpactGetStats().ticks;
        WeatherImpactStepTicks(1u, (Vector3){ 0 },
            (WeatherImpactWeather){ .sampleAt = SpatialWeather,
                                    .simulationTime = NAN });
        assert(WeatherImpactGetStats().ticks == ticks);
    }
}

static WeatherFieldSample SplitRain(int x, int z, double time)
{
    (void)z;
    (void)time;
    return x < 0 ? Dry() : Rain();
}

static void TestPrecipitationAndIgnitionUseTheirOwnWeather(void)
{
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    WeatherImpactWeather weather = { .sampleAt = SplitRain, .simulationTime = 1.0 };
    WeatherImpactStepTicks(100u, (Vector3){ 0.5f, 12.0f, 0.5f }, weather);
    unsigned wetColumns = 0u;
    for (int x = -24; x <= 24; x++) {
        for (int z = -24; z <= 24; z++) {
            uint8_t water = FluidGetVolumeAt(x, 11, z);
            if (x < 0) assert(water == 0u);
            else wetColumns += water > 0u;
        }
    }
    assert(wetColumns > 0u);
    /* Priming the sampler with zero ticks allows ignition to use local
       moisture without changing the simulation clock. */
    ResetGrid(BLOCK_STONE);
    WeatherImpactInit(true);
    blocks[TEST_RADIUS - 2][10][TEST_RADIUS] = BLOCK_WOOD;
    blocks[TEST_RADIUS + 2][10][TEST_RADIUS] = BLOCK_WOOD;
    WeatherImpactStepTicks(0u, (Vector3){ 0 }, weather);
    assert(WeatherImpactIgniteAt(-2, 10, 0, 1.0f));
    assert(WeatherImpactIgniteAt(2, 10, 0, 1.0f));
    WeatherImpactFireSnapshot dry, wet;
    assert(WeatherImpactFireStateAt(-2, 10, 0, &dry));
    assert(WeatherImpactFireStateAt(2, 10, 0, &wet));
    assert(wet.state.moisture > dry.state.moisture);
    assert(wet.state.intensity < dry.state.intensity);
}

static FILE *CurrentFireAliasState(bool duplicate)
{
    FILE *file = tmpfile();
    assert(file);
    CurrentDiskHeader header = {
        .fireCount = duplicate ? 2u : 1u,
        .ticks = 4242u
    };
    CurrentFireRecord fires[2] = {
        {
            .surfaceId = 7u,
            .x = SURFACE_EQUATOR_BLOCKS,
            .y = 10,
            .z = 0,
            .fuelBlock = BLOCK_WOOD,
            .phase = WILDFIRE_PHASE_FLAMING,
            .intensity = 0.85f,
            .fuel = 0.75f,
            .moisture = 0.12f,
            .initialFuel = 0.75f
        },
        {
            .surfaceId = 7u,
            .x = 0,
            .y = 10,
            .z = 0,
            .fuelBlock = BLOCK_WOOD,
            .phase = WILDFIRE_PHASE_FLAMING,
            .intensity = 0.72f,
            .fuel = 0.70f,
            .moisture = 0.15f,
            .initialFuel = 0.70f
        }
    };
    assert(fwrite("WXIMPACT2", 1u, 9u, file) == 9u);
    assert(fwrite(&header, sizeof(header), 1u, file) == 1u);
    assert(fwrite(fires, sizeof(fires[0]), header.fireCount, file) ==
           header.fireCount);
    rewind(file);
    return file;
}

static void TestSphericalFireAliasesAndLoadMigration(void)
{
    ResetGrid(BLOCK_WOOD);
    WeatherImpactInit(true);
    assert(WeatherImpactIgniteAt(SURFACE_EQUATOR_BLOCKS, 10, 0, 1.0f));
    assert(WeatherImpactIgniteAt(0, 10, 0, 0.6f));
    assert(WeatherImpactGetStats().activeFires == 1u);
    WeatherImpactFireSnapshot fire = { 0 };
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(WeatherImpactFireStateAt(SURFACE_EQUATOR_BLOCKS, 10, 0, &fire));
    WeatherImpactFireSnapshot collected[2] = { 0 };
    assert(WeatherImpactCollectFires(
               (Vector3){ SURFACE_EQUATOR_BLOCKS + 0.5f, 10.5f, 0.5f },
               1.0f, collected, 2u) == 1u);
    float distance = -1.0f;
    assert(WeatherImpactNearestFire(
        (Vector3){ SURFACE_EQUATOR_BLOCKS + 0.5f, 10.5f, 0.5f },
        &fire, &distance));
    assert(distance < 0.001f);

    FILE *file = CurrentFireAliasState(false);
    assert(WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactGetStats().ticks == 4242u);
    assert(WeatherImpactGetStats().activeFires == 1u);
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
    assert(fire.x == 0);

    file = tmpfile();
    assert(file);
    assert(WeatherImpactSaveState(file));
    rewind(file);
    WeatherImpactReset();
    assert(WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));

    file = CurrentFireAliasState(true);
    assert(!WeatherImpactLoadState(file));
    fclose(file);
    assert(WeatherImpactGetStats().activeFires == 1u);
    assert(WeatherImpactFireStateAt(0, 10, 0, &fire));
}

static FILE *BurnRecoveryState(float recovery)
{
    FILE *file = tmpfile();
    assert(file);
    CurrentDiskHeader header = {
        .burnSiteCount = 1u,
        .ticks = 1000u
    };
    CurrentBurnRecord burn = {
        .surfaceId = 7u,
        .x = 0,
        .y = 10,
        .z = 0,
        .severity = 1.0f,
        .recovery = recovery,
        .ageSeconds = 32000.0f
    };
    assert(fwrite("WXIMPACT2", 1u, 9u, file) == 9u);
    assert(fwrite(&header, sizeof(header), 1u, file) == 1u);
    assert(fwrite(&burn, sizeof(burn), 1u, file) == 1u);
    rewind(file);
    return file;
}

static void LoadBurnRecoveryState(float recovery)
{
    FILE *file = BurnRecoveryState(recovery);
    assert(WeatherImpactLoadState(file));
    fclose(file);
}

static void TestNamedTreeFuelAndFireweedRecovery(void)
{
    static const BlockType logs[] = {
        BLOCK_OAK_LOG, BLOCK_BIRCH_LOG, BLOCK_ASPEN_LOG,
        BLOCK_SPRUCE_LOG, BLOCK_PINE_LOG, BLOCK_WILLOW_LOG
    };
    static const BlockType foliage[] = {
        BLOCK_OAK_LEAVES, BLOCK_BIRCH_LEAVES, BLOCK_ASPEN_LEAVES,
        BLOCK_SPRUCE_NEEDLES, BLOCK_PINE_NEEDLES, BLOCK_WILLOW_LEAVES
    };
    for (unsigned index = 0u; index < sizeof(logs) / sizeof(logs[0]); index++) {
        assert(WeatherImpactResidueForFuel(logs[index], 0.70f, 0.02f) ==
               BLOCK_CHARRED_WOOD);
        assert(WeatherImpactResidueForFuel(logs[index], 1.0f, 0.02f) ==
               BLOCK_CHARCOAL);
        ResetGrid(BLOCK_STONE);
        blocks[TEST_RADIUS][10][TEST_RADIUS] = foliage[index];
        WeatherImpactInit(true);
        assert(WeatherImpactIgniteAt(0, 10, 0, 1.0f));
        StepUniformTicks(1000u,
                               (Vector3){ 0.5f, 12.0f, 0.5f }, Dry());
        assert(GetBlockAt(0, 10, 0) == BLOCK_FIRE_ASH);
    }

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][9][TEST_RADIUS] = BLOCK_LOAM;
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_FIRE_ASH;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIREWEED);
    assert(lastMutationSource == WORLD_MUTATION_ENVIRONMENT);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][9][TEST_RADIUS] = BLOCK_LOAM;
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_FIRE_ASH;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    surfaceLoaded = false;
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    surfaceLoaded = true;
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIRE_ASH);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_FIRE_ASH;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIRE_ASH);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][9][TEST_RADIUS] = BLOCK_LOAM;
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_PLANK;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(GetBlockAt(0, 10, 0) == BLOCK_PLANK);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][9][TEST_RADIUS] = BLOCK_LOAM;
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_FIRE_ASH;
    blocks[TEST_RADIUS][11][TEST_RADIUS] = BLOCK_PLANK;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIRE_ASH);

    ResetGrid(BLOCK_STONE);
    blocks[TEST_RADIUS][9][TEST_RADIUS] = BLOCK_LOAM;
    blocks[TEST_RADIUS][10][TEST_RADIUS] = BLOCK_FIRE_ASH;
    WeatherImpactInit(true);
    LoadBurnRecoveryState(0.20f);
    FILE *file = tmpfile();
    assert(file);
    assert(WeatherImpactSaveState(file));
    rewind(file);
    WeatherImpactReset();
    assert(WeatherImpactLoadState(file));
    fclose(file);
    StepUniformTicks(1u, (Vector3){ 0.5f, 12.0f, 0.5f }, Rain());
    assert(GetBlockAt(0, 10, 0) == BLOCK_FIREWEED);
}

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
        .x = SURFACE_EQUATOR_BLOCKS + 2,
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
    const WorldSurfaceSnapshot snapshot = {
        .context = {
            .active = true,
            .kind = WORLD_SURFACE_HOME,
            .topology = SURFACE_TOPOLOGY_MAP_V1,
            .bodyId = 7u,
            .seed = 0x45a19c7du
        }
    };
    assert(WorldInstallSurfaceSnapshot(&snapshot));
    TestElapsedTimeIndependentOfRecordCount();
    TestInactiveSurfaceAndUnloadedFireClocks();
    TestOwnedMeltAtCapacity();
    TestSpreadStartsOnNextTick();
    TestImpactsUseTheirOwnWeather();
    TestPrecipitationAndIgnitionUseTheirOwnWeather();
    TestRainBudgetAndDisable();
    TestFireAndRainInteraction();
    TestIgnitionQueriesAndLoadedBounds();
    TestFuelConsumptionAndBurnScar();
    TestNamedTreeFuelAndFireweedRecovery();
    TestBoundedWorkAndNaturalSources();
    TestSnowOwnershipAndPlayerOverride();
    TestSaveLoadAndCorruption();
    TestSphericalFireAliasesAndLoadMigration();
    TestLegacyMigrationAndTransactionalFailure();
    puts("weather impact tests passed");
    return 0;
}
