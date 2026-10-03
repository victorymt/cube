#include "ecology_test_fixture.h"
#include "replay_state_hash.h"

#include "core/simulation_clock.h"
#include "core/simulation_replay.h"
#include "ecology/ecology.h"
#include "world/block_catalog.h"
#include "world/fluid.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/weather.h"
#include "world/weather_impact.h"

#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* The fixture supplies storage, loaded bounds and mutations. Clock, weather,
 * ecology, migration/evolution, fluid and fire updates are production code. */
enum { REPLAY_TICKS = 80, CHECKPOINT_TICK = 16, SCENE_HEIGHT = 8 };
enum { ACTION_MOVE = 1, ACTION_HARVEST, ACTION_RAIN, ACTION_WATER, ACTION_IGNITE };

typedef struct TickState {
    uint64_t clock;
    uint64_t weather;
    uint64_t ecology;
    uint64_t world;
    uint64_t hazards;
} TickState;

typedef struct ReplayRun {
    SimulationClock clock;
    SimulationReplay input;
    unsigned tick;
    int playerX;
    bool rain;
    bool longitudeAlias;
    TickState states[REPLAY_TICKS];
    float firstFauna;
    float lastFauna;
    uint64_t firstNutrients;
    uint64_t lastNutrients;
    double lastPopulationUpdate[4];
    uint64_t transferredVolume;
    int initialRegionX;
    int initialRegionZ;
    bool visitedNeighbor;
    bool returnedToInitialRegion;
} ReplayRun;

typedef struct Checkpoint {
    FILE *file;
    ReplayRun run;
    unsigned short blocks[CHUNK_SIZE][SCENE_HEIGHT][CHUNK_SIZE];
    BlockEdit edits[64];
    int editCount;
} Checkpoint;

static int sceneGround;
static WorldMutationSource mutationSource = WORLD_MUTATION_PLAYER;
static const int ecologyCells[4][2] = {
    { 8, 8 }, { 72, 8 }, { 8, 72 }, { 72, 72 }
};

WorldMutationSource WorldCurrentMutationSource(void) { return mutationSource; }
bool IsLiquidBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_LAVA;
}
bool IsValidBlockType(BlockType type)
{
    return (type >= BLOCK_AIR && type <= BLOCK_NATURAL_END) || IsColorBlock(type);
}
BlockMaterialResponse BlockMaterialResponseFor(BlockType type)
{
    const BlockCatalogEntry *entry = BlockCatalogGet(type);
    return (BlockMaterialResponse){ entry->windResistance,
        entry->impactResistance, entry->flammability, entry->waterErodibility };
}

bool SurfaceBlockReadyAt(int x, int y, int z)
{
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    /* At most 64 surface records and one fire: all fit in one hazard tick.
     * This makes the checkpoint independent of unsaved work cursors. */
    return cx == 0 && cz == 0 && lx < 8 && lz < 8 &&
        y >= sceneGround && y < sceneGround + SCENE_HEIGHT &&
        FindChunk(cx, cz) != NULL;
}

bool SetBlockNoUndoFromSource(int x, int y, int z, BlockType type,
                              WorldMutationSource source)
{
    if (!SurfaceBlockReadyAt(x, y, z)) return false;
    int cx, cz, lx, lz;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    Chunk *chunk = FindChunk(cx, cz);
    BlockType before = ChunkGetLocalBlock(chunk, lx, y, lz);
    if (before == type) return false;
    assert(ChunkSetLocalBlock(chunk, lx, y, lz, type));
    bool found = false;
    for (int index = 0; index < WorldGetEditCount(); index++) {
        BlockEdit edit;
        assert(WorldGetEditForCurrentDimension(index, &edit));
        if (edit.x == lx && edit.y == y && edit.z == lz) {
            EcologyTestSetBlockEditType(index, type);
            found = true;
            break;
        }
    }
    if (!found) EcologyTestAddBlockEdit(lx, y, lz, type);
    mutationSource = source;
    FluidOnBlockChanged(lx, y, lz, before, type);
    WeatherImpactOnBlockChanged(lx, y, lz);
    mutationSource = WORLD_MUTATION_PLAYER;
    return true;
}

static uint64_t HashWeather(WeatherFieldSample weather)
{
    uint64_t hash = ReplayHashBegin();
#define WU(field) hash = ReplayHashU64(hash, weather.field)
#define WF(field) hash = ReplayHashFloat(hash, weather.field)
    WU(phenomena); WU(dominantPhenomenon); WU(cloudGenera); WU(dominantCloudGenus);
    for (int index = 0; index < WEATHER_CLOUD_GENUS_COUNT; index++) {
        WF(cloudGenusCoverage[index]); WF(cloudGenusBaseHeight[index]);
        WF(cloudGenusThickness[index]);
    }
    WF(temperatureK); WF(temperatureAnomalyK); WF(pressureAtm);
    WF(pressureAnomaly); WF(relativeHumidity); WF(dewPointK); WF(wetBulbK);
    WF(instability); WF(cloudCover); WF(cloudBaseHeight); WF(precipitation);
    WF(drizzle); WF(rain); WF(snow); WF(sleet); WF(freezingRain); WF(hail);
    WF(storm); WF(lightning); WF(fog); WF(frost); WF(dust); WF(wind); WF(gust);
    WF(windAngle); WF(visibility); WF(heatWave); WF(coldSnap); WF(rainbow); WF(aurora);
#undef WU
#undef WF
    return hash;
}

static uint64_t HashEcology(PlanetLocalEcology local,
                            PlanetEvolutionRegion evolution)
{
    uint64_t hash = ReplayHashBegin();
#define EF(field) hash = ReplayHashFloat(hash, local.field)
    EF(environment.meanTemperatureK); EF(environment.currentTemperatureK);
    EF(environment.seasonalAmplitudeK); EF(environment.liquidWaterAccess);
    EF(environment.soilMoisture); EF(environment.meanPrecipitation);
    EF(environment.precipitationRate); EF(environment.meanUsableLight);
    EF(environment.currentUsableLight); EF(environment.stormExposure);
    EF(environment.currentStorm); EF(environment.elevation); EF(environment.slope);
    EF(environment.shelter); EF(environment.biomeSupport); EF(environment.disturbance);
    EF(environment.radiationExposure); EF(environment.ejectaExposure);
    EF(suitability.carryingCapacity); EF(suitability.floraCapacity);
    EF(suitability.faunaCapacity); EF(suitability.floraActivity);
    EF(suitability.faunaActivity); EF(suitability.waterScore);
    EF(suitability.temperatureScore); EF(suitability.lightScore);
    EF(suitability.stormScore); EF(suitability.terrainScore);
    EF(suitability.seasonScore); EF(suitability.radiationScore);
    hash = ReplayHashU64(hash, local.suitability.limitingFactor);
    EF(population.floraDensity); EF(population.faunaDensity);
    EF(population.floraCarryingCapacity); EF(population.faunaCarryingCapacity);
    EF(population.seasonalMemory); EF(population.faunaHarvestPressure);
    EF(population.radiationMemory);
    EF(migration.floraNet); EF(migration.faunaNet);
    EF(migration.floraFlowX); EF(migration.floraFlowZ);
    EF(migration.faunaFlowX); EF(migration.faunaFlowZ);
    hash = ReplayHashU64(hash, (uint64_t)(int64_t)local.diagnostics.regionX);
    hash = ReplayHashU64(hash, (uint64_t)(int64_t)local.diagnostics.regionZ);
    EF(diagnostics.habitatStress); EF(diagnostics.harvestStress);
    EF(diagnostics.faunaStress); EF(diagnostics.faunaNetRecoveryRate);
    EF(diagnostics.radiationMemory);
#undef EF
    hash = ReplayHashFloat(hash, evolution.herbivoreDensity);
    hash = ReplayHashFloat(hash, evolution.omnivoreDensity);
    hash = ReplayHashFloat(hash, evolution.carnivoreDensity);
    hash = ReplayHashU64(hash, evolution.bootstrapGeneration);
    hash = ReplayHashU64(hash, evolution.lineageCount);
    hash = ReplayHashU64(hash, evolution.bootstrapComplete);
    for (unsigned index = 0; index < PLANET_EVOLUTION_MAX_LINEAGES; index++) {
        PlanetEvolutionLineage lineage = evolution.lineages[index];
#define LU(field) hash = ReplayHashU64(hash, lineage.field)
#define LF(field) hash = ReplayHashFloat(hash, lineage.field)
        LU(lineageId); LU(speciesId); LU(founderSeed); LU(archetype);
        LU(generation); LU(isolatedGenerations); LU(active);
        LF(density); LF(dietMean); LF(geneticVariance); LF(geneFlow); LF(fitness);
#undef LU
#undef LF
    }
    return hash;
}

static uint64_t HashNutrients(const EcologyNutrientState *nutrients)
{
    uint64_t hash = ReplayHashBegin();
#define ND(field) hash = ReplayHashDouble(hash, nutrients->field)
    ND(mineralNitrogen); ND(mineralPhosphorus);
    ND(soilOrganicNitrogen); ND(soilOrganicPhosphorus);
    ND(plantNitrogen); ND(plantPhosphorus);
    ND(litterNitrogen); ND(litterPhosphorus);
    ND(plantBiomass); ND(litterBiomass); ND(seedBank); ND(meanPlantAgeDays);
#undef ND
    return hash;
}

static uint64_t HashWorld(int alias)
{
    uint64_t hash = ReplayHashBegin();
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int dy = 0; dy < SCENE_HEIGHT; dy++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                hash = ReplayHashU64(hash, GetBlockAt(x + alias, sceneGround + dy, z));
                hash = ReplayHashU64(hash, FluidGetVolumeAt(x + alias, sceneGround + dy, z));
            }
        }
    }
    return hash;
}

static uint64_t HashHazards(int alias)
{
    uint64_t hash = ReplayHashBegin();
    WeatherImpactStats stats = WeatherImpactGetStats();
    hash = ReplayHashU64(hash, stats.ticks);
    hash = ReplayHashU64(hash, stats.activeFires);
    hash = ReplayHashU64(hash, stats.burnedBlocks);
    hash = ReplayHashU64(hash, stats.depositedWater);
    for (int x = 0; x < 8; x++) {
        for (int z = 0; z < 8; z++) {
            for (int dy = 0; dy < SCENE_HEIGHT; dy++) {
                int y = sceneGround + dy;
                WeatherSurfaceState surface = { 0 };
                bool present = WeatherImpactSurfaceAt(x + alias, y, z, &surface);
                hash = ReplayHashU64(hash, present);
                if (present) {
                    hash = ReplayHashU64(hash, surface.flags);
                    hash = ReplayHashFloat(hash, surface.wetness);
                    hash = ReplayHashFloat(hash, surface.snowDepth);
                    hash = ReplayHashFloat(hash, surface.iceAmount);
                    hash = ReplayHashFloat(hash, surface.erosionExposure);
                    hash = ReplayHashFloat(hash, surface.windExposure);
                    hash = ReplayHashFloat(hash, surface.impactExposure);
                }
                WeatherImpactFireSnapshot fire = { 0 };
                present = WeatherImpactFireStateAt(x + alias, y, z, &fire);
                hash = ReplayHashU64(hash, present);
                if (present) {
                    hash = ReplayHashU64(hash, fire.fuelBlock);
                    hash = ReplayHashU64(hash, fire.state.phase);
                    hash = ReplayHashFloat(hash, fire.state.intensity);
                    hash = ReplayHashFloat(hash, fire.state.fuel);
                    hash = ReplayHashFloat(hash, fire.state.moisture);
                    hash = ReplayHashFloat(hash, fire.state.ageSeconds);
                    hash = ReplayHashFloat(hash, fire.state.heatOutput);
                    hash = ReplayHashFloat(hash, fire.state.smokeOutput);
                }
                WeatherBurnSiteState burn = { 0 };
                present = WeatherImpactBurnSiteAt(x + alias, y, z, &burn);
                hash = ReplayHashU64(hash, present);
                if (present) {
                    hash = ReplayHashFloat(hash, burn.severity);
                    hash = ReplayHashFloat(hash, burn.recovery);
                    hash = ReplayHashFloat(hash, burn.ageSeconds);
                }
            }
        }
    }
    return hash;
}

static void RecordInput(SimulationReplay *input, unsigned alteredAction)
{
    SimulationReplayInit(input);
    assert(SimulationReplayBeginRecording(input));
    assert(SimulationReplayRecord(input, 4, ACTION_IGNITE,
                                   alteredAction == ACTION_IGNITE ? 45 : 90));
    assert(SimulationReplayRecord(input, 24, ACTION_MOVE, 72));
    assert(SimulationReplayRecord(input, 28, ACTION_HARVEST,
                                   alteredAction == ACTION_HARVEST ? 2 : 1));
    assert(SimulationReplayRecord(input, 40, ACTION_MOVE, 8));
    assert(SimulationReplayRecord(input, 48, ACTION_WATER,
                                   alteredAction == ACTION_WATER ? 31 : 63));
    assert(SimulationReplayRecord(input, 56, ACTION_RAIN, 1));
    assert(SimulationReplayFinishRecording(input));
    assert(SimulationReplayBeginPlayback(input));
}

static void ApplyInputs(ReplayRun *run)
{
    while (run->input.cursor < run->input.eventCount &&
           run->input.events[run->input.cursor].tick == run->tick) {
        SimulationReplayEvent event;
        assert(SimulationReplayNext(&run->input, &event));
        switch (event.action) {
        case ACTION_IGNITE:
            assert(WeatherImpactIgniteAt(6, sceneGround + 1, 6,
                                         (float)event.value / 100.0f));
            break;
        case ACTION_MOVE:
            run->playerX = event.value;
            break;
        case ACTION_HARVEST:
            for (int count = 0; count < event.value; count++) {
                assert(PlanetEcologyRecordFaunaHarvest(run->playerX, 8, 0.72f,
                                                      1.0f, 0.5f));
            }
            break;
        case ACTION_RAIN:
            run->rain = event.value != 0;
            break;
        case ACTION_WATER:
            assert(FluidSetVolumeAt(3, sceneGround + 4, 3, (uint8_t)event.value));
            break;
        default:
            assert(false);
        }
    }
}

static void StepReplay(double simulationSeconds, double stepSeconds, void *opaque)
{
    ReplayRun *run = opaque;
    assert(run->tick < REPLAY_TICKS);
    run->tick++;
    SpaceAdvanceTime((float)stepSeconds);
    assert(SpaceElapsedSimulationTime() == simulationSeconds);
    assert(PlanetEcologyAdvanceActiveRegionsBudgetedAt(
        run->playerX, 8, 128, simulationSeconds, 0.72f, 8u));
    ApplyInputs(run);
    int alias = run->longitudeAlias ? SURFACE_EQUATOR_BLOCKS : 0;
    WeatherFieldSample sampled = WeatherFieldSampleAtWorldTime(
        run->playerX + alias, 8, simulationSeconds);
    /* Scripted dry/rain forcing isolates a reproducible disturbance while the
     * weather sampled by ecology and included in the trace remains real. */
    WeatherFieldSample forcing = sampled;
    forcing.temperatureK = 302.0f;
    forcing.relativeHumidity = run->rain ? 0.95f : 0.15f;
    forcing.rain = run->rain ? 0.9f : 0.0f;
    forcing.precipitation = forcing.rain;
    forcing.drizzle = forcing.snow = forcing.sleet = forcing.freezingRain = 0.0f;
    forcing.hail = forcing.lightning = forcing.frost = 0.0f;
    WeatherImpactStepTicks(1, (Vector3){ (float)(run->playerX + alias),
                                      (float)sceneGround + 3.0f, 8.0f },
                          (WeatherImpactWeather){ .uniform = forcing });
    /* Tick stepping intentionally bypasses FluidUpdate's wall-clock CPU budget. */
    FluidStepTicks(5);
    TickState *state = &run->states[run->tick - 1];
    state->clock = ReplayHashDouble(ReplayHashBegin(), simulationSeconds);
    state->clock = ReplayHashDouble(state->clock, stepSeconds);
    state->clock = ReplayHashU64(state->clock, run->tick);
    state->clock = ReplayHashU64(state->clock, (uint64_t)run->playerX);
    state->weather = HashWeather(sampled);
    state->ecology = ReplayHashBegin();
    for (unsigned index = 0; index < 4; index++) {
        int x = ecologyCells[index][0] + alias;
        int z = ecologyCells[index][1];
        PlanetLocalEcology local = PlanetEcologyLocalAt(x, z, 0.72f);
        PlanetEvolutionRegion evolution = { 0 };
        assert(PlanetEcologyEvolutionRegionAt(x, z, 0.72f, &evolution));
        assert(evolution.lineageCount > 0);
        state->ecology = ReplayHashU64(state->ecology, HashEcology(local, evolution));
        PlanetEcologyPopulationSnapshot population = { 0 };
        assert(PlanetEcologyTestPopulationSnapshotAt(EcologyWorldSurfaceId(),
            local.diagnostics.regionX, local.diagnostics.regionZ, &population));
        assert(population.nutrientInitialized);
        assert(EcologyNutrientStateValid(&population.nutrients));
        assert(EcologyNutrientTotalNitrogen(&population.nutrients) > 0.0);
        assert(EcologyNutrientTotalPhosphorus(&population.nutrients) > 0.0);
        assert(population.lastUpdateTime >= run->lastPopulationUpdate[index]);
        assert(population.lastUpdateTime <= simulationSeconds);
        run->lastPopulationUpdate[index] = population.lastUpdateTime;
        uint64_t nutrients = HashNutrients(&population.nutrients);
        state->ecology = ReplayHashU64(state->ecology, population.surfaceId);
        state->ecology = ReplayHashU64(state->ecology,
                                      (uint64_t)(int64_t)population.regionX);
        state->ecology = ReplayHashU64(state->ecology,
                                      (uint64_t)(int64_t)population.regionZ);
        state->ecology = ReplayHashU64(state->ecology, population.nutrientInitialized);
        state->ecology = ReplayHashDouble(state->ecology, population.lastUpdateTime);
        state->ecology = ReplayHashU64(state->ecology, nutrients);
        if (index == 0) {
            if (run->tick == 1) {
                run->firstFauna = local.population.faunaDensity;
                run->firstNutrients = nutrients;
            }
            run->lastFauna = local.population.faunaDensity;
            run->lastNutrients = nutrients;
        }
    }
    PlanetLocalEcology playerLocal = PlanetEcologyLocalAt(
        run->playerX + alias, 8, 0.72f);
    int regionX = playerLocal.diagnostics.regionX;
    int regionZ = playerLocal.diagnostics.regionZ;
    if (run->tick == 1) {
        run->initialRegionX = regionX;
        run->initialRegionZ = regionZ;
    }
    assert(regionZ == run->initialRegionZ);
    if (run->tick >= 24 && run->tick < 40) {
        assert(regionX != run->initialRegionX);
        run->visitedNeighbor = true;
    } else {
        assert(regionX == run->initialRegionX);
        if (run->tick >= 40) run->returnedToInitialRegion = run->visitedNeighbor;
    }
    state->ecology = ReplayHashU64(state->ecology, (uint64_t)(int64_t)regionX);
    state->ecology = ReplayHashU64(state->ecology, (uint64_t)(int64_t)regionZ);
    state->world = HashWorld(alias);
    state->hazards = HashHazards(alias);
    run->transferredVolume = FluidGetStats().transferredVolume;
}

static void AdvanceTo(ReplayRun *run, unsigned endTick, bool fragmented)
{
    while (run->tick < endTick) {
        if (fragmented) {
            assert(SimulationClockAdvance(&run->clock, 0.125, StepReplay, run) == 0);
            assert(SimulationClockAdvance(&run->clock, 0.125, StepReplay, run) == 0);
            assert(SimulationClockAdvance(&run->clock, 0.250, StepReplay, run) == 1);
        } else {
            assert(SimulationClockAdvance(&run->clock, 0.5, StepReplay, run) == 1);
        }
    }
    assert(SimulationClockGetAccumulatorSeconds(&run->clock) == 0.0);
}

static void InitializeRun(ReplayRun *run, FILE *initialSpace, uint32_t seed,
                          bool alias, unsigned alteredAction)
{
    *run = (ReplayRun){ .playerX = 8, .longitudeAlias = alias };
    FluidReset();
    WeatherImpactInit(true);
    ChunksTestResetScheduler();
    EcologyTestClearBlockEdits();
    PlanetEcologyResetState();
    EcologyTestSetSeed(seed);
    rewind(initialSpace);
    assert(SpaceLoadState(initialSpace));
    EcologyTestActivatePlanet(seed, 0, 0);
    sceneGround = PlanetTerrainHeight(0, 0);
    for (int x = 0; x < 8; x++) {
        for (int z = 0; z < 8; z++) {
            int ground = PlanetTerrainHeight(x, z);
            if (ground > sceneGround) sceneGround = ground;
        }
    }
    sceneGround += 2;
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    Chunk *chunk = FindChunk(0, 0);
    assert(chunk);
    for (int dy = 0; dy < SCENE_HEIGHT; dy++) {
        assert(ChunkGetSection(chunk, SurfaceSectionYFromBlockY(sceneGround + dy), true));
        for (int x = 0; x < CHUNK_SIZE; x++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                assert(ChunkSetLocalBlock(chunk, x, sceneGround + dy, z,
                                         dy == 0 ? BLOCK_STONE : BLOCK_AIR));
            }
        }
    }
    for (int dy = 1; dy < 6; dy++) {
        assert(ChunkSetLocalBlock(chunk, 2, sceneGround + dy, 3, BLOCK_STONE));
        assert(ChunkSetLocalBlock(chunk, 4, sceneGround + dy, 3, BLOCK_STONE));
        assert(ChunkSetLocalBlock(chunk, 3, sceneGround + dy, 2, BLOCK_STONE));
        assert(ChunkSetLocalBlock(chunk, 3, sceneGround + dy, 4, BLOCK_STONE));
    }
    assert(ChunkSetLocalBlock(chunk, 6, sceneGround + 1, 6, BLOCK_WOOD));
    assert(FluidSetVolumeAt(3, sceneGround + 4, 3, 77));
    SimulationClockConfig config = SimulationClockDefaultConfig();
    config.fixedStepSeconds = 0.5;
    assert(SimulationClockInit(&run->clock, &config));
    RecordInput(&run->input, alteredAction);
    assert(PlanetEcologyAdvanceActiveRegionsBudgetedAt(
        run->playerX, 8, 128, SpaceElapsedSimulationTime(), 0.72f, 8u));
}

static void SaveCheckpoint(Checkpoint *checkpoint, const ReplayRun *run,
                           bool requireSettledFluid)
{
    assert(SimulationClockGetAccumulatorSeconds(&run->clock) == 0.0);
    if (requireSettledFluid) {
        assert(FluidGetStats().activeCells == 0);
        assert(WeatherImpactGetStats().activeFires > 0);
        assert(run->lastFauna > 0.0f);
    }
    checkpoint->run = *run;
    checkpoint->file = tmpfile();
    assert(checkpoint->file);
    EcologyTestSaveSimulationState(checkpoint->file);
    assert(FluidSaveState(checkpoint->file));
    assert(WeatherImpactSaveState(checkpoint->file));
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int dy = 0; dy < SCENE_HEIGHT; dy++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                checkpoint->blocks[x][dy][z] = (unsigned short)GetBlockAt(
                    x, sceneGround + dy, z);
            }
        }
    }
    checkpoint->editCount = WorldGetEditCount();
    assert(checkpoint->editCount <= 64);
    for (int index = 0; index < checkpoint->editCount; index++) {
        assert(WorldGetEditForCurrentDimension(index, &checkpoint->edits[index]));
    }
}

static void LoadCheckpoint(const Checkpoint *checkpoint, ReplayRun *run)
{
    FluidReset();
    WeatherImpactReset();
    PlanetEcologyResetState();
    ChunksTestResetScheduler();
    EcologyTestClearBlockEdits();
    for (int index = 0; index < checkpoint->editCount; index++) {
        const BlockEdit *edit = &checkpoint->edits[index];
        EcologyTestAddBlockEdit(edit->x, edit->y, edit->z, edit->type);
    }
    EcologyTestLoadSimulationState(checkpoint->file);
    assert(FluidLoadState(checkpoint->file));
    assert(WeatherImpactLoadState(checkpoint->file));
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    Chunk *chunk = FindChunk(0, 0);
    assert(chunk);
    for (int x = 0; x < CHUNK_SIZE; x++) {
        for (int dy = 0; dy < SCENE_HEIGHT; dy++) {
            for (int z = 0; z < CHUNK_SIZE; z++) {
                assert(ChunkSetLocalBlock(chunk, x, sceneGround + dy, z,
                    (BlockType)checkpoint->blocks[x][dy][z]));
            }
        }
    }
    FluidOnChunkLoaded(chunk);
    *run = checkpoint->run;
    assert(SimulationClockInit(&run->clock, &checkpoint->run.clock.config));
    SimulationClockReset(&run->clock, SpaceElapsedSimulationTime());
}

static void AssertFirstDifference(const ReplayRun *expected,
                                  const ReplayRun *actual,
                                  unsigned firstTick, unsigned action)
{
    assert(expected->tick == actual->tick);
    for (unsigned tick = 0; tick < firstTick; tick++) {
        const TickState *a = &expected->states[tick];
        const TickState *b = &actual->states[tick];
        assert(a->clock == b->clock && a->weather == b->weather);
        if (tick + 1 < firstTick) {
            assert(a->ecology == b->ecology && a->world == b->world &&
                   a->hazards == b->hazards);
        } else if (action == ACTION_HARVEST) {
            assert(a->ecology != b->ecology);
        } else if (action == ACTION_WATER) {
            assert(a->world != b->world);
        } else {
            assert(action == ACTION_IGNITE && a->hazards != b->hazards);
        }
    }
}

static void AssertReplayEqual(const ReplayRun *expected, const ReplayRun *actual,
                              unsigned firstTick)
{
    static const char *names[] = { "clock", "weather", "ecology", "world", "hazards" };
    assert(expected->tick == actual->tick);
    for (unsigned tick = firstTick; tick < expected->tick; tick++) {
        const TickState *a = &expected->states[tick];
        const TickState *b = &actual->states[tick];
        uint64_t left[] = { a->clock, a->weather, a->ecology, a->world, a->hazards };
        uint64_t right[] = { b->clock, b->weather, b->ecology, b->world, b->hazards };
        for (unsigned lane = 0; lane < sizeof(left) / sizeof(left[0]); lane++) {
            if (left[lane] != right[lane]) {
                fprintf(stderr, "replay diverged tick=%u lane=%s expected=%016" PRIx64
                        " actual=%016" PRIx64 "\n", tick + 1, names[lane],
                        left[lane], right[lane]);
                assert(left[lane] == right[lane]);
            }
        }
    }
}

int main(void)
{
    FILE *initialSpace = tmpfile();
    assert(initialSpace && SpaceSaveState(initialSpace));
    uint32_t seed = 0;
    for (uint32_t index = 0; index < 512; index++) {
        uint32_t candidate = 0x8f3a21d7u + index * 0x9e3779b9u;
        EcologyTestSetSeed(candidate);
        EcologyTestActivatePlanet(candidate, 0, 0);
        PlanetEcologyResetState();
        if (PlanetEcologyCurrent().faunaDensity > 0.08f) {
            seed = candidate;
            break;
        }
    }
    assert(seed != 0);

    ReplayRun baseline;
    InitializeRun(&baseline, initialSpace, seed, false, false);
    AdvanceTo(&baseline, CHECKPOINT_TICK, false);
    Checkpoint checkpoint;
    SaveCheckpoint(&checkpoint, &baseline, true);
    AdvanceTo(&baseline, REPLAY_TICKS, false);
    assert(baseline.transferredVolume > 77);
    assert(baseline.firstFauna > 0.0f && baseline.lastFauna != baseline.firstFauna);
    assert(baseline.firstNutrients != baseline.lastNutrients);
    assert(baseline.visitedNeighbor && baseline.returnedToInitialRegion);
    assert(baseline.input.cursor == baseline.input.eventCount);
    assert(WeatherImpactGetStats().depositedWater > 0);
    assert(baseline.states[0].weather != baseline.states[REPLAY_TICKS - 1].weather);
    assert(baseline.states[0].ecology != baseline.states[REPLAY_TICKS - 1].ecology);
    assert(baseline.states[0].world != baseline.states[47].world);

    ReplayRun repeated;
    InitializeRun(&repeated, initialSpace, seed, false, false);
    AdvanceTo(&repeated, REPLAY_TICKS, true);
    AssertReplayEqual(&baseline, &repeated, 0);

    ReplayRun resumed;
    LoadCheckpoint(&checkpoint, &resumed);
    AdvanceTo(&resumed, REPLAY_TICKS, true);
    AssertReplayEqual(&baseline, &resumed, CHECKPOINT_TICK);

    ReplayRun aliased;
    InitializeRun(&aliased, initialSpace, seed, true, false);
    AdvanceTo(&aliased, REPLAY_TICKS, false);
    AssertReplayEqual(&baseline, &aliased, 0);

    static const unsigned alterations[][2] = {
        { ACTION_IGNITE, 4 }, { ACTION_HARVEST, 28 }, { ACTION_WATER, 48 }
    };
    for (unsigned index = 0; index < 3; index++) {
        ReplayRun altered;
        InitializeRun(&altered, initialSpace, seed, false, alterations[index][0]);
        AdvanceTo(&altered, REPLAY_TICKS, false);
        AssertFirstDifference(&baseline, &altered, alterations[index][1],
                              alterations[index][0]);
    }

    /* FLD2 persists volumes, not the active work queue. Loading the same live
     * volume checkpoint twice must nevertheless rebuild the same continuation. */
    LoadCheckpoint(&checkpoint, &resumed);
    assert(FluidSetVolumeAt(3, sceneGround + 4, 3, 55));
    assert(FluidGetStats().activeCells > 0);
    Checkpoint activeFluid;
    SaveCheckpoint(&activeFluid, &resumed, false);
    ReplayRun restoredFirst;
    LoadCheckpoint(&activeFluid, &restoredFirst);
    AdvanceTo(&restoredFirst, 32, false);
    ReplayRun restoredSecond;
    LoadCheckpoint(&activeFluid, &restoredSecond);
    AdvanceTo(&restoredSecond, 32, true);
    AssertReplayEqual(&restoredFirst, &restoredSecond, CHECKPOINT_TICK);
    assert(restoredFirst.transferredVolume > 0);
    fclose(activeFluid.file);
    fclose(checkpoint.file);

    FluidCleanup();
    ChunksTestResetScheduler();
    fclose(initialSpace);
    puts("simulation integration replay tests passed");
    return 0;
}
