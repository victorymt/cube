#include "world/weather_impact.h"

#include "space/space_state.h"
#include "world/fluid.h"
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WEATHER_IMPACT_SAMPLES_PER_TICK 8u
#define WEATHER_IMPACT_SURFACES_PER_TICK 64u
#define WEATHER_IMPACT_FIRES_PER_TICK 16u
#define WEATHER_IMPACT_RADIUS 24
#define WEATHER_IMPACT_MAGIC "WXIMPACT1"
#define WEATHER_IMPACT_MAGIC_SIZE 9u

enum WeatherOwnedEffect {
    WEATHER_OWNED_NONE = 0,
    WEATHER_OWNED_SNOW,
    WEATHER_OWNED_ICE
};

typedef struct WeatherImpactSurfaceRecord {
    WeatherSurfaceState state;
    BlockType originalBlock;
    BlockType expectedBlock;
    int effectY;
    uint8_t originalFluidVolume;
    uint8_t ownedEffect;
} WeatherImpactSurfaceRecord;

typedef struct WeatherImpactFireRecord {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    float intensity;
    float fuel;
} WeatherImpactFireRecord;

typedef struct WeatherImpactDiskHeader {
    uint32_t surfaceCount;
    uint32_t fireCount;
    uint64_t ticks;
} WeatherImpactDiskHeader;

static WeatherImpactSurfaceRecord impactSurfaces[WEATHER_IMPACT_MAX_SURFACES];
static WeatherImpactFireRecord impactFires[WEATHER_IMPACT_MAX_FIRES];
static uint32_t impactSurfaceCount = 0u;
static uint32_t impactFireCount = 0u;
static uint32_t impactSurfaceCursor = 0u;
static uint32_t impactFireCursor = 0u;
static float impactAccumulator = 0.0f;
static bool impactEnabled = true;
static WeatherImpactStats impactStats = { 0 };

static float ImpactUnit(float value)
{
    if (!isfinite(value) || value <= 0.0f) return 0.0f;
    return value >= 1.0f ? 1.0f : value;
}

static uint32_t ImpactMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t ImpactHash(uint64_t tick, uint32_t lane)
{
    uint32_t low = (uint32_t)tick;
    uint32_t high = (uint32_t)(tick >> 32);
    return ImpactMix(WorldGetSeed() ^ WorldCurrentSurfaceId() * 0x9e3779b9u ^
                     low ^ high * 0x85ebca6bu ^ lane * 0xc2b2ae35u);
}

static float ImpactHashUnit(uint64_t tick, uint32_t lane)
{
    return (float)(ImpactHash(tick, lane) & 0x00ffffffu) / 16777215.0f;
}

static int ImpactGroundHeight(int x, int z)
{
    return PlanetWorldIsActive() ? PlanetTerrainHeight(x, z) :
                                   WorldSurfaceHeightAt(x, z);
}

static bool ImpactTopBlock(int x, int z, int centerY, int *outY,
                           BlockType *outBlock)
{
    int ground = ImpactGroundHeight(x, z);
    int start = ground + 20;
    if (centerY > start) start = centerY + 8;
    int end = ground - 2;
    if (start - end > 48) end = start - 48;
    for (int y = start; y >= end; y--) {
        if (!SurfaceBlockReadyAt(x, y, z)) continue;
        BlockType block = GetBlockAt(x, y, z);
        if (block != BLOCK_AIR && block != BLOCK_WATER && block != BLOCK_LAVA) {
            if (outY) *outY = y;
            if (outBlock) *outBlock = block;
            return true;
        }
    }
    return false;
}

static int ImpactFindSurface(uint32_t surfaceId, int x, int z)
{
    for (uint32_t index = 0u; index < impactSurfaceCount; index++) {
        WeatherSurfaceState *state = &impactSurfaces[index].state;
        if (state->surfaceId == surfaceId && state->x == x && state->z == z) {
            return (int)index;
        }
    }
    return -1;
}

static WeatherImpactSurfaceRecord *ImpactSurfaceEnsure(
    uint32_t surfaceId, int x, int y, int z)
{
    int existing = ImpactFindSurface(surfaceId, x, z);
    if (existing >= 0) {
        impactSurfaces[existing].state.y = y;
        return &impactSurfaces[existing];
    }
    if (impactSurfaceCount >= WEATHER_IMPACT_MAX_SURFACES) {
        impactStats.droppedSurfaceUpdates++;
        return NULL;
    }
    WeatherImpactSurfaceRecord *record = &impactSurfaces[impactSurfaceCount++];
    *record = (WeatherImpactSurfaceRecord){
        .state = { .surfaceId = surfaceId, .x = x, .y = y, .z = z },
        .originalBlock = BLOCK_AIR,
        .expectedBlock = BLOCK_AIR,
        .effectY = y + 1
    };
    return record;
}

static void ImpactRemoveSurface(uint32_t index)
{
    if (index >= impactSurfaceCount) return;
    impactSurfaceCount--;
    if (index != impactSurfaceCount) {
        impactSurfaces[index] = impactSurfaces[impactSurfaceCount];
    }
    if (impactSurfaceCursor >= impactSurfaceCount) impactSurfaceCursor = 0u;
}

static void ImpactRestoreOwned(WeatherImpactSurfaceRecord *record)
{
    if (!record || record->ownedEffect == WEATHER_OWNED_NONE) return;
    if (GetBlockAt(record->state.x, record->effectY, record->state.z) ==
        record->expectedBlock) {
        SetBlockNoUndoFromSource(record->state.x, record->effectY,
                                 record->state.z, record->originalBlock,
                                 WORLD_MUTATION_ENVIRONMENT);
        if (record->originalBlock == BLOCK_WATER &&
            record->originalFluidVolume > 0u) {
            FluidSetVolumeAt(record->state.x, record->effectY,
                             record->state.z, record->originalFluidVolume);
        }
    }
    record->ownedEffect = WEATHER_OWNED_NONE;
    record->originalBlock = BLOCK_AIR;
    record->expectedBlock = BLOCK_AIR;
}

static bool ImpactDamageBlock(WeatherImpactSurfaceRecord *record,
                              BlockType block)
{
    if (!record || block == BLOCK_AIR || block == BLOCK_BEDROCK ||
        IsLiquidBlock(block)) {
        return false;
    }
    if (!SetBlockNoUndoFromSource(record->state.x, record->state.y,
                                  record->state.z, BLOCK_AIR,
                                  WORLD_MUTATION_ENVIRONMENT)) {
        return false;
    }
    impactStats.blockDamageEvents++;
    record->state.erosionExposure = 0.0f;
    record->state.windExposure = 0.0f;
    record->state.impactExposure = 0.0f;
    return true;
}

static void ImpactApplyPrecipitation(WeatherImpactSurfaceRecord *record,
                                     WeatherFieldSample weather,
                                     BlockType topBlock)
{
    WeatherSurfaceState *state = &record->state;
    float rain = ImpactUnit(weather.rain + weather.sleet * 0.45f +
                            weather.freezingRain);
    state->wetness = ImpactUnit(state->wetness + rain * 0.16f - 0.008f);
    if (state->wetness > 0.08f) state->flags |= WEATHER_SURFACE_WET;
    else state->flags &= ~WEATHER_SURFACE_WET;
    if (state->wetness > 0.62f &&
        (topBlock == BLOCK_DIRT || topBlock == BLOCK_GRASS ||
         topBlock == BLOCK_LOAM || topBlock == BLOCK_SILT)) {
        state->flags |= WEATHER_SURFACE_MUD;
    } else if (state->wetness < 0.34f) {
        state->flags &= ~WEATHER_SURFACE_MUD;
    }

    if (rain > 0.34f && record->ownedEffect == WEATHER_OWNED_NONE) {
        int waterY = state->y + 1;
        if (SurfaceBlockReadyAt(state->x, waterY, state->z)) {
            uint8_t before = FluidGetVolumeAt(state->x, waterY, state->z);
            unsigned add = 1u + (unsigned)floorf(rain * 5.0f);
            unsigned after = (unsigned)before + add;
            if (after > FLUID_CAPACITY) after = FLUID_CAPACITY;
            if (after > before && FluidSetVolumeAt(
                    state->x, waterY, state->z, (uint8_t)after)) {
                impactStats.depositedWater += after - before;
            }
        }
    }

    state->snowDepth = ImpactUnit(
        state->snowDepth + weather.snow * 0.14f -
        (weather.temperatureK > 274.5f ? 0.08f : 0.004f));
    if (state->snowDepth > 0.08f) state->flags |= WEATHER_SURFACE_SNOW;
    else state->flags &= ~WEATHER_SURFACE_SNOW;
    if (weather.frost > 0.22f) state->flags |= WEATHER_SURFACE_FROST;
    else if (weather.temperatureK > 274.0f) state->flags &= ~WEATHER_SURFACE_FROST;

    int effectY = state->y + 1;
    if (state->snowDepth > 0.82f && record->ownedEffect == WEATHER_OWNED_NONE &&
        SurfaceBlockReadyAt(state->x, effectY, state->z) &&
        GetBlockAt(state->x, effectY, state->z) == BLOCK_AIR) {
        record->originalBlock = BLOCK_AIR;
        record->expectedBlock = BLOCK_SNOW;
        record->effectY = effectY;
        if (SetBlockNoUndoFromSource(state->x, effectY, state->z, BLOCK_SNOW,
                                     WORLD_MUTATION_ENVIRONMENT)) {
            record->ownedEffect = WEATHER_OWNED_SNOW;
        }
    }
    if (weather.temperatureK < 271.5f &&
        record->ownedEffect == WEATHER_OWNED_NONE &&
        SurfaceBlockReadyAt(state->x, effectY, state->z) &&
        GetBlockAt(state->x, effectY, state->z) == BLOCK_WATER) {
        record->originalFluidVolume = FluidGetVolumeAt(
            state->x, effectY, state->z);
        record->originalBlock = BLOCK_WATER;
        record->expectedBlock = BLOCK_ICE;
        record->effectY = effectY;
        if (SetBlockNoUndoFromSource(state->x, effectY, state->z, BLOCK_ICE,
                                     WORLD_MUTATION_ENVIRONMENT)) {
            record->ownedEffect = WEATHER_OWNED_ICE;
            state->iceAmount = 1.0f;
            state->flags |= WEATHER_SURFACE_ICE;
        }
    }
    if (weather.temperatureK > 275.0f && record->ownedEffect != WEATHER_OWNED_NONE) {
        ImpactRestoreOwned(record);
        state->flags &= ~WEATHER_SURFACE_ICE;
        state->iceAmount = 0.0f;
    }
}

static void ImpactApplyMaterialStress(WeatherImpactSurfaceRecord *record,
                                      WeatherFieldSample weather,
                                      BlockType topBlock)
{
    BlockMaterialResponse material = BlockMaterialResponseFor(topBlock);
    float waterHazard = ImpactUnit(
        weather.rain * 0.55f + record->state.wetness * 0.45f);
    float erosionThreshold = 1.0f - material.waterErodibility * 0.78f;
    if (waterHazard > erosionThreshold) {
        record->state.erosionExposure +=
            (waterHazard - erosionThreshold) * 0.16f;
    } else {
        record->state.erosionExposure = fmaxf(
            record->state.erosionExposure - 0.01f, 0.0f);
    }
    if (weather.gust > material.windResistance) {
        record->state.windExposure +=
            (weather.gust - material.windResistance) * 0.24f;
    } else {
        record->state.windExposure = fmaxf(record->state.windExposure - 0.02f,
                                           0.0f);
    }
    if (weather.hail > material.impactResistance) {
        record->state.impactExposure +=
            (weather.hail - material.impactResistance) * 0.32f;
    } else {
        record->state.impactExposure = fmaxf(
            record->state.impactExposure - 0.02f, 0.0f);
    }
    if (record->state.erosionExposure >= 1.0f ||
        record->state.windExposure >= 1.0f ||
        record->state.impactExposure >= 1.0f) {
        ImpactDamageBlock(record, topBlock);
    }
}

static int ImpactFindFire(uint32_t surfaceId, int x, int y, int z)
{
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        if (fire->surfaceId == surfaceId && fire->x == x && fire->y == y &&
            fire->z == z) {
            return (int)index;
        }
    }
    return -1;
}

static bool ImpactIgnite(uint32_t surfaceId, int x, int y, int z,
                         float intensity)
{
    if (ImpactFindFire(surfaceId, x, y, z) >= 0) return true;
    BlockType block = GetBlockAt(x, y, z);
    BlockMaterialResponse material = BlockMaterialResponseFor(block);
    if (material.flammability <= 0.12f) return false;
    if (impactFireCount >= WEATHER_IMPACT_MAX_FIRES) {
        impactStats.droppedIgnitions++;
        return false;
    }
    impactFires[impactFireCount++] = (WeatherImpactFireRecord){
        .surfaceId = surfaceId,
        .x = x,
        .y = y,
        .z = z,
        .intensity = ImpactUnit(intensity),
        .fuel = fmaxf(material.flammability, 0.15f)
    };
    impactStats.ignitions++;
    return true;
}

static void ImpactRemoveFire(uint32_t index)
{
    if (index >= impactFireCount) return;
    impactFireCount--;
    if (index != impactFireCount) impactFires[index] = impactFires[impactFireCount];
    if (impactFireCursor >= impactFireCount) impactFireCursor = 0u;
}

static void ImpactProcessFires(WeatherFieldSample weather)
{
    uint32_t count = impactFireCount < WEATHER_IMPACT_FIRES_PER_TICK ?
        impactFireCount : WEATHER_IMPACT_FIRES_PER_TICK;
    for (uint32_t processed = 0u; processed < count && impactFireCount > 0u;) {
        if (impactFireCursor >= impactFireCount) impactFireCursor = 0u;
        WeatherImpactFireRecord *fire = &impactFires[impactFireCursor];
        if (fire->surfaceId != WorldCurrentSurfaceId() ||
            !SurfaceBlockReadyAt(fire->x, fire->y, fire->z)) {
            impactFireCursor++;
            processed++;
            continue;
        }
        BlockType block = GetBlockAt(fire->x, fire->y, fire->z);
        BlockMaterialResponse material = BlockMaterialResponseFor(block);
        if (block == BLOCK_AIR || material.flammability <= 0.01f ||
            weather.rain > 0.68f) {
            ImpactRemoveFire(impactFireCursor);
            processed++;
            continue;
        }
        fire->intensity = ImpactUnit(
            fire->intensity + material.flammability * 0.08f -
            weather.rain * 0.20f);
        fire->fuel -= (0.018f + fire->intensity * 0.026f);
        if (fire->fuel <= 0.0f) {
            if (SetBlockNoUndoFromSource(fire->x, fire->y, fire->z, BLOCK_AIR,
                                         WORLD_MUTATION_ENVIRONMENT)) {
                impactStats.blockDamageEvents++;
            }
            ImpactRemoveFire(impactFireCursor);
            processed++;
            continue;
        }

        static const int offsets[6][3] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 },
            { 0, 0, -1 }, { 0, 1, 0 }, { 0, -1, 0 }
        };
        uint32_t choice = ImpactHash(impactStats.ticks,
                                     0x500u + impactFireCursor) % 6u;
        int nx = fire->x + offsets[choice][0];
        int ny = fire->y + offsets[choice][1];
        int nz = fire->z + offsets[choice][2];
        float spreadChance = fire->intensity * (0.08f + weather.gust * 0.14f);
        if (ImpactHashUnit(impactStats.ticks,
                           0x600u + impactFireCursor) < spreadChance) {
            ImpactIgnite(fire->surfaceId, nx, ny, nz,
                         fire->intensity * 0.72f);
        }
        impactFireCursor++;
        processed++;
    }
}

static void ImpactSampleNewSurfaces(Vector3 playerPosition,
                                    WeatherFieldSample weather)
{
    int centerX = (int)floorf(playerPosition.x);
    int centerY = (int)floorf(playerPosition.y);
    int centerZ = (int)floorf(playerPosition.z);
    uint32_t surfaceId = WorldCurrentSurfaceId();
    for (uint32_t sampleIndex = 0u;
         sampleIndex < WEATHER_IMPACT_SAMPLES_PER_TICK; sampleIndex++) {
        uint32_t hash = ImpactHash(
            impactStats.ticks, sampleIndex + 1u);
        int span = WEATHER_IMPACT_RADIUS * 2 + 1;
        int x = centerX + (int)(hash % (uint32_t)span) - WEATHER_IMPACT_RADIUS;
        int z = centerZ + (int)((hash >> 16) % (uint32_t)span) -
                WEATHER_IMPACT_RADIUS;
        int topY = 0;
        BlockType topBlock = BLOCK_AIR;
        if (!ImpactTopBlock(x, z, centerY, &topY, &topBlock)) continue;
        WeatherImpactSurfaceRecord *record = ImpactSurfaceEnsure(
            surfaceId, x, topY, z);
        if (!record) continue;
        ImpactApplyPrecipitation(record, weather, topBlock);
        ImpactApplyMaterialStress(record, weather, topBlock);
        if (weather.lightning > 0.18f &&
            ImpactHashUnit(impactStats.ticks, 0x300u + sampleIndex) <
                weather.lightning * 0.035f) {
            ImpactIgnite(surfaceId, x, topY, z,
                         0.55f + weather.lightning * 0.45f);
        }
    }
}

static void ImpactAgeSurfaces(WeatherFieldSample weather)
{
    uint32_t count = impactSurfaceCount < WEATHER_IMPACT_SURFACES_PER_TICK ?
        impactSurfaceCount : WEATHER_IMPACT_SURFACES_PER_TICK;
    for (uint32_t processed = 0u;
         processed < count && impactSurfaceCount > 0u;) {
        if (impactSurfaceCursor >= impactSurfaceCount) impactSurfaceCursor = 0u;
        WeatherImpactSurfaceRecord *record = &impactSurfaces[impactSurfaceCursor];
        if (record->state.surfaceId != WorldCurrentSurfaceId()) {
            impactSurfaceCursor++;
            processed++;
            continue;
        }
        float evaporation = weather.temperatureK > 273.15f ?
            0.006f + ImpactUnit((weather.temperatureK - 273.15f) / 50.0f) *
            0.025f : 0.002f;
        record->state.wetness = fmaxf(record->state.wetness - evaporation, 0.0f);
        if (weather.temperatureK > 275.0f) {
            record->state.snowDepth = fmaxf(record->state.snowDepth - 0.06f,
                                            0.0f);
            record->state.iceAmount = fmaxf(record->state.iceAmount - 0.08f,
                                           0.0f);
        }
        if (record->state.wetness <= 0.0f) {
            record->state.flags &= ~(WEATHER_SURFACE_WET | WEATHER_SURFACE_MUD);
        }
        if (record->state.snowDepth <= 0.0f) {
            record->state.flags &= ~WEATHER_SURFACE_SNOW;
        }
        if (record->state.iceAmount <= 0.0f) {
            record->state.flags &= ~WEATHER_SURFACE_ICE;
        }
        bool empty = record->state.flags == 0u &&
            record->state.erosionExposure <= 0.0f &&
            record->state.windExposure <= 0.0f &&
            record->state.impactExposure <= 0.0f &&
            record->ownedEffect == WEATHER_OWNED_NONE;
        if (empty) ImpactRemoveSurface(impactSurfaceCursor);
        else impactSurfaceCursor++;
        processed++;
        impactStats.processedSurfaces++;
    }
}

static void ImpactTick(Vector3 playerPosition, WeatherFieldSample weather)
{
    impactStats.ticks++;
    ImpactAgeSurfaces(weather);
    ImpactSampleNewSurfaces(playerPosition, weather);
    ImpactProcessFires(weather);
    impactStats.surfaceCount = impactSurfaceCount;
    impactStats.activeFires = impactFireCount;
}

void WeatherImpactInit(bool damageEnabled)
{
    memset(impactSurfaces, 0, sizeof(impactSurfaces));
    memset(impactFires, 0, sizeof(impactFires));
    impactSurfaceCount = 0u;
    impactFireCount = 0u;
    impactSurfaceCursor = 0u;
    impactFireCursor = 0u;
    impactAccumulator = 0.0f;
    impactEnabled = damageEnabled;
    impactStats = (WeatherImpactStats){ 0 };
}

void WeatherImpactReset(void)
{
    bool enabled = impactEnabled;
    WeatherImpactInit(enabled);
}

void WeatherImpactSetEnabled(bool enabled)
{
    if (impactEnabled == enabled) return;
    if (!enabled) {
        for (uint32_t index = 0u; index < impactSurfaceCount; index++) {
            ImpactRestoreOwned(&impactSurfaces[index]);
        }
        impactSurfaceCount = 0u;
        impactFireCount = 0u;
        impactAccumulator = 0.0f;
        impactStats.surfaceCount = 0u;
        impactStats.activeFires = 0u;
    }
    impactEnabled = enabled;
}

bool WeatherImpactEnabled(void)
{
    return impactEnabled;
}

void WeatherImpactUpdate(float dt, Vector3 playerPosition,
                         WeatherFieldSample weather)
{
    if (!impactEnabled || !WorldIsSurfaceActive() || !isfinite(dt) ||
        dt <= 0.0f || !isfinite(playerPosition.x) ||
        !isfinite(playerPosition.y) || !isfinite(playerPosition.z)) {
        return;
    }
    impactAccumulator += fminf(dt, 0.25f) * WEATHER_IMPACT_TICK_RATE;
    unsigned ticks = (unsigned)floorf(impactAccumulator);
    if (ticks > 2u) ticks = 2u;
    impactAccumulator -= (float)ticks;
    WeatherImpactStepTicks(ticks, playerPosition, weather);
}

void WeatherImpactStepTicks(unsigned ticks, Vector3 playerPosition,
                            WeatherFieldSample weather)
{
    if (!impactEnabled || !WorldIsSurfaceActive() ||
        !isfinite(playerPosition.x) || !isfinite(playerPosition.y) ||
        !isfinite(playerPosition.z)) {
        return;
    }
    for (unsigned tick = 0u; tick < ticks; tick++) {
        ImpactTick(playerPosition, weather);
    }
}

bool WeatherImpactSurfaceAt(int x, int y, int z,
                            WeatherSurfaceState *outState)
{
    if (!outState) return false;
    int index = ImpactFindSurface(WorldCurrentSurfaceId(), x, z);
    if (index < 0 || (y != impactSurfaces[index].state.y &&
                      y != impactSurfaces[index].effectY)) {
        return false;
    }
    *outState = impactSurfaces[index].state;
    return true;
}

bool WeatherImpactFireAt(int x, int y, int z, float *outIntensity)
{
    int index = ImpactFindFire(WorldCurrentSurfaceId(), x, y, z);
    if (index < 0) return false;
    if (outIntensity) *outIntensity = impactFires[index].intensity;
    return true;
}

bool WeatherImpactIgniteAt(int x, int y, int z, float intensity)
{
    return impactEnabled && WorldIsSurfaceActive() && isfinite(intensity) &&
        ImpactIgnite(WorldCurrentSurfaceId(), x, y, z, ImpactUnit(intensity));
}

WeatherImpactStats WeatherImpactGetStats(void)
{
    WeatherImpactStats stats = impactStats;
    stats.surfaceCount = impactSurfaceCount;
    stats.activeFires = impactFireCount;
    return stats;
}

void WeatherImpactOnBlockChanged(int x, int y, int z)
{
    if (WorldCurrentMutationSource() == WORLD_MUTATION_ENVIRONMENT) return;
    uint32_t surfaceId = WorldCurrentSurfaceId();
    for (uint32_t index = 0u; index < impactSurfaceCount;) {
        WeatherImpactSurfaceRecord *record = &impactSurfaces[index];
        if (record->state.surfaceId == surfaceId && record->state.x == x &&
            record->state.z == z &&
            (record->state.y == y || record->effectY == y)) {
            record->ownedEffect = WEATHER_OWNED_NONE;
            ImpactRemoveSurface(index);
            continue;
        }
        index++;
    }
    int fireIndex = ImpactFindFire(surfaceId, x, y, z);
    if (fireIndex >= 0) ImpactRemoveFire((uint32_t)fireIndex);
}

static bool ImpactSurfaceRecordValid(const WeatherImpactSurfaceRecord *record)
{
    const WeatherSurfaceState *state = &record->state;
    return state->surfaceId <= UINT32_MAX &&
        abs(state->x) <= 1000000 && abs(state->z) <= 1000000 &&
        abs(state->y) <= 1000000 && abs(record->effectY) <= 1000000 &&
        isfinite(state->wetness) && state->wetness >= 0.0f &&
        state->wetness <= 1.0f && isfinite(state->snowDepth) &&
        state->snowDepth >= 0.0f && state->snowDepth <= 1.0f &&
        isfinite(state->iceAmount) && state->iceAmount >= 0.0f &&
        state->iceAmount <= 1.0f && isfinite(state->erosionExposure) &&
        isfinite(state->windExposure) && isfinite(state->impactExposure) &&
        IsValidBlockType(record->originalBlock) &&
        IsValidBlockType(record->expectedBlock) &&
        record->ownedEffect <= WEATHER_OWNED_ICE;
}

static bool ImpactFireRecordValid(const WeatherImpactFireRecord *fire)
{
    return abs(fire->x) <= 1000000 && abs(fire->y) <= 1000000 &&
        abs(fire->z) <= 1000000 && isfinite(fire->intensity) &&
        fire->intensity >= 0.0f && fire->intensity <= 1.0f &&
        isfinite(fire->fuel) && fire->fuel > 0.0f && fire->fuel <= 1.0f;
}

bool WeatherImpactSaveState(FILE *file)
{
    if (!file || fwrite(WEATHER_IMPACT_MAGIC, 1, WEATHER_IMPACT_MAGIC_SIZE,
                        file) != WEATHER_IMPACT_MAGIC_SIZE) {
        return false;
    }
    WeatherImpactDiskHeader header = {
        impactSurfaceCount, impactFireCount, impactStats.ticks
    };
    return fwrite(&header, sizeof(header), 1, file) == 1 &&
        fwrite(impactSurfaces, sizeof(impactSurfaces[0]), impactSurfaceCount,
               file) == impactSurfaceCount &&
        fwrite(impactFires, sizeof(impactFires[0]), impactFireCount, file) ==
            impactFireCount;
}

bool WeatherImpactLoadState(FILE *file)
{
    char magic[WEATHER_IMPACT_MAGIC_SIZE];
    WeatherImpactDiskHeader header = { 0 };
    WeatherImpactSurfaceRecord surfaces[WEATHER_IMPACT_MAX_SURFACES];
    WeatherImpactFireRecord fires[WEATHER_IMPACT_MAX_FIRES];
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic) ||
        memcmp(magic, WEATHER_IMPACT_MAGIC, sizeof(magic)) != 0 ||
        fread(&header, sizeof(header), 1, file) != 1 ||
        header.surfaceCount > WEATHER_IMPACT_MAX_SURFACES ||
        header.fireCount > WEATHER_IMPACT_MAX_FIRES ||
        fread(surfaces, sizeof(surfaces[0]), header.surfaceCount, file) !=
            header.surfaceCount ||
        fread(fires, sizeof(fires[0]), header.fireCount, file) !=
            header.fireCount) {
        return false;
    }
    for (uint32_t index = 0u; index < header.surfaceCount; index++) {
        if (!ImpactSurfaceRecordValid(&surfaces[index])) return false;
    }
    for (uint32_t index = 0u; index < header.fireCount; index++) {
        if (!ImpactFireRecordValid(&fires[index])) return false;
    }
    memcpy(impactSurfaces, surfaces,
           (size_t)header.surfaceCount * sizeof(surfaces[0]));
    memcpy(impactFires, fires,
           (size_t)header.fireCount * sizeof(fires[0]));
    impactSurfaceCount = header.surfaceCount;
    impactFireCount = header.fireCount;
    impactSurfaceCursor = 0u;
    impactFireCursor = 0u;
    impactAccumulator = 0.0f;
    impactStats = (WeatherImpactStats){
        .ticks = header.ticks,
        .surfaceCount = header.surfaceCount,
        .activeFires = header.fireCount
    };
    return true;
}
