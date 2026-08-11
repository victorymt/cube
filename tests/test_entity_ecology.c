#include "chunks.h"
#include "ecology.h"
#include "ecology_test_fixture.h"
#include "entity.h"
#include "particles.h"
#include "space.h"
#include "terrain.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    ENTITY_ECOLOGY_PROPERTY_SEEDS = 6,
    ENTITY_ECOLOGY_SEASON_PHASES = 8,
    ENTITY_ECOLOGY_SETTLE_FRAMES = MAX_ENTITIES * 24,
    ENTITY_ECOLOGY_STABILITY_FRAMES = 180
};

typedef struct FertileSite {
    uint32_t seed;
    int originZ;
    int x;
    int z;
    float initialActivity;
} FertileSite;

typedef struct EntityEcologyRunSummary {
    int caps[ENTITY_ECOLOGY_SEASON_PHASES];
    int counts[ENTITY_ECOLOGY_SEASON_PHASES];
    float activities[ENTITY_ECOLOGY_SEASON_PHASES];
    float temperatures[ENTITY_ECOLOGY_SEASON_PHASES];
    float migrationSignal;
    int highCountBeforeMove;
    int highCapBeforeMove;
    int lowCount;
    int lowCap;
} EntityEcologyRunSummary;

uint32_t WorldCurrentSurfaceId(void)
{
    return PlanetWorldIsActive() ? PlanetWorldSeed() : 0u;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    return y >= 0 && y < WORLD_HEIGHT
        ? WORLD_BLOCK_REGION_SURFACE : WORLD_BLOCK_REGION_NONE;
}

float WorldGravityScale(void)
{
    return PlanetWorldGravityScale();
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

Color ColorLerp(Color color1, Color color2, float factor)
{
    (void)color2;
    (void)factor;
    return color1;
}

static unsigned char *CaptureEntityState(size_t *outSize)
{
    assert(outSize);
    FILE *file = tmpfile();
    assert(file);
    assert(EntitiesSaveState(file));
    long end = ftell(file);
    assert(end > 0);
    *outSize = (size_t)end;
    unsigned char *bytes = malloc(*outSize);
    assert(bytes);
    rewind(file);
    assert(fread(bytes, 1, *outSize, file) == *outSize);
    fclose(file);
    return bytes;
}

static void SaveSimulation(FILE *file)
{
    assert(file);
    assert(SpaceSaveState(file));
    assert(PlanetWorldSaveState(file));
    assert(PlanetEcologySaveState(file));
    assert(EntitiesSaveState(file));
}

static unsigned char *CaptureSimulationState(size_t *outSize)
{
    assert(outSize);
    FILE *file = tmpfile();
    assert(file);
    SaveSimulation(file);
    long end = ftell(file);
    assert(end > 0);
    *outSize = (size_t)end;
    unsigned char *bytes = malloc(*outSize);
    assert(bytes);
    rewind(file);
    assert(fread(bytes, 1, *outSize, file) == *outSize);
    fclose(file);
    return bytes;
}

static void LoadSimulation(FILE *file)
{
    assert(file);
    rewind(file);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));
    assert(EntitiesLoadState(file));
}

static void RunFrames(Player *player, int frameCount, float daylight)
{
    const float dt = 0.1f;
    for (int frame = 0; frame < frameCount; frame++) {
        SpaceAdvanceTime(dt);
        EntitiesUpdate(dt, player, daylight);
    }
}

static void RunEntityFrames(Player *player, int frameCount, float daylight)
{
    const float dt = 0.1f;
    for (int frame = 0; frame < frameCount; frame++) {
        EntitiesUpdate(dt, player, daylight);
    }
}

static uint32_t ActivateFertilePlanet(Player *player, float daylight,
                                     float *outFaunaActivity)
{
    assert(player && outFaunaActivity);
    for (uint32_t index = 0; index < 4096u; index++) {
        uint32_t seed = 0x51a7e5edu + index * 0x9e3779b9u;
        EcologyTestSetSeed(seed);
        EcologyTestActivatePlanet(seed, 0, 0);
        PlanetEcologyResetState();
        if (PlanetEcologyCurrent().faunaDensity <= 0.02f) continue;

        for (int sample = 0; sample < 256; sample++) {
            int x = ((sample * 83) % 1024) - 512;
            int z = ((sample * sample * 47) % 1024) - 512;
            float activity = PlanetEcologyFaunaDensityAt(x, z, daylight);
            if (PlanetFaunaPopulationCap(activity, MAX_ENTITIES - 4) <= 0) {
                continue;
            }
            int groundY = PlanetTerrainHeight(x, z);
            player->position = (Vector3){
                (float)x + 0.5f, (float)groundY + 2.0f, (float)z + 0.5f
            };
            *outFaunaActivity = activity;
            return seed;
        }
    }
    assert(false);
    return 0u;
}

static bool TestBiomeSupportsFauna(int x, int z)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    return biome != PLANET_BIOME_OCEAN &&
        biome != PLANET_BIOME_LAVA_SEA &&
        biome != PLANET_BIOME_STORM_BANDS &&
        biome != PLANET_BIOME_VOLCANIC_RIDGE;
}

static void AssertUnitValue(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f && value <= 1.0f);
}

static void AssertSignedUnitValue(float value)
{
    assert(isfinite(value));
    assert(value >= -1.0f && value <= 1.0f);
}

static void AssertEntityLocalEcologyValid(const PlanetLocalEcology *local)
{
    assert(local);
    assert(isfinite(local->environment.currentTemperatureK));
    assert(local->environment.currentTemperatureK > 0.0f);
    AssertUnitValue(local->suitability.faunaActivity);
    AssertUnitValue(local->suitability.faunaCapacity);
    AssertUnitValue(local->population.floraDensity);
    AssertUnitValue(local->population.faunaDensity);
    AssertUnitValue(local->population.floraCarryingCapacity);
    AssertUnitValue(local->population.faunaCarryingCapacity);
    AssertUnitValue(local->population.seasonalMemory);
    AssertUnitValue(local->population.faunaHarvestPressure);
    AssertUnitValue(local->diagnostics.habitatStress);
    AssertUnitValue(local->diagnostics.harvestStress);
    AssertUnitValue(local->diagnostics.faunaStress);
    assert(isfinite(local->diagnostics.faunaNetRecoveryRate));
    assert(local->diagnostics.faunaNetRecoveryRate >= -1.0f &&
           local->diagnostics.faunaNetRecoveryRate <= 1.0f);
    AssertSignedUnitValue(local->migration.floraNet);
    AssertSignedUnitValue(local->migration.faunaNet);
    AssertSignedUnitValue(local->migration.floraFlowX);
    AssertSignedUnitValue(local->migration.floraFlowZ);
    AssertSignedUnitValue(local->migration.faunaFlowX);
    AssertSignedUnitValue(local->migration.faunaFlowZ);
}

static float EntityMigrationSignal(const PlanetPopulationMigrationState *state)
{
    assert(state);
    return fabsf(state->floraNet) + fabsf(state->faunaNet) +
        fabsf(state->floraFlowX) + fabsf(state->floraFlowZ) +
        fabsf(state->faunaFlowX) + fabsf(state->faunaFlowZ);
}

static int EntityPopulationCapAt(int x, int z, float daylight)
{
    return PlanetFaunaPopulationCap(
        PlanetEcologyFaunaDensityAt(x, z, daylight), MAX_ENTITIES - 4);
}

static void PlacePlayerAt(Player *player, int x, int z)
{
    assert(player);
    player->position = (Vector3){
        (float)x + 0.5f,
        (float)PlanetTerrainHeight(x, z) + 2.0f,
        (float)z + 0.5f
    };
}

static void PrepareChunksAt(const Player *player)
{
    assert(player);
    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
}

static int SettleEntitiesAtCurrentTime(Player *player, float daylight,
                                       int *outCap)
{
    assert(player && outCap);
    RunEntityFrames(player, ENTITY_ECOLOGY_SETTLE_FRAMES, daylight);
    int x = (int)floorf(player->position.x);
    int z = (int)floorf(player->position.z);
    int cap = EntityPopulationCapAt(x, z, daylight);
    int count = GetActiveEntityCount();
    assert(count >= 0 && count <= cap);
    if (cap == 0) assert(count == 0);

    for (int frame = 0; frame < ENTITY_ECOLOGY_STABILITY_FRAMES; frame++) {
        RunEntityFrames(player, 1, daylight);
        if (frame % 15 == 14) {
            cap = EntityPopulationCapAt(x, z, daylight);
            count = GetActiveEntityCount();
            assert(count >= 0 && count <= cap);
        }
    }
    *outCap = EntityPopulationCapAt(x, z, daylight);
    count = GetActiveEntityCount();
    assert(count <= *outCap);
    return count;
}

static float LowestNearbyFaunaActivity(int centerX, int centerZ,
                                       float daylight, int *outX, int *outZ)
{
    assert(outX && outZ);
    float lowest = PlanetEcologyFaunaDensityAt(
        centerX, centerZ, daylight);
    *outX = centerX;
    *outZ = centerZ;
    for (int dz = -64; dz <= 64; dz += 16) {
        for (int dx = -64; dx <= 64; dx += 16) {
            if (dx == 0 && dz == 0) continue;
            if (dx * dx + dz * dz > 64 * 64) continue;
            int x = centerX + dx;
            int z = centerZ + dz;
            float activity = PlanetEcologyFaunaDensityAt(x, z, daylight);
            if (activity < lowest) {
                lowest = activity;
                *outX = x;
                *outZ = z;
            }
        }
    }
    return lowest;
}

static int CollectFertileSites(FertileSite *sites, int maximumSites,
                               float daylight)
{
    assert(sites && maximumSites > 0);
    int count = 0;
    for (uint32_t index = 0; index < 4096u && count < maximumSites; index++) {
        uint32_t seed = 0x738bc19du + index * 0x9e3779b9u;
        int latitudeBand = 1536 + (int)(index % 3u) * 256;
        int originZ = (index & 1u) ? latitudeBand : -latitudeBand;
        EcologyTestSetSeed(seed);
        EcologyTestActivatePlanet(seed, 0, originZ);
        PlanetEcologyResetState();
        if (PlanetEcologyCurrent().faunaDensity <= 0.04f) continue;

        float bestScore = -1.0f;
        FertileSite best = { 0 };
        for (int sample = 0; sample < 256; sample++) {
            int x = ((sample * 83) % 512) - 256;
            int z = ((sample * sample * 47) % 512) - 256;
            if (!TestBiomeSupportsFauna(x, z)) continue;
            PlanetLocalEcology local = PlanetEcologyLocalAt(
                x, z, daylight);
            AssertEntityLocalEcologyValid(&local);
            if (PlanetFaunaPopulationCap(
                    local.suitability.faunaActivity,
                    MAX_ENTITIES - 4) < 2 ||
                local.environment.seasonalAmplitudeK < 2.0f) {
                continue;
            }
            float score = local.suitability.faunaActivity +
                fminf(local.environment.seasonalAmplitudeK / 70.0f,
                      1.0f) * 0.08f;
            if (score > bestScore) {
                bestScore = score;
                best = (FertileSite){
                    .seed = seed,
                    .originZ = originZ,
                    .x = x,
                    .z = z,
                    .initialActivity = local.suitability.faunaActivity
                };
            }
        }
        if (bestScore < 0.0f) continue;

        int lowX = best.x;
        int lowZ = best.z;
        float lowActivity = LowestNearbyFaunaActivity(
            best.x, best.z, daylight, &lowX, &lowZ);
        int highCap = PlanetFaunaPopulationCap(
            best.initialActivity, MAX_ENTITIES - 4);
        int lowCap = PlanetFaunaPopulationCap(
            lowActivity, MAX_ENTITIES - 4);
        if (lowCap >= highCap) continue;
        sites[count++] = best;
    }
    return count;
}

static void RunSeasonalEntityScenario(
    const FertileSite *site, Player *player, float daylight,
    EntityEcologyRunSummary *summary)
{
    assert(site && player && summary);
    memset(summary, 0, sizeof(*summary));
    PlacePlayerAt(player, site->x, site->z);
    PrepareChunksAt(player);

    summary->highCountBeforeMove = SettleEntitiesAtCurrentTime(
        player, daylight, &summary->highCapBeforeMove);
    int lowX = site->x;
    int lowZ = site->z;
    LowestNearbyFaunaActivity(
        site->x, site->z, daylight, &lowX, &lowZ);
    PlacePlayerAt(player, lowX, lowZ);
    PrepareChunksAt(player);
    summary->lowCount = SettleEntitiesAtCurrentTime(
        player, daylight, &summary->lowCap);
    assert(summary->lowCount <= summary->lowCap);
    PlacePlayerAt(player, site->x, site->z);
    PrepareChunksAt(player);

    const PlanetProfile *profile = PlanetWorldProfile();
    assert(profile);
    assert(isfinite(profile->yearLength) && profile->yearLength > 0.0f);
    float phaseDuration = profile->yearLength /
        (float)ENTITY_ECOLOGY_SEASON_PHASES;
    assert(phaseDuration > 0.0f);

    for (int phase = 0; phase < ENTITY_ECOLOGY_SEASON_PHASES; phase++) {
        SpaceAdvanceTime(phaseDuration);
        int x = (int)floorf(player->position.x);
        int z = (int)floorf(player->position.z);
        PlanetLocalEcology local = PlanetEcologyLocalAt(
            x, z, daylight);
        AssertEntityLocalEcologyValid(&local);

        int cap = 0;
        int count = SettleEntitiesAtCurrentTime(player, daylight, &cap);
        local = PlanetEcologyLocalAt(x, z, daylight);
        AssertEntityLocalEcologyValid(&local);
        int finalCap = PlanetFaunaPopulationCap(
            local.suitability.faunaActivity, MAX_ENTITIES - 4);
        assert(cap == finalCap);
        assert(count <= finalCap);

        summary->caps[phase] = finalCap;
        summary->counts[phase] = count;
        summary->activities[phase] = local.suitability.faunaActivity;
        summary->temperatures[phase] =
            local.environment.currentTemperatureK;
        summary->migrationSignal += EntityMigrationSignal(&local.migration);
    }
}

static void AssertEntityEcologySummariesEqual(
    const EntityEcologyRunSummary *first,
    const EntityEcologyRunSummary *second)
{
    assert(first && second);
    for (int phase = 0; phase < ENTITY_ECOLOGY_SEASON_PHASES; phase++) {
        assert(first->caps[phase] == second->caps[phase]);
        assert(first->counts[phase] == second->counts[phase]);
        assert(first->activities[phase] == second->activities[phase]);
        assert(first->temperatures[phase] == second->temperatures[phase]);
    }
    assert(first->migrationSignal == second->migrationSignal);
    assert(first->highCountBeforeMove == second->highCountBeforeMove);
    assert(first->highCapBeforeMove == second->highCapBeforeMove);
    assert(first->lowCount == second->lowCount);
    assert(first->lowCap == second->lowCap);
}

static void TestCrossSeedSeasonalEntityProperties(void)
{
    const float daylight = 0.72f;
    FertileSite sites[ENTITY_ECOLOGY_PROPERTY_SEEDS];
    FILE *spaceBaseline = tmpfile();
    assert(spaceBaseline);
    assert(SpaceSaveState(spaceBaseline));
    int siteCount = CollectFertileSites(
        sites, ENTITY_ECOLOGY_PROPERTY_SEEDS, daylight);
    assert(siteCount == ENTITY_ECOLOGY_PROPERTY_SEEDS);

    int temperatureResponsiveSeeds = 0;
    int activityResponsiveSeeds = 0;
    int migrationSeeds = 0;
    int entitySeeds = 0;
    int lowerCapacitySeeds = 0;
    int contractionOpportunities = 0;
    assert(ChunksStartGenThread());

    for (int index = 0; index < siteCount; index++) {
        const FertileSite *site = &sites[index];
        rewind(spaceBaseline);
        assert(SpaceLoadState(spaceBaseline));
        EcologyTestSetSeed(site->seed);
        EcologyTestActivatePlanet(site->seed, 0, site->originZ);
        PlanetEcologyResetState();
        EntitiesInit();
        UnloadAllChunks();

        Player player = { 0 };
        PlacePlayerAt(&player, site->x, site->z);
        PrepareChunksAt(&player);
        PlanetLocalEcology initial = PlanetEcologyLocalAt(
            site->x, site->z, daylight);
        AssertEntityLocalEcologyValid(&initial);

        FILE *checkpoint = tmpfile();
        assert(checkpoint);
        SaveSimulation(checkpoint);
        UnloadAllChunks();
        EntityEcologyRunSummary expected;
        RunSeasonalEntityScenario(site, &player, daylight, &expected);
        size_t expectedSize = 0;
        unsigned char *expectedState = CaptureSimulationState(&expectedSize);

        UnloadAllChunks();
        EcologyTestSetSeed(site->seed);
        LoadSimulation(checkpoint);
        EntityEcologyRunSummary replay;
        RunSeasonalEntityScenario(site, &player, daylight, &replay);
        size_t replaySize = 0;
        unsigned char *replayState = CaptureSimulationState(&replaySize);
        AssertEntityEcologySummariesEqual(&expected, &replay);
        assert(replaySize == expectedSize);
        assert(memcmp(replayState, expectedState, expectedSize) == 0);

        float minTemperature = expected.temperatures[0];
        float maxTemperature = expected.temperatures[0];
        float minActivity = expected.activities[0];
        float maxActivity = expected.activities[0];
        bool sawEntity = false;
        for (int phase = 0; phase < ENTITY_ECOLOGY_SEASON_PHASES; phase++) {
            minTemperature = fminf(
                minTemperature, expected.temperatures[phase]);
            maxTemperature = fmaxf(
                maxTemperature, expected.temperatures[phase]);
            minActivity = fminf(minActivity, expected.activities[phase]);
            maxActivity = fmaxf(maxActivity, expected.activities[phase]);
            assert(expected.counts[phase] >= 0);
            assert(expected.counts[phase] <= expected.caps[phase]);
            if (expected.counts[phase] > 0) sawEntity = true;
        }
        if (maxTemperature - minTemperature > 0.5f) {
            temperatureResponsiveSeeds++;
        }
        if (maxActivity - minActivity > 0.0005f) {
            activityResponsiveSeeds++;
        }
        if (expected.migrationSignal > 0.000001f) migrationSeeds++;
        if (sawEntity) entitySeeds++;
        if (expected.lowCap < expected.highCapBeforeMove) {
            lowerCapacitySeeds++;
        }
        if (expected.highCountBeforeMove > expected.lowCap) {
            contractionOpportunities++;
            assert(expected.lowCount < expected.highCountBeforeMove);
        }

        free(replayState);
        free(expectedState);
        fclose(checkpoint);
    }

    assert(temperatureResponsiveSeeds >= siteCount - 1);
    assert(activityResponsiveSeeds > 0);
    assert(migrationSeeds > 0);
    assert(entitySeeds > 0);
    assert(lowerCapacitySeeds > 0);
    assert(contractionOpportunities > 0);

    UnloadAllChunks();
    ChunksShutdownGenThread();
    rewind(spaceBaseline);
    assert(SpaceLoadState(spaceBaseline));
    fclose(spaceBaseline);
    PlanetEcologyResetState();
    EntitiesInit();
    printf("entity ecology properties: seeds=%d seasonal=%d activity=%d "
           "migration=%d entities=%d local-drops=%d contractions=%d\n",
           siteCount, temperatureResponsiveSeeds, activityResponsiveSeeds,
           migrationSeeds, entitySeeds, lowerCapacitySeeds,
           contractionOpportunities);
}

static float MaxNearbyHarvestPressure(int centerX, int centerZ,
                                      float daylight)
{
    float maximum = 0.0f;
    for (int dz = -96; dz <= 96; dz += 16) {
        for (int dx = -96; dx <= 96; dx += 16) {
            PlanetLocalEcology local = PlanetEcologyLocalAt(
                centerX + dx, centerZ + dz, daylight);
            maximum = fmaxf(
                maximum, local.population.faunaHarvestPressure);
        }
    }
    return maximum;
}

static void WaitForEntitySpawn(Player *player, float daylight)
{
    for (int frame = 0;
         frame < 10000 && GetActiveEntityCount() == 0; frame++) {
        RunEntityFrames(player, 1, daylight);
    }
    assert(GetActiveEntityCount() == 1);
}

static int EntityEcologyRegionCenterLocal(int localCoordinate,
                                          int worldOrigin)
{
    int global = localCoordinate + worldOrigin;
    int region = global / 64;
    if (global % 64 < 0) region--;
    return region * 64 + 32 - worldOrigin;
}

static void TestEntityDeathCauseFeedback(void)
{
    const float daylight = 0.72f;
    Player player = { 0 };
    float faunaActivity = 0.0f;
    uint32_t seed = ActivateFertilePlanet(
        &player, daylight, &faunaActivity);
    assert(seed != 0u && faunaActivity > 0.0f);
    int centerX = (int)floorf(player.position.x);
    int centerZ = (int)floorf(player.position.z);

    assert(ChunksStartGenThread());
    PrepareChunksAt(&player);
    EntitiesInit();
    WaitForEntitySpawn(&player, daylight);
    assert(MaxNearbyHarvestPressure(centerX, centerZ, daylight) == 0.0f);

    assert(EntityKill(0, ENTITY_DEATH_PLAYER, daylight));
    assert(GetActiveEntityCount() == 0);
    float playerPressure = MaxNearbyHarvestPressure(
        centerX, centerZ, daylight);
    assert(playerPressure > 0.0f);
    assert(!EntityKill(0, ENTITY_DEATH_PLAYER, daylight));
    assert(MaxNearbyHarvestPressure(centerX, centerZ, daylight) ==
           playerPressure);

    WaitForEntitySpawn(&player, daylight);
    assert(EntityKill(0, ENTITY_DEATH_ENVIRONMENT, daylight));
    assert(GetActiveEntityCount() == 0);
    assert(MaxNearbyHarvestPressure(centerX, centerZ, daylight) ==
           playerPressure);

    WaitForEntitySpawn(&player, daylight);
    PlacePlayerAt(&player, centerX + 256, centerZ);
    RunEntityFrames(&player, 1, daylight);
    assert(GetActiveEntityCount() == 0);
    assert(MaxNearbyHarvestPressure(centerX, centerZ, daylight) ==
           playerPressure);

    UnloadAllChunks();
    ChunksShutdownGenThread();
}

static void TestCrossSeedEntityHarvestFeedback(void)
{
    const float daylight = 0.72f;
    FertileSite sites[4];
    int siteCount = CollectFertileSites(
        sites, (int)(sizeof(sites) / sizeof(sites[0])), daylight);
    assert(siteCount == (int)(sizeof(sites) / sizeof(sites[0])));
    assert(ChunksStartGenThread());

    for (int siteIndex = 0; siteIndex < siteCount; siteIndex++) {
        const FertileSite *site = &sites[siteIndex];
        EcologyTestSetSeed(site->seed);
        EcologyTestActivatePlanet(site->seed, 0, site->originZ);
        PlanetEcologyResetState();
        Player player = { 0 };
        PlacePlayerAt(&player, site->x, site->z);
        PrepareChunksAt(&player);
        EntitiesInit();
        WaitForEntitySpawn(&player, daylight);

        int playerX = (int)floorf(player.position.x);
        int playerZ = (int)floorf(player.position.z);
        int centerX = EntityEcologyRegionCenterLocal(
            playerX, PlanetWorldOriginX());
        int centerZ = EntityEcologyRegionCenterLocal(
            playerZ, PlanetWorldOriginZ());
        PlanetLocalEcology before[9];
        int sample = 0;
        for (int rz = -1; rz <= 1; rz++) {
            for (int rx = -1; rx <= 1; rx++) {
                before[sample++] = PlanetEcologyLocalAt(
                    centerX + rx * 64, centerZ + rz * 64, daylight);
            }
        }

        assert(EntityKill(0, ENTITY_DEATH_PLAYER, daylight));
        int affectedRegions = 0;
        sample = 0;
        for (int rz = -1; rz <= 1; rz++) {
            for (int rx = -1; rx <= 1; rx++) {
                PlanetLocalEcology after = PlanetEcologyLocalAt(
                    centerX + rx * 64, centerZ + rz * 64, daylight);
                if (after.population.faunaHarvestPressure >
                    before[sample].population.faunaHarvestPressure) {
                    affectedRegions++;
                    assert(after.population.faunaDensity <
                           before[sample].population.faunaDensity);
                    assert(after.suitability.faunaActivity <
                           before[sample].suitability.faunaActivity);
                    assert(PlanetFaunaPopulationCap(
                               after.suitability.faunaActivity,
                               MAX_ENTITIES - 4) <=
                           PlanetFaunaPopulationCap(
                               before[sample].suitability.faunaActivity,
                               MAX_ENTITIES - 4));
                }
                sample++;
            }
        }
        assert(affectedRegions == 1);
        UnloadAllChunks();
    }
    ChunksShutdownGenThread();
}

static void TestEntityEcologySystemReplay(void)
{
    const float daylight = 0.72f;
    Player player = { 0 };
    float faunaActivity = 0.0f;
    uint32_t seed = ActivateFertilePlanet(
        &player, daylight, &faunaActivity);
    assert(seed != 0u);
    int expectedCap = PlanetFaunaPopulationCap(
        faunaActivity, MAX_ENTITIES - 4);
    assert(expectedCap > 0);

    assert(ChunksStartGenThread());
    UpdateChunks(player.position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    EntitiesInit();

    int frames = 0;
    while (GetActiveEntityCount() == 0 && frames < 10000) {
        RunFrames(&player, 1, daylight);
        frames++;
    }
    assert(GetActiveEntityCount() > 0);
    assert(GetActiveEntityCount() <= expectedCap);

    FILE *checkpoint = tmpfile();
    assert(checkpoint);
    SaveSimulation(checkpoint);
    RunFrames(&player, 480, daylight);
    size_t expectedSize = 0;
    unsigned char *expected = CaptureEntityState(&expectedSize);

    EcologyTestSetSeed(seed);
    LoadSimulation(checkpoint);
    RunFrames(&player, 480, daylight);
    size_t replaySize = 0;
    unsigned char *replay = CaptureEntityState(&replaySize);
    assert(replaySize == expectedSize);
    assert(memcmp(replay, expected, expectedSize) == 0);

    EcologyTestActivatePlanetStyle(
        seed, 0, 0, SOLAR_STYLE_GAS);
    PlanetEcologyResetState();
    assert(PlanetEcologyFaunaDensityAt(
               (int)player.position.x, (int)player.position.z,
               daylight) == 0.0f);
    RunFrames(&player, MAX_ENTITIES * 20, daylight);
    assert(GetActiveEntityCount() == 0);

    free(replay);
    free(expected);
    fclose(checkpoint);
    UnloadAllChunks();
    ChunksShutdownGenThread();
}

static bool TestLightVectorFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static void AssertPlanetLightStateValid(const PlanetLightState *state)
{
    assert(state);
    assert(state->sourceCount > 0 && state->sourceCount <= MAX_SOLAR_LIGHTS);
    assert(TestLightVectorFinite(state->sunDirection));
    assert(TestLightVectorFinite(state->moonDirection));
    assert(isfinite(state->daylight) && state->daylight >= 0.0f &&
           state->daylight <= 1.0f);
    assert(isfinite(state->sunset) && state->sunset >= 0.0f &&
           state->sunset <= 1.0f);
    assert(isfinite(state->ringShadow) && state->ringShadow >= 0.0f &&
           state->ringShadow <= 1.0f);
    assert(isfinite(state->eclipse) && state->eclipse >= 0.0f &&
           state->eclipse <= 1.0f);
    assert(isfinite(state->moonIllumination) &&
           state->moonIllumination >= 0.0f && state->moonIllumination <= 1.0f);
    assert(isfinite(state->moonAngularRadius) && state->moonAngularRadius >= 0.0f);
    assert(isfinite(state->moonUmbra) && state->moonUmbra >= 0.0f &&
           state->moonUmbra <= 1.0f);
    assert(isfinite(state->totalIntensity) && state->totalIntensity > 0.0f);
    for (int index = 0; index < state->sourceCount; index++) {
        assert(TestLightVectorFinite(state->sourceDirections[index]));
        assert(isfinite(state->sourceIntensities[index]) &&
               state->sourceIntensities[index] >= 0.0f);
        assert(isfinite(state->sourceVisibility[index]) &&
               state->sourceVisibility[index] >= 0.0f &&
               state->sourceVisibility[index] <= 1.0f);
        assert(isfinite(state->sourceOccultations[index]) &&
               state->sourceOccultations[index] >= 0.0f &&
               state->sourceOccultations[index] <= 1.0f);
    }
}

static void TestPlanetLightStateDeterminism(void)
{
    SolarSystemDef system;
    assert(StarSystemAt(0, 0, &system));
    int planetIndex = 0;
    for (int index = 0; index < system.planetCount; index++) {
        PlanetProfile profile = SolarPlanetProfile(&system, index);
        SpaceSatelliteOrbit satellite;
        if (SolarPlanetSatelliteOrbit(&system, index, &profile, &satellite)) {
            planetIndex = index;
            break;
        }
    }

    FILE *file = tmpfile();
    assert(file);
    EcologyTestActivateGeneratedPlanetWithFile(
        file, &system, planetIndex, 0, 0);
    PlanetLightState invalid;
    const PlanetLightState cleared = { 0 };
    memset(&invalid, 0xa5, sizeof(invalid));
    assert(!PlanetWorldLightStateAt(
        (Vector3){ NAN, 70.0f, 0.5f }, &invalid));
    assert(memcmp(&invalid, &cleared, sizeof(invalid)) == 0);
    static const Vector3 surfacePositions[] = {
        { 0.5f, 70.0f, 0.5f },
        { 18.5f, 72.0f, -11.5f },
        { -31.5f, 68.0f, 24.5f }
    };
    for (size_t index = 0;
         index < sizeof(surfacePositions) / sizeof(surfacePositions[0]);
         index++) {
        PlanetLightState first;
        PlanetLightState second;
        assert(PlanetWorldLightStateAt(surfacePositions[index], &first));
        assert(PlanetWorldLightStateAt(surfacePositions[index], &second));
        AssertPlanetLightStateValid(&first);
        assert(memcmp(&first, &second, sizeof(first)) == 0);
    }
    SpaceAdvanceTime(37.5f);
    PlanetLightState advanced;
    assert(PlanetWorldLightStateAt(surfacePositions[1], &advanced));
    AssertPlanetLightStateValid(&advanced);

    fclose(file);
    PlanetWorldReset();
}

int main(void)
{
    TestCrossSeedSeasonalEntityProperties();
    TestEntityDeathCauseFeedback();
    TestCrossSeedEntityHarvestFeedback();
    TestEntityEcologySystemReplay();
    TestPlanetLightStateDeterminism();
    puts("entity ecology tests passed");
    return 0;
}
