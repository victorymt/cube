#include "app/game_debug_volcano_mountain.h"
#include "app/game_debug_deposition.h"
#include "app/game_debug_volcano_history.h"
#include "app/game_geology.h"
#include "app/game_internal.h"
#include "app/game_debug.h"
#include "gameplay/inventory.h"
#include "presentation/render_volcano.h"
#include "presentation/surface_render_transform.h"
#include "space/planet_surface.h"
#include "world/volcano_runtime.h"
#include "world/volcano_history_cache.h"
#include "world/lava_runtime.h"
#include "world/ash_runtime.h"
#include "world/fluid.h"
#include "world/weather.h"
#include "world/chunks.h"
#include "space/space_state.h"
#include "space/space_runtime.h"
#include "world/world_surface_snapshot.h"
#include "world/surface_contract.h"
#include "world/block_types.h"
#include "world/block_edits.h"
#include "world/terrain.h"
#include "raymath.h"
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static bool solidifying;
typedef struct GeologyTickContext {
    GameRuntime *game;
    VolcanoContext volcano;
    LavaContext lava;
    AshContext ash;
    double weatherTime;
    unsigned activeCount;
    VolcanoDefinition active[VOLCANO_MAX_ACTIVE];
} GeologyTickContext;

typedef struct LavaBoundaryCacheEntry {
    SurfaceAddress address;
    uint64_t editRevision;
    uint8_t cellClass; /* 1 = known air, 2 = known solid */
} LavaBoundaryCacheEntry;

#define LAVA_BOUNDARY_CACHE_SIZE 16384u
static LavaBoundaryCacheEntry lavaBoundaryCache[LAVA_BOUNDARY_CACHE_SIZE];

static unsigned LavaBoundaryCacheSlot(SurfaceAddress address)
{
    uint32_t hash = address.bodyId * 0x9e3779b9u ^
        (uint32_t)address.face * 0x85ebca6bu ^
        (uint32_t)address.u * 0xc2b2ae35u ^
        (uint32_t)address.v * 0x27d4eb2fu ^
        (uint32_t)address.radial * 0x165667b1u;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    return hash & (LAVA_BOUNDARY_CACHE_SIZE - 1u);
}
static void GameGeologyTick(void *user);
static void PrepareTick(GeologyTickContext *tick);

static bool LocalSurfaceAddressAt(int x, int y, int z, SurfaceAddress *out)
{
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    if (!out || !surface.active) return false;
    *out = SurfaceAddressFromMapCoordinates(
        surface.bodyId, (float)(surface.originX + x) + 0.5f,
        (float)(surface.originZ + z) + 0.5f, y);
    return SurfaceAddressIsValid(*out);
}

static bool AddressLocalMap(SurfaceAddress address, int *outX, int *outZ)
{
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    SurfaceMapCell map;
    if (!outX || !outZ || !SurfaceAddressCanonicalMapCell(address, &map)) {
        return false;
    }
    *outX = map.x - surface.originX;
    *outZ = map.z - surface.originZ;
    return true;
}

static bool AddressCell(SurfaceAddress address, WorldCell *out)
{
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    if (!out) return false;
    if (contract && SurfaceContractCellRead(contract, address, out)) return true;
    int x, z;
    return AddressLocalMap(address, &x, &z) &&
        WorldSurfaceCellAt(x, address.radial, z, out);
}

static bool CachedMapCellClass(uint32_t bodyId, int x, int y, int z,
                               uint8_t *outClass)
{
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    SurfaceAddress address = SurfaceAddressFromMapCoordinates(
        bodyId, (float)(surface.originX + x) + 0.5f,
        (float)(surface.originZ + z) + 0.5f, y);
    uint64_t revision = WorldGetEditRevision();
    unsigned slot = LavaBoundaryCacheSlot(address);
    LavaBoundaryCacheEntry *cached = &lavaBoundaryCache[slot];
    if (cached->editRevision == revision &&
        (cached->cellClass == 1u || cached->cellClass == 2u) &&
        SurfaceAddressEqual(cached->address, address)) {
        *outClass = cached->cellClass;
        return true;
    }
    WorldCell cell;
    if (!AddressCell(address, &cell)) return false;
    *outClass = cell.block == BLOCK_AIR ? 1u : 2u;
    *cached = (LavaBoundaryCacheEntry){
        .address = address,
        .editRevision = revision,
        .cellClass = *outClass
    };
    return true;
}

static float AddressDistance(SurfaceAddress a, SurfaceAddress b)
{
    Vector3 pa = Vector3Scale(SurfaceAddressDirection(a),
        SURFACE_RADIUS_BLOCKS + (float)a.radial);
    Vector3 pb = Vector3Scale(SurfaceAddressDirection(b),
        SURFACE_RADIUS_BLOCKS + (float)b.radial);
    return Vector3Distance(pa, pb);
}

static bool LocalLavaAt(int x, int y, int z, LavaCell *out)
{
    SurfaceAddress address;
    if (WorldNetherIsActive() || !out ||
        !LocalSurfaceAddressAt(x, y, z, &address)) return false;
    if (LavaRuntimeCellAtAddress(address, out)) return true;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    return LavaRuntimeCellAt(surface.bodyId, surface.originX + x, y,
                             surface.originZ + z, out);
}
static BlockType RockBlock(LavaRock rock);
static bool LocalAshAt(int x, int y, int z, AshCell *out)
{
    SurfaceAddress address;
    if (WorldNetherIsActive() || !out ||
        !LocalSurfaceAddressAt(x, y, z, &address)) return false;
    if (AshRuntimeCellAtAddress(address, out)) return true;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    return AshRuntimeCellAt(surface.bodyId, surface.originX + x, y,
                            surface.originZ + z, out);
}

static LavaBoundary LavaBoundaryAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    bool active = false;
    for (unsigned i = 0u; i < tick->activeCount; i++) {
        VolcanoDefinition *d = &tick->active[i];
        SurfaceMapOffset delta = SurfaceShortestMapOffset(d->mapX, d->mapZ, x, z);
        if (fabsf(delta.x) <= 96.0f && fabsf(delta.z) <= 96.0f &&
            abs(y - d->address.radial) <= 96) active = true;
    }
    if (!active) return LAVA_UNKNOWN;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    x -= s.originX; z -= s.originZ;
    if (WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE ||
        !WorldCanAccessBlockY(y)) return LAVA_BLOCKED;
    uint8_t cellClass = 0u;
    if (!CachedMapCellClass(s.bodyId, x, y, z, &cellClass)) {
        return LAVA_UNKNOWN;
    }
    AshCell ash;
    if (LocalAshAt(x, y, z, &ash)) return LAVA_BLOCKED;
    return cellClass == 1u ? LAVA_OPEN : LAVA_BLOCKED;
}
static double LavaAmbientAt(int x, int z, void *user)
{
    GeologyTickContext *tick = user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return WeatherFieldSampleAtWorldTime(x - s.originX, z - s.originZ,
                                         tick->weatherTime).temperatureK;
}
static bool LavaWaterAt(int x, int y, int z, unsigned *volume, void *user)
{
    if (LavaBoundaryAt(x, y, z, user) == LAVA_UNKNOWN) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    *volume = FluidGetVolumeAt(x - s.originX, y, z - s.originZ);
    return true;
}
static bool LavaEvaporateAt(int x, int y, int z, unsigned expected, unsigned units,
                            bool *reservoir, void *user)
{
    unsigned current = 0u;
    if (FLUID_CAPACITY != LAVA_WATER_CAPACITY || units > FLUID_CAPACITY ||
        !LavaWaterAt(x, y, z, &current, user) || current != expected) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    return FluidTryEvaporateAt(x - s.originX, y, z - s.originZ,
                               (uint8_t)expected, (uint8_t)units, reservoir);
}
static bool LavaSolidifyAt(int x, int y, int z, void *user)
{
    if (LavaBoundaryAt(x, y, z, user) != LAVA_OPEN) return false;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    LavaCell cell;
    if (!LavaRuntimeCellAt(s.bodyId, x, y, z, &cell)) return false;
    solidifying = true;
    bool ok = SetBlockNoUndoFromSource(x - s.originX, y, z - s.originZ,
                                      RockBlock(LavaRockForMaterial(cell.material, cell.thermal.volume)),
                                      WORLD_MUTATION_ENVIRONMENT);
    solidifying = false;
    if (ok) VolcanoRuntimeSolidifiedSource(s.bodyId, x, y, z);
    return ok;
}
static AshBoundary AshBoundaryAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    SurfaceMapOffset offset = SurfaceShortestMapOffset(tick->volcano.observerMap.x,
        tick->volcano.observerMap.z, x, z);
    if (fabsf(offset.x) > 96 || fabsf(offset.z) > 96 ||
        fabsf(y - tick->volcano.observerMap.y) > 96) return ASH_UNKNOWN;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    x -= s.originX; z -= s.originZ;
    if (!WorldCanAccessBlockY(y) || WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE) return ASH_BLOCKED;
    uint8_t cellClass = 0u;
    if (!CachedMapCellClass(s.bodyId, x, y, z, &cellClass)) {
        return ASH_UNKNOWN;
    }
    LavaCell lava;
    return cellClass == 1u && !LocalLavaAt(x, y, z, &lava) ?
        ASH_OPEN : ASH_BLOCKED;
}
static double AshWetnessAt(int x, int y, int z, void *user)
{
    GeologyTickContext *tick = user;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    for (int dy = 1; dy <= 12; dy++) {
        AshCell cover;
        if (AshBoundaryAt(x, y + dy, z, user) != ASH_OPEN ||
            AshRuntimeCellAt(s.bodyId, x, y + dy, z, &cover)) return 0;
    }
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(x - s.originX, z - s.originZ, tick->weatherTime);
    return fmax(weather.rain, weather.drizzle);
}
static BlockType RockBlock(LavaRock rock)
{
    switch (rock) {
    case LAVA_ROCK_OBSIDIAN: return BLOCK_OBSIDIAN;
    case LAVA_ROCK_SCORIA: return BLOCK_SCORIA;
    case LAVA_ROCK_PUMICE: return BLOCK_PUMICE;
    case LAVA_ROCK_RHYOLITE: return BLOCK_RHYOLITE;
    default: return BLOCK_BASALT;
    }
}
BlockType GameGeologyBlockAt(int x, int y, int z)
{
    LavaCell c;
    if (LocalLavaAt(x, y, z, &c)) return LavaLiquidVolume(c.thermal) > 0u ? BLOCK_LAVA :
        RockBlock(LavaRockForMaterial(c.material, c.thermal.volume));
    AshCell ash;
    return LocalAshAt(x, y, z, &ash) ? BLOCK_VOLCANIC_ASH : BLOCK_AIR;
}
float GameGeologyCollisionHeightAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c)) {
        AshCell ash;
        return LocalAshAt(x, y, z, &ash) ? (float)ash.volume / ASH_CAPACITY : 0.0f;
    }
    return (float)(c.thermal.volume - LavaLiquidVolume(c.thermal)) / LAVA_CAPACITY;
}
float GameGeologyLiquidHeightAt(int x, int y, int z)
{
    LavaCell c;
    if (!LocalLavaAt(x, y, z, &c) || !LavaLiquidVolume(c.thermal)) return 0.0f;
    return (float)c.thermal.volume / LAVA_CAPACITY;
}
bool GameGeologyCanSetBlock(int x, int y, int z, BlockType next)
{
    (void)next;
    LavaCell c;
    /* Ordinary edits cannot erase deposits. Collection has its own volume
       transaction, independent of the base-block undo history.
       Solidification may replace an occupied overlay. */
    AshCell ash;
    return solidifying || (!LocalLavaAt(x, y, z, &c) && !LocalAshAt(x, y, z, &ash));
}

typedef struct CollectDeposit {
    SurfaceAddress address;
    bool addressed;
    uint32_t bodyId;
    int x, y, z;
    unsigned units;
    bool ash;
    LavaRock rock;
} CollectDeposit;
static bool RemoveDeposit(void *user)
{
    CollectDeposit *c = user;
    if (c->addressed) {
        return c->ash ? AshRuntimeCollectAtAddress(c->address, c->units) :
            LavaRuntimeCollectAtAddress(c->address, c->units, c->rock);
    }
    return c->ash ? AshRuntimeCollect(c->bodyId, c->x, c->y, c->z,
                                      c->units) :
        LavaRuntimeCollect(c->bodyId, c->x, c->y, c->z, c->units, c->rock);
}
GameGeologyCollection GameGeologyCollectAt(int x, int y, int z, BlockType expectedType)
{
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    if (!s.active || WorldNetherIsActive() || WorldBlockRegionAt(y) != WORLD_BLOCK_REGION_SURFACE)
        return GAME_GEOLOGY_NO_DEPOSIT;
    LavaCell lava; AshCell ash;
    bool hasLava = LocalLavaAt(x, y, z, &lava), hasAsh = LocalAshAt(x, y, z, &ash);
    if (!hasLava && !hasAsh) return GAME_GEOLOGY_NO_DEPOSIT;
    SurfaceAddress address;
    WorldCell cell;
    if (!LocalSurfaceAddressAt(x, y, z, &address) ||
        !AddressCell(address, &cell)) {
        return GAME_GEOLOGY_COLLECTION_BLOCKED;
    }
    /* Never excavate parked/hidden records, or solid crust beneath liquid. */
    if (!SurfaceBlockReadyAt(x, y, z) || cell.block != BLOCK_AIR ||
        (hasLava && (!lava.cooled || LavaLiquidVolume(lava.thermal)))) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    LavaRock rock = hasLava ? LavaRockForMaterial(lava.material, lava.thermal.volume) : LAVA_ROCK_BASALT;
    BlockType type = hasLava ? RockBlock(rock) : BLOCK_VOLCANIC_ASH;
    if (expectedType != type) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    const LavaCell *lavaCell = hasLava ? &lava : NULL;
    const AshCell *ashCell = hasAsh ? &ash : NULL;
    bool addressed = lavaCell ? lavaCell->addressed : ashCell->addressed;
    CollectDeposit source = { .address = lavaCell ? lavaCell->address :
            ashCell->address, .addressed = addressed,
        .bodyId = s.bodyId,
        .x = x + s.originX, .y = y, .z = z + s.originZ,
        .units = hasLava ? lava.thermal.volume : ash.volume,
        .ash = !hasLava, .rock = rock };
    if (!InventoryCollectVolume(type, source.units, RemoveDeposit, &source)) return GAME_GEOLOGY_COLLECTION_BLOCKED;
    /* Overlays draw directly; only neighboring sleeping water needs waking. */
    FluidOnBlockChanged(x, y, z, type, BLOCK_AIR);
    return GAME_GEOLOGY_COLLECTED;
}

static bool AddressNearActiveVolcano(const GeologyTickContext *tick,
                                     SurfaceAddress address, float radius)
{
    if (!tick) return false;
    for (unsigned i = 0u; i < tick->activeCount; i++) {
        if (AddressDistance(address, tick->active[i].address) <= radius) {
            return true;
        }
    }
    return false;
}

static LavaBoundary LavaAddressBoundaryAt(SurfaceAddress address, void *user)
{
    GeologyTickContext *tick = user;
    LavaBoundary result = LAVA_UNKNOWN;
    if (!AddressNearActiveVolcano(tick, address, 136.0f)) {
        result = LAVA_UNKNOWN;
    } else if (WorldBlockRegionAt(address.radial) != WORLD_BLOCK_REGION_SURFACE ||
               !WorldCanAccessBlockY(address.radial)) {
        result = LAVA_BLOCKED;
    }
    AshCell ash;
    if (result == LAVA_UNKNOWN &&
        AshRuntimeCellAtAddress(address, &ash)) result = LAVA_BLOCKED;
    if (result == LAVA_UNKNOWN) {
        uint64_t revision = WorldGetEditRevision();
        unsigned slot = LavaBoundaryCacheSlot(address);
        LavaBoundaryCacheEntry *cached = &lavaBoundaryCache[slot];
        bool hit = cached->editRevision == revision &&
            (cached->cellClass == 1u || cached->cellClass == 2u) &&
            SurfaceAddressEqual(cached->address, address);
        if (hit) {
            result = cached->cellClass == 1u ? LAVA_OPEN : LAVA_BLOCKED;
        } else {
            WorldCell cell;
            if (AddressCell(address, &cell)) {
                uint8_t cellClass = cell.block == BLOCK_AIR ? 1u : 2u;
                *cached = (LavaBoundaryCacheEntry){
                    .address = address,
                    .editRevision = revision,
                    .cellClass = cellClass
                };
                result = cellClass == 1u ? LAVA_OPEN : LAVA_BLOCKED;
            }
        }
    }
    return result;
}

static __attribute__((unused)) double LavaAddressAmbientAt(
    SurfaceAddress address, void *user)
{
    GeologyTickContext *tick = user;
    int x, z;
    if (!tick || !AddressLocalMap(address, &x, &z)) return 293.0;
    return WeatherFieldSampleAtWorldTime(x, z, tick->weatherTime).temperatureK;
}

static __attribute__((unused)) bool LavaAddressWaterAt(
    SurfaceAddress address, unsigned *volume, void *user)
{
    if (LavaAddressBoundaryAt(address, user) == LAVA_UNKNOWN) return false;
    WorldCell cell;
    if (!AddressCell(address, &cell)) return false;
    *volume = cell.waterVolume;
    return true;
}

static __attribute__((unused)) bool LavaAddressEvaporateAt(
    SurfaceAddress address, unsigned expected, unsigned units,
    bool *reservoir, void *user)
{
    if (!reservoir || units > expected ||
        LavaAddressBoundaryAt(address, user) == LAVA_UNKNOWN) return false;
    WorldCell cell;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    if (!AddressCell(address, &cell) || cell.waterVolume != expected ||
        !contract) return false;
    cell.waterVolume = (uint8_t)(expected - units);
    if (!SurfaceContractCellWrite(contract, address, cell)) return false;
    *reservoir = false;
    return true;
}

static __attribute__((unused)) bool LavaAddressSolidifyAt(
    SurfaceAddress address, void *user)
{
    if (LavaAddressBoundaryAt(address, user) != LAVA_OPEN) return false;
    const SurfaceContract *contract = WorldCurrentSurfaceContract();
    LavaCell lava;
    WorldCell cell;
    if (!contract || !LavaRuntimeCellAtAddress(address, &lava) ||
        !AddressCell(address, &cell)) return false;
    solidifying = true;
    cell = WorldCellWithBlock(cell,
        RockBlock(LavaRockForMaterial(lava.material, lava.thermal.volume)));
    bool ok = SurfaceContractCellWrite(contract, address, cell);
    solidifying = false;
    if (ok) VolcanoRuntimeSolidifiedSourceAt(address);
    return ok;
}
static unsigned EmitLava(const VolcanoDefinition *d, unsigned units, void *user)
{
    GeologyTickContext *tick = user;
    /* Stable source archetypes; forced legacy vents remain dense/mafic. */
    uint32_t profile = (uint32_t)d->id ^ (uint32_t)(d->id >> 32) ^ d->seed;
    profile ^= profile >> 16; profile *= 0x7feb352du;
    profile ^= profile >> 15; profile *= 0x846ca68bu; profile ^= profile >> 16;
    double silica = !d->forced && (profile & 1u) ? 1.0 : 0.0;
    double vesicles = !d->forced && (profile & 2u) ? 1.0 : 0.0;
    unsigned accepted = 0;
    /* The conduit stays at the crater center. Stack only above FULL lava
       cells; unknown terrain and player/ordinary solid caps stop injection. */
    for (int dy = 0; dy < 64 && accepted < units; dy++) {
        int y = d->address.radial + dy;
        if (LavaBoundaryAt(d->mapX, y, d->mapZ, user) != LAVA_OPEN) break;
        unsigned remaining = units - accepted;
        if (remaining > LAVA_CAPACITY) remaining = LAVA_CAPACITY;
        accepted += LavaRuntimeDepositMaterial(
            &tick->lava, d->mapX, y, d->mapZ, remaining, LAVA_SOURCE_K,
            silica, vesicles);
        LavaCell cell;
        if (!LavaRuntimeCellAt(d->address.bodyId, d->mapX, y, d->mapZ,
                               &cell) ||
            cell.thermal.volume != LAVA_CAPACITY) break;
    }
    return accepted;
}

static AshBoundary AshAddressBoundaryAt(SurfaceAddress address, void *user)
{
    GeologyTickContext *tick = user;
    SurfaceAddress observer = tick->volcano.hasObserver ?
        tick->volcano.observer : tick->volcano.observer;
    if (!tick->volcano.hasObserver ||
        AddressDistance(address, observer) > 136.0f) return ASH_UNKNOWN;
    if (!WorldCanAccessBlockY(address.radial) ||
        WorldBlockRegionAt(address.radial) != WORLD_BLOCK_REGION_SURFACE) {
        return ASH_BLOCKED;
    }
    LavaCell lava;
    WorldCell cell;
    if (!AddressCell(address, &cell)) return ASH_UNKNOWN;
    return cell.block == BLOCK_AIR &&
        !LavaRuntimeCellAtAddress(address, &lava) ? ASH_OPEN : ASH_BLOCKED;
}

static __attribute__((unused)) double AshAddressWetnessAt(
    SurfaceAddress address, void *user)
{
    GeologyTickContext *tick = user;
    for (int dy = 1; dy <= 12; dy++) {
        SurfaceAddress cover = address;
        cover.radial += dy;
        AshCell ash;
        if (AshAddressBoundaryAt(cover, user) != ASH_OPEN ||
            AshRuntimeCellAtAddress(cover, &ash)) return 0.0;
    }
    int x, z;
    if (!tick || !AddressLocalMap(address, &x, &z)) return 0.0;
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(
        x, z, tick->weatherTime);
    return fmax(weather.rain, weather.drizzle);
}

static unsigned EmitAsh(const VolcanoDefinition *d, unsigned units, uint64_t activeTick, void *user)
{
    GeologyTickContext *tick = user;
    for (int dy = 1; dy <= 12; dy++) {
        if (AshBoundaryAt(d->mapX, d->address.radial + dy, d->mapZ, user) != ASH_OPEN) return 0;
    }
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    WeatherFieldSample weather = WeatherFieldSampleAtWorldTime(d->mapX - s.originX,
        d->mapZ - s.originZ, tick->weatherTime);
    uint32_t sequence = (uint32_t)activeTick + (uint32_t)(d->id ^ d->seed);
    int dx = ((int)(sequence % 7u) - 3) * 2 + (int)lroundf(cosf(weather.windAngle) * weather.wind * 4);
    int dz = ((int)((sequence / 7u) % 7u) - 3) * 2 + (int)lroundf(sinf(weather.windAngle) * weather.wind * 4);
    SurfaceMapCell map = SurfaceCanonicalMapCell(d->mapX + dx, d->mapZ + dz);
    int bottom = d->address.radial - 24;
    for (int y = d->address.radial + 12; y >= bottom; y--) {
        AshBoundary boundary = AshBoundaryAt(map.x, y, map.z, user);
        if (boundary == ASH_UNKNOWN) return 0;
        AshCell ash;
        if (boundary == ASH_OPEN && AshRuntimeCellAt(s.bodyId, map.x, y, map.z, &ash)) {
            return AshRuntimeDeposit(&tick->ash, map.x, ash.volume == ASH_CAPACITY ? y + 1 : y, map.z, units);
        }
        if (boundary == ASH_BLOCKED) {
            int x = map.x - s.originX, z = map.z - s.originZ;
            if (BlockCollisionHeightAt(x, y, z) < 1.0f) return 0;
            return AshRuntimeDeposit(&tick->ash, map.x, y + 1, map.z, units);
        }
    }
    return 0;
}

static bool HasHotLava(const VolcanoDefinition *d, void *user)
{
    GeologyTickContext *tick = user;
    return LavaRuntimeHasActiveHotNear(&tick->lava, d->mapX,
                                       d->address.radial, d->mapZ, 180.0f);
}

static bool SampleSite(SurfaceAddress address, float *potential, int *height, void *opaque)
{
    (void)opaque;
    const WorldSurfaceSnapshot *surface = WorldCurrentSurfaceSnapshot();
    if (surface->context.kind != WORLD_SURFACE_PLANET) return false;
    VolcanoTerrainDefinition mountain;
    if (!VolcanoTerrainDefine(surface->context.seed, surface->planetProfile.volcanicActivity,
                               address, &mountain)) return false;
    SurfaceMapCell cell = VolcanoMapCell(address);
    *height = WorldSurfaceHeightAt(cell.x - surface->context.originX,
                                   cell.z - surface->context.originZ) + 1;
    *potential = mountain.potential;
    BlockType edit;
    int x = cell.x - surface->context.originX, z = cell.z - surface->context.originZ;
    return *height > PlanetTerrainSeaLevel() &&
        GetBlockBaseAt(x, *height, z) == BLOCK_AIR &&
        !WorldGetBlockEditForCurrentDimensionAt(x, *height, z, &edit) &&
        !WorldGetBlockEditForCurrentDimensionAt(x, *height - 1, z, &edit);
}

static bool SiteReady(const VolcanoDefinition *d, void *opaque)
{
    (void)opaque;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    int x = d->mapX - surface.originX;
    int z = d->mapZ - surface.originZ;
    int y = d->address.radial;
    return SurfaceBlockReadyAt(x, y - 1, z) && SurfaceBlockReadyAt(x, y, z);
}

static VolcanoContext ContextFor(const GameRuntime *game)
{
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    VolcanoContext context = {
        .valid = game && game->screen == SCREEN_PLAYING && surface.active &&
                 !WorldNetherIsActive() && !game->perfMode,
        .discover = surface.kind == WORLD_SURFACE_PLANET,
        .bodyId = surface.bodyId, .seed = surface.seed,
        .observerMap = game ? (Vector3){ game->player.position.x + surface.originX,
            game->player.position.y, game->player.position.z + surface.originZ } : (Vector3){ 0 },
        .sample = SampleSite, .ready = SiteReady
    };
    if (game) {
        SurfacePose pose;
        SurfaceAddress observer;
        if (PlayerSurfacePoseFromLocal(game->player.position, &pose) &&
            SurfaceCellFromPoint(pose.bodyId, pose.position, &observer)) {
            context.observer = observer;
            context.hasObserver = true;
        }
    }
    return context;
}

LavaHeatExposure GameGeologyHeatExposureAt(const GameRuntime *game,
                                           Vector3 position, float radius,
                                           float shelter)
{
    LavaHeatExposure empty = { .nearestDistance = INFINITY };
    if (!game) return empty;
    WorldSurfaceContext surface = WorldCurrentSurfaceContext();
    GeologyTickContext tick = { .game = (GameRuntime *)game,
        .volcano = ContextFor(game), .weatherTime = SpaceElapsedSimulationTime() };
    if (!tick.volcano.valid || !surface.active) return empty;
    PrepareTick(&tick);
    SurfaceAddress observer;
    if (!LocalSurfaceAddressAt((int)floorf(position.x),
                               (int)floorf(position.y),
                               (int)floorf(position.z), &observer)) return empty;
    return LavaRuntimeHeatExposureAtAddress(&tick.lava, observer, radius,
                                            shelter);
}

void GameGeologyUpdate(GameRuntime *game, double seconds)
{
    /* Keep saved admission, inventories and fractional clocks unchanged. The
       setting gates simulation only; rendering/queries keep their own context. */
    if (!game || !game->settings.geologyEnabled) return;
    GeologyTickContext tick = { .game = game, .volcano = ContextFor(game),
                               .weatherTime = SpaceElapsedSimulationTime() };
    if (!tick.volcano.valid) {
        VolcanoRuntimeUpdate(0.0, &tick.volcano);
        return;
    }
    LavaRuntimeAdvance(seconds, true, GameGeologyTick, &tick);
}

static void PrepareTick(GeologyTickContext *tick)
{
    /* Update admission without moving time, then use the same admitted set for
       source discharge and all lava cells in this 0.1-second step. */
    VolcanoRuntimeUpdate(0.0, &tick->volcano);
    tick->activeCount = 0u;
    VolcanoRecord r;
    for (unsigned i = 0u; VolcanoRuntimeAt(i, &r); i++) {
        if (r.active && tick->activeCount < VOLCANO_MAX_ACTIVE) tick->active[tick->activeCount++] = r.definition;
    }
    tick->lava = (LavaContext){ .valid = tick->volcano.valid, .bodyId = tick->volcano.bodyId,
        .boundary = LavaBoundaryAt, .ambient = LavaAmbientAt,
        .solidify = LavaSolidifyAt, .user = tick,
        .water = LavaWaterAt, .evaporate = LavaEvaporateAt };
    tick->ash = (AshContext){ .valid = tick->volcano.valid, .bodyId = tick->volcano.bodyId,
        .boundary = AshBoundaryAt, .wetness = AshWetnessAt, .user = tick };
    tick->volcano.emitLava = EmitLava;
    tick->volcano.emitAsh = EmitAsh;
    tick->volcano.hasHotLava = HasHotLava;
    tick->volcano.userData = tick;
}
static void GameGeologyTick(void *user)
{
    GeologyTickContext *tick = user;
    PrepareTick(tick);
    VolcanoRuntimeUpdate(LAVA_STEP_SECONDS, &tick->volcano);
    LavaRuntimeStep(&tick->lava);
    AshRuntimeStep(&tick->ash);
}

void GameGeologyDraw(const GameRuntime *game, const GameFrameView *frame)
{
    VolcanoContext context = ContextFor(game);
    if (!context.valid) return;
    VolcanoVisual visuals[VOLCANO_MAX_RECORDS];
    unsigned count = 0u;
    VolcanoRecord record;
    for (unsigned i = 0u; VolcanoRuntimeAt(i, &record); i++) {
        if (record.definition.address.bodyId != context.bodyId ||
            !SiteReady(&record.definition, NULL)) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(context.observerMap.x,
            context.observerMap.z, record.definition.mapX, record.definition.mapZ);
        if (hypotf(offset.x, offset.z) > VOLCANO_LEAVE_RADIUS) continue;
        visuals[count++] = (VolcanoVisual){ .position = {
            game->player.position.x + offset.x, (float)record.definition.address.radial,
            game->player.position.z + offset.z }, .state = record.state };
    }
    DrawVolcanoPreviews(visuals, count, frame->weatherVisual.windAngle,
                        frame->weatherVisual.windDrift);
    LavaCell cell;
    WorldSurfaceContext s = WorldCurrentSurfaceContext();
    unsigned steamCells = 0u;
    uint64_t lavaTicks = LavaRuntimeStats().ticks;
    for (unsigned i = 0u; LavaRuntimeAt(i, &cell); i++) {
        WorldSurfaceCellRuntime runtime;
        if (cell.bodyId != context.bodyId ||
            !WorldSurfaceCellRuntimeAt(cell.address, &runtime) ||
            runtime.block != BLOCK_AIR) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(context.observerMap.x,
            context.observerMap.z, cell.x, cell.z);
        if (hypotf(offset.x, offset.z) > VOLCANO_LEAVE_RADIUS) continue;
        Matrix transform = SurfaceChunkRenderTransform(runtime.chunk, runtime.sectionY,
            game->camera.position, s.originX, s.originZ);
        DrawLavaCell(transform,
            (Vector3){ (float)runtime.localX, (float)runtime.localY,
                       (float)runtime.localZ },
            cell.thermal, LavaRockForMaterial(cell.material, cell.thermal.volume));
        if (cell.steam >= 0.05 && steamCells < LAVA_STEAM_MAX_CELLS) {
            DrawLavaSteam(transform,
                (Vector3){ runtime.localX + 0.5f, runtime.localY +
                    (float)cell.thermal.volume / LAVA_CAPACITY,
                    runtime.localZ + 0.5f },
                cell.steam, lavaTicks, frame->weatherVisual.windAngle, frame->weatherVisual.windDrift);
            steamCells++;
        }
    }
    AshCell ash;
    for (unsigned i = 0; AshRuntimeAt(i, &ash); i++) {
        WorldSurfaceCellRuntime runtime;
        if (ash.bodyId != s.bodyId ||
            !WorldSurfaceCellRuntimeAt(ash.address, &runtime) ||
            runtime.block != BLOCK_AIR) continue;
        Matrix transform = SurfaceChunkRenderTransform(runtime.chunk, runtime.sectionY,
            game->camera.position, s.originX, s.originZ);
        DrawAshCell(transform, (Vector3){ (float)runtime.localX,
            (float)runtime.localY, (float)runtime.localZ }, ash.volume);
    }
}

static void ReplyVent(GameRuntime *game, const VolcanoRecord *r)
{
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology vent id=%016" PRIx64 " body=%u position=%d,%d,%d "
        "phase=%s event=%u active=%d ticks=%" PRIu64 " pressure=%.6f reservoir=%.6f lava_physics=1\n",
        r->definition.id, r->definition.address.bodyId, r->definition.mapX,
        r->definition.address.radial, r->definition.mapZ, VolcanoPhaseName(r->state.phase),
        r->state.event, r->active, r->state.activeTicks, r->state.pressure, r->state.reservoir);
}
static const char *collectionNames[] = { "ash", "basalt", "obsidian", "scoria", "pumice", "rhyolite" };
static const BlockType collectionTypes[] = { BLOCK_VOLCANIC_ASH, BLOCK_BASALT, BLOCK_OBSIDIAN,
    BLOCK_SCORIA, BLOCK_PUMICE, BLOCK_RHYOLITE };

static float GeologyFaceEdgeDistance(Vector3 direction)
{
    float ax = fabsf(direction.x), ay = fabsf(direction.y), az = fabsf(direction.z);
    float largest = fmaxf(ax, fmaxf(ay, az));
    float second = fmaxf(fminf(ax, ay), fmaxf(fminf(ax, az), fminf(ay, az)));
    return SURFACE_RADIUS_BLOCKS * asinf((largest - second) * 0.70710678118f);
}

bool GameDebugGeologyDispatch(GameRuntime *game, DebugControlCommand command)
{
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_LOCATE) {
        DebugControl *c = &game->debugControl;
        const WorldSurfaceSnapshot *s = WorldCurrentSurfaceSnapshot();
        VolcanoTerrainDefinition d;
        c->geologyTerrainFound = false;
        if (s->context.kind == WORLD_SURFACE_PLANET && ContextFor(game).valid &&
            VolcanoTerrainDefine(s->context.seed, s->planetProfile.volcanicActivity,
                (SurfaceAddress){s->context.bodyId, c->geologyTerrainFace,
                    c->geologyTerrainU, c->geologyTerrainV, 0}, &d)) {
            SurfaceMapCell cell = VolcanoMapCell(d.center);
            c->geologyTerrainFound = true;
            c->geologyTerrainBody = s->context.bodyId; c->geologyTerrainSeed = s->context.seed;
            c->geologyTerrainX = cell.x; c->geologyTerrainY =
                WorldSurfaceHeightAt(cell.x - s->context.originX, cell.z - s->context.originZ) + 1;
            c->geologyTerrainZ = cell.z; c->geologyTerrainRadius = d.radius;
            c->geologyTerrainHeight = d.height; c->geologyCraterRadius = d.craterRadius;
            Vector3 direction = SurfaceAddressDirection(d.center);
            c->geologyTerrainEdgeDistance = GeologyFaceEdgeDistance(direction);
            c->geologyTerrainAbsLatitude = asinf(fabsf(direction.y)) * 180.0f / PI;
        }
        DebugControlReply(c, "DEBUG_CONTROL geology locate found=%d face=%u u=%u v=%u position=%d,%d,%d\n",
            c->geologyTerrainFound, c->geologyTerrainFace, c->geologyTerrainU,
            c->geologyTerrainV, c->geologyTerrainX, c->geologyTerrainY, c->geologyTerrainZ);
        if (!c->geologyTerrainFound) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                     "geology_address_has_no_natural_volcano");
        }
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_HISTORY_WORLD) {
        if (!GameDebugVolcanoHistoryWorld(&game->debugControl)) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "history_world_requires_historical_find");
        }
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_HISTORY) {
        if (!GameDebugVolcanoHistory(&game->debugControl)) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "history_requires_geology_find_or_build_failed");
        }
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_MOUNTAIN) {
        GameDebugVolcanoMountain(game);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSITION_TEST) {
        if (!GameDebugDepositionRun(&game->debugControl)) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "deposition_fixture_failed");
        }
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_FIND) {
        DebugControl *c = &game->debugControl;
        const WorldSurfaceSnapshot *s = WorldCurrentSurfaceSnapshot();
        c->geologyTerrainFound = false;
        if (s->context.kind != WORLD_SURFACE_PLANET || !ContextFor(game).valid) {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure), "geology_planet_required");
            return true;
        }
        float best = INFINITY;
        Vector3 observer = ContextFor(game).observerMap;
        SurfaceFace firstFace = c->geologyFindFace >= 0 ? (SurfaceFace)c->geologyFindFace : 0;
        SurfaceFace lastFace = c->geologyFindFace >= 0 ? firstFace + 1 : SURFACE_FACE_COUNT;
        for (SurfaceFace face = firstFace; face < lastFace; face++) {
            for (int v = VOLCANO_TERRAIN_OFFSET; v < SURFACE_FACE_BLOCKS; v += VOLCANO_TERRAIN_SPACING) {
                for (int u = VOLCANO_TERRAIN_OFFSET; u < SURFACE_FACE_BLOCKS; u += VOLCANO_TERRAIN_SPACING) {
                    VolcanoTerrainDefinition d;
                    if (!VolcanoTerrainDefine(s->context.seed, s->planetProfile.volcanicActivity,
                            (SurfaceAddress){s->context.bodyId,face,u,v,0}, &d)) continue;
                    if (c->geologyFindHistorical && !VolcanoHistoricalTerrainEligible(
                        s->context.seed, &s->planetProfile, d.center)) continue;
                    Vector3 direction = SurfaceAddressDirection(d.center);
                    float ay = fabsf(direction.y);
                    float edgeDistance = GeologyFaceEdgeDistance(direction);
                    if (c->geologyFindRegion == 1 && edgeDistance >= d.radius) continue;
                    if (c->geologyFindRegion == 2 && ay < 0.95f) continue;
                    SurfaceMapCell cell = VolcanoMapCell(d.center);
                    SurfaceMapOffset delta = SurfaceShortestMapOffset(observer.x, observer.z, cell.x, cell.z);
                    float distance = hypotf(delta.x, delta.z);
                    if (distance >= best) continue;
                    int height = WorldSurfaceHeightAt(cell.x - s->context.originX, cell.z - s->context.originZ) + 1;
                    if (height <= PlanetTerrainSeaLevel()) continue;
                    best = distance;
                    c->geologyTerrainFound = true;
                    c->geologyTerrainBody = s->context.bodyId; c->geologyTerrainSeed = s->context.seed;
                    c->geologyTerrainX = cell.x; c->geologyTerrainY = height; c->geologyTerrainZ = cell.z;
                    c->geologyTerrainRadius = d.radius; c->geologyTerrainHeight = d.height;
                    c->geologyTerrainEdgeDistance = edgeDistance;
                    c->geologyTerrainAbsLatitude = asinf(ay) * 180.0f / PI;
                    c->geologyCraterRadius = d.craterRadius;
                    c->geologyTerrainFace = face; c->geologyTerrainU = u; c->geologyTerrainV = v;
                }
            }
        }
        DebugControlReply(c, "DEBUG_CONTROL geology find found=%d position=%d,%d,%d radius=%.3f height=%.3f crater_radius=%.3f face=%d u=%d v=%d region=%u edge_distance=%.3f abs_latitude=%.3f\n",
            c->geologyTerrainFound, c->geologyTerrainX, c->geologyTerrainY, c->geologyTerrainZ,
            c->geologyTerrainRadius, c->geologyTerrainHeight, c->geologyCraterRadius,
            c->geologyTerrainFace, c->geologyTerrainU, c->geologyTerrainV, c->geologyFindRegion,
            c->geologyTerrainEdgeDistance, c->geologyTerrainAbsLatitude);
        return true;
    }
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_ENABLED) {
        GameSettings settings = game->settings;
        settings.geologyEnabled = game->debugControl.geologyEnabled;
        bool saved = GameSettingsSave(&settings);
        if (saved) game->settings = settings;
        else {
            game->debugCommandFailed = true;
            snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                     "geology_settings_save_failed");
        }
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology enabled=%d saved=%d\n",
            game->settings.geologyEnabled, saved);
        return true;
    }
    if (command < DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT ||
        command > DEBUG_CONTROL_COMMAND_GEOLOGY_INVENTORY) return false;
    VolcanoContext context = ContextFor(game);
    bool ok = context.valid;
    if (!game->settings.geologyEnabled &&
        (command == DEBUG_CONTROL_COMMAND_GEOLOGY_FORCE ||
         command == DEBUG_CONTROL_COMMAND_GEOLOGY_STOP ||
         command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSIT)) ok = false;
    if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_INVENTORY) {
        unsigned kind = game->debugControl.geologyMaterial;
        if (kind >= 6) ok = false;
        else {
            BlockType type = collectionTypes[kind];
            int desired = (int)game->debugControl.geologyUnits, current = InventoryCount(type);
            if (desired > current) ok = InventoryAdd(type, desired - current) == desired - current;
            if (desired < current) ok = InventoryConsume(type, current - desired);
            DebugControlReply(&game->debugControl, "DEBUG_CONTROL geology inventory %s blocks=%d remainder=%u\n",
                collectionNames[kind], InventoryCount(type), InventoryRemainder(type));
        }
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_FORCE) {
        SurfaceMapCell cell = SurfaceCanonicalMapCell(
            context.observerMap.x + 24.0f, context.observerMap.z);
        WorldSurfaceContext surface = WorldCurrentSurfaceContext();
        int height = WorldSurfaceHeightAt(cell.x - surface.originX,
                                          cell.z - surface.originZ) + 1;
        VolcanoDefinition definition = VolcanoDefine(context.bodyId, context.seed,
            cell.x, height, cell.z, 1.0f, true);
        ok = SiteReady(&definition, NULL) &&
            GetBlockBaseAt(cell.x - surface.originX, height, cell.z - surface.originZ) == BLOCK_AIR &&
            VolcanoRuntimeForce(definition,
            game->debugControl.geologyIntensity, game->debugControl.geologyTicks);
        VolcanoRuntimeUpdate(0.0, &context);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STOP) {
        uint64_t id = game->debugControl.geologyId;
        ok = (uint32_t)(id >> 32) == context.bodyId && VolcanoRuntimeStop(id);
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_STEP) {
        for (unsigned i = 0u; i < game->debugControl.geologyTicks; i++) {
            GameGeologyUpdate(game, VOLCANO_STEP_SECONDS);
        }
    } else if (ok && command == DEBUG_CONTROL_COMMAND_GEOLOGY_DEPOSIT) {
        GeologyTickContext tick = { .game = game, .volcano = context,
            .weatherTime = SpaceElapsedSimulationTime() };
        PrepareTick(&tick);
        WorldSurfaceContext s = WorldCurrentSurfaceContext();
        DebugControl *c = &game->debugControl;
        int x = c->geologyX + s.originX, z = c->geologyZ + s.originZ;
        unsigned kind = c->geologyMaterial;
        unsigned accepted = kind == 0 ? AshRuntimeDeposit(&tick.ash, x, c->geologyY, z, c->geologyUnits) :
            LavaRuntimeDepositMaterial(&tick.lava, x, c->geologyY, z, c->geologyUnits,
                LAVA_SOURCE_K, kind == 2 || kind == 4, kind == 3 || kind == 4);
        ok = accepted > 0;
        DebugControlReply(c, "DEBUG_CONTROL geology deposit accepted=%u requested=%u\n", accepted, c->geologyUnits);
    }
    if (!ok) {
        game->debugCommandFailed = true;
        snprintf(game->debugCommandFailure, sizeof(game->debugCommandFailure),
                 "geology_unavailable_or_invalid_state");
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    LavaStats lava = LavaRuntimeStats();
    AshStats ash = AshRuntimeStats();
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT) {
        DebugDslValue radius;
        GameDebugGeologyResolve(game, "geology.flow_radius", &radius);
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology flow radius=%.3f supplied=%.3f solidified=%.3f hot=%u records=%u\n",
            radius.as.number, (double)lava.supplied / LAVA_CAPACITY,
            (double)lava.solidified / LAVA_CAPACITY, lava.hotCells, lava.records);
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology heat exposure=%.6f damage=%.6f contact=%d sources=%u blocked=%u\n",
            game->playerHeatExposure, game->playerHeatDamage, game->playerHeatContact,
            game->playerHeatSources, game->playerHeatBlockedSources);
    }
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology %s records=%u active=%u rejected=%" PRIu64 " lava_physics=1 enabled=%d\n",
        ok ? "ok" : "error", stats.count, stats.active, stats.rejectedAdmissions,
        game->settings.geologyEnabled);
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology ash cells=%u volume=%" PRIu64 " supplied=%" PRIu64
        " washed=%" PRIu64 " collected=%" PRIu64 " volume_error=%.0f\n", ash.cells, ash.volume, ash.supplied,
        ash.washed, ash.collected, (double)ash.supplied - ash.volume - ash.washed - ash.collected);
    DebugControlReply(&game->debugControl,
        "DEBUG_CONTROL geology water evaporated=%" PRIu64 " reservoir=%" PRIu64
        " heat=%.6f energy_error=%.9f\n", lava.evaporatedWater, lava.reservoirWater,
        lava.waterEnergy, lava.suppliedEnergy - lava.energy - lava.cooledEnergy -
            lava.exportedEnergy - lava.waterEnergy);
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_INSPECT) {
        LavaCell cell = { 0 };
        LocalLavaAt((int)floorf(game->player.position.x), (int)floorf(game->player.position.y),
                    (int)floorf(game->player.position.z), &cell);
        double credit = 0.0;
        for (unsigned face = 0u; face < 6u; face++) credit += cell.contactCredit[face];
        DebugControlReply(&game->debugControl,
            "DEBUG_CONTROL geology cell volume=%u temperature=%.6f credit=%.6f steam=%.6f\n",
            cell.thermal.volume, LavaTemperature(cell.thermal), credit, cell.steam);
    }
    VolcanoRecord record;
    if (command == DEBUG_CONTROL_COMMAND_GEOLOGY_VENTS) {
        for (unsigned i = 0u; VolcanoRuntimeAt(i, &record); i++) ReplyVent(game, &record);
    } else if (VolcanoRuntimeNearest(context.bodyId, context.observerMap, &record)) {
        ReplyVent(game, &record);
    }
    return true;
}

bool GameDebugGeologyResolve(GameRuntime *game, const char *name, DebugDslValue *out)
{
    if (strncmp(name, "geology.", 8u) != 0) return false;
    const char *field = name + 8;
    if (!strcmp(field, "heat_exposure")) {
        out->type = DEBUG_DSL_VALUE_NUMBER; out->as.number = game->playerHeatExposure; return true;
    }
    if (!strcmp(field, "heat_damage")) {
        out->type = DEBUG_DSL_VALUE_NUMBER; out->as.number = game->playerHeatDamage; return true;
    }
    if (!strcmp(field, "heat_contact")) {
        out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = game->playerHeatContact; return true;
    }
    if (!strcmp(field, "heat_sources")) {
        out->type = DEBUG_DSL_VALUE_NUMBER; out->as.number = game->playerHeatSources; return true;
    }
    if (!strcmp(field, "heat_blocked_sources")) {
        out->type = DEBUG_DSL_VALUE_NUMBER; out->as.number = game->playerHeatBlockedSources; return true;
    }
    if (!strncmp(field, "deposition_", 11)) return GameDebugDepositionResolve(&game->debugControl, field, out);
    if (!strncmp(field, "history_", 8)) return GameDebugVolcanoHistoryResolve(&game->debugControl, field, out);
    if (!strncmp(field, "terrain_", 8)) {
        const DebugControl *c = &game->debugControl;
        WorldSurfaceContext s = WorldCurrentSurfaceContext();
        bool found = c->geologyTerrainFound && s.kind == WORLD_SURFACE_PLANET &&
            s.bodyId == c->geologyTerrainBody && s.seed == c->geologyTerrainSeed;
        if (!strcmp(field, "terrain_found")) {
            out->type = DEBUG_DSL_VALUE_BOOL; out->as.boolean = found;
        } else if (!strcmp(field, "terrain_position")) {
            out->type = DEBUG_DSL_VALUE_VEC3;
            out->as.vec3 = found ? (DebugDslVec3){ c->geologyTerrainX - s.originX,
                c->geologyTerrainY, c->geologyTerrainZ - s.originZ } : (DebugDslVec3){0};
        } else {
            out->type = DEBUG_DSL_VALUE_NUMBER;
            if (!strcmp(field, "terrain_radius")) out->as.number = found ? c->geologyTerrainRadius : 0;
            else if (!strcmp(field, "terrain_height")) out->as.number = found ? c->geologyTerrainHeight : 0;
            else if (!strcmp(field, "terrain_crater_radius")) out->as.number = found ? c->geologyCraterRadius : 0;
            else if (!strcmp(field, "terrain_edge_distance")) out->as.number = found ? c->geologyTerrainEdgeDistance : 0;
            else if (!strcmp(field, "terrain_abs_latitude")) out->as.number = found ? c->geologyTerrainAbsLatitude : 0;
            else if (!strcmp(field, "terrain_ground")) out->as.number = WorldSurfaceHeightAt(
                (int)floorf(game->player.position.x), (int)floorf(game->player.position.z));
            else return false;
        }
        return true;
    }
    if (!strcmp(field, "enabled")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = game->settings.geologyEnabled;
        return true;
    }
    if (!strncmp(field, "inventory_", 10) || !strncmp(field, "remainder_", 10)) {
        for (unsigned i = 0; i < 6; i++) {
            if (strcmp(field + 10, collectionNames[i])) continue;
            out->type = DEBUG_DSL_VALUE_NUMBER;
            out->as.number = !strncmp(field, "inventory_", 10) ?
                (double)InventoryCount(collectionTypes[i]) : (double)InventoryRemainder(collectionTypes[i]);
            return true;
        }
        return false;
    }
    VolcanoContext context = ContextFor(game);
    VolcanoRecord record = { 0 };
    bool present = context.valid && VolcanoRuntimeNearest(context.bodyId, context.observerMap, &record);
    if (!strcmp(field, "present") || !strcmp(field, "active")) {
        out->type = DEBUG_DSL_VALUE_BOOL;
        out->as.boolean = !strcmp(field, "present") ? present : present && record.active;
        return true;
    }
    if (!strcmp(field, "phase") || !strcmp(field, "id")) {
        out->type = DEBUG_DSL_VALUE_STRING;
        if (!strcmp(field, "phase")) {
            out->as.string = present ? VolcanoPhaseName(record.state.phase) : "none";
        } else {
            static char idText[17];
            snprintf(idText, sizeof(idText), "%016" PRIx64,
                     present ? record.definition.id : UINT64_C(0));
            out->as.string = idText;
        }
        return true;
    }
    if (!strcmp(field, "position")) {
        out->type = DEBUG_DSL_VALUE_VEC3;
        out->as.vec3 = (DebugDslVec3){ record.definition.mapX,
            record.definition.address.radial, record.definition.mapZ };
        return true;
    }
    if (!strcmp(field, "cell_rock")) {
        int x = (int)floorf(game->player.position.x), y = (int)floorf(game->player.position.y);
        int z = (int)floorf(game->player.position.z);
        LavaCell cell;
        out->type = DEBUG_DSL_VALUE_STRING;
        out->as.string = "none";
        if (context.valid && LocalLavaAt(x, y, z, &cell)) {
            out->as.string = LavaRockName(LavaRockForMaterial(cell.material, cell.thermal.volume));
        } else if (context.valid) {
            BlockType base = GetBlockAt(x, y, z);
            for (LavaRock rock = LAVA_ROCK_BASALT; rock <= LAVA_ROCK_RHYOLITE; rock++)
                if (base == RockBlock(rock)) out->as.string = LavaRockName(rock);
        }
        return true;
    }
    VolcanoStats stats = VolcanoRuntimeStats();
    LavaStats lava = LavaRuntimeStats();
    AshStats ash = AshRuntimeStats();
    double value;
    if (!strcmp(field, "flow_radius")) {
        value = 0;
        LavaCell cell;
        for (unsigned i = 0; present && LavaRuntimeAt(i, &cell); i++) {
            if (cell.bodyId != context.bodyId || !LavaLiquidVolume(cell.thermal)) continue;
            SurfaceMapOffset delta = SurfaceShortestMapOffset(record.definition.mapX,
                record.definition.mapZ, cell.x, cell.z);
            value = fmax(value, hypot(delta.x, delta.z));
        }
    }
    else if (!strcmp(field, "records")) value = stats.count;
    else if (!strcmp(field, "active_count")) value = stats.active;
    else if (!strcmp(field, "rejected")) value = (double)stats.rejectedAdmissions;
    else if (!strcmp(field, "event")) value = record.state.event;
    else if (!strcmp(field, "ticks")) value = (double)record.state.activeTicks;
    else if (!strcmp(field, "pressure")) value = record.state.pressure;
    else if (!strcmp(field, "reservoir")) value = record.state.reservoir;
    else if (!strcmp(field, "ash_cells")) value = ash.cells;
    else if (!strcmp(field, "ash_volume")) value = (double)ash.volume / ASH_CAPACITY;
    else if (!strcmp(field, "ash_supplied")) value = (double)ash.supplied / ASH_CAPACITY;
    else if (!strcmp(field, "ash_washed")) value = (double)ash.washed / ASH_CAPACITY;
    else if (!strcmp(field, "ash_collected")) value = (double)ash.collected / ASH_CAPACITY;
    else if (!strcmp(field, "ash_volume_error")) value = ((double)ash.supplied - ash.volume - ash.washed - ash.collected) / ASH_CAPACITY;
    else if (!strcmp(field, "ash_ticks")) value = (double)ash.ticks;
    else if (!strcmp(field, "cell_ash_height")) {
        AshCell cell;
        bool exists = context.valid && LocalAshAt((int)floorf(game->player.position.x),
            (int)floorf(game->player.position.y), (int)floorf(game->player.position.z), &cell);
        value = exists ? (double)cell.volume / ASH_CAPACITY : 0;
    }
    else if (!strcmp(field, "lava_cells")) value = lava.records;
    else if (!strcmp(field, "hot_cells")) value = lava.hotCells;
    else if (!strcmp(field, "lava_volume")) value = (double)lava.volume / LAVA_CAPACITY;
    else if (!strcmp(field, "liquid_volume")) value = (double)lava.liquidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solid_volume")) value = (double)lava.solidVolume / LAVA_CAPACITY;
    else if (!strcmp(field, "solidified")) value = (double)lava.solidified / LAVA_CAPACITY;
    else if (!strcmp(field, "collected")) value = (double)lava.removed / LAVA_CAPACITY;
    else if (!strcmp(field, "supplied")) value = (double)lava.supplied / LAVA_CAPACITY;
    else if (!strcmp(field, "volume_error")) value =
        ((double)lava.supplied - lava.volume - lava.solidified - lava.removed) / LAVA_CAPACITY;
    else if (!strcmp(field, "energy_error")) value =
        lava.suppliedEnergy - lava.energy - lava.cooledEnergy - lava.exportedEnergy - lava.waterEnergy;
    else if (!strcmp(field, "evaporated_units")) value = (double)lava.evaporatedWater;
    else if (!strcmp(field, "evaporated_water")) value = (double)lava.evaporatedWater / FLUID_CAPACITY;
    else if (!strcmp(field, "reservoir_units")) value = (double)lava.reservoirWater;
    else if (!strcmp(field, "water_energy")) value = lava.waterEnergy;
    else if (!strcmp(field, "lava_ticks")) value = (double)lava.ticks;
    else if (!strcmp(field, "lava_accumulator")) value = lava.accumulator;
    else if (!strcmp(field, "vent_accumulator")) value = record.accumulator;
    else if (!strcmp(field, "blocked_transfers")) value = (double)lava.blockedTransfers;
    else if (!strcmp(field, "rejected_cells")) value = (double)lava.rejectedCells;
    else if (!strcmp(field, "cell_volume") || !strcmp(field, "cell_liquid") ||
             !strcmp(field, "cell_temperature") || !strcmp(field, "cell_collision_height") ||
             !strcmp(field, "cell_contact_credit") || !strcmp(field, "cell_steam") ||
             !strcmp(field, "cell_silica") || !strcmp(field, "cell_vesicles") || !strcmp(field, "cell_quenched")) {
        int x = (int)floorf(game->player.position.x);
        int y = (int)floorf(game->player.position.y);
        int z = (int)floorf(game->player.position.z);
        LavaCell cell = { 0 };
        bool exists = context.valid && LocalLavaAt(x, y, z, &cell);
        if (!strcmp(field, "cell_collision_height")) value = BlockCollisionHeightAt(x, y, z);
        else if (!strcmp(field, "cell_volume")) value = exists ? (double)cell.thermal.volume / LAVA_CAPACITY : 0.0;
        else if (!strcmp(field, "cell_liquid")) value = exists ? (double)LavaLiquidVolume(cell.thermal) / LAVA_CAPACITY : 0.0;
        else if (!strcmp(field, "cell_steam")) value = exists ? cell.steam : 0.0;
        else if (!strcmp(field, "cell_silica")) value = exists ? cell.material.silica / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_vesicles")) value = exists ? cell.material.vesicles / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_quenched")) value = exists ? cell.material.quenched / cell.thermal.volume : 0;
        else if (!strcmp(field, "cell_contact_credit")) {
            value = 0.0;
            if (exists) for (unsigned face = 0u; face < 6u; face++) value += cell.contactCredit[face];
        }
        else value = exists ? LavaTemperature(cell.thermal) : 0.0;
    }
    else return false;
    out->type = DEBUG_DSL_VALUE_NUMBER;
    out->as.number = value;
    return true;
}
