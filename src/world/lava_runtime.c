#include "world/lava_runtime.h"
#include "world/surface_topology.h"
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

static void Canonical(int *x, int *z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)*x, (float)*z);
    *x = cell.x; *z = cell.z;
}
static uint32_t Hash(uint32_t b, int x, int y, int z)
{
    uint32_t h = b * 0x9e3779b9u ^ (uint32_t)x * 0x85ebca6bu ^
                 (uint32_t)y * 0xc2b2ae35u ^ (uint32_t)z * 0x27d4eb2fu;
    h ^= h >> 16; h *= 0x7feb352du; h ^= h >> 15;
    return h & (LAVA_HASH_SIZE - 1u);
}
static unsigned Slot(uint32_t b, int x, int y, int z)
{
    unsigned slot = Hash(b, x, y, z);
    while (lookup[slot]) {
        LavaCell *c = &cells[lookup[slot] - 1];
        if (c->bodyId == b && c->x == x && c->y == y && c->z == z) break;
        slot = (slot + 1u) & (LAVA_HASH_SIZE - 1u);
    }
    return slot;
}
static void Reindex(void)
{
    memset(lookup, 0, sizeof(lookup));
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        lookup[Slot(c->bodyId, c->x, c->y, c->z)] = (int)i + 1;
    }
}
static int Compare(const void *a, const void *b)
{
    const LavaCell *l = a, *r = b;
#define CMP(f) if (l->f != r->f) return l->f < r->f ? -1 : 1
    CMP(bodyId); CMP(y); CMP(z); CMP(x);
#undef CMP
    return 0;
}
static bool ValidCoordinate(int x, int y, int z)
{
    return x >= -1000000 && x <= 1000000 && z >= -1000000 && z <= 1000000 &&
           y > -1000000 && y < 1000000;
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
bool LavaRuntimeCellAt(uint32_t body, int x, int y, int z, LavaCell *out)
{
    if (!out || !count || !ValidCoordinate(x, y, z)) return false;
    Canonical(&x, &z);
    int index = lookup[Slot(body, x, y, z)] - 1;
    if (index < 0 || !cells[index].thermal.volume) return false;
    *out = cells[index]; return true;
}
static int Ensure(const LavaContext *ctx, int x, int y, int z, unsigned *hot)
{
    if (!ctx || !ctx->valid || !ctx->boundary || !ValidCoordinate(x, y, z)) return -1;
    Canonical(&x, &z);
    if (ctx->boundary(x, y, z, ctx->user) != LAVA_OPEN) {
        ledger.blockedTransfers++;
        return -1;
    }
    unsigned slot = Slot(ctx->bodyId, x, y, z);
    int index = lookup[slot] - 1;
    if (index >= 0 && !cells[index].cooled) return index;
    if (*hot >= LAVA_MAX_HOT_CELLS || (index < 0 && count >= LAVA_MAX_RECORDS)) {
        ledger.rejectedCells++; return -1;
    }
    if (index < 0) {
        index = (int)count++;
        cells[index] = (LavaCell){ .bodyId = ctx->bodyId, .x = x, .y = y, .z = z };
        lookup[slot] = index + 1;
    }
    cells[index].cooled = false;
    (*hot)++;
    return index;
}
unsigned LavaRuntimeDeposit(const LavaContext *ctx, int x, int y, int z,
                            unsigned volume, double temperatureK)
{
    if (!volume || volume > LAVA_CAPACITY || !isfinite(temperatureK) ||
        temperatureK <= LAVA_FREEZING_K || temperatureK > 2000.0 ||
        UINT64_MAX - ledger.supplied < volume) return 0u;
    unsigned hot = HotCount();
    int index = Ensure(ctx, x, y, z, &hot);
    if (index < 0) return 0u;
    LavaCell *c = &cells[index];
    unsigned accepted = LAVA_CAPACITY - c->thermal.volume;
    if (accepted > volume) accepted = volume;
    double energy = accepted * LavaSpecificEnthalpy(temperatureK);
    c->thermal.volume += accepted;
    c->thermal.energy += energy;
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
void LavaRuntimeStep(const LavaContext *ctx)
{
    if (!ctx || !ctx->valid || !ctx->boundary) return;
    ledger.ticks++;
    qsort(cells, count, sizeof(cells[0]), Compare);
    Reindex();
    memset(incoming, 0, sizeof(incoming));
    memset(deltaVolume, 0, sizeof(deltaVolume));
    memset(deltaEnergy, 0, sizeof(deltaEnergy));
    unsigned original = count, hot = HotCount(), used = 0u;
    static const int offsets[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
    for (unsigned i = 0u; i < original; i++) {
        LavaCell c = cells[i];
        if (c.cooled || c.bodyId != ctx->bodyId ||
            ctx->boundary(c.x, c.y, c.z, ctx->user) != LAVA_OPEN) continue;
        unsigned budget = LavaFlowBudget(c.thermal);
        if (!budget) continue;
        int below = Ensure(ctx, c.x, c.y - 1, c.z, &hot);
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
            int x = c.x + offsets[side][0], z = c.z + offsets[side][1];
            targets[side] = Ensure(ctx, x, c.y, z, &hot);
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
    }
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        c->thermal.volume = (unsigned)((int)c->thermal.volume + deltaVolume[i]);
        c->thermal.energy = c->thermal.volume ? fmax(0.0, c->thermal.energy + deltaEnergy[i]) : 0.0;
        if (!c->thermal.volume || c->cooled || c->bodyId != ctx->bodyId ||
            ctx->boundary(c->x, c->y, c->z, ctx->user) != LAVA_OPEN) continue;
        double ambient = ctx->ambient ? ctx->ambient(c->x, c->z, ctx->user) : 293.0;
        if (!isfinite(ambient)) ambient = 293.0;
        ambient = fmin(330.0, fmax(LAVA_REFERENCE_K, ambient));
        ledger.cooledEnergy += LavaCool(&c->thermal, ambient, LAVA_STEP_SECONDS);
        c->cooled = LavaTemperature(c->thermal) <= ambient + 1e-8;
    }
    /* Preserve the hash until all world callbacks finish; they may read cells. */
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        if (c->bodyId == ctx->bodyId && c->cooled && c->thermal.volume == LAVA_CAPACITY &&
            ctx->solidify && ctx->boundary(c->x, c->y, c->z, ctx->user) == LAVA_OPEN &&
            ctx->solidify(c->x, c->y, c->z, ctx->user)) {
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
    if (!ctx || !ctx->valid || !ctx->boundary || !isfinite(radius) || radius < 0.0f) return false;
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        if (c->bodyId != ctx->bodyId || c->cooled) continue;
        SurfaceMapOffset offset = SurfaceShortestMapOffset(x, z, c->x, c->z);
        double dy = (double)c->y - y;
        if ((double)offset.x * offset.x + (double)offset.z * offset.z + dy * dy <= radius * radius &&
            ctx->boundary(c->x, c->y, c->z, ctx->user) == LAVA_OPEN) return true;
    }
    return false;
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
        fwrite("LAVA0001", 1u, 8u, f) != 8u) return false;
    uint64_t fields[] = { count, ledger.supplied, ledger.solidified, ledger.removed,
        ledger.blockedTransfers, ledger.rejectedCells, ledger.ticks };
    for (unsigned i = 0u; i < 7u; i++) if (!WriteU64(f, fields[i])) return false;
    if (!WriteReal(f, ledger.suppliedEnergy) || !WriteReal(f, ledger.cooledEnergy) ||
        !WriteReal(f, ledger.exportedEnergy) || !WriteReal(f, ledger.accumulator)) return false;
    for (unsigned i = 0u; i < count; i++) {
        LavaCell *c = &cells[i];
        uint64_t values[] = { c->bodyId, (uint32_t)c->x, (uint32_t)c->y, (uint32_t)c->z,
            c->thermal.volume, c->cooled };
        for (unsigned n = 0u; n < 6u; n++) if (!WriteU64(f, values[n])) return false;
        if (!WriteReal(f, c->thermal.energy)) return false;
    }
    return !ferror(f);
}
bool LavaRuntimeLoad(FILE *f)
{
    char magic[8]; uint64_t v[7]; double e[4];
    if (!f || sizeof(double) != 8u || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fread(magic, 1u, 8u, f) != 8u || memcmp(magic, "LAVA0001", 8u)) return false;
    for (unsigned i = 0u; i < 7u; i++) if (!ReadU64(f, &v[i])) return false;
    for (unsigned i = 0u; i < 4u; i++) if (!ReadReal(f, &e[i]) || e[i] < 0.0) return false;
    if (v[0] > LAVA_MAX_RECORDS || e[3] >= LAVA_STEP_SECONDS ||
        v[2] > v[1] || v[3] > v[1] - v[2]) return false;
    LavaCell *loaded = calloc(v[0] ? (size_t)v[0] : 1u, sizeof(*loaded));
    if (!loaded) return false;
    bool ok = true; unsigned hot = 0u; uint64_t volume = 0u; double energy = 0.0;
    for (unsigned i = 0u; ok && i < v[0]; i++) {
        uint64_t d[6]; double heat;
        for (unsigned n = 0u; n < 6u; n++) {
            if (!ReadU64(f, &d[n]) || d[n] > UINT32_MAX) { ok = false; break; }
        }
        if (!ok || !ReadReal(f, &heat)) { ok = false; break; }
        LavaCell c = { (uint32_t)d[0], (int32_t)d[1], (int32_t)d[2], (int32_t)d[3],
            { (uint32_t)d[4], heat }, d[5] != 0u };
        if (!ValidCoordinate(c.x, c.y, c.z) || !c.thermal.volume ||
            d[5] > 1u || !LavaThermalValid(c.thermal) ||
            (c.cooled && LavaTemperature(c.thermal) > 330.0 + 1e-8)) { ok = false; break; }
        int x = c.x, z = c.z; Canonical(&x, &z);
        if (x != c.x || z != c.z) { ok = false; break; }
        loaded[i] = c; hot += !c.cooled; volume += c.thermal.volume; energy += c.thermal.energy;
    }
    if (ok) {
        qsort(loaded, (size_t)v[0], sizeof(*loaded), Compare);
        for (unsigned i = 1u; i < v[0]; i++) if (!Compare(&loaded[i - 1u], &loaded[i])) ok = false;
        double error = fabs(e[0] - e[1] - e[2] - energy);
        if (hot > LAVA_MAX_HOT_CELLS || volume != v[1] - v[2] - v[3] ||
            error > fmax(0.01, e[0] * 1e-9)) ok = false;
    }
    if (ok) {
        memcpy(cells, loaded, (size_t)v[0] * sizeof(*loaded)); count = (unsigned)v[0];
        ledger = (LavaStats){ .supplied = v[1], .solidified = v[2], .removed = v[3],
            .blockedTransfers = v[4], .rejectedCells = v[5], .ticks = v[6],
            .suppliedEnergy = e[0], .cooledEnergy = e[1], .exportedEnergy = e[2], .accumulator = e[3] };
        Reindex();
    }
    free(loaded); return ok;
}
