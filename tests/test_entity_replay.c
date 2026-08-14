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

typedef struct TestEntityDiskStateV3 {
    TestEntityDiskStateV2 entity;
    float ecologyFoodAvailability;
    float ecologyWaterAvailability;
    float ecologyShelterAvailability;
    float ecologyStormPressure;
    float ecologyTemperatureStress;
    float energy;
    float hydration;
    float fatigue;
    float stress;
    uint32_t behavior;
} TestEntityDiskStateV3;

typedef struct TestEntityDiskStateV4 {
    TestEntityDiskStateV3 entity;
    uint32_t evolvable;
    uint32_t aquatic;
    uint32_t corpse;
    uint32_t pregnant;
    uint32_t sex;
    uint32_t organismId;
    uint32_t lineageId;
    uint32_t speciesId;
    uint32_t motherId;
    uint32_t fatherId;
    uint32_t pendingFatherId;
    float ageDays;
    float lifespanDays;
    float maturityAgeDays;
    float reproductionCooldownDays;
    float gestationProgressDays;
    float gestationDurationDays;
    float health;
    float corpseEnergy;
    int32_t targetEntity;
    CreatureGenome genome;
    CreatureGenome pendingOffspring;
} TestEntityDiskStateV4;

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
    local.suitability.floraActivity = 0.9f;
    local.suitability.temperatureScore = 1.0f;
    local.environment.liquidWaterAccess = 0.9f;
    local.environment.soilMoisture = 0.8f;
    local.environment.shelter = 0.8f;
    local.population.floraDensity = 0.9f;
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

bool PlanetEcologySampleGenome(int x, int z, float daylight,
                               uint32_t sampleSeed, CreatureGenome *outGenome,
                               uint32_t *outLineageId,
                               uint32_t *outSpeciesId)
{
    (void)x;
    (void)z;
    (void)daylight;
    (void)sampleSeed;
    (void)outGenome;
    (void)outLineageId;
    (void)outSpeciesId;
    return false;
}

bool PlanetEcologyRecordEvolutionEvent(
    int x, int z, float daylight, uint32_t lineageId,
    PlanetEvolutionEvent event, float biomass)
{
    (void)x;
    (void)z;
    (void)daylight;
    (void)lineageId;
    (void)event;
    (void)biomass;
    return true;
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

static void LoadVersion2MovingEntity(PlanetBodyPlan bodyPlan,
                                     float motionTargetYaw)
{
    const uint32_t header[3] = { 2u, MAX_ENTITIES, 0x51c73a29u };
    const float spawnTimer = 100.0f;
    TestEntityDiskStateV2 saved[MAX_ENTITIES] = { 0 };
    saved[0].entity = TestMovingEntity(bodyPlan);
    saved[0].motionTargetYaw = motionTargetYaw;
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(header, sizeof(header), 1, file) == 1);
    assert(fwrite(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fwrite(saved, sizeof(saved), 1, file) == 1);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static void LoadVersion3Entity(const TestEntityDiskStateV3 *entity)
{
    const uint32_t header[3] = { 3u, MAX_ENTITIES, 0x7d931b45u };
    const float spawnTimer = 100.0f;
    TestEntityDiskStateV3 saved[MAX_ENTITIES] = { 0 };
    saved[0] = *entity;
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(header, sizeof(header), 1, file) == 1);
    assert(fwrite(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fwrite(saved, sizeof(saved), 1, file) == 1);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static TestEntityDiskStateV3 CurrentMovingEntity(void)
{
    uint32_t header[3];
    float spawnTimer = 0.0f;
    TestEntityDiskStateV4 saved[MAX_ENTITIES];
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    rewind(file);
    assert(fread(header, sizeof(header), 1, file) == 1);
    assert(fread(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fread(saved, sizeof(saved), 1, file) == 1);
    fclose(file);
    assert(header[0] == 4u && header[1] == MAX_ENTITIES);
    assert(spawnTimer > 0.0f);
    assert(saved[0].entity.entity.entity.active == 1u);
    return saved[0].entity;
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
    TestEntityDiskStateV3 migrated = CurrentMovingEntity();
    assert(migrated.entity.motionTargetYaw == migrated.entity.entity.yaw);
    assert(migrated.energy == 0.82f);
    assert(migrated.hydration == 0.78f);
    assert(migrated.behavior == FAUNA_ACTION_WANDER);

    LoadVersion2MovingEntity(PLANET_BODY_QUADRUPED, 0.27f);
    TestEntityDiskStateV3 version2 = CurrentMovingEntity();
    assert(version2.entity.motionTargetYaw == 0.27f);
    assert(version2.energy == 0.82f);
    assert(version2.behavior == FAUNA_ACTION_WANDER);

    LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
    migrated = CurrentMovingEntity();

    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };
    RunFrames(&player, 12);
    TestEntityDiskStateV3 moved = CurrentMovingEntity();
    assert(moved.entity.entity.position[0] > migrated.entity.entity.position[0]);
    assert(moved.entity.entity.velocity[0] > 0.0f);
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
        TestEntityDiskStateV3 entity = CurrentMovingEntity();
        assert(entity.entity.entity.position[0] < 1.0f);
    }

    testTerrainMode = TEST_TERRAIN_STEP;
    LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
    RunFrames(&player, 30);
    TestEntityDiskStateV3 quadruped = CurrentMovingEntity();
    assert(quadruped.entity.entity.position[0] > 1.0f);
    assert(quadruped.entity.entity.position[1] >= 12.0f);

    LoadLegacyMovingEntity(PLANET_BODY_SERPENTINE);
    RunFrames(&player, 30);
    TestEntityDiskStateV3 serpentine = CurrentMovingEntity();
    assert(serpentine.entity.entity.position[0] < 1.0f);
    testTerrainMode = TEST_TERRAIN_FLAT;
}

static void TestTerrainFollowingFlight(void)
{
    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };

    testTerrainMode = TEST_TERRAIN_WALL;
    LoadLegacyMovingEntity(PLANET_BODY_FLOATING);
    RunFrames(&player, 20);
    TestEntityDiskStateV3 blocked = CurrentMovingEntity();
    assert(blocked.entity.entity.position[0] < 1.0f);

    testTerrainMode = TEST_TERRAIN_FLAT;
    LoadLegacyMovingEntity(PLANET_BODY_FLOATING);
    RunFrames(&player, 30);
    TestEntityDiskStateV3 floating = CurrentMovingEntity();
    assert(floating.entity.entity.position[0] > 1.0f);
    assert(floating.entity.entity.position[1] > 11.5f);
}

static TestEntityDiskStateV3 BehaviorTestEntity(void)
{
    LoadLegacyMovingEntity(PLANET_BODY_QUADRUPED);
    TestEntityDiskStateV3 entity = CurrentMovingEntity();
    entity.entity.entity.moveTimer = 0.0f;
    entity.entity.entity.thinkTimer = 0.01f;
    entity.entity.entity.velocity[0] = 0.0f;
    entity.entity.entity.velocity[2] = 0.0f;
    entity.ecologyFoodAvailability = 0.90f;
    entity.ecologyWaterAvailability = 0.90f;
    entity.ecologyShelterAvailability = 0.90f;
    entity.ecologyStormPressure = 0.0f;
    entity.ecologyTemperatureStress = 0.0f;
    entity.energy = 0.85f;
    entity.hydration = 0.85f;
    entity.fatigue = 0.10f;
    entity.stress = 0.0f;
    entity.behavior = FAUNA_ACTION_IDLE;
    return entity;
}

static TestEntityDiskStateV4 EvolutionTestEntity(
    EvolutionArchetype archetype, uint32_t seed, CreatureSex sex)
{
    TestEntityDiskStateV4 entity = { 0 };
    entity.entity = BehaviorTestEntity();
    entity.entity.entity.entity.type = archetype == EVOLUTION_ARCHETYPE_FLIGHT ?
        ENTITY_ALIEN_HOPPER : archetype == EVOLUTION_ARCHETYPE_AQUATIC ?
        ENTITY_ALIEN_STRIDER : ENTITY_ALIEN_GRAZER;
    entity.entity.entity.entity.bodyPlan = archetype ==
        EVOLUTION_ARCHETYPE_FLIGHT ? PLANET_BODY_FLOATING : archetype ==
        EVOLUTION_ARCHETYPE_AQUATIC ? PLANET_BODY_SERPENTINE :
        PLANET_BODY_QUADRUPED;
    entity.entity.entity.entity.airborne = archetype ==
        EVOLUTION_ARCHETYPE_FLIGHT;
    entity.entity.energy = 0.95f;
    entity.entity.hydration = 0.95f;
    entity.entity.entity.entity.thinkTimer = 0.01f;
    entity.evolvable = 1u;
    entity.aquatic = archetype == EVOLUTION_ARCHETYPE_AQUATIC;
    entity.sex = (uint32_t)sex;
    entity.genome = EvolutionGenomeSeed(seed, archetype);
    entity.organismId = seed ^ 0x51ed270bu;
    entity.lineageId = seed ^ 0xa511e9b3u;
    entity.speciesId = seed ^ 0x9e3779b9u;
    if (entity.organismId == 0u) entity.organismId = 1u;
    if (entity.lineageId == 0u) entity.lineageId = 2u;
    if (entity.speciesId == 0u) entity.speciesId = 3u;
    CreaturePhenotype phenotype = EvolutionDevelop(&entity.genome);
    assert(phenotype.valid);
    entity.ageDays = phenotype.maturityAgeDays + 2.0f;
    entity.lifespanDays = 180.0f;
    entity.maturityAgeDays = phenotype.maturityAgeDays;
    entity.gestationDurationDays = archetype == EVOLUTION_ARCHETYPE_AQUATIC ?
        3.0f : archetype == EVOLUTION_ARCHETYPE_FLIGHT ? 5.0f : 7.0f;
    entity.health = 1.0f;
    entity.targetEntity = -1;
    return entity;
}

static void LoadVersion4Entities(const TestEntityDiskStateV4 *first,
                                 const TestEntityDiskStateV4 *second)
{
    const uint32_t header[3] = { 4u, MAX_ENTITIES, 0x671fd2a9u };
    const float spawnTimer = 10000.0f;
    TestEntityDiskStateV4 saved[MAX_ENTITIES] = { 0 };
    if (first) saved[0] = *first;
    if (second) saved[1] = *second;
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(header, sizeof(header), 1, file) == 1);
    assert(fwrite(&spawnTimer, sizeof(spawnTimer), 1, file) == 1);
    assert(fwrite(saved, sizeof(saved), 1, file) == 1);
    rewind(file);
    assert(EntitiesLoadState(file));
    fclose(file);
}

static void TestEvolutionLifecycleAndPredation(void)
{
    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };
    testTerrainMode = TEST_TERRAIN_FLAT;

    TestEntityDiskStateV4 mother = EvolutionTestEntity(
        EVOLUTION_ARCHETYPE_GROUND, 0x11117231u, CREATURE_SEX_FEMALE);
    TestEntityDiskStateV4 father = EvolutionTestEntity(
        EVOLUTION_ARCHETYPE_GROUND, 0x22228463u, CREATURE_SEX_MALE);
    father.entity.entity.entity.position[0] = 0.65f;
    LoadVersion4Entities(&mother, &father);
    EntityEvolutionDebugInfo inspected = { 0 };
    assert(EntityEvolutionInspect(0, &inspected));
    assert(inspected.genomeId == mother.genome.genomeId);
    assert(inspected.moduleCount > 0u);
    RunFrames(&player, 3800);
    EntityEvolutionDebugInfo child = { 0 };
    assert(EntityEvolutionInspect(2, &child));
    assert(child.generation == 1u);
    assert(child.juvenile);
    assert(child.mutationCount <= 100u);
    FILE *familyState = tmpfile();
    assert(familyState);
    assert(EntitiesSaveState(familyState));
    uint32_t familyHeader[3];
    float familySpawnTimer = 0.0f;
    TestEntityDiskStateV4 family[MAX_ENTITIES];
    rewind(familyState);
    assert(fread(familyHeader, sizeof(familyHeader), 1, familyState) == 1);
    assert(fread(&familySpawnTimer, sizeof(familySpawnTimer), 1,
                 familyState) == 1);
    assert(fread(family, sizeof(family), 1, familyState) == 1);
    fclose(familyState);
    assert(familyHeader[0] == 4u);
    assert(family[0].motherId == mother.motherId);
    assert(family[0].fatherId == mother.fatherId);
    assert(family[0].pendingFatherId == 0u);
    assert(family[2].motherId == mother.organismId);
    assert(family[2].fatherId == father.organismId);

    TestEntityDiskStateV4 predator = EvolutionTestEntity(
        EVOLUTION_ARCHETYPE_AQUATIC, 0x33339695u, CREATURE_SEX_MALE);
    TestEntityDiskStateV4 prey = EvolutionTestEntity(
        EVOLUTION_ARCHETYPE_GROUND, 0x4444a8c7u, CREATURE_SEX_MALE);
    predator.entity.energy = 0.45f;
    prey.entity.entity.entity.position[0] = 0.60f;
    LoadVersion4Entities(&predator, &prey);
    RunFrames(&player, 300);
    EntityEvolutionDebugInfo corpse = { 0 };
    assert(EntityEvolutionInspect(1, &corpse));
    assert(corpse.corpse);
    assert(corpse.health == 0.0f);
}

static void TestNeedsDriveEntityBehavior(void)
{
    Player player = { 0 };
    player.position = (Vector3){ -20.0f, 12.0f, 0.5f };
    testTerrainMode = TEST_TERRAIN_FLAT;

    TestEntityDiskStateV3 hungry = BehaviorTestEntity();
    hungry.energy = 0.03f;
    LoadVersion3Entity(&hungry);
    RunFrames(&player, 2);
    TestEntityDiskStateV3 feeding = CurrentMovingEntity();
    assert(feeding.behavior == FAUNA_ACTION_FORAGE);
    assert(feeding.energy > hungry.energy);

    TestEntityDiskStateV3 thirsty = BehaviorTestEntity();
    thirsty.hydration = 0.03f;
    LoadVersion3Entity(&thirsty);
    RunFrames(&player, 2);
    TestEntityDiskStateV3 drinking = CurrentMovingEntity();
    assert(drinking.behavior == FAUNA_ACTION_DRINK);
    assert(drinking.hydration > thirsty.hydration);

    TestEntityDiskStateV3 tired = BehaviorTestEntity();
    tired.fatigue = 0.95f;
    LoadVersion3Entity(&tired);
    RunFrames(&player, 2);
    TestEntityDiskStateV3 resting = CurrentMovingEntity();
    assert(resting.behavior == FAUNA_ACTION_REST);
    assert(resting.fatigue < tired.fatigue);
}

int main(void)
{
    TestEntityReplay();
    TestEntityLoadIsAtomic();
    TestLegacyEntityMotionMigration();
    TestTerrainAwareGroundMotion();
    TestTerrainFollowingFlight();
    TestNeedsDriveEntityBehavior();
    TestEvolutionLifecycleAndPredation();
    puts("entity replay tests passed");
    return 0;
}
