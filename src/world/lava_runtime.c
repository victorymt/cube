#include "world/lava_runtime.h"
#include "world/surface_topology.h"
#include "raymath.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LAVA_HASH_SIZE 32768u
#define LAVA_MAX_FLUX (LAVA_MAX_HOT_CELLS * 5u)
typedef struct LavaFlux { unsigned from, to, volume; double enthalpy; } LavaFlux;
static LavaCell cells[LAVA_MAX_RECORDS];
static int lookup[LAVA_HASH_SIZE]; /* index + 1; zero is empty */
static unsigned count;
static LavaStats ledger;
static LavaFlux fluxes[LAVA_MAX_FLUX];
static unsigned incoming[LAVA_MAX_RECORDS];
static int deltaVolume[LAVA_MAX_RECORDS];
static double deltaEnergy[LAVA_MAX_RECORDS];
static LavaMaterial deltaMaterial[LAVA_MAX_RECORDS];

static void SyncMapFields(LavaCell *cell)
{
    SurfaceMapCell map;
    if (!cell) return;
    cell->bodyId = cell->address.bodyId;
    cell->y = cell->address.radial;
    cell->x = cell->address.u;
    cell->z = cell->address.v;
    if (SurfaceAddressCanonicalMapCell(cell->address, &map)) {
        cell->x = map.x;
        cell->z = map.z;
    }
}
static uint32_t HashMap(uint32_t bodyId, int x, int y, int z)
{
    uint32_t h = bodyId * 0x9e3779b9u ^ (uint32_t)x * 0x85ebca6bu ^
        (uint32_t)y * 0xc2b2ae35u ^ (uint32_t)z * 0x27d4eb2fu;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    return h & (LAVA_HASH_SIZE - 1u);
}
static uint32_t HashAddress(SurfaceAddress address)
{
    uint32_t h = address.bodyId * 0x9e3779b9u ^
        (uint32_t)address.face * 0x85ebca6bu ^
        (uint32_t)address.u * 0xc2b2ae35u ^
        (uint32_t)address.v * 0x27d4eb2fu ^
        (uint32_t)address.radial * 0x165667b1u;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    return h & (LAVA_HASH_SIZE - 1u);
}
static bool CellMatches(const LavaCell *cell, bool addressed,
                        SurfaceAddress address, uint32_t bodyId,
                        int x, int y, int z)
{
    if (!cell || cell->addressed != addressed) return false;
    return addressed ? SurfaceAddressEqual(cell->address, address) :
        cell->bodyId == bodyId && cell->x == x && cell->y == y &&
        cell->z == z;
}
static unsigned SlotKey(bool addressed, SurfaceAddress address,
                        uint32_t bodyId, int x, int y, int z)
{
    unsigned slot = addressed ? HashAddress(address) :
        HashMap(bodyId, x, y, z);
    while (lookup[slot]) {
        LavaCell *c = &cells[lookup[slot] - 1];
        if (CellMatches(c, addressed, address, bodyId, x, y, z)) break;
        slot = (slot + 1u) & (LAVA_HASH_SIZE - 1u);
    }
    return slot;
}
static unsigned SlotCell(const LavaCell *cell)
{
    if (!cell) return 0u;
    return SlotKey(cell->addressed, cell->address, cell->bodyId,
                   cell->x, cell->y, cell->z);
}
static void Reindex(void)
{
    memset(lookup, 0, sizeof(lookup));
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        lookup[SlotCell(c)] = (int)i + 1;
    }
}
static int Compare(const void *a, const void *b)
{
    const LavaCell *l = a, *r = b;
#define CMP(f) if (l->f != r->f) return l->f < r->f ? -1 : 1
    CMP(addressed); CMP(bodyId); CMP(y); CMP(z); CMP(x);
    CMP(address.face); CMP(address.radial); CMP(address.v); CMP(address.u);
#undef CMP
    return 0;
}
static bool ValidAddress(SurfaceAddress address)
{
    return SurfaceAddressIsValid(address) &&
        address.radial > -1000000 && address.radial < 1000000;
}

static SurfaceAddress MapAddress(uint32_t bodyId, int x, int y, int z)
{
    return SurfaceAddressFromMapCoordinates(
        bodyId, (float)x + 0.5f, (float)z + 0.5f, y);
}

static LavaBoundary BoundaryAt(const LavaContext *ctx, SurfaceAddress address)
{
    if (!ctx) return LAVA_UNKNOWN;
    if (ctx->addressBoundary) {
        return ctx->addressBoundary(address, ctx->user);
    }
    SurfaceMapCell map;
    if (!ctx->boundary ||
        !SurfaceAddressCanonicalMapCell(address, &map)) return LAVA_UNKNOWN;
    return ctx->boundary(map.x, address.radial, map.z, ctx->user);
}

static double AmbientAt(const LavaContext *ctx, SurfaceAddress address)
{
    if (!ctx) return 293.0;
    if (ctx->addressAmbient) {
        return ctx->addressAmbient(address, ctx->user);
    }
    SurfaceMapCell map;
    if (!ctx->ambient ||
        !SurfaceAddressCanonicalMapCell(address, &map)) return 293.0;
    return ctx->ambient(map.x, map.z, ctx->user);
}

static bool SolidifyAt(const LavaContext *ctx, SurfaceAddress address)
{
    if (!ctx || (!ctx->addressSolidify && !ctx->solidify)) return false;
    if (ctx->addressSolidify) {
        return ctx->addressSolidify(address, ctx->user);
    }
    SurfaceMapCell map;
    if (!SurfaceAddressCanonicalMapCell(address, &map)) return false;
    return ctx->solidify(map.x, address.radial, map.z, ctx->user);
}

static bool WaterAt(const LavaContext *ctx, SurfaceAddress address,
                    unsigned *volume)
{
    if (!ctx || (!ctx->addressWater && !ctx->water)) return false;
    if (ctx->addressWater) return ctx->addressWater(address, volume, ctx->user);
    SurfaceMapCell map;
    if (!SurfaceAddressCanonicalMapCell(address, &map)) return false;
    return ctx->water(map.x, address.radial, map.z, volume, ctx->user);
}

static bool EvaporateAt(const LavaContext *ctx, SurfaceAddress address,
                        unsigned expected, unsigned units, bool *reservoir)
{
    if (!ctx || (!ctx->addressEvaporate && !ctx->evaporate)) return false;
    if (ctx->addressEvaporate) {
        return ctx->addressEvaporate(address, expected, units, reservoir,
                                     ctx->user);
    }
    SurfaceMapCell map;
    if (!SurfaceAddressCanonicalMapCell(address, &map)) return false;
    return ctx->evaporate(map.x, address.radial, map.z, expected, units,
                          reservoir, ctx->user);
}

static SurfaceAddress VerticalNeighbor(SurfaceAddress address, int deltaY)
{
    address.radial += deltaY;
    return address;
}

static SurfaceAddress SideNeighbor(SurfaceAddress address, int deltaX,
                                   int deltaZ)
{
    return SurfaceAddressOffset(address, deltaX, deltaZ, 0);
}
static unsigned HotCount(void)
{
    unsigned hot = 0u;
    for (unsigned i = 0u; i < count; i++) hot += !cells[i].cooled;
    return hot;
}
void LavaRuntimeReset(void)
{
    count = 0u;
    ledger = (LavaStats){ 0 };
    memset(lookup, 0, sizeof(lookup));
}
bool LavaRuntimeAt(unsigned index, LavaCell *out)
{
    if (!out || index >= count) return false;
    *out = cells[index]; return true;
}
bool LavaRuntimeCellAtAddress(SurfaceAddress address, LavaCell *out)
{
    if (!out || !count || !ValidAddress(address)) return false;
    int index = lookup[SlotKey(true, address, address.bodyId, 0, 0, 0)] - 1;
    if (index < 0 || !cells[index].thermal.volume) return false;
    *out = cells[index]; return true;
}
bool LavaRuntimeCellAt(uint32_t body, int x, int y, int z, LavaCell *out)
{
    if (!out || !count) return false;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    int index = lookup[SlotKey(false, (SurfaceAddress){0}, body, x, y, z)] - 1;
    if (index < 0 || !cells[index].thermal.volume) return false;
    *out = cells[index]; return true;
}
bool LavaRuntimeCollect(uint32_t body, int x, int y, int z,
                        unsigned expectedUnits, LavaRock expectedRock)
{
    LavaCell cell;
    if (!expectedUnits || !LavaRuntimeCellAt(body, x, y, z, &cell) ||
        !cell.cooled || LavaLiquidVolume(cell.thermal) ||
        cell.thermal.volume != expectedUnits ||
        LavaRockForMaterial(cell.material, cell.thermal.volume) != expectedRock ||
        UINT64_MAX - ledger.removed < cell.thermal.volume) return false;
    unsigned index = (unsigned)(lookup[SlotCell(&cell)] - 1);
    ledger.removed += cell.thermal.volume;
    ledger.exportedEnergy += cell.thermal.energy;
    cells[index] = cells[--count];
    Reindex();
    return true;
}
bool LavaRuntimeCollectAtAddress(SurfaceAddress address,
                                 unsigned expectedUnits, LavaRock expectedRock)
{
    LavaCell cell;
    if (!expectedUnits || !LavaRuntimeCellAtAddress(address, &cell) ||
        !cell.cooled || LavaLiquidVolume(cell.thermal) || cell.thermal.volume != expectedUnits ||
        LavaRockForMaterial(cell.material, cell.thermal.volume) != expectedRock ||
        UINT64_MAX - ledger.removed < cell.thermal.volume) return false;
    unsigned index = (unsigned)(lookup[SlotCell(&cell)] - 1);
    ledger.removed += cell.thermal.volume;
    ledger.exportedEnergy += cell.thermal.energy;
    cells[index] = cells[--count];
    Reindex();
    return true;
}
static int EnsureAddress(const LavaContext *ctx, SurfaceAddress address,
                         unsigned *hot)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary) ||
        !ValidAddress(address) || address.bodyId != ctx->bodyId) return -1;
    if (BoundaryAt(ctx, address) != LAVA_OPEN) {
        ledger.blockedTransfers++;
        return -1;
    }
    unsigned slot = SlotKey(true, address, address.bodyId, 0, 0, 0);
    int index = lookup[slot] - 1;
    if (index >= 0 && !cells[index].cooled) return index;
    if (*hot >= LAVA_MAX_HOT_CELLS || (index < 0 && count >= LAVA_MAX_RECORDS)) {
        ledger.rejectedCells++; return -1;
    }
    if (index < 0) {
        index = (int)count++;
        cells[index] = (LavaCell){ .address = address, .addressed = true };
        lookup[slot] = index + 1;
    }
    SyncMapFields(&cells[index]);
    cells[index].cooled = false;
    (*hot)++;
    return index;
}

static int EnsureMap(const LavaContext *ctx, int x, int y, int z,
                     unsigned *hot)
{
    if (!ctx || !ctx->valid || !ctx->boundary || !ctx->bodyId ||
        x < -1000000 || x > 1000000 || z < -1000000 || z > 1000000 ||
        y <= -1000000 || y >= 1000000) return -1;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    if (ctx->boundary(x, y, z, ctx->user) != LAVA_OPEN) {
        ledger.blockedTransfers++;
        return -1;
    }
    unsigned slot = SlotKey(false, (SurfaceAddress){0}, ctx->bodyId, x, y, z);
    int index = lookup[slot] - 1;
    if (index >= 0 && !cells[index].cooled) return index;
    if (*hot >= LAVA_MAX_HOT_CELLS ||
        (index < 0 && count >= LAVA_MAX_RECORDS)) {
        ledger.rejectedCells++;
        return -1;
    }
    if (index < 0) {
        index = (int)count++;
        cells[index] = (LavaCell){
            .bodyId = ctx->bodyId, .x = x, .y = y, .z = z,
            .address = MapAddress(ctx->bodyId, x, y, z),
            .addressed = false
        };
        lookup[slot] = index + 1;
    }
    cells[index].cooled = false;
    (*hot)++;
    return index;
}

static LavaBoundary CellBoundary(const LavaContext *ctx, const LavaCell *cell)
{
    if (!cell) return LAVA_UNKNOWN;
    return cell->addressed ? BoundaryAt(ctx, cell->address) :
        (ctx && ctx->boundary ?
         ctx->boundary(cell->x, cell->y, cell->z, ctx->user) : LAVA_UNKNOWN);
}

static int EnsureNeighbor(const LavaContext *ctx, const LavaCell *cell,
                          int dx, int dy, int dz, unsigned *hot)
{
    if (!cell) return -1;
    if (!cell->addressed) {
        return EnsureMap(ctx, cell->x + dx, cell->y + dy, cell->z + dz,
                         hot);
    }
    SurfaceAddress address = SurfaceAddressOffset(cell->address, dx, dz, dy);
    return EnsureAddress(ctx, address, hot);
}

static bool CellAtNeighbor(const LavaCell *cell, int dx, int dy, int dz,
                           LavaCell *out)
{
    if (!cell || !out) return false;
    if (!cell->addressed) {
        return LavaRuntimeCellAt(cell->bodyId, cell->x + dx,
                                 cell->y + dy, cell->z + dz, out);
    }
    return LavaRuntimeCellAtAddress(
        SurfaceAddressOffset(cell->address, dx, dz, dy), out);
}

static double CellAmbient(const LavaContext *ctx, const LavaCell *cell)
{
    if (!cell) return 293.0;
    if (cell->addressed) return AmbientAt(ctx, cell->address);
    return ctx && ctx->ambient ? ctx->ambient(cell->x, cell->z, ctx->user) :
        293.0;
}

static bool CellSolidify(const LavaContext *ctx, const LavaCell *cell)
{
    if (!cell) return false;
    if (cell->addressed) return SolidifyAt(ctx, cell->address);
    return ctx && ctx->solidify ?
        ctx->solidify(cell->x, cell->y, cell->z, ctx->user) : false;
}

static LavaBoundary NeighborBoundary(const LavaContext *ctx,
                                     const LavaCell *cell,
                                     int dx, int dy, int dz)
{
    if (!cell) return LAVA_UNKNOWN;
    if (cell->addressed) {
        return BoundaryAt(ctx, SurfaceAddressOffset(cell->address, dx, dz,
                                                    dy));
    }
    SurfaceMapCell map = SurfaceCanonicalMapCell(
        (float)(cell->x + dx), (float)(cell->z + dz));
    return ctx && ctx->boundary ?
        ctx->boundary(map.x, cell->y + dy, map.z, ctx->user) :
        LAVA_UNKNOWN;
}
unsigned LavaRuntimeDeposit(const LavaContext *ctx, int x, int y, int z,
                            unsigned volume, double temperatureK)
{
    return LavaRuntimeDepositMaterial(ctx, x, y, z, volume, temperatureK, 0,
                                      0);
}
unsigned LavaRuntimeDepositAtAddress(const LavaContext *ctx,
                                     SurfaceAddress address, unsigned volume,
                                     double temperatureK)
{
    return LavaRuntimeDepositMaterialAtAddress(
        ctx, address, volume, temperatureK, 0, 0);
}
bool LavaRuntimeImportThermal(const LavaContext *ctx, int x, int y, int z,
                              LavaThermalState state)
{
    if (!ctx || !ctx->valid || !ctx->boundary || !state.volume ||
        !LavaThermalValid(state) ||
        UINT64_MAX - ledger.supplied < state.volume) return false;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    if (lookup[SlotKey(false, (SurfaceAddress){0}, ctx->bodyId, x, y, z)]) {
        return false;
    }
    unsigned hot = HotCount();
    int index = EnsureMap(ctx, x, y, z, &hot);
    if (index < 0) return false;
    cells[index].thermal = state;
    double ambient = ctx->ambient ? ctx->ambient(x, z, ctx->user) : 293.0;
    if (!isfinite(ambient)) ambient = 293.0;
    ambient = fmin(330.0, fmax(LAVA_REFERENCE_K, ambient));
    cells[index].cooled = LavaTemperature(state) <= ambient + 1e-8;
    ledger.supplied += state.volume;
    ledger.suppliedEnergy += state.energy;
    return true;
}
bool LavaRuntimeImportThermalAtAddress(const LavaContext *ctx,
                                       SurfaceAddress address,
                                       LavaThermalState state)
{
    if (!ctx || !ctx->valid || !state.volume || !LavaThermalValid(state) ||
        UINT64_MAX - ledger.supplied < state.volume || !ValidAddress(address) ||
        address.bodyId != ctx->bodyId) return false;
    if (lookup[SlotKey(true, address, address.bodyId, 0, 0, 0)]) return false;
    unsigned hot = HotCount();
    int index = EnsureAddress(ctx, address, &hot);
    if (index < 0) return false;
    cells[index].thermal = state;
    double ambient = AmbientAt(ctx, address);
    if (!isfinite(ambient)) ambient = 293.0;
    ambient = fmin(330.0, fmax(LAVA_REFERENCE_K, ambient));
    cells[index].cooled = LavaTemperature(state) <= ambient + 1e-8;
    ledger.supplied += state.volume;
    ledger.suppliedEnergy += state.energy;
    return true;
}
unsigned LavaRuntimeDepositMaterial(const LavaContext *ctx, int x, int y, int z,
                                     unsigned volume, double temperatureK,
                                     double silica, double vesicles)
{
    if (!volume || volume > LAVA_CAPACITY || !isfinite(temperatureK) ||
        temperatureK <= LAVA_FREEZING_K || temperatureK > 2000.0 ||
        !isfinite(silica) || silica < 0 || silica > 1 ||
        !isfinite(vesicles) || vesicles < 0 || vesicles > 1 ||
        UINT64_MAX - ledger.supplied < volume) return 0u;
    if (!ctx || !ctx->valid || !ctx->boundary) return 0u;
    unsigned hot = HotCount();
    int index = EnsureMap(ctx, x, y, z, &hot);
    if (index < 0) return 0u;
    LavaCell *c = &cells[index];
    unsigned accepted = LAVA_CAPACITY - c->thermal.volume;
    if (accepted > volume) accepted = volume;
    double energy = accepted * LavaSpecificEnthalpy(temperatureK);
    c->thermal.volume += accepted;
    c->thermal.energy += energy;
    c->material.silica += accepted * silica;
    c->material.vesicles += accepted * vesicles;
    ledger.supplied += accepted;
    ledger.suppliedEnergy += energy;
    return accepted;
}
unsigned LavaRuntimeDepositMaterialAtAddress(
    const LavaContext *ctx, SurfaceAddress address, unsigned volume,
    double temperatureK, double silica, double vesicles)
{
    if (!volume || volume > LAVA_CAPACITY || !isfinite(temperatureK) ||
        temperatureK <= LAVA_FREEZING_K || temperatureK > 2000.0 ||
        !isfinite(silica) || silica < 0 || silica > 1 ||
        !isfinite(vesicles) || vesicles < 0 || vesicles > 1 ||
        UINT64_MAX - ledger.supplied < volume) return 0u;
    unsigned hot = HotCount();
    int index = EnsureAddress(ctx, address, &hot);
    if (index < 0) return 0u;
    LavaCell *c = &cells[index];
    unsigned accepted = LAVA_CAPACITY - c->thermal.volume;
    if (accepted > volume) accepted = volume;
    double energy = accepted * LavaSpecificEnthalpy(temperatureK);
    c->thermal.volume += accepted;
    c->thermal.energy += energy;
    c->material.silica += accepted * silica;
    c->material.vesicles += accepted * vesicles;
    ledger.supplied += accepted;
    ledger.suppliedEnergy += energy;
    return accepted;
}
static void AddFlux(unsigned from, unsigned to, unsigned volume, unsigned *used)
{
    if (!volume || from == to || *used == LAVA_MAX_FLUX) return;
    fluxes[(*used)++] = (LavaFlux){ from, to, volume, LavaLiquidEnthalpy(cells[from].thermal) };
    incoming[to] += volume;
}

static void ReactWater(LavaCell *c, const LavaContext *ctx)
{
    if ((!ctx->water && !ctx->addressWater) ||
        (!ctx->evaporate && !ctx->addressEvaporate)) return;
    static const int offsets[6][3] = {
        { -1, 0, 0 }, { 1, 0, 0 }, { 0, 0, -1 }, { 0, 0, 1 },
        { 0, -1, 0 }, { 0, 1, 0 }
    };
    for (unsigned face = 0u; face < 6u; face++) {
        SurfaceAddress neighbor;
        int x = c->x + offsets[face][0];
        int y = c->y + offsets[face][1];
        int z = c->z + offsets[face][2];
        if (c->addressed) {
            if (offsets[face][1] != 0) {
                neighbor = VerticalNeighbor(c->address, offsets[face][1]);
            } else {
                neighbor = SideNeighbor(c->address, offsets[face][0],
                                        offsets[face][2]);
            }
        } else {
            SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
            x = map.x;
            z = map.z;
        }
        unsigned water = 0u;
        bool haveWater = c->addressed ? WaterAt(ctx, neighbor, &water) :
            ctx->water(x, y, z, &water, ctx->user);
        if (!haveWater) continue;
        double area = fmin((double)c->thermal.volume / LAVA_CAPACITY,
                           (double)water / LAVA_WATER_CAPACITY);
        /* Horizontal faces touch only when the lower cell reaches its top. */
        if (face == 4u) area = water == LAVA_WATER_CAPACITY ? 1.0 : 0.0;
        if (face == 5u) area = c->thermal.volume == LAVA_CAPACITY && water ? 1.0 : 0.0;
        double temperature = LavaTemperature(c->thermal);
        double available = fmax(0.0, c->thermal.energy -
            c->thermal.volume * (LAVA_WATER_BOILING_K - LAVA_REFERENCE_K));
        if (!water || water > LAVA_WATER_CAPACITY || area <= 0.0 ||
            temperature <= LAVA_WATER_BOILING_K || available < LAVA_WATER_UNIT_ENERGY) {
            c->contactCredit[face] = 0.0;
            continue;
        }
        double budget = fmin(available, c->contactCredit[face] +
            128.0 * area * (temperature - LAVA_WATER_BOILING_K) * LAVA_STEP_SECONDS);
        unsigned units = (unsigned)fmin((double)water, floor(budget / LAVA_WATER_UNIT_ENERGY));
        if (!units) { c->contactCredit[face] = budget; continue; }
        bool reservoir = false;
        bool evaporated = c->addressed ?
            EvaporateAt(ctx, neighbor, water, units, &reservoir) :
            ctx->evaporate(x, y, z, water, units, &reservoir, ctx->user);
        if (UINT64_MAX - ledger.evaporatedWater < units || !evaporated) {
            c->contactCredit[face] = 0.0;
            continue;
        }
        /* The successful main-thread water commit is the only path that can
           debit lava enthalpy or emit steam. Credits themselves carry no heat. */
        double heat = units * LAVA_WATER_UNIT_ENERGY;
        unsigned liquidBefore = LavaLiquidVolume(c->thermal);
        c->thermal.energy -= heat;
        unsigned frozen = liquidBefore - LavaLiquidVolume(c->thermal);
        c->material.quenched = fmin(c->thermal.volume, c->material.quenched + frozen);
        ledger.waterEnergy += heat;
        ledger.evaporatedWater += units;
        if (reservoir) ledger.reservoirWater += units;
        c->steam = fmin(64.0, c->steam + units);
        c->contactCredit[face] = units == water ? 0.0 : fmod(budget, LAVA_WATER_UNIT_ENERGY);
    }
}
void LavaRuntimeStep(const LavaContext *ctx)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary)) return;
    ledger.ticks++;
    qsort(cells, count, sizeof(cells[0]), Compare);
    Reindex();
    memset(incoming, 0, sizeof(incoming));
    memset(deltaVolume, 0, sizeof(deltaVolume));
    memset(deltaEnergy, 0, sizeof(deltaEnergy));
    memset(deltaMaterial, 0, sizeof(deltaMaterial));
    unsigned original = count, hot = HotCount(), used = 0u;
    static const int offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (unsigned i = 0u; i < original; i++) {
        LavaCell c = cells[i];
        if (c.cooled || c.bodyId != ctx->bodyId ||
            CellBoundary(ctx, &c) != LAVA_OPEN) continue;
        unsigned budget = LavaFlowBudget(c.thermal);
        if (!budget) continue;
        int below = EnsureNeighbor(ctx, &c, 0, -1, 0, &hot);
        if (below >= 0) {
            unsigned volume = LAVA_CAPACITY - cells[below].thermal.volume;
            if (volume > budget) volume = budget;
            AddFlux(i, (unsigned)below, volume, &used);
            budget -= volume;
        }
        if (!budget) continue;
        unsigned wants[4] = { 0 }, total = 0u;
        int targets[4];
        for (unsigned side = 0u; side < 4u; side++) {
            targets[side] = EnsureNeighbor(
                ctx, &c, offsets[side][0], 0, offsets[side][1], &hot);
            if (targets[side] < 0) continue;
            unsigned target = cells[targets[side]].thermal.volume;
            if (c.thermal.volume > target + 8u) wants[side] = (c.thermal.volume - target) / 2u;
            total += wants[side];
        }
        for (unsigned side = 0u; total && side < 4u; side++) {
            unsigned volume = wants[side];
            if (total > budget) volume = (unsigned)((uint64_t)volume * budget / total);
            if (targets[side] >= 0) AddFlux(i, (unsigned)targets[side], volume, &used);
        }
    }
    /* Resolve shared destination capacity from the old state. Rounding stays
       in the source; no iteration mutates a neighbor's sampling state. */
    for (unsigned i = 0u; i < used; i++) {
        LavaFlux f = fluxes[i];
        unsigned space = LAVA_CAPACITY - cells[f.to].thermal.volume;
        unsigned accepted = incoming[f.to] > space ?
            (unsigned)((uint64_t)f.volume * space / incoming[f.to]) : f.volume;
        deltaVolume[f.from] -= (int)accepted;
        deltaVolume[f.to] += (int)accepted;
        deltaEnergy[f.from] -= accepted * f.enthalpy;
        deltaEnergy[f.to] += accepted * f.enthalpy;
        double fraction = (double)accepted / cells[f.from].thermal.volume;
#define MOVE_MARKER(field) do { \
        double amount = fraction * cells[f.from].material.field; \
        deltaMaterial[f.from].field -= amount; deltaMaterial[f.to].field += amount; \
    } while (0)
        MOVE_MARKER(silica); MOVE_MARKER(vesicles); MOVE_MARKER(quenched);
#undef MOVE_MARKER
    }
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        c->thermal.volume = (unsigned)((int)c->thermal.volume + deltaVolume[i]);
        c->thermal.energy = c->thermal.volume ? fmax(0.0, c->thermal.energy + deltaEnergy[i]) : 0.0;
#define APPLY_MARKER(field) c->material.field = fmin(c->thermal.volume, \
        fmax(0.0, c->material.field + deltaMaterial[i].field))
        APPLY_MARKER(silica); APPLY_MARKER(vesicles); APPLY_MARKER(quenched);
#undef APPLY_MARKER
    }
    /* Cool only exposed surface. Previously every buried cell lost heat
       through all four sides and its top, freezing a fed pool into a tower.
       Snapshot volumes are fully committed before sampling neighbors. */
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        c->steam *= 0.8;
        if (!c->thermal.volume || c->cooled || c->bodyId != ctx->bodyId ||
            CellBoundary(ctx, c) != LAVA_OPEN) continue;
        ReactWater(c, ctx);
        double ambient = CellAmbient(ctx, c);
        if (!isfinite(ambient)) ambient = 293.0;
        ambient = fmin(330.0, fmax(LAVA_REFERENCE_K, ambient));
        double height = (double)c->thermal.volume / LAVA_CAPACITY;
        LavaCell neighbor;
        double area = height < 1.0 ||
            !CellAtNeighbor(c, 0, 1, 0, &neighbor) ? 1.0 : 0.0;
        for (unsigned side = 0; side < 4; side++) {
            /* Loaded rock is a heat sink; unknown faces remain parked. */
            if (NeighborBoundary(ctx, c, offsets[side][0], 0,
                                 offsets[side][1]) == LAVA_UNKNOWN) continue;
            double adjacent = CellAtNeighbor(
                c, offsets[side][0], 0, offsets[side][1], &neighbor) ?
                (double)neighbor.thermal.volume / LAVA_CAPACITY : 0.0;
            area += fmax(0.0, height - adjacent);
        }
        /* Small effective conduction loss also cools completely buried lava. */
        area = fmax(area, 0.1 * height);
        ledger.cooledEnergy += LavaCool(&c->thermal, ambient,
            LAVA_STEP_SECONDS * area / (1.0 + 4.0 * height));
        c->cooled = LavaTemperature(c->thermal) <= ambient + 1e-8;
        if (c->cooled) memset(c->contactCredit, 0, sizeof(c->contactCredit));
    }
    /* Preserve the hash until all world callbacks finish; they may read cells. */
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        if (c->bodyId == ctx->bodyId && c->cooled && c->thermal.volume == LAVA_CAPACITY &&
            CellBoundary(ctx, c) == LAVA_OPEN && CellSolidify(ctx, c)) {
            ledger.solidified += c->thermal.volume;
            ledger.exportedEnergy += c->thermal.energy;
            c->thermal = (LavaThermalState){ 0 };
        }
    }
    unsigned kept = 0u;
    for (unsigned i = 0u; i < count; i++) {
        if (cells[i].thermal.volume) cells[kept++] = cells[i];
    }
    count = kept;
    Reindex();
}
void LavaRuntimeAdvance(double seconds, bool active, void (*tick)(void *user), void *user)
{
    if (!active || !tick || !isfinite(seconds) || seconds < 0.0 || seconds > 0.5) return;
    ledger.accumulator += seconds;
    while (ledger.accumulator + 1e-9 >= LAVA_STEP_SECONDS) {
        ledger.accumulator = fmax(0.0, ledger.accumulator - LAVA_STEP_SECONDS);
        tick(user);
    }
}
LavaStats LavaRuntimeStats(void)
{
    LavaStats stats = ledger;
    stats.records = count;
    stats.hotCells = HotCount();
    for (unsigned i = 0u; i < count; i++) {
        stats.volume += cells[i].thermal.volume;
        stats.liquidVolume += LavaLiquidVolume(cells[i].thermal);
        stats.energy += cells[i].thermal.energy;
    }
    stats.solidVolume = stats.volume - stats.liquidVolume;
    return stats;
}
bool LavaRuntimeHasActiveHotNear(const LavaContext *ctx,
                                 int x, int y, int z, float radius)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary) ||
        !isfinite(radius) || radius < 0.0f) return false;
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        if (c->bodyId != ctx->bodyId || c->cooled) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(x, z, c->x, c->z);
        double dy = (double)c->y - y;
        if ((double)offset.x * offset.x + (double)offset.z * offset.z + dy * dy <= radius * radius &&
            CellBoundary(ctx, c) == LAVA_OPEN) return true;
    }
    return false;
}

bool LavaRuntimeHasActiveHotNearAddress(const LavaContext *ctx,
                                        SurfaceAddress address, float radius)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary) ||
        !ValidAddress(address) || !isfinite(radius) || radius < 0.0f) {
        return false;
    }
    Vector3 point = Vector3Scale(SurfaceAddressDirection(address),
        SURFACE_RADIUS_BLOCKS + (float)address.radial);
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *cell = &cells[i];
        if (cell->bodyId != ctx->bodyId || cell->cooled ||
            !cell->addressed) continue;
        Vector3 cellPoint = Vector3Scale(SurfaceAddressDirection(cell->address),
            SURFACE_RADIUS_BLOCKS + (float)cell->address.radial);
        if (Vector3DistanceSqr(point, cellPoint) <= radius * radius &&
            BoundaryAt(ctx, cell->address) == LAVA_OPEN) return true;
    }
    return false;
}

static bool ExposureLineOpen(const LavaContext *ctx, Vector3 from,
                             const LavaCell *cell)
{
    if (!ctx || (!ctx->boundary && !ctx->addressBoundary) || !cell) return false;
    float dx = (float)cell->x + 0.5f - from.x;
    float dy = (float)cell->y + 0.5f - from.y;
    float dz = (float)cell->z + 0.5f - from.z;
    float distance = sqrtf(dx * dx + dy * dy + dz * dz);
    unsigned steps = (unsigned)ceilf(distance * 2.0f);
    for (unsigned step = 1u; step < steps; step++) {
        float t = (float)step / (float)steps;
        int x = (int)floorf(from.x + dx * t);
        int y = (int)floorf(from.y + dy * t);
        int z = (int)floorf(from.z + dz * t);
        LavaBoundary boundary = ctx->addressBoundary
            ? BoundaryAt(ctx, MapAddress(ctx->bodyId, x, y, z))
            : ctx->boundary(x, y, z, ctx->user);
        if (boundary != LAVA_OPEN) return false;
    }
    return true;
}

LavaHeatExposure LavaRuntimeHeatExposureAt(const LavaContext *ctx,
                                           Vector3 position, float radius,
                                           float shelter)
{
    LavaHeatExposure result = { .nearestDistance = INFINITY };
    if (!ctx || !ctx->valid || !isfinite(radius) || radius < 0.0f ||
        !isfinite(position.x) || !isfinite(position.y) || !isfinite(position.z)) return result;
    shelter = fminf(fmaxf(shelter, 0.0f), 1.0f);
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *cell = &cells[i];
        if (cell->bodyId != ctx->bodyId || cell->cooled || !cell->thermal.volume) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset((int)floorf(position.x),
            (int)floorf(position.z), cell->x, cell->z);
        float dx = (float)offset.x + 0.5f - (position.x - floorf(position.x));
        float dy = (float)cell->y + 0.5f - position.y;
        float dz = (float)offset.z + 0.5f - (position.z - floorf(position.z));
        float distance = sqrtf(dx * dx + dy * dy + dz * dz);
        if (distance > radius) continue;
        result.nearestDistance = fminf(result.nearestDistance, distance);
        if (CellBoundary(ctx, cell) != LAVA_OPEN ||
            !ExposureLineOpen(ctx, position, cell)) {
            result.blockedSources++;
            continue;
        }
        float temperature = (float)LavaTemperature(cell->thermal);
        float intensity = fminf(fmaxf((temperature - LAVA_FREEZING_K) / 900.0f, 0.0f), 1.0f);
        intensity *= fminf(fmaxf((float)cell->thermal.volume / (float)LAVA_CAPACITY, 0.0f), 1.0f);
        intensity *= 1.0f - shelter * 0.85f;
        intensity *= 1.0f / fmaxf(1.0f, distance * distance);
        result.heat += intensity;
        result.hotSources++;
        if (distance <= 0.75f) result.contact = true;
    }
    if (result.hotSources > 0u) result.heat = fminf(result.heat, 1.0f);
    return result;
}

LavaHeatExposure LavaRuntimeHeatExposureAtAddress(
    const LavaContext *ctx, SurfaceAddress observer, float radius,
    float shelter)
{
    LavaHeatExposure result = { .nearestDistance = INFINITY };
    if (!ctx || !ctx->valid || !ValidAddress(observer) ||
        !isfinite(radius) || radius < 0.0f) return result;
    shelter = fminf(fmaxf(shelter, 0.0f), 1.0f);
    Vector3 observerPoint = Vector3Scale(SurfaceAddressDirection(observer),
        SURFACE_RADIUS_BLOCKS + (float)observer.radial);
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *cell = &cells[i];
        if (cell->bodyId != ctx->bodyId || cell->cooled ||
            !cell->thermal.volume || !cell->addressed) continue;
        Vector3 cellPoint = Vector3Scale(SurfaceAddressDirection(cell->address),
            SURFACE_RADIUS_BLOCKS + (float)cell->address.radial);
        float distance = Vector3Distance(observerPoint, cellPoint);
        if (distance > radius) continue;
        result.nearestDistance = fminf(result.nearestDistance, distance);
        if (BoundaryAt(ctx, cell->address) != LAVA_OPEN) {
            result.blockedSources++;
            continue;
        }
        float temperature = (float)LavaTemperature(cell->thermal);
        float intensity = fminf(fmaxf(
            (temperature - LAVA_FREEZING_K) / 900.0f, 0.0f), 1.0f);
        intensity *= fminf(fmaxf(
            (float)cell->thermal.volume / (float)LAVA_CAPACITY, 0.0f), 1.0f);
        intensity *= 1.0f - shelter * 0.85f;
        intensity *= 1.0f / fmaxf(1.0f, distance * distance);
        result.heat += intensity;
        result.hotSources++;
        if (distance <= 0.75f) result.contact = true;
    }
    if (result.hotSources > 0u) result.heat = fminf(result.heat, 1.0f);
    return result;
}

static bool WriteU64(FILE *f, uint64_t value)
{
    unsigned char b[8];
    for (unsigned i = 0u; i < 8u; i++) b[i] = (unsigned char)(value >> (i * 8u));
    return fwrite(b, 1u, 8u, f) == 8u;
}
static bool ReadU64(FILE *f, uint64_t *value)
{
    unsigned char b[8];
    if (fread(b, 1u, 8u, f) != 8u) return false;
    *value = 0u;
    for (unsigned i = 0u; i < 8u; i++) *value |= (uint64_t)b[i] << (i * 8u);
    return true;
}
static bool WriteReal(FILE *f, double v)
{
    uint64_t b; memcpy(&b, &v, sizeof(b)); return WriteU64(f, b);
}
static bool ReadReal(FILE *f, double *v)
{
    uint64_t b; if (!ReadU64(f, &b)) return false;
    memcpy(v, &b, sizeof(b)); return isfinite(*v);
}
bool LavaRuntimeSave(FILE *f)
{
    if (!f || sizeof(double) != 8u || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fwrite("LAVA0005", 1u, 8u, f) != 8u) return false;
    uint64_t fields[] = { count, ledger.supplied, ledger.solidified, ledger.removed,
        ledger.blockedTransfers, ledger.rejectedCells, ledger.ticks };
    for (unsigned i = 0u; i < 7u; i++) if (!WriteU64(f, fields[i])) return false;
    if (!WriteReal(f, ledger.suppliedEnergy) || !WriteReal(f, ledger.cooledEnergy) ||
        !WriteReal(f, ledger.exportedEnergy) || !WriteReal(f, ledger.accumulator)) return false;
    if (!WriteU64(f, ledger.evaporatedWater) || !WriteU64(f, ledger.reservoirWater) ||
        !WriteReal(f, ledger.waterEnergy)) return false;
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        uint64_t values[] = { c->bodyId, c->addressed,
            (uint32_t)c->x, (uint32_t)c->y, (uint32_t)c->z,
            (uint32_t)c->address.face, (uint32_t)c->address.u,
            (uint32_t)c->address.v, (uint32_t)c->address.radial,
            c->thermal.volume, c->cooled };
        for (unsigned n = 0u; n < 11u; n++) if (!WriteU64(f, values[n])) return false;
        if (!WriteReal(f, c->thermal.energy)) return false;
        for (unsigned face = 0u; face < 6u; face++) {
            if (!WriteReal(f, c->contactCredit[face])) return false;
        }
        if (!WriteReal(f, c->material.silica) || !WriteReal(f, c->material.vesicles) ||
            !WriteReal(f, c->material.quenched)) return false;
    }
    return !ferror(f);
}
bool LavaRuntimeLoad(FILE *f)
{
    char magic[8]; uint64_t v[7]; double e[4];
    if (!f || sizeof(double) != 8u || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fread(magic, 1u, 8u, f) != 8u) return false;
    if (memcmp(magic, "LAVA0005", 8u)) return false;
    for (unsigned i = 0u; i < 7u; i++) if (!ReadU64(f, &v[i])) return false;
    for (unsigned i = 0u; i < 4u; i++) if (!ReadReal(f, &e[i]) || e[i] < 0.0) return false;
    if (v[0] > LAVA_MAX_RECORDS || e[3] >= LAVA_STEP_SECONDS ||
        v[2] > v[1] || v[3] > v[1] - v[2]) return false;
    uint64_t evaporated = 0u, reservoir = 0u;
    double waterEnergy = 0.0;
    if (!ReadU64(f, &evaporated) || !ReadU64(f, &reservoir) ||
        !ReadReal(f, &waterEnergy) || waterEnergy < 0.0 || reservoir > evaporated ||
        fabs(waterEnergy - (double)evaporated * LAVA_WATER_UNIT_ENERGY) >
            fmax(0.01, waterEnergy * 1e-9)) return false;
    LavaCell *loaded = calloc(v[0] ? (size_t)v[0] : 1u, sizeof(*loaded));
    if (!loaded) return false;
    bool ok = true; unsigned hot = 0u; uint64_t volume = 0u; double energy = 0.0;
    for (unsigned i = 0u; ok && i < v[0]; i++) {
        uint64_t d[11]; double heat;
        for (unsigned n = 0u; n < 11u; n++) {
            if (!ReadU64(f, &d[n]) || d[n] > UINT32_MAX) { ok = false; break; }
        }
        if (!ok || !ReadReal(f, &heat)) { ok = false; break; }
        LavaCell c = {
            .bodyId = (uint32_t)d[0], .addressed = d[1] != 0u,
            .x = (int32_t)d[2], .y = (int32_t)d[3], .z = (int32_t)d[4],
            .address = { (uint32_t)d[0], (SurfaceFace)d[5], (int32_t)d[6],
                         (int32_t)d[7], (int32_t)d[8] },
            .thermal = { (uint32_t)d[9], heat }, .cooled = d[10] != 0u
        };
        if (c.addressed) SyncMapFields(&c);
        for (unsigned face = 0u; face < 6u; face++) {
            if (!ReadReal(f, &c.contactCredit[face]) || c.contactCredit[face] < 0.0 ||
                c.contactCredit[face] >= LAVA_WATER_UNIT_ENERGY ||
                (c.cooled && c.contactCredit[face] != 0.0)) ok = false;
        }
        if (!ok) break;
        if (!ReadReal(f, &c.material.silica) || !ReadReal(f, &c.material.vesicles) ||
            !ReadReal(f, &c.material.quenched) ||
            !LavaMaterialValid(c.material, c.thermal.volume)) {
            ok = false; break;
        }
        bool coordinateValid = c.addressed ? ValidAddress(c.address) &&
            c.address.bodyId == c.bodyId :
            c.x >= -1000000 && c.x <= 1000000 &&
            c.z >= -1000000 && c.z <= 1000000 &&
            c.y > -1000000 && c.y < 1000000;
        if (!coordinateValid || !c.thermal.volume ||
            d[1] > 1u || d[10] > 1u || !LavaThermalValid(c.thermal) ||
            (c.cooled && LavaTemperature(c.thermal) > 330.0 + 1e-8)) { ok = false; break; }
        loaded[i] = c; hot += !c.cooled; volume += c.thermal.volume; energy += c.thermal.energy;
    }
    if (ok) {
        qsort(loaded, (size_t)v[0], sizeof(*loaded), Compare);
        for (unsigned i = 1u; i < v[0]; i++) if (!Compare(&loaded[i - 1u], &loaded[i])) ok = false;
        double error = fabs(e[0] - e[1] - e[2] - waterEnergy - energy);
        if (hot > LAVA_MAX_HOT_CELLS || volume != v[1] - v[2] - v[3] ||
            error > fmax(0.01, e[0] * 1e-9)) ok = false;
    }
    if (ok) {
        memcpy(cells, loaded, (size_t)v[0] * sizeof(*loaded)); count = (unsigned)v[0];
        ledger = (LavaStats){ .supplied = v[1], .solidified = v[2], .removed = v[3],
            .blockedTransfers = v[4], .rejectedCells = v[5], .ticks = v[6],
            .suppliedEnergy = e[0], .cooledEnergy = e[1], .exportedEnergy = e[2], .accumulator = e[3],
            .evaporatedWater = evaporated, .reservoirWater = reservoir, .waterEnergy = waterEnergy };
        Reindex();
    }
    free(loaded); return ok;
}
