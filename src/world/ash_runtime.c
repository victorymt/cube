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

static bool Coordinate(int x, int y, int z)
{
    return x >= -1000000 && x <= 1000000 && z >= -1000000 && z <= 1000000 &&
        y > -1000000 && y < 1000000;
}
static void Canonical(int *x, int *z)
{
    SurfaceMapCell c = SurfaceCanonicalMapCell((float)*x, (float)*z);
    *x = c.x; *z = c.z;
}
static unsigned Slot(uint32_t body, int x, int y, int z)
{
    unsigned slot = (body * 0x9e3779b9u ^ (uint32_t)x * 0x85ebca6bu ^
        (uint32_t)y * 0xc2b2ae35u ^ (uint32_t)z * 0x27d4eb2fu) & (ASH_HASH_SIZE - 1u);
    while (lookup[slot]) {
        const AshCell *c = &cells[lookup[slot] - 1u];
        if (c->bodyId == body && c->x == x && c->y == y && c->z == z) break;
        slot = (slot + 1u) & (ASH_HASH_SIZE - 1u);
    }
    return slot;
}
static void Reindex(void)
{
    memset(lookup, 0, sizeof(lookup));
    for (unsigned i = 0; i < count; i++) {
        AshCell *c = &cells[i];
        lookup[Slot(c->bodyId, c->x, c->y, c->z)] = i + 1u;
    }
}
static int Compare(const void *left, const void *right)
{
    const AshCell *a = left, *b = right;
#define CMP(f) if (a->f != b->f) return a->f < b->f ? -1 : 1
    CMP(bodyId); CMP(y); CMP(z); CMP(x);
#undef CMP
    return 0;
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
bool AshRuntimeCellAt(uint32_t body, int x, int y, int z, AshCell *out)
{
    if (!out || !Coordinate(x, y, z)) return false;
    Canonical(&x, &z);
    unsigned at = lookup[Slot(body, x, y, z)];
    if (!at || !cells[at - 1u].volume) return false;
    *out = cells[at - 1u]; return true;
}
static int Ensure(uint32_t body, int x, int y, int z)
{
    unsigned slot = Slot(body, x, y, z);
    if (lookup[slot]) return (int)lookup[slot] - 1;
    if (count == ASH_MAX_CELLS) { ledger.rejected++; return -1; }
    unsigned at = count++;
    cells[at] = (AshCell){ .bodyId = body, .x = x, .y = y, .z = z };
    lookup[slot] = at + 1u;
    return (int)at;
}
bool AshRuntimeCollect(uint32_t body, int x, int y, int z, unsigned expectedUnits)
{
    AshCell cell;
    if (!expectedUnits || !AshRuntimeCellAt(body, x, y, z, &cell) || cell.volume != expectedUnits ||
        UINT64_MAX - ledger.collected < cell.volume) return false;
    unsigned index = lookup[Slot(body, cell.x, cell.y, cell.z)] - 1u;
    ledger.collected += cell.volume;
    cells[index] = cells[--count];
    Reindex();
    return true;
}
unsigned AshRuntimeDeposit(const AshContext *ctx, int x, int y, int z, unsigned units)
{
    if (!ctx || !ctx->valid || !ctx->boundary || !units || units > ASH_CAPACITY ||
        !Coordinate(x, y, z) || UINT64_MAX - ledger.supplied < units) return 0;
    Canonical(&x, &z);
    if (ctx->boundary(x, y, z, ctx->user) != ASH_OPEN) return 0;
    int at = Ensure(ctx->bodyId, x, y, z);
    if (at < 0) return 0;
    unsigned accepted = ASH_CAPACITY - cells[at].volume;
    if (accepted > units) accepted = units;
    cells[at].volume += accepted;
    ledger.supplied += accepted;
    return accepted;
}
void AshRuntimeStep(const AshContext *ctx)
{
    if (!ctx || !ctx->valid || !ctx->boundary) return;
    ledger.ticks++;
    qsort(cells, count, sizeof(*cells), Compare); Reindex();
    unsigned original = count;
    for (unsigned i = 0; i < original; i++) {
        AshCell *c = &cells[i];
        if (!c->volume || c->bodyId != ctx->bodyId ||
            ctx->boundary(c->x, c->y, c->z, ctx->user) != ASH_OPEN) continue;
        double wet = ctx->wetness ? ctx->wetness(c->x, c->y, c->z, ctx->user) : 0;
        if (isfinite(wet) && wet > 0) {
            c->washCredit += fmin(1.0, wet) * 2.0 * ASH_STEP_SECONDS;
            unsigned washed = (unsigned)floor(c->washCredit + 1e-9);
            if (washed > c->volume) washed = c->volume;
            c->volume -= washed; ledger.washed += washed;
            c->washCredit = fmax(0.0, c->washCredit - washed);
        }
        if (!c->volume || !Coordinate(c->x, c->y - 1, c->z) ||
            ctx->boundary(c->x, c->y - 1, c->z, ctx->user) != ASH_OPEN) continue;
        int below = Ensure(c->bodyId, c->x, c->y - 1, c->z);
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
        fwrite("ASH00002", 1, 8, f) != 8) return false;
    uint64_t fields[] = { count, ledger.supplied, ledger.washed, ledger.rejected, ledger.ticks };
    for (unsigned i = 0; i < 5; i++) if (!WriteU64(f, fields[i])) return false;
    if (!WriteU64(f, ledger.collected)) return false;
    for (unsigned i = 0; i < count; i++) {
        const AshCell *c = &cells[i]; uint64_t credit;
        memcpy(&credit, &c->washCredit, sizeof(credit));
        uint64_t data[] = { c->bodyId, (uint32_t)c->x, (uint32_t)c->y, (uint32_t)c->z, c->volume, credit };
        for (unsigned n = 0; n < 6; n++) if (!WriteU64(f, data[n])) return false;
    }
    return !ferror(f);
}
bool AshRuntimeLoad(FILE *f)
{
    char magic[8]; uint64_t fields[5];
    if (!f || sizeof(double) != 8 || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fread(magic, 1, 8, f) != 8 ||
        (memcmp(magic, "ASH00001", 8) && memcmp(magic, "ASH00002", 8))) return false;
    for (unsigned i = 0; i < 5; i++) if (!ReadU64(f, &fields[i])) return false;
    if (fields[0] > ASH_MAX_CELLS || fields[2] > fields[1]) return false;
    uint64_t collected = 0;
    if (magic[7] == '2' && (!ReadU64(f, &collected) || collected > fields[1] - fields[2])) return false;
    AshCell *loaded = calloc(fields[0] ? (size_t)fields[0] : 1u, sizeof(*loaded));
    if (!loaded) return false;
    bool ok = true; uint64_t volume = 0;
    for (unsigned i = 0; ok && i < fields[0]; i++) {
        uint64_t v[6];
        for (unsigned n = 0; n < 6; n++) {
            if (!ReadU64(f, &v[n]) || (n < 5 && v[n] > UINT32_MAX)) { ok = false; break; }
        }
        if (!ok) break;
        AshCell c = { .bodyId = (uint32_t)v[0], .x = (int32_t)v[1], .y = (int32_t)v[2],
            .z = (int32_t)v[3], .volume = (unsigned)v[4] };
        memcpy(&c.washCredit, &v[5], sizeof(c.washCredit));
        if (!Coordinate(c.x, c.y, c.z)) { ok = false; break; }
        int x = c.x, z = c.z; Canonical(&x, &z);
        if (!Coordinate(c.x, c.y, c.z) || x != c.x || z != c.z || !c.volume ||
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
