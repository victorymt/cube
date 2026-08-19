#include "world/tornado.h"

#include "core/game_effects.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <math.h>
#include <stdint.h>

#define TORNADO_NATURAL_CELL_SIZE 64.0f
#define TORNADO_NATURAL_CHANCE_SCALE 0.00008f
#define TORNADO_DUST_RATE 11.0f

static TornadoState tornadoState = { 0 };
static TornadoStats tornadoStats = { 0 };
static float tornadoTickAccumulator = 0.0f;
static float tornadoDustAccumulator = 0.0f;
static float tornadoParticleScale = 1.0f;
static unsigned tornadoForcedFrames = 0u;
static uint32_t tornadoSurfaceId = 0u;
static bool tornadoHasSurfaceId = false;
static bool tornadoDamageEnabled = true;

static float TornadoRuntimeUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static uint32_t TornadoMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t TornadoHash(uint64_t tick, uint32_t lane)
{
    uint32_t low = (uint32_t)tick;
    uint32_t high = (uint32_t)(tick >> 32);
    return TornadoMix(WorldGetSeed() ^
        WorldCurrentSurfaceId() * 0x9e3779b9u ^ low ^
        high * 0x85ebca6bu ^ lane * 0xc2b2ae35u);
}

static float TornadoHashUnit(uint64_t tick, uint32_t lane)
{
    return (float)(TornadoHash(tick, lane) & 0x00ffffffu) / 16777215.0f;
}

static bool TornadoPositionFinite(Vector3 position)
{
    return isfinite(position.x) && isfinite(position.y) &&
           isfinite(position.z);
}

static float TornadoGroundY(float x, float z)
{
    return (float)WorldSurfaceHeightAt((int)floorf(x), (int)floorf(z)) + 1.0f;
}

static void TornadoEmitStyled(Vector3 position, Vector3 velocity,
                              Color color, Vector3 startSize,
                              Vector3 endSize, float life, float gravity,
                              bool debris)
{
    if (GameEffectsPendingCount() >= GAME_EFFECTS_CAPACITY) {
        tornadoStats.droppedEffects++;
        return;
    }
    GameParticleStyle style = {
        .startSize = startSize,
        .endSize = endSize,
        .startColor = color,
        .endColor = (Color){ color.r, color.g, color.b, 0 },
        .gravity = gravity
    };
    GameEffectsEmitParticleStyled(position, velocity, &style, life);
    if (debris) tornadoStats.debrisEmitted++;
    else tornadoStats.dustEmitted++;
}

static bool TornadoTopBlock(int x, int z, int *outY, BlockType *outBlock)
{
    int ground = WorldSurfaceHeightAt(x, z);
    int start = ground + 24;
    int end = ground - 2;
    for (int y = start; y >= end; y--) {
        if (!SurfaceBlockReadyAt(x, y, z)) continue;
        BlockType block = GetBlockAt(x, y, z);
        if (block != BLOCK_AIR && !IsLiquidBlock(block)) {
            if (outY) *outY = y;
            if (outBlock) *outBlock = block;
            return true;
        }
    }
    return false;
}

static void TornadoDamageSample(uint32_t lane)
{
    float angle = TornadoHashUnit(tornadoStats.ticks, lane) * 2.0f * PI;
    float radius = sqrtf(TornadoHashUnit(tornadoStats.ticks,
                                         lane + 0x100u)) *
        tornadoState.radius * 1.35f;
    int x = (int)floorf(tornadoState.center.x + cosf(angle) * radius);
    int z = (int)floorf(tornadoState.center.z + sinf(angle) * radius);
    int y = 0;
    BlockType block = BLOCK_AIR;
    tornadoStats.processedDamageSamples++;
    if (!TornadoTopBlock(x, z, &y, &block) || block == BLOCK_BEDROCK) return;

    TornadoForceSample force = TornadoModelForceAt(
        &tornadoState, (Vector3){ x + 0.5f, y + 1.0f, z + 0.5f });
    BlockMaterialResponse material = BlockMaterialResponseFor(block);
    float normalizedWind = tornadoState.maximumWindMps > 0.0f ?
        force.localWindMps / tornadoState.maximumWindMps *
        tornadoState.intensity : 0.0f;
    float excess = normalizedWind - material.windResistance;
    if (excess <= 0.0f || TornadoHashUnit(
            tornadoStats.ticks, lane + 0x200u) >
            TornadoRuntimeUnit(excess * 0.72f)) {
        return;
    }
    if (!SetBlockNoUndoFromSource(x, y, z, BLOCK_AIR,
                                  WORLD_MUTATION_ENVIRONMENT)) {
        return;
    }
    tornadoStats.blockDamageEvents++;
    Vector3 velocity = {
        force.acceleration.x * 0.42f,
        3.0f + force.acceleration.y * 0.38f,
        force.acceleration.z * 0.42f
    };
    Color color = BlockBaseColor(block);
    color.a = 235;
    TornadoEmitStyled(
        (Vector3){ x + 0.5f, y + 0.65f, z + 0.5f }, velocity, color,
        (Vector3){ 0.34f, 0.34f, 0.34f },
        (Vector3){ 0.08f, 0.08f, 0.08f }, 2.8f, 3.8f, true);
}

static void TornadoTryNaturalFormation(Vector3 observerPosition,
                                       WeatherFieldSample weather)
{
    TornadoFormationInput formation = {
        .weather = weather,
        .atmosphereActive = weather.pressureAtm >= 0.50f,
        .supportsWaterCycle = weather.precipitation > 0.0f
    };
    float potential = TornadoFormationPotential(&formation);
    if (potential <= 0.0f) return;
    tornadoStats.formationAttempts++;
    int cellX = (int)floorf(observerPosition.x / TORNADO_NATURAL_CELL_SIZE);
    int cellZ = (int)floorf(observerPosition.z / TORNADO_NATURAL_CELL_SIZE);
    uint32_t lane = (uint32_t)cellX * 0x9e3779b9u ^
                    (uint32_t)cellZ * 0x85ebca6bu;
    float chance = potential * potential * potential * potential *
                   TORNADO_NATURAL_CHANCE_SCALE;
    if (TornadoHashUnit(tornadoStats.ticks, lane) >= chance) return;

    float distance = 40.0f + TornadoHashUnit(
        tornadoStats.ticks, lane + 1u) * 28.0f;
    float lateral = (TornadoHashUnit(tornadoStats.ticks, lane + 2u) - 0.5f) *
                    30.0f;
    float windX = cosf(weather.windAngle);
    float windZ = sinf(weather.windAngle);
    Vector3 center = {
        observerPosition.x + windX * distance - windZ * lateral,
        0.0f,
        observerPosition.z + windZ * distance + windX * lateral
    };
    center.y = TornadoGroundY(center.x, center.z);
    float peak = fminf(0.52f + potential * 0.46f, 0.98f);
    float lifetime = 90.0f + TornadoHashUnit(
        tornadoStats.ticks, lane + 3u) * 120.0f;
    tornadoState = TornadoModelCreate(
        TornadoHash(tornadoStats.ticks, lane + 4u),
        WorldCurrentSurfaceId(), center, peak, lifetime, false);
    tornadoStats.naturalFormations++;
}

static void TornadoFixedTick(Vector3 observerPosition,
                             WeatherFieldSample weather)
{
    tornadoStats.ticks++;
    if (!tornadoState.active) {
        TornadoTryNaturalFormation(observerPosition, weather);
        return;
    }
    if (!tornadoDamageEnabled || tornadoState.phase == TORNADO_PHASE_FORMING ||
        tornadoState.intensity < 0.34f) {
        return;
    }
    for (uint32_t lane = 0u; lane < TORNADO_DAMAGE_SAMPLES_PER_TICK; lane++) {
        TornadoDamageSample(lane + 1u);
    }
}

static void TornadoEmitDust(float dt, Vector3 observerPosition)
{
    if (!tornadoState.active || tornadoState.dustLoading <= 0.01f ||
        TornadoDistanceTo(observerPosition) > 150.0f) {
        tornadoDustAccumulator = 0.0f;
        return;
    }
    tornadoDustAccumulator += TornadoRuntimeUnit(tornadoState.dustLoading) *
        TORNADO_DUST_RATE * tornadoParticleScale * fminf(dt, 0.25f);
    unsigned emissions = (unsigned)floorf(tornadoDustAccumulator);
    if (emissions > 4u) emissions = 4u;
    tornadoDustAccumulator -= (float)emissions;
    if (tornadoDustAccumulator >= 1.0f) {
        tornadoDustAccumulator = fmodf(tornadoDustAccumulator, 1.0f);
    }
    for (unsigned index = 0u; index < emissions; index++) {
        uint32_t lane = tornadoStats.dustEmitted + index + 0x700u;
        float angle = TornadoHashUnit(tornadoStats.ticks, lane) * 2.0f * PI;
        float radius = tornadoState.radius *
            (0.45f + TornadoHashUnit(tornadoStats.ticks, lane + 1u) * 0.95f);
        Vector3 position = {
            tornadoState.center.x + cosf(angle) * radius,
            tornadoState.center.y + 0.2f +
                TornadoHashUnit(tornadoStats.ticks, lane + 2u) * 2.4f,
            tornadoState.center.z + sinf(angle) * radius
        };
        TornadoForceSample force = TornadoModelForceAt(&tornadoState, position);
        Vector3 velocity = {
            force.acceleration.x * 0.34f,
            1.0f + force.acceleration.y * 0.24f,
            force.acceleration.z * 0.34f
        };
        TornadoEmitStyled(
            position, velocity, (Color){ 126, 108, 82, 175 },
            (Vector3){ 0.24f, 0.18f, 0.24f },
            (Vector3){ 0.66f, 0.42f, 0.66f }, 1.8f, 0.18f, false);
    }
}

void TornadoInit(bool damageEnabled)
{
    tornadoState = (TornadoState){ 0 };
    tornadoStats = (TornadoStats){ 0 };
    tornadoTickAccumulator = 0.0f;
    tornadoDustAccumulator = 0.0f;
    tornadoForcedFrames = 0u;
    tornadoSurfaceId = 0u;
    tornadoHasSurfaceId = false;
    tornadoDamageEnabled = damageEnabled;
}

void TornadoReset(void)
{
    bool damageEnabled = tornadoDamageEnabled;
    TornadoInit(damageEnabled);
}

void TornadoClear(void)
{
    tornadoState = (TornadoState){ 0 };
    tornadoForcedFrames = 0u;
    tornadoDustAccumulator = 0.0f;
}

void TornadoSuspend(void)
{
    TornadoClear();
    tornadoTickAccumulator = 0.0f;
    tornadoSurfaceId = 0u;
    tornadoHasSurfaceId = false;
}

void TornadoSetDamageEnabled(bool enabled)
{
    tornadoDamageEnabled = enabled;
}

bool TornadoDamageEnabled(void)
{
    return tornadoDamageEnabled;
}

void TornadoSetParticleScale(float scale)
{
    if (!isfinite(scale)) scale = 1.0f;
    tornadoParticleScale = fminf(fmaxf(scale, 0.20f), 1.50f);
}

bool TornadoForce(Vector3 observerPosition, float intensity,
                  unsigned frames, float distance,
                  WeatherFieldSample weather)
{
    if (!WorldIsSurfaceActive() || !TornadoPositionFinite(observerPosition) ||
        !isfinite(intensity) || intensity < 0.0f || intensity > 1.0f ||
        frames == 0u || !isfinite(distance) || distance < 8.0f ||
        distance > 160.0f || !isfinite(weather.windAngle)) {
        return false;
    }
    float windX = cosf(weather.windAngle);
    float windZ = sinf(weather.windAngle);
    Vector3 center = {
        observerPosition.x + windX * distance,
        0.0f,
        observerPosition.z + windZ * distance
    };
    center.y = TornadoGroundY(center.x, center.z);
    float lifetime = fmaxf((float)frames / 60.0f, 1.0f / 60.0f);
    uint32_t seed = TornadoMix(WorldGetSeed() ^ WorldCurrentSurfaceId() ^
                               frames * 0x9e3779b9u);
    tornadoState = TornadoModelCreate(seed, WorldCurrentSurfaceId(), center,
                                      intensity, lifetime, true);
    tornadoForcedFrames = frames;
    tornadoSurfaceId = WorldCurrentSurfaceId();
    tornadoHasSurfaceId = true;
    tornadoStats.forcedFormations++;
    return true;
}

void TornadoStepTicks(unsigned ticks, Vector3 observerPosition,
                      WeatherFieldSample weather)
{
    if (!WorldIsSurfaceActive() || !TornadoPositionFinite(observerPosition)) {
        return;
    }
    for (unsigned index = 0u; index < ticks; index++) {
        TornadoFixedTick(observerPosition, weather);
    }
}

void TornadoUpdate(float dt, Vector3 observerPosition,
                   WeatherFieldSample weather)
{
    if (!WorldIsSurfaceActive() || !isfinite(dt) || dt <= 0.0f ||
        !TornadoPositionFinite(observerPosition)) {
        return;
    }
    uint32_t surfaceId = WorldCurrentSurfaceId();
    if (tornadoHasSurfaceId && tornadoSurfaceId != surfaceId) {
        TornadoClear();
    }
    tornadoSurfaceId = surfaceId;
    tornadoHasSurfaceId = true;
    float step = fminf(dt, 0.25f);
    if (tornadoState.active) {
        float groundY = TornadoGroundY(tornadoState.center.x,
                                       tornadoState.center.z);
        TornadoModelAdvance(&tornadoState, step, weather, groundY);
        if (tornadoState.forced && tornadoForcedFrames > 0u) {
            tornadoForcedFrames--;
            if (tornadoForcedFrames == 0u) {
                tornadoState.age = tornadoState.lifetime;
                TornadoModelAdvance(&tornadoState, step, weather, groundY);
            }
        }
    }
    tornadoTickAccumulator += step * TORNADO_TICK_RATE;
    unsigned ticks = (unsigned)floorf(tornadoTickAccumulator);
    if (ticks > 2u) ticks = 2u;
    tornadoTickAccumulator -= (float)ticks;
    TornadoStepTicks(ticks, observerPosition, weather);
    TornadoEmitDust(step, observerPosition);
}

TornadoState TornadoCurrent(void)
{
    return tornadoState;
}

TornadoStats TornadoGetStats(void)
{
    return tornadoStats;
}

unsigned TornadoForcedFramesRemaining(void)
{
    return tornadoForcedFrames;
}

TornadoForceSample TornadoForceAt(Vector3 position)
{
    return TornadoModelForceAt(&tornadoState, position);
}

float TornadoDistanceTo(Vector3 position)
{
    if (!tornadoState.active || !TornadoPositionFinite(position)) return -1.0f;
    float dx = position.x - tornadoState.center.x;
    float dz = position.z - tornadoState.center.z;
    return sqrtf(dx * dx + dz * dz);
}
