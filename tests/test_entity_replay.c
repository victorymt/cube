#include "entity.h"
#include "ecology_model.h"
#include "terrain.h"
#include "weather_model.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t testSeed = 0x51f15eadu;

typedef enum TestTerrainMode {
    TEST_TERRAIN_FLAT = 0,
    TEST_TERRAIN_WALL,
    TEST_TERRAIN_WATER,
    TEST_TERRAIN_CLIFF,
    TEST_TERRAIN_STEP
} TestTerrainMode;

typedef struct TestEntityDiskStateV1 {
    uint32_t active;
    uint32_t type;
    float position[3];
    float velocity[3];
    float yaw;
    float moveTimer;
    float thinkTimer;
    float burnTimer;
    uint32_t bodyPlan;
    uint32_t chemistry;
    uint32_t niche;
    float organismScale;
    float bodyArmor;
    float movementSpeed;
    float temperament;
    int32_t limbCount;
    uint32_t airborne;
    uint32_t colony;
    float hoverHeight;
    float phase;
    float ecologyActivity;
    float ecologyCapacity;
    float ecologySampleTimer;
    float ecologyWindStrength;
    float ecologyWindAngle;
    uint32_t primaryBlock;
    uint32_t accentBlock;
} TestEntityDiskStateV1;

typedef struct TestEntityDiskStateV2 {
    TestEntityDiskStateV1 entity;
    float motionTargetYaw;
} TestEntityDiskStateV2;

static TestTerrainMode testTerrainMode = TEST_TERRAIN_FLAT;

uint32_t WorldGetSeed(void)
{
    return testSeed;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 0u;
}

bool PlanetWorldIsActive(void)
{
    return false;
}

uint32_t PlanetWorldSeed(void)
{
    return testSeed;
}

const PlanetProfile *PlanetWorldProfile(void)
{
    static PlanetProfile profile;
    return &profile;
}

PlanetBiome PlanetBiomeAt(int x, int z)
{
    (void)x;
    (void)z;
    return PLANET_BIOME_PLAINS;
}

PlanetLocalEcology PlanetEcologyLocalAt(int x, int z, float daylight)
{
    (void)x;
    (void)z;
    (void)daylight;
    PlanetLocalEcology local = { 0 };
    local.suitability.faunaActivity = 1.0f;
    local.suitability.faunaCapacity = 1.0f;
    return local;
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile profile = { 0 };
    profile.faunaDensity = 1.0f;
    profile.bodyPlan = PLANET_BODY_QUADRUPED;
    profile.chemistry = PLANET_CHEMISTRY_CARBON;
    profile.niche = PLANET_NICHE_GRAZER;
    profile.organismScale = 1.0f;
    profile.movementSpeed = 0.85f;
    profile.limbCount = 4;
    profile.primaryBlock = BLOCK_GRASS;
    profile.accentBlock = BLOCK_DIRT;
    return profile;
}

float PlanetEcologyFaunaDensityAt(int x, int z, float daylight)
{
    (void)x;
    (void)z;
    (void)daylight;
    return 1.0f;
}

WeatherFieldSample WeatherFieldSampleAtWorld(int x, int z)
{
    (void)x;
    (void)z;
    WeatherFieldSample sample = { 0 };
    return sample;
}

float WeatherWindAngleAtWorld(int x, int z)
{
    (void)x;
    (void)z;
    return 0.0f;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    return y >= 0 && y < WORLD_HEIGHT
        ? WORLD_BLOCK_REGION_SURFACE : WORLD_BLOCK_REGION_NONE;
}

float WorldGravityScale(void)
{
    return 1.0f;
}

int WorldSurfaceHeightAt(int x, int z)
{
    (void)x;
    (void)z;
    return 10;
}

BlockType GetBlockAt(int x, int y, int z)
{
    (void)z;
    if (testTerrainMode == TEST_TERRAIN_WALL && x >= 1) {
        return y <= 20 ? BLOCK_DIRT : BLOCK_AIR;
    }
    if (testTerrainMode == TEST_TERRAIN_CLIFF && x >= 1) {
        return BLOCK_AIR;
    }
    if (testTerrainMode == TEST_TERRAIN_STEP && x >= 1) {
        return y <= 11 ? BLOCK_DIRT : BLOCK_AIR;
    }
    if (testTerrainMode == TEST_TERRAIN_WATER && x >= 1 && y == 11) {
        return BLOCK_WATER;
    }
    return y <= 10 ? BLOCK_DIRT : BLOCK_AIR;
}

bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}

void ParticlesEmitBurst(Vector3 position, Color color, int count,
                        float speed, float life)
{
    (void)position;
    (void)color;
    (void)count;
    (void)speed;
    (void)life;
}

void AudioPlayBreak(void)
{
}

static unsigned char *CaptureState(size_t *size)
{
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    long end = ftell(file);
    assert(end > 0);
    *size = (size_t)end;
    unsigned char *data = malloc(*size);
    assert(data);
    rewind(file);
    assert(fread(data, 1, *size, file) == *size);
    fclose(file);
    return data;
}

static void RunFrames(Player *player, int count)
{
    for (int frame = 0; frame < count; frame++) {
        EntitiesUpdate(0.1f, player, 1.0f);
    }
}

static void LoadBytes(const unsigned char *data, size_t size)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(data, 1, size, file) == size);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static TestEntityDiskStateV1 TestMovingEntity(PlanetBodyPlan bodyPlan)
{
    TestEntityDiskStateV1 entity = { 0 };
    entity.active = 1u;
    entity.type = ENTITY_COW;
    entity.position[0] = 0.5f;
    entity.position[1] = 11.0f;
    entity.position[2] = 0.5f;
    entity.yaw = 0.5f * 3.14159265358979323846f;
    entity.moveTimer = 20.0f;
    entity.thinkTimer = 100.0f;
    entity.bodyPlan = (uint32_t)bodyPlan;
    entity.chemistry = PLANET_CHEMISTRY_CARBON;
    entity.niche = PLANET_NICHE_GRAZER;
    entity.organismScale = 1.0f;
    entity.movementSpeed = 1.0f;
    entity.temperament = 0.2f;
    entity.limbCount = bodyPlan == PLANET_BODY_SERPENTINE ? 0 : 4;
    entity.airborne = bodyPlan == PLANET_BODY_FLOATING ? 1u : 0u;
    entity.colony = bodyPlan == PLANET_BODY_COLONY ? 1u : 0u;
    entity.hoverHeight = 11.0f;
    entity.ecologyActivity = 1.0f;
    entity.ecologyCapacity = 1.0f;
    entity.ecologySampleTimer = 1.0f;
    entity.primaryBlock = BLOCK_GRASS;
    entity.accentBlock = BLOCK_DIRT;
    return entity;
}

static void LoadLegacyMovingEntity(PlanetBodyPlan bodyPlan)
{
    const uint32_t header[3] = { 1u, MAX_ENTITIES, 0x31f2a7cdu };
    const float spawnTimer = 100.0f;
    TestEntityDiskStateV1 saved[MAX_ENTITIES] = { 0 };
    saved[0] = TestMovingEntity(bodyPlan);
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(header, sizeof(header), 1, file) == 1);
    assert(fwrite(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fwrite(saved, sizeof(saved), 1, file) == 1);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static TestEntityDiskStateV2 CurrentMovingEntity(void)
{
    uint32_t header[3];
    float spawnTimer = 0.0f;
    TestEntityDiskStateV2 saved[MAX_ENTITIES];
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    rewind(file);
    assert(fread(header, sizeof(header), 1, file) == 1);
    assert(fread(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fread(saved, sizeof(saved), 1, file) == 1);
    fclose(file);
    assert(header[0] == 2u && header[1] == MAX_ENTITIES);
    assert(spawnTimer > 0.0f);
    assert(saved[0].entity.active == 1u);
    return saved[0];
}

static void TestEntityReplay(void)
{
    Player player = { 0 };
    player.position = (Vector3){ 0.5f, 12.0f, 0.5f };

    EntitiesInit();
    RunFrames(&player, 3);
    size_t checkpointSize = 0;
    unsigned char *checkpoint = CaptureState(&checkpointSize);
    RunFrames(&player, 80);
    size_t expectedSize = 0;
    unsigned char *expected = CaptureState(&expectedSize);

    LoadBytes(checkpoint, checkpointSize);
    RunFrames(&player, 80);
    size_t replaySize = 0;
    unsigned char *replay = CaptureState(&replaySize);
    assert(replaySize == expectedSize);
    assert(memcmp(replay, expected, expectedSize) == 0);

    srand(1);
    EntitiesInit();
    RunFrames(&player, 80);
    size_t firstSize = 0;
    unsigned char *first = CaptureState(&firstSize);
    srand(9999);
    EntitiesInit();
    RunFrames(&player, 80);
    size_t secondSize = 0;
    unsigned char *second = CaptureState(&secondSize);
    assert(firstSize == secondSize);
    assert(memcmp(first, second, firstSize) == 0);

    free(checkpoint);
    free(expected);
    free(replay);
    free(first);
    free(second);
}

static void TestEntityLoadIsAtomic(void)
{
    EntitiesInit();
    Player player = { 0 };
    player.position = (Vector3){ 0.5f, 12.0f, 0.5f };
    RunFrames(&player, 5);
    size_t beforeSize = 0;
    unsigned char *before = CaptureState(&beforeSize);

    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(before, 1, beforeSize - 1, file) == beforeSize - 1);
    rewind(file);
    assert(!EntitiesLoadState(file));
    fclose(file);

    size_t afterSize = 0;
    unsigned char *after = CaptureState(&afterSize);
    assert(afterSize == beforeSize);
    assert(memcmp(after, before, beforeSize) == 0);
    free(before);
    free(after);
}

static void TestLegacyEntityMotionMigration(void)
{
    testTerrainMode = TEST_TERRAIN_FLAT;
    LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
    TestEntityDiskStateV2 migrated = CurrentMovingEntity();
    assert(migrated.motionTargetYaw == migrated.entity.yaw);

    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };
    RunFrames(&player, 12);
    TestEntityDiskStateV2 moved = CurrentMovingEntity();
    assert(moved.entity.position[0] > migrated.entity.position[0]);
    assert(moved.entity.velocity[0] > 0.0f);
}

static void TestTerrainAwareGroundMotion(void)
{
    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };
    const TestTerrainMode blockedModes[] = {
        TEST_TERRAIN_WALL,
        TEST_TERRAIN_WATER,
        TEST_TERRAIN_CLIFF
    };
    for (unsigned index = 0;
         index < sizeof(blockedModes) / sizeof(blockedModes[0]); index++) {
        testTerrainMode = blockedModes[index];
        LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
        RunFrames(&player, 30);
        TestEntityDiskStateV2 entity = CurrentMovingEntity();
        assert(entity.entity.position[0] < 1.0f);
    }

    testTerrainMode = TEST_TERRAIN_STEP;
    LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
    RunFrames(&player, 30);
    TestEntityDiskStateV2 quadruped = CurrentMovingEntity();
    assert(quadruped.entity.position[0] > 1.0f);
    assert(quadruped.entity.position[1] >= 12.0f);

    LoadLegacyMovingEntity(PLANET_BODY_SERPENTINE);
    RunFrames(&player, 30);
    TestEntityDiskStateV2 serpentine = CurrentMovingEntity();
    assert(serpentine.entity.position[0] < 1.0f);
    testTerrainMode = TEST_TERRAIN_FLAT;
}

static void TestTerrainFollowingFlight(void)
{
    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };

    testTerrainMode = TEST_TERRAIN_WALL;
    LoadLegacyMovingEntity(PLANET_BODY_FLOATING);
    RunFrames(&player, 20);
    TestEntityDiskStateV2 blocked = CurrentMovingEntity();
    assert(blocked.entity.position[0] < 1.0f);

    testTerrainMode = TEST_TERRAIN_FLAT;
    LoadLegacyMovingEntity(PLANET_BODY_FLOATING);
    RunFrames(&player, 30);
    TestEntityDiskStateV2 floating = CurrentMovingEntity();
    assert(floating.entity.position[0] > 1.0f);
    assert(floating.entity.position[1] > 11.5f);
}

int main(void)
{
    TestEntityReplay();
    TestEntityLoadIsAtomic();
    TestLegacyEntityMotionMigration();
    TestTerrainAwareGroundMotion();
    TestTerrainFollowingFlight();
    puts("entity replay tests passed");
    return 0;
}
