#include "app/game_ecology.h"
#include "app/game_hydrology.h"
#include "app/game_internal.h"
#include "app/game_radiation.h"
#include "core/simulation_clock.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "presentation/homeworld_map.h"
#include "presentation/render.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/fluid.h"
#include "world/weather.h"
#include "world/weather_impact.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum { MAX_RECORDED_TICKS = 256 };

typedef struct TickRecord {
    double time;
    double weatherTime;
    double ecologyTime;
    double entityTime;
    float dt;
    float daylight;
    float entityDaylight;
    float hazardDt[3];
    float playerX;
    int centerX;
    int centerZ;
    int radius;
    unsigned sequence;
    bool damage;
} TickRecord;

static TickRecord records[MAX_RECORDED_TICKS];
void GameGeologyUpdate(GameRuntime *game, double seconds)
{
    (void)game;
    assert(seconds > 0.0);
}
static unsigned recordCount;
static double spaceTime;
static float sampledDaylight;
static bool mapOpen;
static bool surfaceActive;
static bool planetActive;
static unsigned suspendedWeather;
static unsigned suspendedTornado;
static bool ecologyReady;
static unsigned preparationCount;
static unsigned readinessCount;
static unsigned preparationBudget;
static double preparationTime;
static float preparationDaylight;

static TickRecord *CurrentTick(void)
{
    assert(recordCount > 0u && recordCount <= MAX_RECORDED_TICKS);
    return &records[recordCount - 1u];
}

static void ExpectSequence(unsigned expected)
{
    assert(CurrentTick()->sequence == expected);
    CurrentTick()->sequence++;
}

bool HomeWorldMapIsOpen(void) { return mapOpen; }
bool WorldIsSurfaceActive(void) { return surfaceActive; }
bool HomeWorldSurfaceIsActive(void) { return surfaceActive && !planetActive; }
bool PlanetWorldIsActive(void) { return planetActive; }
double SpaceElapsedSimulationTime(void) { return spaceTime; }
double GameDebugTraceMainCpuNowMs(void) { return 0.0; }

void SpaceAdvanceTime(float dt)
{
    assert(recordCount < MAX_RECORDED_TICKS);
    spaceTime += (double)dt;
    TickRecord *tick = &records[recordCount++];
    *tick = (TickRecord){ .time = spaceTime, .dt = dt };
}

void DayNightFactors(float phase, float *daylight, float *sunset)
{
    /* Expose the sampled phase directly; this test owns orchestration, not
     * the rendering module's daylight transfer function. */
    *daylight = phase;
    *sunset = 0.0f;
}

bool PlanetWorldLightStateAtTime(Vector3 position, double time,
                                  PlanetLightState *light)
{
    (void)position;
    if (!planetActive) return false;
    light->daylight = (float)fmod(time / 2.0, 1.0);
    return true;
}

void WeatherSetDaylight(float daylight)
{
    ExpectSequence(0u);
    sampledDaylight = daylight;
    CurrentTick()->daylight = daylight;
}

void GameAdvanceRadiationSimulationStep(GameRuntime *game, double time,
                                         double step)
{
    (void)game;
    assert(time > 0.0 && (float)step == CurrentTick()->dt);
    ExpectSequence(1u);
}

void GameAdvanceHydrologySimulationStep(GameRuntime *game, double time,
                                        double step)
{
    (void)game;
    assert(time > 0.0 && (float)step == CurrentTick()->dt);
    ExpectSequence(2u);
}

void FluidUpdate(float dt)
{
    assert(dt == CurrentTick()->dt);
    ExpectSequence(4u);
}

void EntitiesCaptureRenderPoses(void) {}

void PlanetEcologyAdvanceActiveRegionsAt(int x, int z, int radius,
                                         double time, float daylight)
{
    ExpectSequence(3u);
    assert(daylight == sampledDaylight);
    CurrentTick()->centerX = x;
    CurrentTick()->centerZ = z;
    CurrentTick()->radius = radius;
    CurrentTick()->ecologyTime = time;
}

bool PlanetEcologyAdvanceActiveRegionsBudgetedAt(int x, int z, int radius,
                                                  double time, float daylight,
                                                  unsigned maxStepBatches)
{
    assert(x == 4 && z == -3 && radius == 128);
    assert(maxStepBatches == 0u || maxStepBatches == 8u);
    if (maxStepBatches == 0u) readinessCount++;
    else preparationCount++;
    preparationBudget += maxStepBatches;
    preparationTime = time;
    preparationDaylight = daylight;
    return ecologyReady;
}

bool GameEnvironmentSheltered(Vector3 position)
{
    (void)position;
    return false;
}

PlayerWaterState PlayerWaterStateAt(Vector3 position)
{
    (void)position;
    return (PlayerWaterState){ 0 };
}

void WeatherSetSheltered(bool sheltered) { assert(!sheltered); }

void WeatherUpdate(float dt, Vector3 playerPosition, double time)
{
    ExpectSequence(5u);
    assert(dt == CurrentTick()->dt);
    CurrentTick()->weatherTime = time;
    CurrentTick()->playerX = playerPosition.x;
}

WeatherFieldSample WeatherCurrentSample(void)
{
    return (WeatherFieldSample){ .precipitation = sampledDaylight };
}

void TornadoUpdate(float dt, Vector3 position, WeatherFieldSample weather)
{
    (void)position;
    ExpectSequence(6u);
    assert(dt == CurrentTick()->dt);
    assert(weather.precipitation == sampledDaylight);
}

WeatherFieldSample WeatherFieldSampleAtWorldTime(int x, int z, double time)
{
    (void)x;
    (void)z;
    assert(time == spaceTime);
    return (WeatherFieldSample){ .precipitation = sampledDaylight };
}

void WeatherImpactUpdate(float dt, Vector3 position, WeatherImpactWeather weather)
{
    (void)position;
    ExpectSequence(7u);
    assert(dt == CurrentTick()->dt);
    assert(weather.sampleAt == WeatherFieldSampleAtWorldTime);
    assert(weather.simulationTime == spaceTime);
}

void EntitiesApplyWeatherHazards(float dt, WeatherFieldSample weather,
                                  float daylight)
{
    ExpectSequence(8u);
    CurrentTick()->hazardDt[0] = dt;
    assert(weather.precipitation == daylight && daylight == sampledDaylight);
}

TornadoState TornadoCurrent(void) { return (TornadoState){ 0 }; }

void EntitiesApplyTornadoHazards(float dt, const TornadoState *tornado,
                                  bool damageEnabled, float daylight)
{
    assert(tornado && daylight == sampledDaylight);
    if (!damageEnabled) ExpectSequence(8u);
    ExpectSequence(9u);
    CurrentTick()->hazardDt[1] = dt;
    CurrentTick()->damage = damageEnabled;
}

WeatherImpactExposure WeatherImpactExposureAt(Vector3 position, float shelter,
                                               float immersion)
{
    (void)position;
    (void)shelter;
    (void)immersion;
    return (WeatherImpactExposure){ 0 };
}

void EntitiesApplyWildfireHazards(float dt, float daylight, bool damageEnabled,
                                  EntityWildfireExposureQuery exposureAt)
{
    ExpectSequence(10u);
    assert(daylight == sampledDaylight);
    assert(damageEnabled == CurrentTick()->damage);
    assert(exposureAt == WeatherImpactExposureAt);
    CurrentTick()->hazardDt[2] = dt;
}

void EntitiesUpdate(float dt, const Player *player, float daylight, double time)
{
    ExpectSequence(11u);
    assert(dt == CurrentTick()->dt && player->position.x == CurrentTick()->playerX);
    CurrentTick()->entityTime = time;
    CurrentTick()->entityDaylight = daylight;
}

void WeatherSuspend(void) { suspendedWeather++; }
void TornadoSuspend(void) { suspendedTornado++; }

static GameRuntime ResetGame(void)
{
    memset(records, 0, sizeof(records));
    recordCount = 0u;
    spaceTime = 0.0;
    sampledDaylight = 0.0f;
    mapOpen = false;
    surfaceActive = true;
    planetActive = false;
    suspendedWeather = 0u;
    suspendedTornado = 0u;
    ecologyReady = true;
    preparationCount = 0u;
    readinessCount = 0u;
    preparationBudget = 0u;
    preparationTime = 0.0;
    preparationDaylight = 0.0f;
    GameRuntime game = { 0 };
    SimulationClockConfig config = SimulationClockDefaultConfig();
    assert(SimulationClockInit(&game.simulationClock, &config));
    game.player.position = (Vector3){ 4.5f, 10.0f, -2.5f };
    game.settings.weatherDamageEnabled = true;
    GameEcologyResetClock();
    GameEcologyRestoreClock(&game);
    return game;
}

static void RunFramePattern(const double *pattern, unsigned patternLength,
                            bool usePlanet, TickRecord *output)
{
    GameRuntime game = ResetGame();
    planetActive = usePlanet;
    for (unsigned frame = 0u; recordCount < 120u; frame++) {
        assert(frame < 1000u);
        GameAdvanceSimulationFrame(&game, pattern[frame % patternLength]);
    }
    for (unsigned i = 0; i < 120u; i++) {
        const TickRecord *tick = &records[i];
        assert(tick->sequence == 12u);
        assert(tick->entityTime == tick->time);
        assert(tick->weatherTime == tick->time);
        assert(tick->ecologyTime == tick->time);
        assert(tick->entityDaylight == tick->daylight);
        if (!usePlanet) {
            assert(tick->daylight == (float)(0.30 + tick->time / DAY_LENGTH_SECONDS));
        }
        assert(tick->radius == 128 && tick->centerX == 4 && tick->centerZ == -3);
        for (unsigned hazard = 0; hazard < 3u; hazard++) {
            assert(tick->hazardDt[hazard] == tick->dt);
        }
    }
    memcpy(output, records, sizeof(TickRecord) * 120u);
}

static void TestRenderFramePartitioning(void)
{
    TickRecord reference[120];
    TickRecord comparison[120];
    const double fps60[] = { 1.0 / 60.0 };
    const double fps30[] = { 1.0 / 30.0 };
    const double fps144[] = { 1.0 / 144.0 };
    const double jitter[] = { 1.0 / 240.0, 1.0 / 30.0, 1.0 / 48.0,
                              1.0 / 120.0 };
    for (int planet = 0; planet < 2; planet++) {
        RunFramePattern(fps60, 1u, planet != 0, reference);
        RunFramePattern(fps30, 1u, planet != 0, comparison);
        assert(memcmp(reference, comparison, sizeof(reference)) == 0);
        RunFramePattern(fps144, 1u, planet != 0, comparison);
        assert(memcmp(reference, comparison, sizeof(reference)) == 0);
        RunFramePattern(jitter, 4u, planet != 0, comparison);
        assert(memcmp(reference, comparison, sizeof(reference)) == 0);
    }
}

static void TestCatchUpKeepsItsTargetAndDoesNotAccumulateRealTime(void)
{
    GameRuntime game = ResetGame();
    double step = game.simulationClock.config.fixedStepSeconds;
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 0u);
    unsigned previousPreparationCount = preparationCount;
    ecologyReady = false;
    for (unsigned frame = 0; frame < 3u; frame++) {
        assert(GameAdvanceSimulationFrame(&game, 1.0) == 0u);
        assert(game.ecologyCatchupPending && GameWorldSimulationPaused(&game));
        assert(recordCount == 0u && spaceTime == 0.0 && preparationTime == 0.0);
        assert(SimulationClockGetSimulationSeconds(&game.simulationClock) == 0.0);
        assert(SimulationClockGetAccumulatorSeconds(&game.simulationClock) ==
               step * 0.5);
    }
    assert(preparationCount == previousPreparationCount + 3u);
    ecologyReady = true;
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 1u);
    assert(!game.ecologyCatchupPending && !GameWorldSimulationPaused(&game));
    assert(recordCount == 1u);
}

static void TestReadinessGatesNewSurfaceWithoutSpendingCatchUpBudget(void)
{
    GameRuntime game = ResetGame();
    double step = game.simulationClock.config.fixedStepSeconds;
    surfaceActive = false;
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 0u);
    assert(!game.ecologyCatchupPending);

    /* Landing replaces the previous frame's ready space with an old surface.
     * Refresh the gate before an album/import action can pause simulation. */
    surfaceActive = true;
    planetActive = true;
    ecologyReady = false;
    for (unsigned check = 0u; check < 3u; check++) {
        assert(!GameRefreshEcologyReadiness(&game));
        assert(game.ecologyCatchupPending && GameWorldSimulationPaused(&game));
        assert(!GameWorldSimulationUserPaused(&game));
        assert(recordCount == 0u && spaceTime == 0.0);
        assert(SimulationClockGetSimulationSeconds(&game.simulationClock) == 0.0);
        assert(SimulationClockGetAccumulatorSeconds(&game.simulationClock) ==
               step * 0.5);
    }
    assert(readinessCount == 3u && preparationCount == 0u);
    assert(preparationBudget == 0u);

    assert(GameAdvanceSimulationFrame(&game, step) == 0u);
    assert(preparationCount == 1u && preparationBudget == 8u);
    assert(recordCount == 0u && game.ecologyCatchupPending);
    ecologyReady = true;
    assert(GameRefreshEcologyReadiness(&game));
    assert(!game.ecologyCatchupPending && !GameWorldSimulationPaused(&game));
    assert(preparationBudget == 8u);
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 1u);
    assert(preparationCount == 2u && preparationBudget == 16u);

    ecologyReady = false;
    assert(!GameRefreshEcologyReadiness(&game));
    unsigned previousChecks = readinessCount;
    surfaceActive = false;
    assert(GameRefreshEcologyReadiness(&game));
    assert(!game.ecologyCatchupPending && readinessCount == previousChecks);
    assert(!GameRefreshEcologyReadiness(NULL));
}

static void TestPauseStopsEverySimulationConsumer(void)
{
    GameRuntime game = ResetGame();
    double step = game.simulationClock.config.fixedStepSeconds;
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 0u);
    bool *flags[] = { &game.paused, &game.albumOpen, &game.importDialog.open,
                      &game.landingTransition.active, &game.biologyAtlasOpen,
                      &mapOpen };
    for (unsigned i = 0u; i < sizeof(flags) / sizeof(flags[0]); i++) {
        *flags[i] = true;
        assert(GameWorldSimulationPaused(&game));
        assert(GameAdvanceSimulationFrame(&game, 10.0) == 0u);
        assert(recordCount == 0u && spaceTime == 0.0);
        assert(SimulationClockGetAccumulatorSeconds(&game.simulationClock) ==
               step * 0.5);
        *flags[i] = false;
    }
    assert(!GameWorldSimulationPaused(&game));
    assert(GameAdvanceSimulationFrame(&game, step * 0.5) == 1u);
    assert(recordCount == 1u);
}

static void TestTimeScaleAndCatchUp(void)
{
    GameRuntime game = ResetGame();
    double step = game.simulationClock.config.fixedStepSeconds;
    game.simulationClock.config.simulationSecondsPerRealSecond = 4.0;
    assert(GameAdvanceSimulationFrame(&game, step) == 4u);
    assert(recordCount == 4u);
    for (unsigned i = 0; i < recordCount; i++) {
        assert(records[i].dt == (float)step);
    }
    float previousPhase = game.dayTime;
    game.dayCycleEnabled = false;
    assert(GameAdvanceSimulationFrame(&game, 10.0) == 8u);
    assert(recordCount == 12u);
    assert(game.dayTime == previousPhase);
    assert(SimulationClockGetAccumulatorSeconds(&game.simulationClock) < step);
    assert(GameAdvanceSimulationFrame(&game, NAN) == 0u);
    assert(GameAdvanceSimulationFrame(&game, -1.0) == 0u);
}

static void TestWorldAndDamageGates(void)
{
    GameRuntime game = ResetGame();
    game.settings.weatherDamageEnabled = false;
    double step = game.simulationClock.config.fixedStepSeconds;
    assert(GameAdvanceSimulationFrame(&game, step) == 1u);
    assert(records[0].hazardDt[0] == 0.0f);
    assert(records[0].hazardDt[1] > 0.0f && !records[0].damage);
    surfaceActive = false;
    assert(GameAdvanceSimulationFrame(&game, step) == 1u);
    assert(records[1].sequence == 3u);
    assert(records[1].entityTime == 0.0 && records[1].ecologyTime == 0.0);
    assert(suspendedWeather == 1u && suspendedTornado == 1u);
}

static void AssertClockEqual(GameHomeDaylightSnapshot expected)
{
    GameHomeDaylightSnapshot actual = GameCaptureHomeDaylight();
    assert(actual.simulationTime == expected.simulationTime);
    assert(actual.dayPhase == expected.dayPhase);
    assert(actual.cycleEnabled == expected.cycleEnabled);
}

static FILE *ClockPayload(uint32_t magic, uint32_t version, double time,
                          double phase, uint8_t enabled)
{
    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(&magic, sizeof(magic), 1, file) == 1);
    assert(fwrite(&version, sizeof(version), 1, file) == 1);
    assert(fwrite(&time, sizeof(time), 1, file) == 1);
    assert(fwrite(&phase, sizeof(phase), 1, file) == 1);
    assert(fwrite(&enabled, sizeof(enabled), 1, file) == 1);
    rewind(file);
    return file;
}

static void TestHistoricalDaylightAndClockPersistence(void)
{
    GameRuntime game = ResetGame();
    GameHomeDaylightSnapshot snapshot = {
        .simulationTime = 1000.0, .dayPhase = 0.25, .cycleEnabled = true
    };
    GameRestoreHomeDaylight(snapshot);
    assert(GameHomeDaylightAtSimulationTime(940.0) == 0.0f);
    assert(GameHomeDaylightAtSimulationTime(1060.0) == 0.5f);
    assert(GameHomeDaylightAtSimulationTime(880.0) == 0.75f);
    FILE *saved = tmpfile();
    assert(saved && GameEcologySaveClock(saved));
    assert(ftell(saved) == 25L);
    GameEcologyResetClock();
    rewind(saved);
    assert(GameEcologyLoadClock(saved));
    AssertClockEqual(snapshot);
    fclose(saved);

    spaceTime = 1000.0;
    game.dayTime = 0.9f;
    game.dayCycleEnabled = false;
    game.paused = true;
    ecologyReady = false;
    /* The early edit gate must use the newly loaded anchor even while paused,
     * without consuming catch-up work or letting a stale ready flag survive. */
    assert(!GameRefreshEcologyReadiness(&game));
    assert(game.ecologyCatchupPending);
    assert(preparationTime == 1000.0 && preparationDaylight == 0.25f);
    assert(preparationCount == 0u && preparationBudget == 0u);
    assert(readinessCount == 1u && recordCount == 0u);
    /* A load performed by the DSL cannot be overwritten by a stale runtime
     * phase before the first resumed simulation tick. */
    assert(GameAdvanceSimulationFrame(&game, 0.0) == 0u);
    assert(game.dayTime == 0.25f && game.dayCycleEnabled);
    AssertClockEqual(snapshot);

    const struct {
        uint32_t magic;
        uint32_t version;
        double time;
        double phase;
        uint8_t enabled;
    } invalid[] = {
        { 0u, 1u, 1000.0, 0.25, 1u },
        { 0x45434c31u, 2u, 1000.0, 0.25, 1u },
        { 0x45434c31u, 1u, NAN, 0.25, 1u },
        { 0x45434c31u, 1u, -1.0, 0.25, 1u },
        { 0x45434c31u, 1u, 1000.0, NAN, 1u },
        { 0x45434c31u, 1u, 1000.0, 1.0, 1u },
        { 0x45434c31u, 1u, 1000.0, 0.25, 2u }
    };
    for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
        FILE *file = ClockPayload(invalid[i].magic, invalid[i].version,
                                  invalid[i].time, invalid[i].phase,
                                  invalid[i].enabled);
        assert(!GameEcologyLoadClock(file));
        AssertClockEqual(snapshot);
        fclose(file);
    }
    FILE *truncated = tmpfile();
    assert(truncated && fputc('E', truncated) != EOF);
    rewind(truncated);
    assert(!GameEcologyLoadClock(truncated));
    AssertClockEqual(snapshot);
    fclose(truncated);
    assert(!GameEcologyLoadClock(NULL) && !GameEcologySaveClock(NULL));

    snapshot.cycleEnabled = false;
    GameRestoreHomeDaylight(snapshot);
    assert(GameHomeDaylightAtSimulationTime(940.0) == 0.25f);
    assert(GameHomeDaylightAtSimulationTime(1060.0) == 0.25f);
}

int main(void)
{
    TestRenderFramePartitioning();
    TestCatchUpKeepsItsTargetAndDoesNotAccumulateRealTime();
    TestReadinessGatesNewSurfaceWithoutSpendingCatchUpBudget();
    TestPauseStopsEverySimulationConsumer();
    TestTimeScaleAndCatchUp();
    TestWorldAndDamageGates();
    TestHistoricalDaylightAndClockPersistence();
    puts("game ecology tick tests passed");
    return 0;
}
