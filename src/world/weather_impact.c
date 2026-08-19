#include "world/weather_impact.h"

#include "ecology/flora_taxa.h"

#include "space/space_state.h"
#include "world/fluid.h"
#include "world/surface_topology.h"
#include "world/terrain.h"
#include "world/wildfire_model.h"
#include "world/world.h"
#include "world/world_environment.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define WEATHER_IMPACT_SAMPLES_PER_TICK 8u
#define WEATHER_IMPACT_SURFACES_PER_TICK 64u
#define WEATHER_IMPACT_FIRES_PER_TICK 16u
#define WEATHER_IMPACT_BURN_SITES_PER_TICK 32u
#define WEATHER_IMPACT_RADIUS 24
#define WEATHER_IMPACT_MAGIC "WXIMPACT2"
#define WEATHER_IMPACT_LEGACY_MAGIC "WXIMPACT1"
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
    BlockType fuelBlock;
    float initialFuel;
    WildfireState state;
} WeatherImpactFireRecord;

typedef struct WeatherImpactLegacyFireRecord {
    uint32_t surfaceId;
    int x;
    int y;
    int z;
    float intensity;
    float fuel;
} WeatherImpactLegacyFireRecord;

typedef struct WeatherImpactLegacyDiskHeader {
    uint32_t surfaceCount;
    uint32_t fireCount;
    uint64_t ticks;
} WeatherImpactLegacyDiskHeader;

typedef struct WeatherImpactDiskHeader {
    uint32_t surfaceCount;
    uint32_t fireCount;
    uint32_t burnSiteCount;
    uint32_t reserved;
    uint64_t ticks;
    uint64_t processedSurfaces;
    uint64_t depositedWater;
    uint32_t blockDamageEvents;
    uint32_t ignitions;
    uint32_t spreadIgnitions;
    uint32_t extinctions;
    uint32_t suppressions;
    uint32_t recoveredBurnSites;
    uint32_t droppedSurfaceUpdates;
    uint32_t droppedIgnitions;
    uint32_t droppedBurnSites;
    uint32_t burnedBlocks;
} WeatherImpactDiskHeader;

typedef struct WeatherImpactSurfaceDiskRecord {
    uint32_t surfaceId;
    int32_t x;
    int32_t y;
    int32_t z;
    uint32_t flags;
    float wetness;
    float snowDepth;
    float iceAmount;
    float erosionExposure;
    float windExposure;
    float impactExposure;
    uint32_t originalBlock;
    uint32_t expectedBlock;
    int32_t effectY;
    uint8_t originalFluidVolume;
    uint8_t ownedEffect;
    uint8_t reserved[2];
} WeatherImpactSurfaceDiskRecord;

typedef struct WeatherImpactFireDiskRecord {
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
} WeatherImpactFireDiskRecord;

typedef struct WeatherImpactBurnDiskRecord {
    uint32_t surfaceId;
    int32_t x;
    int32_t y;
    int32_t z;
    float severity;
    float recovery;
    float ageSeconds;
} WeatherImpactBurnDiskRecord;

static WeatherImpactSurfaceRecord impactSurfaces[WEATHER_IMPACT_MAX_SURFACES];
static WeatherImpactFireRecord impactFires[WEATHER_IMPACT_MAX_FIRES];
static WeatherBurnSiteState impactBurnSites[WEATHER_IMPACT_MAX_BURN_SITES];
static uint32_t impactSurfaceCount = 0u;
static uint32_t impactFireCount = 0u;
static uint32_t impactBurnSiteCount = 0u;
static uint32_t impactSurfaceCursor = 0u;
static uint32_t impactFireCursor = 0u;
static uint32_t impactBurnSiteCursor = 0u;
static float impactAccumulator = 0.0f;
static bool impactEnabled = true;
static WeatherImpactStats impactStats = { 0 };
static WeatherFieldSample impactLastWeather = {
    .temperatureK = 293.15f,
    .relativeHumidity = 0.28f,
    .visibility = 1.0f
};

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

static bool ImpactCoordinateValid(int value)
{
    return value >= -1000000 && value <= 1000000;
}

static void ImpactCanonicalizeXZ(int *x, int *z)
{
    if (!x || !z) return;
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)*x, (float)*z);
    *x = cell.x;
    *z = cell.z;
}

static float ImpactFuelLoad(BlockType block, float flammability)
{
    switch (block) {
    case BLOCK_TALL_GRASS:
    case BLOCK_FLOWER:
    case BLOCK_FERN:
    case BLOCK_REED:
    case BLOCK_MOSS_CARPET:
    case BLOCK_LICHEN:
    case BLOCK_CANOPY_FROND:
        return 0.28f + flammability * 0.24f;
    case BLOCK_LEAVES:
    case BLOCK_OAK_LEAVES:
    case BLOCK_BIRCH_LEAVES:
    case BLOCK_ASPEN_LEAVES:
    case BLOCK_SPRUCE_NEEDLES:
    case BLOCK_PINE_NEEDLES:
    case BLOCK_WILLOW_LEAVES:
    case BLOCK_HAY_BALE:
    case BLOCK_MUSHROOM:
    case BLOCK_SPORE_CAP:
    case BLOCK_LUMINOUS_POD:
        return 0.62f + flammability * 0.36f;
    case BLOCK_WOOD:
    case BLOCK_OAK_LOG:
    case BLOCK_BIRCH_LOG:
    case BLOCK_ASPEN_LOG:
    case BLOCK_SPRUCE_LOG:
    case BLOCK_PINE_LOG:
    case BLOCK_WILLOW_LOG:
    case BLOCK_LIVING_STEM:
    case BLOCK_FUNGAL_STEM:
        return 2.20f + flammability * 1.30f;
    case BLOCK_PLANK:
    case BLOCK_WOOD_STAIRS:
    case BLOCK_FENCE:
    case BLOCK_FENCE_GATE:
    case BLOCK_FENCE_GATE_OPEN:
    case BLOCK_DOOR:
    case BLOCK_DOOR_OPEN:
    case BLOCK_BOOKSHELF:
        return 1.35f + flammability * 0.90f;
    case BLOCK_PEAT:
        return 2.80f;
    default:
        return 0.42f + ImpactUnit(flammability) * 0.82f;
    }
}

BlockType WeatherImpactResidueForFuel(BlockType fuel, float severity,
                                      float moisture)
{
    severity = ImpactUnit(severity);
    moisture = ImpactUnit(moisture);
    switch (fuel) {
    case BLOCK_WOOD:
    case BLOCK_OAK_LOG:
    case BLOCK_BIRCH_LOG:
    case BLOCK_ASPEN_LOG:
    case BLOCK_SPRUCE_LOG:
    case BLOCK_PINE_LOG:
    case BLOCK_WILLOW_LOG:
    case BLOCK_LIVING_STEM:
    case BLOCK_FUNGAL_STEM:
        return severity < 0.80f || moisture > 0.05f
            ? BLOCK_CHARRED_WOOD : BLOCK_CHARCOAL;
    case BLOCK_PLANK:
    case BLOCK_WOOD_STAIRS:
    case BLOCK_FENCE:
    case BLOCK_FENCE_GATE:
    case BLOCK_FENCE_GATE_OPEN:
    case BLOCK_DOOR:
    case BLOCK_DOOR_OPEN:
    case BLOCK_BOOKSHELF:
        return severity < 0.60f || moisture > 0.45f
            ? BLOCK_CHARRED_WOOD : BLOCK_CHARCOAL;
    case BLOCK_PEAT:
    case BLOCK_HUMUS:
    case BLOCK_COMPOST:
    case BLOCK_MYCELIUM:
        return severity > 0.85f && moisture < 0.30f
            ? BLOCK_CHARCOAL : BLOCK_FIRE_ASH;
    default:
        return BLOCK_FIRE_ASH;
    }
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
    ImpactCanonicalizeXZ(&x, &z);
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
    ImpactCanonicalizeXZ(&x, &z);
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
    ImpactCanonicalizeXZ(&x, &z);
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        if (fire->surfaceId == surfaceId && fire->x == x && fire->y == y &&
            fire->z == z) {
            return (int)index;
        }
    }
    return -1;
}

static int ImpactFindBurnSite(uint32_t surfaceId, int x, int y, int z)
{
    ImpactCanonicalizeXZ(&x, &z);
    for (uint32_t index = 0u; index < impactBurnSiteCount; index++) {
        WeatherBurnSiteState *site = &impactBurnSites[index];
        if (site->surfaceId == surfaceId && site->x == x && site->y == y &&
            site->z == z) {
            return (int)index;
        }
    }
    return -1;
}

static void ImpactRemoveBurnSite(uint32_t index)
{
    if (index >= impactBurnSiteCount) return;
    impactBurnSiteCount--;
    if (index != impactBurnSiteCount) {
        impactBurnSites[index] = impactBurnSites[impactBurnSiteCount];
    }
    if (impactBurnSiteCursor >= impactBurnSiteCount) impactBurnSiteCursor = 0u;
}

static void ImpactRecordBurnSite(const WeatherImpactFireRecord *fire,
                                 float severity)
{
    severity = ImpactUnit(severity);
    if (!fire || severity <= 0.01f) return;
    int existing = ImpactFindBurnSite(
        fire->surfaceId, fire->x, fire->y, fire->z);
    if (existing >= 0) {
        WeatherBurnSiteState *site = &impactBurnSites[existing];
        site->severity = fmaxf(site->severity, severity);
        site->recovery = fminf(site->recovery, 0.18f);
        site->ageSeconds = 0.0f;
        return;
    }
    if (impactBurnSiteCount >= WEATHER_IMPACT_MAX_BURN_SITES) {
        impactStats.droppedBurnSites++;
        return;
    }
    impactBurnSites[impactBurnSiteCount++] = (WeatherBurnSiteState){
        .surfaceId = fire->surfaceId,
        .x = fire->x,
        .y = fire->y,
        .z = fire->z,
        .severity = severity
    };
}

static float ImpactWaterExposure(int x, int y, int z)
{
    static const int offsets[7][3] = {
        { 0, 0, 0 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    float exposure = 0.0f;
    for (unsigned index = 0u; index < 7u; index++) {
        int nx = x + offsets[index][0];
        int ny = y + offsets[index][1];
        int nz = z + offsets[index][2];
        if (!SurfaceBlockReadyAt(nx, ny, nz)) continue;
        if (GetBlockAt(nx, ny, nz) == BLOCK_WATER) exposure = 1.0f;
        float fluid = (float)FluidGetVolumeAt(nx, ny, nz) /
                      (float)FLUID_CAPACITY;
        exposure = fmaxf(exposure, ImpactUnit(fluid));
    }
    return exposure;
}

static WildfireEnvironment ImpactFireEnvironment(
    WeatherFieldSample weather, float waterExposure, float suppression)
{
    return (WildfireEnvironment){
        .temperatureK = isfinite(weather.temperatureK) &&
                        weather.temperatureK > 0.0f ?
                            weather.temperatureK : 293.15f,
        .relativeHumidity = ImpactUnit(weather.relativeHumidity),
        .rain = ImpactUnit(weather.rain + weather.sleet * 0.55f +
                           weather.freezingRain * 0.85f),
        .wind = ImpactUnit(weather.wind),
        .gust = ImpactUnit(weather.gust),
        .waterExposure = ImpactUnit(waterExposure),
        .suppression = ImpactUnit(suppression)
    };
}

static float ImpactFuelMoisture(int x, int y, int z,
                                WeatherFieldSample weather)
{
    float surfaceWetness = 0.0f;
    int surfaceIndex = ImpactFindSurface(WorldCurrentSurfaceId(), x, z);
    if (surfaceIndex >= 0) {
        surfaceWetness = impactSurfaces[surfaceIndex].state.wetness;
    }
    float water = ImpactWaterExposure(x, y, z);
    float equilibrium = WildfireEquilibriumMoisture(
        ImpactFireEnvironment(weather, water, 0.0f));
    return ImpactUnit(fmaxf(fmaxf(equilibrium * 0.72f,
                                  surfaceWetness * 0.90f),
                           water * 0.94f));
}

static bool ImpactIgnite(uint32_t surfaceId, int x, int y, int z,
                         float intensity, float moisture, bool *outCreated)
{
    if (outCreated) *outCreated = false;
    ImpactCanonicalizeXZ(&x, &z);
    int existing = ImpactFindFire(surfaceId, x, y, z);
    if (existing >= 0) {
        WeatherImpactFireRecord *fire = &impactFires[existing];
        fire->state.intensity = fmaxf(fire->state.intensity,
                                      ImpactUnit(intensity));
        WildfireModelNormalize(&fire->state);
        return true;
    }
    if (!SurfaceBlockReadyAt(x, y, z)) return false;
    BlockType block = GetBlockAt(x, y, z);
    BlockMaterialResponse material = BlockMaterialResponseFor(block);
    float fuelLoad = ImpactFuelLoad(block, material.flammability);
    WildfireState state = WildfireModelCreate(
        material.flammability, fuelLoad, ImpactUnit(moisture), intensity);
    if (state.phase == WILDFIRE_PHASE_INACTIVE) return false;
    if (impactFireCount >= WEATHER_IMPACT_MAX_FIRES) {
        impactStats.droppedIgnitions++;
        return false;
    }
    impactFires[impactFireCount++] = (WeatherImpactFireRecord){
        .surfaceId = surfaceId,
        .x = x,
        .y = y,
        .z = z,
        .fuelBlock = block,
        .initialFuel = fuelLoad,
        .state = state
    };
    impactStats.ignitions++;
    if (outCreated) *outCreated = true;
    return true;
}

static void ImpactRemoveFire(uint32_t index)
{
    if (index >= impactFireCount) return;
    impactFireCount--;
    if (index != impactFireCount) impactFires[index] = impactFires[impactFireCount];
    if (impactFireCursor >= impactFireCount) impactFireCursor = 0u;
}

static void ImpactFinishFire(uint32_t index, bool countExtinction)
{
    if (index >= impactFireCount) return;
    WeatherImpactFireRecord fire = impactFires[index];
    float consumed = fire.initialFuel > 0.0f ?
        1.0f - fire.state.fuel / fire.initialFuel : 0.0f;
    ImpactRecordBurnSite(&fire, consumed);
    if (countExtinction) impactStats.extinctions++;
    ImpactRemoveFire(index);
}

static uint32_t ImpactFireLane(const WeatherImpactFireRecord *fire,
                               uint32_t salt)
{
    uint32_t value = fire ? fire->surfaceId : 0u;
    if (fire) {
        value ^= ImpactMix((uint32_t)fire->x * 0x9e3779b9u);
        value ^= ImpactMix((uint32_t)fire->y * 0x85ebca6bu);
        value ^= ImpactMix((uint32_t)fire->z * 0xc2b2ae35u);
    }
    return ImpactMix(value ^ salt);
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
        bool fuelConsumed = fire->state.fuel <= 0.0f;
        if (!fuelConsumed &&
            (block == BLOCK_AIR || material.flammability <= 0.12f)) {
            ImpactFinishFire(impactFireCursor, true);
            processed++;
            continue;
        }
        float previousFuel = fire->state.fuel;
        float flammability = fuelConsumed ?
            BlockMaterialResponseFor(fire->fuelBlock).flammability :
            material.flammability;
        float waterExposure = ImpactWaterExposure(fire->x, fire->y, fire->z);
        WildfireModelAdvance(
            &fire->state, 1.0f / WEATHER_IMPACT_TICK_RATE, flammability,
            ImpactFireEnvironment(weather, waterExposure, 0.0f));
        if (previousFuel > 0.0f && fire->state.fuel <= 0.0f) {
            float severity = fire->initialFuel > 0.0f
                ? 1.0f - fire->state.fuel / fire->initialFuel : 1.0f;
            BlockType residue = WeatherImpactResidueForFuel(
                fire->fuelBlock, severity, fire->state.moisture);
            if (GetBlockAt(fire->x, fire->y, fire->z) == fire->fuelBlock &&
                SetBlockNoUndoFromSource(fire->x, fire->y, fire->z,
                                         residue,
                                         WORLD_MUTATION_ENVIRONMENT)) {
                impactStats.blockDamageEvents++;
                impactStats.burnedBlocks++;
            }
            ImpactRecordBurnSite(fire, 1.0f);
        }
        if (fire->state.phase == WILDFIRE_PHASE_INACTIVE) {
            ImpactFinishFire(impactFireCursor, true);
            processed++;
            continue;
        }

        static const int offsets[10][3] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
            { 1, 0, 1 }, { -1, 0, 1 }, { 1, 0, -1 }, { -1, 0, -1 },
            { 0, 1, 0 }, { 0, -1, 0 }
        };
        uint32_t lane = ImpactFireLane(fire, 0x500u);
        uint32_t choice = ImpactHash(impactStats.ticks, lane) % 10u;
        int nx = fire->x + offsets[choice][0];
        int ny = fire->y + offsets[choice][1];
        int nz = fire->z + offsets[choice][2];
        if (SurfaceBlockReadyAt(nx, ny, nz)) {
            BlockType targetBlock = GetBlockAt(nx, ny, nz);
            BlockMaterialResponse target = BlockMaterialResponseFor(targetBlock);
            float dx = (float)offsets[choice][0];
            float dz = (float)offsets[choice][2];
            if (dx == 0.0f && dz == 0.0f) dx = 0.20f;
            float horizontal = fmaxf(hypotf(dx, dz), 0.20f);
            float targetMoisture = ImpactFuelMoisture(nx, ny, nz, weather);
            float spreadChance = WildfireSpreadProbability(
                (WildfireSpreadInput){
                    .sourceIntensity = fire->state.intensity,
                    .targetFlammability = target.flammability,
                    .targetMoisture = targetMoisture,
                    .wind = weather.wind,
                    .gust = weather.gust,
                    .windAngle = weather.windAngle,
                    .offsetX = dx,
                    .offsetZ = dz,
                    .slope = (float)offsets[choice][1] / horizontal
                });
            bool created = false;
            if (ImpactHashUnit(impactStats.ticks,
                               ImpactFireLane(fire, 0x600u)) < spreadChance &&
                ImpactIgnite(fire->surfaceId, nx, ny, nz,
                             fire->state.intensity * 0.72f,
                             targetMoisture, &created) && created) {
                impactStats.spreadIgnitions++;
            }
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
        float moisture = ImpactFuelMoisture(x, topY, z, weather);
        if (weather.lightning > 0.18f &&
            SurfaceBlockReadyAt(x, topY + 1, z) &&
            GetBlockAt(x, topY + 1, z) == BLOCK_AIR &&
            ImpactHashUnit(impactStats.ticks, 0x300u + sampleIndex) <
                weather.lightning * 0.035f) {
            ImpactIgnite(surfaceId, x, topY, z,
                         0.55f + weather.lightning * 0.45f,
                         moisture, NULL);
        }
        static const int lavaOffsets[6][3] = {
            { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
            { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
        };
        bool adjacentLava = false;
        for (unsigned offset = 0u; offset < 6u; offset++) {
            int lx = x + lavaOffsets[offset][0];
            int ly = topY + lavaOffsets[offset][1];
            int lz = z + lavaOffsets[offset][2];
            if (SurfaceBlockReadyAt(lx, ly, lz) &&
                GetBlockAt(lx, ly, lz) == BLOCK_LAVA) {
                adjacentLava = true;
                break;
            }
        }
        if (adjacentLava &&
            ImpactHashUnit(impactStats.ticks, 0x400u + sampleIndex) < 0.18f) {
            ImpactIgnite(surfaceId, x, topY, z, 0.72f, moisture, NULL);
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

static void ImpactRecoverBurnSites(WeatherFieldSample weather)
{
    uint32_t count = impactBurnSiteCount < WEATHER_IMPACT_BURN_SITES_PER_TICK ?
        impactBurnSiteCount : WEATHER_IMPACT_BURN_SITES_PER_TICK;
    for (uint32_t processed = 0u;
         processed < count && impactBurnSiteCount > 0u;) {
        if (impactBurnSiteCursor >= impactBurnSiteCount) {
            impactBurnSiteCursor = 0u;
        }
        WeatherBurnSiteState *site = &impactBurnSites[impactBurnSiteCursor];
        if (site->surfaceId != WorldCurrentSurfaceId()) {
            impactBurnSiteCursor++;
            processed++;
            continue;
        }
        float temperate = ImpactUnit(
            1.0f - fabsf(weather.temperatureK - 291.0f) / 42.0f);
        float wet = ImpactUnit(weather.relativeHumidity * 0.55f +
                               weather.rain * 0.85f);
        float recoverySeconds = 6.0f * 24.0f * 3600.0f;
        float rate = (0.45f + temperate * 0.72f + wet * 1.10f) /
                     recoverySeconds;
        site->ageSeconds += 1.0f / WEATHER_IMPACT_TICK_RATE;
        site->recovery = ImpactUnit(
            site->recovery + rate / WEATHER_IMPACT_TICK_RATE);
        FloraDisturbanceStage succession = FloraDisturbanceStageForBurn(
            site->severity, site->recovery);
        if (succession == FLORA_DISTURBANCE_HERB_PIONEER &&
            SurfaceBlockReadyAt(site->x, site->y, site->z) &&
            SurfaceBlockReadyAt(site->x, site->y + 1, site->z)) {
            BlockType current = GetBlockAt(site->x, site->y, site->z);
            BlockType below = GetBlockAt(site->x, site->y - 1, site->z);
            BlockType above = GetBlockAt(site->x, site->y + 1, site->z);
            bool residue = current == BLOCK_FIRE_ASH ||
                           current == BLOCK_CHARCOAL;
            bool soil = below == BLOCK_GRASS || below == BLOCK_DIRT ||
                        below == BLOCK_MUD || below == BLOCK_LOAM ||
                        below == BLOCK_PODZOL || below == BLOCK_PEAT ||
                        below == BLOCK_CHERNOZEM ||
                        below == BLOCK_TERRA_ROSSA ||
                        below == BLOCK_ALLUVIUM || below == BLOCK_HUMUS ||
                        below == BLOCK_COMPOST;
            if (residue && soil && above == BLOCK_AIR) {
                SetBlockNoUndoFromSource(site->x, site->y, site->z,
                                         BLOCK_FIREWEED,
                                         WORLD_MUTATION_ENVIRONMENT);
            }
        }
        if (site->recovery >= 1.0f) {
            ImpactRemoveBurnSite(impactBurnSiteCursor);
            impactStats.recoveredBurnSites++;
        } else {
            impactBurnSiteCursor++;
        }
        processed++;
    }
}

static void ImpactTick(Vector3 playerPosition, WeatherFieldSample weather)
{
    impactStats.ticks++;
    impactLastWeather = weather;
    ImpactAgeSurfaces(weather);
    ImpactSampleNewSurfaces(playerPosition, weather);
    ImpactProcessFires(weather);
    ImpactRecoverBurnSites(weather);
    impactStats.surfaceCount = impactSurfaceCount;
    impactStats.activeFires = impactFireCount;
    impactStats.burnSiteCount = impactBurnSiteCount;
}

void WeatherImpactInit(bool damageEnabled)
{
    memset(impactSurfaces, 0, sizeof(impactSurfaces));
    memset(impactFires, 0, sizeof(impactFires));
    memset(impactBurnSites, 0, sizeof(impactBurnSites));
    impactSurfaceCount = 0u;
    impactFireCount = 0u;
    impactBurnSiteCount = 0u;
    impactSurfaceCursor = 0u;
    impactFireCursor = 0u;
    impactBurnSiteCursor = 0u;
    impactAccumulator = 0.0f;
    impactEnabled = damageEnabled;
    impactStats = (WeatherImpactStats){ 0 };
    impactLastWeather = (WeatherFieldSample){
        .temperatureK = 293.15f,
        .relativeHumidity = 0.28f,
        .visibility = 1.0f
    };
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
    if (outIntensity) *outIntensity = impactFires[index].state.intensity;
    return true;
}

static WeatherImpactFireSnapshot ImpactFireSnapshot(
    const WeatherImpactFireRecord *fire)
{
    return (WeatherImpactFireSnapshot){
        .surfaceId = fire->surfaceId,
        .x = fire->x,
        .y = fire->y,
        .z = fire->z,
        .fuelBlock = fire->fuelBlock,
        .state = fire->state
    };
}

bool WeatherImpactFireStateAt(int x, int y, int z,
                              WeatherImpactFireSnapshot *outFire)
{
    if (!outFire) return false;
    int index = ImpactFindFire(WorldCurrentSurfaceId(), x, y, z);
    if (index < 0) return false;
    *outFire = ImpactFireSnapshot(&impactFires[index]);
    return true;
}

bool WeatherImpactIgniteAt(int x, int y, int z, float intensity)
{
    return impactEnabled && WorldIsSurfaceActive() && isfinite(intensity) &&
        intensity > 0.0f && ImpactIgnite(
            WorldCurrentSurfaceId(), x, y, z, ImpactUnit(intensity),
            ImpactFuelMoisture(x, y, z, impactLastWeather), NULL);
}

unsigned WeatherImpactSuppressAt(int x, int y, int z, float radius,
                                 float amount)
{
    if (!impactEnabled || !WorldIsSurfaceActive() || !isfinite(radius) ||
        radius < 0.0f || radius > 64.0f || !isfinite(amount) ||
        amount <= 0.0f) {
        return 0u;
    }
    ImpactCanonicalizeXZ(&x, &z);
    float radiusSquared = radius * radius;
    uint32_t surfaceId = WorldCurrentSurfaceId();
    unsigned affected = 0u;
    for (uint32_t index = 0u; index < impactFireCount;) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        SurfaceMapOffset offset = SurfaceShortestMapOffset(
            (float)x, (float)z, (float)fire->x, (float)fire->z);
        float dy = (float)(fire->y - y);
        if (fire->surfaceId != surfaceId ||
            offset.x * offset.x + dy * dy + offset.z * offset.z >
                radiusSquared ||
            !SurfaceBlockReadyAt(fire->x, fire->y, fire->z)) {
            index++;
            continue;
        }
        WildfireModelApplySuppression(&fire->state, amount);
        affected++;
        impactStats.suppressions++;
        if (fire->state.phase == WILDFIRE_PHASE_INACTIVE) {
            ImpactFinishFire(index, true);
        } else {
            index++;
        }
    }
    return affected;
}

unsigned WeatherImpactClearFires(void)
{
    unsigned cleared = impactFireCount;
    while (impactFireCount > 0u) {
        ImpactFinishFire(impactFireCount - 1u, false);
    }
    return cleared;
}

static float ImpactFireDistanceSquared(const WeatherImpactFireRecord *fire,
                                       Vector3 origin)
{
    SurfaceMapOffset offset = SurfaceShortestMapOffset(
        origin.x, origin.z, (float)fire->x + 0.5f,
        (float)fire->z + 0.5f);
    float dy = (float)fire->y + 0.5f - origin.y;
    return offset.x * offset.x + dy * dy + offset.z * offset.z;
}

unsigned WeatherImpactCollectFires(Vector3 origin, float radius,
                                   WeatherImpactFireSnapshot *outFires,
                                   unsigned capacity)
{
    if (!outFires || capacity == 0u || !isfinite(origin.x) ||
        !isfinite(origin.y) || !isfinite(origin.z) || !isfinite(radius) ||
        radius <= 0.0f) {
        return 0u;
    }
    if (capacity > WEATHER_IMPACT_MAX_FIRES) {
        capacity = WEATHER_IMPACT_MAX_FIRES;
    }
    float distances[WEATHER_IMPACT_MAX_FIRES];
    float radiusSquared = radius * radius;
    uint32_t surfaceId = WorldCurrentSurfaceId();
    unsigned count = 0u;
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        if (fire->surfaceId != surfaceId ||
            !SurfaceBlockReadyAt(fire->x, fire->y, fire->z)) {
            continue;
        }
        float distance = ImpactFireDistanceSquared(fire, origin);
        if (!isfinite(distance) || distance > radiusSquared) continue;
        unsigned insertion = 0u;
        while (insertion < count && distances[insertion] <= distance) {
            insertion++;
        }
        if (insertion >= capacity) continue;
        unsigned end = count < capacity ? count : capacity - 1u;
        for (unsigned move = end; move > insertion; move--) {
            distances[move] = distances[move - 1u];
            outFires[move] = outFires[move - 1u];
        }
        distances[insertion] = distance;
        outFires[insertion] = ImpactFireSnapshot(fire);
        if (count < capacity) count++;
    }
    return count;
}

bool WeatherImpactNearestFire(Vector3 position,
                              WeatherImpactFireSnapshot *outFire,
                              float *outDistance)
{
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z)) {
        return false;
    }
    uint32_t surfaceId = WorldCurrentSurfaceId();
    float nearest = INFINITY;
    const WeatherImpactFireRecord *nearestFire = NULL;
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        if (fire->surfaceId != surfaceId ||
            !SurfaceBlockReadyAt(fire->x, fire->y, fire->z)) {
            continue;
        }
        float distance = ImpactFireDistanceSquared(fire, position);
        if (distance < nearest) {
            nearest = distance;
            nearestFire = fire;
        }
    }
    if (!nearestFire) return false;
    if (outFire) *outFire = ImpactFireSnapshot(nearestFire);
    if (outDistance) *outDistance = sqrtf(nearest);
    return true;
}

WeatherImpactExposure WeatherImpactExposureAt(Vector3 position,
                                              float shelter,
                                              float immersion)
{
    WeatherImpactExposure result = { .nearestDistance = INFINITY };
    if (!isfinite(position.x) || !isfinite(position.y) ||
        !isfinite(position.z)) {
        return result;
    }
    uint32_t surfaceId = WorldCurrentSurfaceId();
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireRecord *fire = &impactFires[index];
        if (fire->surfaceId != surfaceId ||
            !SurfaceBlockReadyAt(fire->x, fire->y, fire->z)) {
            continue;
        }
        float distance = sqrtf(ImpactFireDistanceSquared(fire, position));
        result.nearestDistance = fminf(result.nearestDistance, distance);
        result.heat += WildfireHeatExposure(
            &fire->state, distance, shelter, immersion);
        result.smoke += WildfireSmokeExposure(
            &fire->state, distance, shelter, immersion);
    }
    result.heat = ImpactUnit(result.heat);
    result.smoke = ImpactUnit(result.smoke);
    return result;
}

bool WeatherImpactBurnSiteAt(int x, int y, int z,
                             WeatherBurnSiteState *outSite)
{
    if (!outSite) return false;
    int index = ImpactFindBurnSite(WorldCurrentSurfaceId(), x, y, z);
    if (index < 0) return false;
    *outSite = impactBurnSites[index];
    return true;
}

float WeatherImpactBurnSeverityAt(int x, int y, int z)
{
    int index = ImpactFindBurnSite(WorldCurrentSurfaceId(), x, y, z);
    if (index < 0) return 0.0f;
    WeatherBurnSiteState *site = &impactBurnSites[index];
    return ImpactUnit(site->severity * (1.0f - site->recovery));
}

WeatherImpactStats WeatherImpactGetStats(void)
{
    WeatherImpactStats stats = impactStats;
    stats.surfaceCount = impactSurfaceCount;
    stats.activeFires = impactFireCount;
    stats.burnSiteCount = impactBurnSiteCount;
    return stats;
}

void WeatherImpactOnBlockChanged(int x, int y, int z)
{
    if (WorldCurrentMutationSource() == WORLD_MUTATION_ENVIRONMENT) return;
    ImpactCanonicalizeXZ(&x, &z);
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
    if (fireIndex >= 0) ImpactFinishFire((uint32_t)fireIndex, false);
}

static bool ImpactSurfaceRecordValid(const WeatherImpactSurfaceRecord *record)
{
    const WeatherSurfaceState *state = &record->state;
    const uint32_t knownFlags = WEATHER_SURFACE_WET | WEATHER_SURFACE_MUD |
        WEATHER_SURFACE_SNOW | WEATHER_SURFACE_FROST | WEATHER_SURFACE_ICE;
    return ImpactCoordinateValid(state->x) &&
        ImpactCoordinateValid(state->z) && ImpactCoordinateValid(state->y) &&
        ImpactCoordinateValid(record->effectY) &&
        ((uint32_t)state->flags & ~knownFlags) == 0u &&
        isfinite(state->wetness) && state->wetness >= 0.0f &&
        state->wetness <= 1.0f && isfinite(state->snowDepth) &&
        state->snowDepth >= 0.0f && state->snowDepth <= 1.0f &&
        isfinite(state->iceAmount) && state->iceAmount >= 0.0f &&
        state->iceAmount <= 1.0f && isfinite(state->erosionExposure) &&
        state->erosionExposure >= 0.0f && state->erosionExposure <= 2.0f &&
        isfinite(state->windExposure) && state->windExposure >= 0.0f &&
        state->windExposure <= 2.0f && isfinite(state->impactExposure) &&
        state->impactExposure >= 0.0f && state->impactExposure <= 2.0f &&
        IsValidBlockType(record->originalBlock) &&
        IsValidBlockType(record->expectedBlock) &&
        record->ownedEffect <= WEATHER_OWNED_ICE;
}

static bool ImpactFireRecordValid(WeatherImpactFireRecord *fire)
{
    return fire && ImpactCoordinateValid(fire->x) &&
        ImpactCoordinateValid(fire->y) && ImpactCoordinateValid(fire->z) &&
        IsValidBlockType(fire->fuelBlock) &&
        BlockMaterialResponseFor(fire->fuelBlock).flammability > 0.12f &&
        isfinite(fire->initialFuel) && fire->initialFuel > 0.0f &&
        fire->initialFuel <= 4.0f && fire->state.fuel <= fire->initialFuel &&
        fire->state.phase != WILDFIRE_PHASE_INACTIVE &&
        WildfireModelNormalize(&fire->state);
}

static bool ImpactBurnSiteRecordValid(const WeatherBurnSiteState *site)
{
    return site && ImpactCoordinateValid(site->x) &&
        ImpactCoordinateValid(site->y) && ImpactCoordinateValid(site->z) &&
        isfinite(site->severity) && site->severity > 0.0f &&
        site->severity <= 1.0f && isfinite(site->recovery) &&
        site->recovery >= 0.0f && site->recovery < 1.0f &&
        isfinite(site->ageSeconds) && site->ageSeconds >= 0.0f;
}

static bool ImpactNormalizeLoadedSurfaces(WeatherImpactSurfaceRecord *records,
                                          uint32_t count)
{
    for (uint32_t index = 0u; index < count; index++) {
        WeatherImpactSurfaceRecord *record = &records[index];
        if (!ImpactSurfaceRecordValid(record)) return false;
        ImpactCanonicalizeXZ(&record->state.x, &record->state.z);
        for (uint32_t previous = 0u; previous < index; previous++) {
            const WeatherSurfaceState *other = &records[previous].state;
            if (other->surfaceId == record->state.surfaceId &&
                other->x == record->state.x && other->z == record->state.z) {
                return false;
            }
        }
    }
    return true;
}

static bool ImpactNormalizeLoadedFires(WeatherImpactFireRecord *records,
                                       uint32_t count)
{
    for (uint32_t index = 0u; index < count; index++) {
        WeatherImpactFireRecord *fire = &records[index];
        if (!ImpactFireRecordValid(fire)) return false;
        ImpactCanonicalizeXZ(&fire->x, &fire->z);
        for (uint32_t previous = 0u; previous < index; previous++) {
            const WeatherImpactFireRecord *other = &records[previous];
            if (other->surfaceId == fire->surfaceId && other->x == fire->x &&
                other->y == fire->y && other->z == fire->z) {
                return false;
            }
        }
    }
    return true;
}

static bool ImpactNormalizeLoadedBurnSites(WeatherBurnSiteState *records,
                                           uint32_t count)
{
    for (uint32_t index = 0u; index < count; index++) {
        WeatherBurnSiteState *site = &records[index];
        if (!ImpactBurnSiteRecordValid(site)) return false;
        ImpactCanonicalizeXZ(&site->x, &site->z);
        for (uint32_t previous = 0u; previous < index; previous++) {
            const WeatherBurnSiteState *other = &records[previous];
            if (other->surfaceId == site->surfaceId && other->x == site->x &&
                other->y == site->y && other->z == site->z) {
                return false;
            }
        }
    }
    return true;
}

static WeatherImpactSurfaceDiskRecord ImpactSurfaceToDisk(
    const WeatherImpactSurfaceRecord *record)
{
    return (WeatherImpactSurfaceDiskRecord){
        .surfaceId = record->state.surfaceId,
        .x = record->state.x,
        .y = record->state.y,
        .z = record->state.z,
        .flags = (uint32_t)record->state.flags,
        .wetness = record->state.wetness,
        .snowDepth = record->state.snowDepth,
        .iceAmount = record->state.iceAmount,
        .erosionExposure = record->state.erosionExposure,
        .windExposure = record->state.windExposure,
        .impactExposure = record->state.impactExposure,
        .originalBlock = (uint32_t)record->originalBlock,
        .expectedBlock = (uint32_t)record->expectedBlock,
        .effectY = record->effectY,
        .originalFluidVolume = record->originalFluidVolume,
        .ownedEffect = record->ownedEffect
    };
}

static WeatherImpactSurfaceRecord ImpactSurfaceFromDisk(
    const WeatherImpactSurfaceDiskRecord *record)
{
    return (WeatherImpactSurfaceRecord){
        .state = {
            .surfaceId = record->surfaceId,
            .x = record->x,
            .y = record->y,
            .z = record->z,
            .flags = (WeatherSurfaceFlags)record->flags,
            .wetness = record->wetness,
            .snowDepth = record->snowDepth,
            .iceAmount = record->iceAmount,
            .erosionExposure = record->erosionExposure,
            .windExposure = record->windExposure,
            .impactExposure = record->impactExposure
        },
        .originalBlock = (BlockType)record->originalBlock,
        .expectedBlock = (BlockType)record->expectedBlock,
        .effectY = record->effectY,
        .originalFluidVolume = record->originalFluidVolume,
        .ownedEffect = record->ownedEffect
    };
}

static WeatherImpactFireDiskRecord ImpactFireToDisk(
    const WeatherImpactFireRecord *fire)
{
    return (WeatherImpactFireDiskRecord){
        .surfaceId = fire->surfaceId,
        .x = fire->x,
        .y = fire->y,
        .z = fire->z,
        .fuelBlock = (uint32_t)fire->fuelBlock,
        .phase = (uint32_t)fire->state.phase,
        .intensity = fire->state.intensity,
        .fuel = fire->state.fuel,
        .moisture = fire->state.moisture,
        .ageSeconds = fire->state.ageSeconds,
        .initialFuel = fire->initialFuel
    };
}

static WeatherImpactFireRecord ImpactFireFromDisk(
    const WeatherImpactFireDiskRecord *fire)
{
    return (WeatherImpactFireRecord){
        .surfaceId = fire->surfaceId,
        .x = fire->x,
        .y = fire->y,
        .z = fire->z,
        .fuelBlock = (BlockType)fire->fuelBlock,
        .initialFuel = fire->initialFuel,
        .state = {
            .phase = (WildfirePhase)fire->phase,
            .intensity = fire->intensity,
            .fuel = fire->fuel,
            .moisture = fire->moisture,
            .ageSeconds = fire->ageSeconds
        }
    };
}

static WeatherImpactBurnDiskRecord ImpactBurnSiteToDisk(
    const WeatherBurnSiteState *site)
{
    return (WeatherImpactBurnDiskRecord){
        .surfaceId = site->surfaceId,
        .x = site->x,
        .y = site->y,
        .z = site->z,
        .severity = site->severity,
        .recovery = site->recovery,
        .ageSeconds = site->ageSeconds
    };
}

static WeatherBurnSiteState ImpactBurnSiteFromDisk(
    const WeatherImpactBurnDiskRecord *site)
{
    return (WeatherBurnSiteState){
        .surfaceId = site->surfaceId,
        .x = site->x,
        .y = site->y,
        .z = site->z,
        .severity = site->severity,
        .recovery = site->recovery,
        .ageSeconds = site->ageSeconds
    };
}

bool WeatherImpactSaveState(FILE *file)
{
    if (!file || fwrite(WEATHER_IMPACT_MAGIC, 1, WEATHER_IMPACT_MAGIC_SIZE,
                        file) != WEATHER_IMPACT_MAGIC_SIZE) {
        return false;
    }
    WeatherImpactDiskHeader header = {
        .surfaceCount = impactSurfaceCount,
        .fireCount = impactFireCount,
        .burnSiteCount = impactBurnSiteCount,
        .ticks = impactStats.ticks,
        .processedSurfaces = impactStats.processedSurfaces,
        .depositedWater = impactStats.depositedWater,
        .blockDamageEvents = impactStats.blockDamageEvents,
        .ignitions = impactStats.ignitions,
        .spreadIgnitions = impactStats.spreadIgnitions,
        .extinctions = impactStats.extinctions,
        .suppressions = impactStats.suppressions,
        .recoveredBurnSites = impactStats.recoveredBurnSites,
        .droppedSurfaceUpdates = impactStats.droppedSurfaceUpdates,
        .droppedIgnitions = impactStats.droppedIgnitions,
        .droppedBurnSites = impactStats.droppedBurnSites,
        .burnedBlocks = impactStats.burnedBlocks
    };
    if (fwrite(&header, sizeof(header), 1, file) != 1) return false;
    for (uint32_t index = 0u; index < impactSurfaceCount; index++) {
        WeatherImpactSurfaceDiskRecord record =
            ImpactSurfaceToDisk(&impactSurfaces[index]);
        if (fwrite(&record, sizeof(record), 1, file) != 1) return false;
    }
    for (uint32_t index = 0u; index < impactFireCount; index++) {
        WeatherImpactFireDiskRecord record = ImpactFireToDisk(&impactFires[index]);
        if (fwrite(&record, sizeof(record), 1, file) != 1) return false;
    }
    for (uint32_t index = 0u; index < impactBurnSiteCount; index++) {
        WeatherImpactBurnDiskRecord record =
            ImpactBurnSiteToDisk(&impactBurnSites[index]);
        if (fwrite(&record, sizeof(record), 1, file) != 1) return false;
    }
    return true;
}

static void ImpactCommitLoadedState(
    const WeatherImpactSurfaceRecord *surfaces, uint32_t surfaceCount,
    const WeatherImpactFireRecord *fires, uint32_t fireCount,
    const WeatherBurnSiteState *burnSites, uint32_t burnSiteCount,
    WeatherImpactStats stats)
{
    memcpy(impactSurfaces, surfaces,
           (size_t)surfaceCount * sizeof(impactSurfaces[0]));
    memcpy(impactFires, fires,
           (size_t)fireCount * sizeof(impactFires[0]));
    memcpy(impactBurnSites, burnSites,
           (size_t)burnSiteCount * sizeof(impactBurnSites[0]));
    impactSurfaceCount = surfaceCount;
    impactFireCount = fireCount;
    impactBurnSiteCount = burnSiteCount;
    impactSurfaceCursor = 0u;
    impactFireCursor = 0u;
    impactBurnSiteCursor = 0u;
    impactAccumulator = 0.0f;
    stats.surfaceCount = surfaceCount;
    stats.activeFires = fireCount;
    stats.burnSiteCount = burnSiteCount;
    impactStats = stats;
}

static bool ImpactLoadLegacyState(FILE *file)
{
    WeatherImpactLegacyDiskHeader header = { 0 };
    WeatherImpactSurfaceRecord surfaces[WEATHER_IMPACT_MAX_SURFACES];
    WeatherImpactLegacyFireRecord legacyFires[WEATHER_IMPACT_MAX_FIRES];
    WeatherImpactFireRecord fires[WEATHER_IMPACT_MAX_FIRES];
    WeatherBurnSiteState burnSites[WEATHER_IMPACT_MAX_BURN_SITES] = { 0 };
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        header.surfaceCount > WEATHER_IMPACT_MAX_SURFACES ||
        header.fireCount > WEATHER_IMPACT_MAX_FIRES ||
        fread(surfaces, sizeof(surfaces[0]), header.surfaceCount, file) !=
            header.surfaceCount ||
        fread(legacyFires, sizeof(legacyFires[0]), header.fireCount, file) !=
            header.fireCount) {
        return false;
    }
    if (!ImpactNormalizeLoadedSurfaces(surfaces, header.surfaceCount)) {
        return false;
    }
    for (uint32_t index = 0u; index < header.fireCount; index++) {
        WeatherImpactLegacyFireRecord legacy = legacyFires[index];
        if (!ImpactCoordinateValid(legacy.x) ||
            !ImpactCoordinateValid(legacy.y) ||
            !ImpactCoordinateValid(legacy.z) ||
            !isfinite(legacy.intensity) || legacy.intensity < 0.0f ||
            legacy.intensity > 1.0f || !isfinite(legacy.fuel) ||
            legacy.fuel <= 0.0f || legacy.fuel > 1.0f) {
            return false;
        }
        fires[index] = (WeatherImpactFireRecord){
            .surfaceId = legacy.surfaceId,
            .x = legacy.x,
            .y = legacy.y,
            .z = legacy.z,
            .fuelBlock = BLOCK_WOOD,
            .initialFuel = legacy.fuel,
            .state = {
                .phase = legacy.intensity >= 0.18f ?
                    WILDFIRE_PHASE_FLAMING : WILDFIRE_PHASE_SMOLDERING,
                .intensity = fmaxf(legacy.intensity, 0.04f),
                .fuel = legacy.fuel,
                .moisture = 0.18f
            }
        };
    }
    if (!ImpactNormalizeLoadedFires(fires, header.fireCount)) return false;
    ImpactCommitLoadedState(
        surfaces, header.surfaceCount, fires, header.fireCount, burnSites, 0u,
        (WeatherImpactStats){
        .ticks = header.ticks,
        .ignitions = header.fireCount
    });
    return true;
}

static bool ImpactLoadCurrentState(FILE *file)
{
    WeatherImpactDiskHeader header = { 0 };
    WeatherImpactSurfaceDiskRecord diskSurfaces[WEATHER_IMPACT_MAX_SURFACES];
    WeatherImpactFireDiskRecord diskFires[WEATHER_IMPACT_MAX_FIRES];
    WeatherImpactBurnDiskRecord diskBurnSites[WEATHER_IMPACT_MAX_BURN_SITES];
    WeatherImpactSurfaceRecord surfaces[WEATHER_IMPACT_MAX_SURFACES];
    WeatherImpactFireRecord fires[WEATHER_IMPACT_MAX_FIRES];
    WeatherBurnSiteState burnSites[WEATHER_IMPACT_MAX_BURN_SITES];
    if (fread(&header, sizeof(header), 1, file) != 1 ||
        header.surfaceCount > WEATHER_IMPACT_MAX_SURFACES ||
        header.fireCount > WEATHER_IMPACT_MAX_FIRES ||
        header.burnSiteCount > WEATHER_IMPACT_MAX_BURN_SITES ||
        header.reserved != 0u ||
        fread(diskSurfaces, sizeof(diskSurfaces[0]), header.surfaceCount,
              file) != header.surfaceCount ||
        fread(diskFires, sizeof(diskFires[0]), header.fireCount, file) !=
            header.fireCount ||
        fread(diskBurnSites, sizeof(diskBurnSites[0]), header.burnSiteCount,
              file) != header.burnSiteCount) {
        return false;
    }
    for (uint32_t index = 0u; index < header.surfaceCount; index++) {
        if (diskSurfaces[index].reserved[0] != 0u ||
            diskSurfaces[index].reserved[1] != 0u) {
            return false;
        }
        surfaces[index] = ImpactSurfaceFromDisk(&diskSurfaces[index]);
    }
    for (uint32_t index = 0u; index < header.fireCount; index++) {
        fires[index] = ImpactFireFromDisk(&diskFires[index]);
    }
    for (uint32_t index = 0u; index < header.burnSiteCount; index++) {
        burnSites[index] = ImpactBurnSiteFromDisk(&diskBurnSites[index]);
    }
    if (!ImpactNormalizeLoadedSurfaces(surfaces, header.surfaceCount) ||
        !ImpactNormalizeLoadedFires(fires, header.fireCount) ||
        !ImpactNormalizeLoadedBurnSites(burnSites, header.burnSiteCount)) {
        return false;
    }
    ImpactCommitLoadedState(
        surfaces, header.surfaceCount, fires, header.fireCount, burnSites,
        header.burnSiteCount,
        (WeatherImpactStats){
            .ticks = header.ticks,
            .processedSurfaces = header.processedSurfaces,
            .depositedWater = header.depositedWater,
            .blockDamageEvents = header.blockDamageEvents,
            .ignitions = header.ignitions,
            .spreadIgnitions = header.spreadIgnitions,
            .extinctions = header.extinctions,
            .suppressions = header.suppressions,
            .recoveredBurnSites = header.recoveredBurnSites,
            .droppedSurfaceUpdates = header.droppedSurfaceUpdates,
            .droppedIgnitions = header.droppedIgnitions,
            .droppedBurnSites = header.droppedBurnSites,
            .burnedBlocks = header.burnedBlocks
        });
    return true;
}

bool WeatherImpactLoadState(FILE *file)
{
    char magic[WEATHER_IMPACT_MAGIC_SIZE];
    if (!file || fread(magic, 1, sizeof(magic), file) != sizeof(magic)) {
        return false;
    }
    if (memcmp(magic, WEATHER_IMPACT_MAGIC, sizeof(magic)) == 0) {
        return ImpactLoadCurrentState(file);
    }
    if (memcmp(magic, WEATHER_IMPACT_LEGACY_MAGIC, sizeof(magic)) == 0) {
        return ImpactLoadLegacyState(file);
    }
    return false;
}
