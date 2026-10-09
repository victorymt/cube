#include "app/game_ecology.h"
#include "app/game_geology.h"

#include "app/game_debug_trace.h"
#include "app/game_hydrology.h"
#include "app/game_internal.h"
#include "app/game_radiation.h"
#include "ecology/ecology.h"
#include "ecology/entity.h"
#include "presentation/homeworld_map.h"
#include "presentation/render.h"
#include "space/space_runtime.h"
#include "space/space_state.h"
#include "world/fluid.h"
#include "world/weather.h"
#include "world/weather_impact.h"
#include "world/world_environment.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>

/* The simulation neighborhood covers the 96-cell entity despawn radius and
 * stays independent of render distance and asynchronous chunk completion. */
#define GAME_ECOLOGY_RADIUS_CELLS 128

/* Isolated ecology unit tests link this scheduler without the geology module;
   the game link provides the strong world-aware implementation. */
__attribute__((weak)) LavaHeatExposure GameGeologyHeatExposureAt(
    const struct GameRuntime *game, Vector3 position, float radius, float shelter)
{
    (void)game; (void)position; (void)radius; (void)shelter;
    return (LavaHeatExposure){ .nearestDistance = INFINITY };
}

static pthread_mutex_t homeDaylightMutex = PTHREAD_MUTEX_INITIALIZER;
static GameHomeDaylightSnapshot homeDaylight = {
    .dayPhase = 0.30,
    .cycleEnabled = true
};
static bool homeClockRestorePending;

bool GameWorldSimulationUserPaused(const GameRuntime *game)
{
    return game && (game->paused || game->albumOpen ||
        game->importDialog.open || game->landingTransition.active ||
        game->biologyAtlasOpen || game->inventoryOpen || HomeWorldMapIsOpen());
}

bool GameWorldSimulationPaused(const GameRuntime *game)
{
    return game && (game->ecologyCatchupPending || GameWorldSimulationUserPaused(game));
}

GameHomeDaylightSnapshot GameCaptureHomeDaylight(void)
{
    pthread_mutex_lock(&homeDaylightMutex);
    GameHomeDaylightSnapshot snapshot = homeDaylight;
    pthread_mutex_unlock(&homeDaylightMutex);
    return snapshot;
}

void GameRestoreHomeDaylight(GameHomeDaylightSnapshot snapshot)
{
    if (!isfinite(snapshot.simulationTime) || snapshot.simulationTime < 0.0 ||
        !isfinite(snapshot.dayPhase)) return;
    snapshot.dayPhase = fmod(snapshot.dayPhase, 1.0);
    if (snapshot.dayPhase < 0.0) snapshot.dayPhase += 1.0;
    pthread_mutex_lock(&homeDaylightMutex);
    homeDaylight = snapshot;
    pthread_mutex_unlock(&homeDaylightMutex);
}

static double GameHomeDayPhaseAt(GameHomeDaylightSnapshot snapshot,
                                 double simulationTime)
{
    double phase = snapshot.dayPhase;
    if (snapshot.cycleEnabled && isfinite(simulationTime) &&
        simulationTime >= 0.0) {
        phase += (simulationTime - snapshot.simulationTime) /
                 (double)DAY_LENGTH_SECONDS;
    }
    phase = fmod(phase, 1.0);
    if (phase < 0.0) phase += 1.0;
    return phase;
}

void GameSyncHomeDaylight(const GameRuntime *game)
{
    if (!game) return;
    double now = SpaceElapsedSimulationTime();
    GameHomeDaylightSnapshot snapshot = GameCaptureHomeDaylight();
    float expectedPhase = (float)GameHomeDayPhaseAt(snapshot, now);
    if (game->dayTime == expectedPhase &&
        game->dayCycleEnabled == snapshot.cycleEnabled) return;
    /* The double anchor remains stable during ordinary ticks. Only an
     * explicit phase/cycle change creates a new segment. */
    GameRestoreHomeDaylight((GameHomeDaylightSnapshot){
        .simulationTime = now,
        .dayPhase = game->dayTime,
        .cycleEnabled = game->dayCycleEnabled
    });
}

float GameHomeDaylightAtSimulationTime(double simulationTime)
{
    double phase = GameHomeDayPhaseAt(GameCaptureHomeDaylight(), simulationTime);
    float daylight = 0.0f;
    float sunset = 0.0f;
    DayNightFactors((float)phase, &daylight, &sunset);
    return daylight;
}

bool GameEcologySaveClock(FILE *file)
{
    if (!file) return false;
    const uint32_t magic = 0x45434c31u; /* ECL1 */
    const uint32_t version = 1u;
    GameHomeDaylightSnapshot snapshot = GameCaptureHomeDaylight();
    uint8_t enabled = snapshot.cycleEnabled ? 1u : 0u;
    return fwrite(&magic, sizeof(magic), 1, file) == 1 &&
        fwrite(&version, sizeof(version), 1, file) == 1 &&
        fwrite(&snapshot.simulationTime, sizeof(double), 1, file) == 1 &&
        fwrite(&snapshot.dayPhase, sizeof(double), 1, file) == 1 &&
        fwrite(&enabled, sizeof(enabled), 1, file) == 1;
}

bool GameEcologyLoadClock(FILE *file)
{
    if (!file) return false;
    uint32_t magic = 0u;
    uint32_t version = 0u;
    uint8_t enabled = 0u;
    GameHomeDaylightSnapshot snapshot = { 0 };
    if (fread(&magic, sizeof(magic), 1, file) != 1 ||
        fread(&version, sizeof(version), 1, file) != 1 ||
        fread(&snapshot.simulationTime, sizeof(double), 1, file) != 1 ||
        fread(&snapshot.dayPhase, sizeof(double), 1, file) != 1 ||
        fread(&enabled, sizeof(enabled), 1, file) != 1 ||
        magic != 0x45434c31u || version != 1u || enabled > 1u ||
        !isfinite(snapshot.simulationTime) || snapshot.simulationTime < 0.0 ||
        !isfinite(snapshot.dayPhase) || snapshot.dayPhase < 0.0 ||
        snapshot.dayPhase >= 1.0) return false;
    snapshot.cycleEnabled = enabled != 0u;
    pthread_mutex_lock(&homeDaylightMutex);
    homeDaylight = snapshot;
    homeClockRestorePending = true;
    pthread_mutex_unlock(&homeDaylightMutex);
    return true;
}

void GameEcologyResetClock(void)
{
    pthread_mutex_lock(&homeDaylightMutex);
    homeDaylight = (GameHomeDaylightSnapshot){
        .simulationTime = SpaceElapsedSimulationTime(),
        .dayPhase = 0.30,
        .cycleEnabled = true
    };
    homeClockRestorePending = true;
    pthread_mutex_unlock(&homeDaylightMutex);
}

void GameEcologyRestoreClock(GameRuntime *game)
{
    if (!game) return;
    pthread_mutex_lock(&homeDaylightMutex);
    GameHomeDaylightSnapshot snapshot = homeDaylight;
    homeClockRestorePending = false;
    pthread_mutex_unlock(&homeDaylightMutex);
    game->dayTime = (float)GameHomeDayPhaseAt(
        snapshot, SpaceElapsedSimulationTime());
    game->dayCycleEnabled = snapshot.cycleEnabled;
}

static void GameApplyPendingHomeClock(GameRuntime *game)
{
    pthread_mutex_lock(&homeDaylightMutex);
    bool pending = homeClockRestorePending;
    pthread_mutex_unlock(&homeDaylightMutex);
    if (pending) GameEcologyRestoreClock(game);
}

static float GameSimulationDaylight(const GameRuntime *game,
                                    double simulationTime)
{
    PlanetLightState light = { 0 };
    if (PlanetWorldLightStateAtTime(game->player.position,
                                    simulationTime, &light)) {
        return light.daylight;
    }
    return GameHomeDaylightAtSimulationTime(simulationTime);
}

void GameAdvanceSimulationStep(double simulationSeconds,
                               double stepSeconds, void *context)
{
    GameRuntime *game = context;
    if (!game || !isfinite(stepSeconds) || stepSeconds <= 0.0) return;

    SpaceAdvanceTime((float)stepSeconds);
    double simulationTime = SpaceElapsedSimulationTime();
    float dt = (float)stepSeconds;
    game->dayTime = (float)GameHomeDayPhaseAt(
        GameCaptureHomeDaylight(), simulationTime);
    float daylight = GameSimulationDaylight(game, simulationTime);
    WeatherSetDaylight(daylight);
    GameAdvanceRadiationSimulationStep(game, simulationSeconds, stepSeconds);
    GameAdvanceHydrologySimulationStep(game, simulationSeconds, stepSeconds);

    bool localWorldActive = WorldIsSurfaceActive();
    bool weatherActive = HomeWorldSurfaceIsActive() || PlanetWorldIsActive();
    GameGeologyUpdate(game, stepSeconds);
    if (localWorldActive) {
        EntitiesCaptureRenderPoses();
        double ecologyStarted = game->debugTraceEnabled
            ? GameDebugTraceMainCpuNowMs() : 0.0;
        PlanetEcologyAdvanceActiveRegionsAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), GAME_ECOLOGY_RADIUS_CELLS,
            simulationTime, daylight);
        if (game->debugTraceEnabled) {
            double elapsed = GameDebugTraceMainCpuNowMs() - ecologyStarted;
            game->debugSimulationEcologyMainCpuMs += elapsed;
            game->debugEcologyStepMainCpuMs += elapsed;
        }
        FluidUpdate(dt);
    }
    if (weatherActive) {
        bool sheltered = GameEnvironmentSheltered(game->player.position) ||
            PlayerWaterStateAt(game->player.position).eyesSubmerged;
        WeatherSetSheltered(sheltered);
        WeatherUpdate(dt, game->player.position, simulationTime);
        WeatherFieldSample weather = WeatherCurrentSample();
        TornadoUpdate(dt, game->player.position, weather);
        WeatherImpactUpdate(dt, game->player.position, (WeatherImpactWeather){
            .sampleAt = WeatherFieldSampleAtWorldTime,
            .simulationTime = simulationTime
        });
        if (game->settings.weatherDamageEnabled) {
            EntitiesApplyWeatherHazards(dt, weather, daylight);
        }
        TornadoState tornado = TornadoCurrent();
        EntitiesApplyTornadoHazards(
            dt, &tornado, game->settings.weatherDamageEnabled, daylight);
        EntitiesApplyWildfireHazards(
            dt, daylight, game->settings.weatherDamageEnabled,
            WeatherImpactExposureAt);
    } else {
        WeatherSetSheltered(false);
        WeatherSuspend();
        TornadoSuspend();
    }
    if (localWorldActive) {
        bool shelteredForHeat = GameEnvironmentSheltered(game->player.position);
        LavaHeatExposure heat = GameGeologyHeatExposureAt(
            game, game->player.position, 6.0f, shelteredForHeat ? 1.0f : 0.0f);
        game->playerHeatExposure = heat.heat;
        game->playerHeatContact = heat.contact;
        game->playerHeatSources = heat.hotSources;
        game->playerHeatBlockedSources = heat.blockedSources;
        if (game->settings.weatherDamageEnabled && isfinite(heat.heat)) {
            game->playerHeatDamage = fminf(1.0f,
                fmaxf(0.0f, game->playerHeatDamage + heat.heat * dt * 0.12f));
        }
        double entitiesStarted = game->debugTraceEnabled
            ? GameDebugTraceMainCpuNowMs() : 0.0;
        EntitiesUpdate(dt, &game->player, daylight, simulationTime);
        if (game->debugTraceEnabled) {
            game->debugSimulationEntitiesMainCpuMs +=
                GameDebugTraceMainCpuNowMs() - entitiesStarted;
        }
    }
}

static bool GamePrepareEcologyRegions(GameRuntime *game,
                                      unsigned maxStepBatches)
{
    if (WorldIsSurfaceActive()) {
        double ecologyStarted = game->debugTraceEnabled
            ? GameDebugTraceMainCpuNowMs() : 0.0;
        double now = SpaceElapsedSimulationTime();
        bool ready = PlanetEcologyAdvanceActiveRegionsBudgetedAt(
            (int)floorf(game->player.position.x),
            (int)floorf(game->player.position.z), GAME_ECOLOGY_RADIUS_CELLS,
            now, GameSimulationDaylight(game, now), maxStepBatches);
        if (game->debugTraceEnabled) {
            double elapsed = GameDebugTraceMainCpuNowMs() - ecologyStarted;
            game->debugSimulationEcologyMainCpuMs += elapsed;
            game->debugEcologyPrepareMainCpuMs += elapsed;
        }
        game->ecologyCatchupPending = !ready;
    } else {
        game->ecologyCatchupPending = false;
    }
    return !game->ecologyCatchupPending;
}

bool GameRefreshEcologyReadiness(GameRuntime *game)
{
    if (!game) return false;
    GameApplyPendingHomeClock(game);
    GameSyncHomeDaylight(game);
    return GamePrepareEcologyRegions(game, 0u);
}

unsigned GameAdvanceSimulationFrame(GameRuntime *game, double realDeltaSeconds)
{
    if (!game) return 0u;
    game->debugSimulationEcologyMainCpuMs = 0.0;
    game->debugEcologyPrepareMainCpuMs = 0.0;
    game->debugEcologyStepMainCpuMs = 0.0;
    game->debugSimulationEntitiesMainCpuMs = 0.0;
    game->debugSimulationStepCount = 0u;
    GameApplyPendingHomeClock(game);
    GameSyncHomeDaylight(game);
    if (GameWorldSimulationUserPaused(game)) return 0u;
    /* Keep the catch-up target fixed and discard real time spent loading
     * regional history. No births, deaths or world edits run ahead of it. */
    if (!GamePrepareEcologyRegions(game, 8u)) return 0u;
    game->debugSimulationStepCount = SimulationClockAdvance(
        &game->simulationClock, realDeltaSeconds,
        GameAdvanceSimulationStep, game);
    game->simulationTick += game->debugSimulationStepCount;
    return game->debugSimulationStepCount;
}
