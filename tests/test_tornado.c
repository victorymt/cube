#include "world/tornado.h"

#include "core/game_effects.h"
#include "world/surface_topology.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static bool surfaceActive = true;
static uint32_t surfaceId = 3u;
static BlockType surfaceBlock = BLOCK_LEAVES;
static unsigned mutations = 0u;
static unsigned pendingEffects = 0u;

bool WorldIsSurfaceActive(void) { return surfaceActive; }
uint32_t WorldGetSeed(void) { return 0x51a7u; }
uint32_t WorldCurrentSurfaceId(void) { return surfaceId; }
int WorldSurfaceHeightAt(int x, int z)
{
    (void)x;
    (void)z;
    return 10;
}
bool SurfaceBlockReadyAt(int x, int y, int z)
{
    (void)x;
    (void)z;
    return y >= 8 && y <= 34;
}
BlockType GetBlockAt(int x, int y, int z)
{
    (void)x;
    (void)z;
    return y == 10 ? surfaceBlock : BLOCK_AIR;
}
bool SetBlockNoUndoFromSource(int x, int y, int z, BlockType block,
                              WorldMutationSource source)
{
    (void)x;
    (void)y;
    (void)z;
    assert(block == BLOCK_AIR);
    assert(source == WORLD_MUTATION_ENVIRONMENT);
    mutations++;
    return true;
}
bool IsLiquidBlock(BlockType block)
{
    return block == BLOCK_WATER || block == BLOCK_LAVA;
}
BlockMaterialResponse BlockMaterialResponseFor(BlockType block)
{
    if (block == BLOCK_BEDROCK) {
        return (BlockMaterialResponse){ .windResistance = 1.0f };
    }
    return (BlockMaterialResponse){ .windResistance = 0.0f };
}
Color BlockBaseColor(BlockType block)
{
    (void)block;
    return (Color){ 44, 118, 52, 255 };
}
unsigned GameEffectsPendingCount(void) { return pendingEffects; }
void GameEffectsEmitParticleStyled(Vector3 position, Vector3 velocity,
                                   const GameParticleStyle *style,
                                   float life)
{
    (void)position;
    (void)velocity;
    assert(style != NULL);
    assert(life > 0.0f);
    pendingEffects++;
}

static WeatherFieldSample Storm(void)
{
    WeatherFieldSample weather = {
        .temperatureK = 298.0f,
        .pressureAtm = 0.98f,
        .pressureAnomaly = -0.04f,
        .relativeHumidity = 0.90f,
        .instability = 0.92f,
        .cloudCover = 0.95f,
        .precipitation = 0.76f,
        .rain = 0.72f,
        .storm = 0.92f,
        .wind = 0.58f,
        .gust = 0.82f,
        .windAngle = 0.0f
    };
    weather.cloudGenusCoverage[
        WEATHER_CLOUD_GENUS_CUMULONIMBUS] = 0.92f;
    weather.cloudGenera = WEATHER_CLOUD_GENUS_FLAG(
        WEATHER_CLOUD_GENUS_CUMULONIMBUS);
    return weather;
}

static void AdvanceToMature(Vector3 observer, WeatherFieldSample weather)
{
    for (int frame = 0; frame < 540; frame++) {
        TornadoUpdate(1.0f / 60.0f, observer, weather);
    }
    assert(TornadoCurrent().phase == TORNADO_PHASE_MATURE);
}

static void TestForcingAndBoundedDamage(void)
{
    Vector3 observer = { 0.0f, 11.0f, 0.0f };
    WeatherFieldSample weather = Storm();
    TornadoInit(true);
    assert(TornadoForce(observer, 1.0f, 3600u, 8.0f, weather));
    TornadoState initial = TornadoCurrent();
    assert(initial.active && initial.forced);
    assert(fabsf(TornadoDistanceTo(observer) - 8.0f) < 0.001f);
    AdvanceToMature(observer, weather);
    TornadoState mature = TornadoCurrent();
    assert(mature.center.x > initial.center.x);
    TornadoForceSample force = TornadoForceAt((Vector3){
        mature.center.x + mature.radius, mature.center.y + 1.0f,
        mature.center.z
    });
    assert(force.exposure > 0.8f);

    TornadoStats before = TornadoGetStats();
    TornadoStepTicks(100u, observer, weather);
    TornadoStats after = TornadoGetStats();
    assert(after.processedDamageSamples - before.processedDamageSamples ==
           100u * TORNADO_DAMAGE_SAMPLES_PER_TICK);
    assert(after.blockDamageEvents > before.blockDamageEvents);
    assert(after.blockDamageEvents - before.blockDamageEvents <=
           100u * TORNADO_DAMAGE_SAMPLES_PER_TICK);
    assert(after.debrisEmitted > before.debrisEmitted);
    assert(mutations == after.blockDamageEvents);
}

static void TestProtectionAndQueueBudget(void)
{
    Vector3 observer = { 0.0f, 11.0f, 0.0f };
    WeatherFieldSample weather = Storm();
    surfaceBlock = BLOCK_BEDROCK;
    mutations = 0u;
    pendingEffects = 0u;
    TornadoInit(true);
    assert(TornadoForce(observer, 1.0f, 3600u, 8.0f, weather));
    AdvanceToMature(observer, weather);
    TornadoStepTicks(80u, observer, weather);
    assert(TornadoGetStats().blockDamageEvents == 0u);
    assert(mutations == 0u);

    surfaceBlock = BLOCK_LEAVES;
    TornadoSetDamageEnabled(false);
    TornadoStats before = TornadoGetStats();
    TornadoStepTicks(20u, observer, weather);
    TornadoStats after = TornadoGetStats();
    assert(after.processedDamageSamples == before.processedDamageSamples);

    TornadoSetDamageEnabled(true);
    pendingEffects = GAME_EFFECTS_CAPACITY;
    TornadoStepTicks(100u, observer, weather);
    after = TornadoGetStats();
    assert(after.blockDamageEvents > 0u);
    assert(after.droppedEffects > 0u);
    assert(pendingEffects == GAME_EFFECTS_CAPACITY);
}

static void TestExpiryAndSurfaceReset(void)
{
    Vector3 observer = { 0.0f, 11.0f, 0.0f };
    WeatherFieldSample weather = Storm();
    TornadoInit(false);
    assert(TornadoForce(observer, 0.8f, 2u, 12.0f, weather));
    TornadoUpdate(1.0f / 60.0f, observer, weather);
    TornadoUpdate(1.0f / 60.0f, observer, weather);
    assert(!TornadoCurrent().active);
    assert(TornadoForcedFramesRemaining() == 0u);
    assert(TornadoDistanceTo(observer) == -1.0f);

    assert(TornadoForce(observer, 0.8f, 120u, 12.0f, weather));
    surfaceId++;
    TornadoUpdate(1.0f / 60.0f, observer, weather);
    assert(!TornadoCurrent().active);
    TornadoSuspend();
    assert(!TornadoCurrent().active);
}

static void TestSphericalAliases(void)
{
    const float circumference = (float)SURFACE_EQUATOR_BLOCKS;
    const float pole = (float)SURFACE_POLE_TO_POLE_BLOCKS * 0.5f;
    WeatherFieldSample weather = Storm();
    Vector3 seamObserver = {
        circumference * 0.5f - 4.0f, 11.0f, 0.0f
    };
    TornadoInit(false);
    assert(TornadoForce(seamObserver, 0.9f, 600u, 8.0f, weather));
    TornadoState seam = TornadoCurrent();
    assert(seam.center.x < -circumference * 0.5f + 8.1f);
    assert(fabsf(TornadoDistanceTo(seamObserver) - 8.0f) < 0.02f);
    Vector3 observerAlias = seamObserver;
    observerAlias.x -= circumference;
    assert(fabsf(TornadoDistanceTo(observerAlias) - 8.0f) < 0.02f);
    Vector3 sample = {
        seam.center.x + seam.radius, seam.center.y + 1.0f, seam.center.z
    };
    TornadoForceSample canonicalForce = TornadoForceAt(sample);
    sample.x += circumference;
    TornadoForceSample aliasForce = TornadoForceAt(sample);
    assert(fabsf(canonicalForce.exposure - aliasForce.exposure) < 0.001f);
    assert(fabsf(canonicalForce.acceleration.x - aliasForce.acceleration.x) <
           0.001f);
    assert(fabsf(canonicalForce.acceleration.z - aliasForce.acceleration.z) <
           0.001f);

    weather.windAngle = PI * 0.5f;
    Vector3 polarObserver = { 0.0f, 11.0f, pole - 4.0f };
    TornadoInit(false);
    assert(TornadoForce(polarObserver, 0.9f, 600u, 8.0f, weather));
    TornadoState polar = TornadoCurrent();
    assert(polar.center.z <= pole);
    assert(fabsf(TornadoDistanceTo(polarObserver) - 8.0f) < 0.02f);
    Vector3 polarAlias = { 0.0f, polar.center.y, pole + 4.0f };
    assert(TornadoDistanceTo(polarAlias) < 0.02f);
    TornadoForceSample polarCanonical = TornadoForceAt((Vector3){
        polar.center.x, polar.center.y + 1.0f, polar.center.z
    });
    polarAlias.y += 1.0f;
    TornadoForceSample polarAliasForce = TornadoForceAt(polarAlias);
    assert(fabsf(polarCanonical.exposure - polarAliasForce.exposure) <
           0.001f);
}

int main(void)
{
    TestForcingAndBoundedDamage();
    TestProtectionAndQueueBudget();
    TestExpiryAndSurfaceReset();
    TestSphericalAliases();
    puts("tornado runtime tests passed");
    return 0;
}
