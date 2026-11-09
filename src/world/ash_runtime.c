#include "world/ash_runtime.h"
#include "world/surface_topology.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ASH_HASH_SIZE 8192u
static AshCell cells[ASH_MAX_CELLS];
static unsigned count;
static unsigned lookup[ASH_HASH_SIZE];
static AshStats ledger;

static bool ValidAddress(SurfaceAddress address)
{
    return SurfaceAddressIsValid(address) &&
        address.radial > -1000000 && address.radial < 1000000;
}
static void SyncMapFields(AshCell *cell)
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
static SurfaceAddress MapAddress(uint32_t bodyId, int x, int y, int z)
{
    return SurfaceAddressFromMapCoordinates(
        bodyId, (float)x + 0.5f, (float)z + 0.5f, y);
}
static uint32_t HashMap(uint32_t bodyId, int x, int y, int z)
{
    return (bodyId * 0x9e3779b9u ^ (uint32_t)x * 0x85ebca6bu ^
        (uint32_t)y * 0xc2b2ae35u ^ (uint32_t)z * 0x27d4eb2fu) &
        (ASH_HASH_SIZE - 1u);
}
static uint32_t HashAddress(SurfaceAddress address)
{
    return (address.bodyId * 0x9e3779b9u ^
        (uint32_t)address.face * 0x85ebca6bu ^
        (uint32_t)address.u * 0xc2b2ae35u ^
        (uint32_t)address.v * 0x27d4eb2fu ^
        (uint32_t)address.radial * 0x165667b1u) & (ASH_HASH_SIZE - 1u);
}
static bool CellMatches(const AshCell *cell, bool addressed,
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
        const AshCell *c = &cells[lookup[slot] - 1u];
        if (CellMatches(c, addressed, address, bodyId, x, y, z)) break;
        slot = (slot + 1u) & (ASH_HASH_SIZE - 1u);
    }
    return slot;
}
static unsigned SlotCell(const AshCell *cell)
{
    if (!cell) return 0u;
    return SlotKey(cell->addressed, cell->address, cell->bodyId,
                   cell->x, cell->y, cell->z);
}
static void Reindex(void)
{
    memset(lookup, 0, sizeof(lookup));
    for (unsigned i = 0; i < count; i++) {
        AshCell *c = &cells[i];
        lookup[SlotCell(c)] = i + 1u;
    }
}
static int Compare(const void *left, const void *right)
{
    const AshCell *a = left, *b = right;
#define CMP(f) if (a->f != b->f) return a->f < b->f ? -1 : 1
    CMP(addressed); CMP(bodyId); CMP(y); CMP(z); CMP(x);
    CMP(address.face); CMP(address.radial); CMP(address.v); CMP(address.u);
#undef CMP
    return 0;
}
static AshBoundary BoundaryAt(const AshContext *ctx, SurfaceAddress address)
{
    if (!ctx) return ASH_UNKNOWN;
    if (ctx->addressBoundary) {
        return ctx->addressBoundary(address, ctx->user);
    }
    SurfaceMapCell map;
    if (!ctx->boundary ||
        !SurfaceAddressCanonicalMapCell(address, &map)) return ASH_UNKNOWN;
    return ctx->boundary(map.x, address.radial, map.z, ctx->user);
}

static double WetnessAt(const AshContext *ctx, SurfaceAddress address)
{
    if (!ctx) return 0.0;
    if (ctx->addressWetness) {
        return ctx->addressWetness(address, ctx->user);
    }
    SurfaceMapCell map;
    if (!ctx->wetness ||
        !SurfaceAddressCanonicalMapCell(address, &map)) return 0.0;
    return ctx->wetness(map.x, address.radial, map.z, ctx->user);
}

static SurfaceAddress Below(SurfaceAddress address)
{
    address.radial--;
    return address;
}

static AshBoundary CellBoundary(const AshContext *ctx, const AshCell *cell)
{
    if (!cell) return ASH_UNKNOWN;
    return cell->addressed ? BoundaryAt(ctx, cell->address) :
        (ctx && ctx->boundary ?
         ctx->boundary(cell->x, cell->y, cell->z, ctx->user) : ASH_UNKNOWN);
}

static double CellWetness(const AshContext *ctx, const AshCell *cell)
{
    if (!cell) return 0.0;
    if (cell->addressed) return WetnessAt(ctx, cell->address);
    return ctx && ctx->wetness ?
        ctx->wetness(cell->x, cell->y, cell->z, ctx->user) : 0.0;
}

void AshRuntimeReset(void)
{
    count = 0; ledger = (AshStats){ 0 };
    memset(lookup, 0, sizeof(lookup));
}
bool AshRuntimeAt(unsigned index, AshCell *out)
{
    if (!out || index >= count) return false;
    *out = cells[index]; return true;
}
bool AshRuntimeCellAtAddress(SurfaceAddress address, AshCell *out)
{
    if (!out || !ValidAddress(address)) return false;
    unsigned at = lookup[SlotKey(true, address, address.bodyId, 0, 0, 0)];
    if (!at || !cells[at - 1u].volume) return false;
    *out = cells[at - 1u]; return true;
}
bool AshRuntimeCellAt(uint32_t body, int x, int y, int z, AshCell *out)
{
    if (!out) return false;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    unsigned at = lookup[SlotKey(false, (SurfaceAddress){0}, body, x, y, z)];
    if (!at || !cells[at - 1u].volume) return false;
    *out = cells[at - 1u]; return true;
}
static int EnsureAddress(uint32_t body, SurfaceAddress address)
{
    if (address.bodyId != body) return -1;
    unsigned slot = SlotKey(true, address, body, 0, 0, 0);
    if (lookup[slot]) return (int)lookup[slot] - 1;
    if (count == ASH_MAX_CELLS) { ledger.rejected++; return -1; }
    unsigned at = count++;
    cells[at] = (AshCell){ .address = address, .addressed = true };
    SyncMapFields(&cells[at]);
    lookup[slot] = at + 1u;
    return (int)at;
}
static int EnsureMap(uint32_t body, int x, int y, int z)
{
    if (x < -1000000 || x > 1000000 || z < -1000000 || z > 1000000 ||
        y <= -1000000 || y >= 1000000) return -1;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    unsigned slot = SlotKey(false, (SurfaceAddress){0}, body, x, y, z);
    if (lookup[slot]) return (int)lookup[slot] - 1;
    if (count == ASH_MAX_CELLS) { ledger.rejected++; return -1; }
    unsigned at = count++;
    cells[at] = (AshCell){
        .bodyId = body, .x = x, .y = y, .z = z,
        .address = MapAddress(body, x, y, z), .addressed = false
    };
    lookup[slot] = at + 1u;
    return (int)at;
}
bool AshRuntimeCollect(uint32_t body, int x, int y, int z, unsigned expectedUnits)
{
    AshCell cell;
    if (!expectedUnits || !AshRuntimeCellAt(body, x, y, z, &cell) ||
        cell.volume != expectedUnits ||
        UINT64_MAX - ledger.collected < cell.volume) return false;
    unsigned index = lookup[SlotCell(&cell)] - 1u;
    ledger.collected += cell.volume;
    cells[index] = cells[--count];
    Reindex();
    return true;
}
bool AshRuntimeCollectAtAddress(SurfaceAddress address, unsigned expectedUnits)
{
    AshCell cell;
    if (!expectedUnits || !AshRuntimeCellAtAddress(address, &cell) ||
        cell.volume != expectedUnits ||
        UINT64_MAX - ledger.collected < cell.volume) return false;
    unsigned index = lookup[SlotCell(&cell)] - 1u;
    ledger.collected += cell.volume;
    cells[index] = cells[--count];
    Reindex();
    return true;
}
unsigned AshRuntimeDeposit(const AshContext *ctx, int x, int y, int z, unsigned units)
{
    if (!ctx || !ctx->valid || !ctx->boundary || !units ||
        units > ASH_CAPACITY || UINT64_MAX - ledger.supplied < units) return 0;
    SurfaceMapCell map = SurfaceCanonicalMapCell((float)x, (float)z);
    x = map.x;
    z = map.z;
    if (ctx->boundary(x, y, z, ctx->user) != ASH_OPEN) return 0;
    int at = EnsureMap(ctx->bodyId, x, y, z);
    if (at < 0) return 0;
    unsigned accepted = ASH_CAPACITY - cells[at].volume;
    if (accepted > units) accepted = units;
    cells[at].volume += accepted;
    ledger.supplied += accepted;
    return accepted;
}
unsigned AshRuntimeDepositAtAddress(const AshContext *ctx,
                                    SurfaceAddress address, unsigned units)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary) ||
        !units || units > ASH_CAPACITY || !ValidAddress(address) ||
        address.bodyId != ctx->bodyId ||
        UINT64_MAX - ledger.supplied < units) return 0;
    if (BoundaryAt(ctx, address) != ASH_OPEN) return 0;
    int at = EnsureAddress(ctx->bodyId, address);
    if (at < 0) return 0;
    unsigned accepted = ASH_CAPACITY - cells[at].volume;
    if (accepted > units) accepted = units;
    cells[at].volume += accepted;
    ledger.supplied += accepted;
    return accepted;
}
void AshRuntimeStep(const AshContext *ctx)
{
    if (!ctx || !ctx->valid || (!ctx->boundary && !ctx->addressBoundary)) return;
    ledger.ticks++;
    qsort(cells, count, sizeof(*cells), Compare); Reindex();
    unsigned original = count;
    for (unsigned i = 0; i < original; i++) {
        AshCell *c = &cells[i];
        if (!c->volume || c->bodyId != ctx->bodyId ||
            CellBoundary(ctx, c) != ASH_OPEN) continue;
        double wet = CellWetness(ctx, c);
        if (isfinite(wet) && wet > 0) {
            c->washCredit += fmin(1.0, wet) * 2.0 * ASH_STEP_SECONDS;
            unsigned washed = (unsigned)floor(c->washCredit + 1e-9);
            if (washed > c->volume) washed = c->volume;
            c->volume -= washed; ledger.washed += washed;
            c->washCredit = fmax(0.0, c->washCredit - washed);
        }
        SurfaceAddress belowAddress = Below(c->address);
        bool open = c->addressed ?
            ValidAddress(belowAddress) &&
                BoundaryAt(ctx, belowAddress) == ASH_OPEN :
            ctx->boundary(c->x, c->y - 1, c->z, ctx->user) == ASH_OPEN;
        if (!c->volume || !open) continue;
        int below = c->addressed ?
            EnsureAddress(c->bodyId, belowAddress) :
            EnsureMap(c->bodyId, c->x, c->y - 1, c->z);
        if (below < 0) continue;
        AshCell *target = &cells[below];
        unsigned moved = ASH_CAPACITY - target->volume;
        if (moved > c->volume) moved = c->volume;
        if (!target->volume) target->washCredit = c->washCredit;
        target->volume += moved; c->volume -= moved;
        /* Ascending Y plus the original count moves each parcel at most one
           cell per tick. Unknown/supporting cells never swallow its mass. */
    }
    unsigned kept = 0;
    for (unsigned i = 0; i < count; i++) if (cells[i].volume) cells[kept++] = cells[i];
    count = kept; Reindex();
}
AshStats AshRuntimeStats(void)
{
    AshStats result = ledger; result.cells = count;
    for (unsigned i = 0; i < count; i++) result.volume += cells[i].volume;
    return result;
}
static bool WriteU64(FILE *f, uint64_t value)
{
    unsigned char bytes[8];
    for (unsigned i = 0; i < 8; i++) bytes[i] = (unsigned char)(value >> (8u * i));
    return fwrite(bytes, 1, 8, f) == 8;
}
static bool ReadU64(FILE *f, uint64_t *value)
{
    unsigned char bytes[8];
    if (fread(bytes, 1, 8, f) != 8) return false;
    *value = 0;
    for (unsigned i = 0; i < 8; i++) *value |= (uint64_t)bytes[i] << (8u * i);
    return true;
}
bool AshRuntimeSave(FILE *f)
{
    if (!f || sizeof(double) != 8 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fwrite("ASH00004", 1, 8, f) != 8) return false;
    uint64_t fields[] = { count, ledger.supplied, ledger.washed, ledger.rejected, ledger.ticks };
    for (unsigned i = 0; i < 5; i++) if (!WriteU64(f, fields[i])) return false;
    if (!WriteU64(f, ledger.collected)) return false;
    for (unsigned i = 0; i < count; i++) {
        const AshCell *c = &cells[i]; uint64_t credit;
        memcpy(&credit, &c->washCredit, sizeof(credit));
        uint64_t data[] = { c->bodyId, c->addressed,
            (uint32_t)c->x, (uint32_t)c->y, (uint32_t)c->z,
            (uint32_t)c->address.face, (uint32_t)c->address.u,
            (uint32_t)c->address.v, (uint32_t)c->address.radial,
            c->volume, credit };
        for (unsigned n = 0; n < 11; n++) if (!WriteU64(f, data[n])) return false;
    }
    return !ferror(f);
}
bool AshRuntimeLoad(FILE *f)
{
    char magic[8]; uint64_t fields[5];
    if (!f || sizeof(double) != 8 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fread(magic, 1, 8, f) != 8 ||
        (memcmp(magic, "ASH00001", 8) && memcmp(magic, "ASH00002", 8) &&
         memcmp(magic, "ASH00003", 8) && memcmp(magic, "ASH00004", 8))) return false;
    bool current = !memcmp(magic, "ASH00004", 8);
    bool addressV3 = !memcmp(magic, "ASH00003", 8);
    bool collectedFormat = !memcmp(magic, "ASH00002", 8) || addressV3 || current;
    for (unsigned i = 0; i < 5; i++) if (!ReadU64(f, &fields[i])) return false;
    if (fields[0] > ASH_MAX_CELLS || fields[2] > fields[1]) return false;
    uint64_t collected = 0;
    if (collectedFormat &&
        (!ReadU64(f, &collected) || collected > fields[1] - fields[2])) return false;
    AshCell *loaded = calloc(fields[0] ? (size_t)fields[0] : 1u, sizeof(*loaded));
    if (!loaded) return false;
    bool ok = true; uint64_t volume = 0;
    for (unsigned i = 0; ok && i < fields[0]; i++) {
        uint64_t v[11];
        unsigned fieldCount = current ? 11u : addressV3 ? 7u : 6u;
        for (unsigned n = 0; n < fieldCount; n++) {
            if (!ReadU64(f, &v[n]) || (n < 5 && v[n] > UINT32_MAX)) { ok = false; break; }
        }
        if (!ok) break;
        AshCell c;
        if (current) {
            c = (AshCell){
                .bodyId = (uint32_t)v[0], .addressed = v[1] != 0u,
                .x = (int32_t)v[2], .y = (int32_t)v[3],
                .z = (int32_t)v[4],
                .address = { (uint32_t)v[0], (SurfaceFace)v[5],
                             (int32_t)v[6], (int32_t)v[7],
                             (int32_t)v[8] },
                .volume = (unsigned)v[9]
            };
            memcpy(&c.washCredit, &v[10], sizeof(c.washCredit));
            if (c.addressed) SyncMapFields(&c);
        } else if (addressV3) {
            c = (AshCell){
                .address = { (uint32_t)v[0], (SurfaceFace)v[1],
                             (int32_t)v[2], (int32_t)v[3],
                             (int32_t)v[4] },
                .volume = (unsigned)v[5], .addressed = true
            };
            memcpy(&c.washCredit, &v[6], sizeof(c.washCredit));
            SyncMapFields(&c);
        } else {
            c = (AshCell){
                .address = MapAddress((uint32_t)v[0], (int32_t)v[1],
                                      (int32_t)v[2], (int32_t)v[3]),
                .volume = (unsigned)v[4]
            };
            memcpy(&c.washCredit, &v[5], sizeof(c.washCredit));
            SyncMapFields(&c);
        }
        bool coordinateValid = c.addressed ? ValidAddress(c.address) &&
            c.address.bodyId == c.bodyId :
            c.x >= -1000000 && c.x <= 1000000 &&
            c.z >= -1000000 && c.z <= 1000000 &&
            c.y > -1000000 && c.y < 1000000;
        if (!coordinateValid || !c.volume ||
            c.volume > ASH_CAPACITY || !isfinite(c.washCredit) ||
            c.washCredit < 0 || c.washCredit >= 1) { ok = false; break; }
        volume += c.volume; loaded[i] = c;
    }
    if (ok) {
        qsort(loaded, (size_t)fields[0], sizeof(*loaded), Compare);
        for (unsigned i = 1; i < fields[0]; i++) if (!Compare(&loaded[i - 1], &loaded[i])) ok = false;
        if (volume != fields[1] - fields[2] - collected) ok = false;
    }
    if (ok) {
        memcpy(cells, loaded, (size_t)fields[0] * sizeof(*loaded)); count = (unsigned)fields[0];
        ledger = (AshStats){ .supplied = fields[1], .washed = fields[2], .rejected = fields[3],
            .ticks = fields[4], .collected = collected };
        Reindex();
    }
    free(loaded); return ok;
}
