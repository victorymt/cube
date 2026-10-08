#include "world/volcano_runtime.h"

#include <math.h>
#include <float.h>
#include <string.h>

static VolcanoRecord records[VOLCANO_MAX_RECORDS];
static unsigned recordCount;
static uint64_t rejectedAdmissions;
static double discoverySeconds;
static uint32_t discoveryBody;

void VolcanoRuntimeReset(void)
{
    memset(records, 0, sizeof(records));
    recordCount = 0u;
    rejectedAdmissions = 0u;
    discoverySeconds = 0.0;
    discoveryBody = 0u;
}

static int Find(uint64_t id)
{
    for (unsigned i = 0u; i < recordCount; i++) {
        if (records[i].definition.id == id) return (int)i;
    }
    return -1;
}

static int Admit(VolcanoDefinition definition)
{
    if (!VolcanoDefinitionValid(&definition)) return -1;
    int existing = Find(definition.id);
    if (existing >= 0) return existing;
    if (recordCount == VOLCANO_MAX_RECORDS) {
        if (rejectedAdmissions != UINT64_MAX) rejectedAdmissions++;
        return -1;
    }
    unsigned index = recordCount++;
    records[index] = (VolcanoRecord){ .definition = definition,
                                    .state = VolcanoModelCreate(&definition) };
    return (int)index;
}

bool VolcanoRuntimeForce(VolcanoDefinition definition, float intensity, unsigned ticks)
{
    if (!VolcanoDefinitionValid(&definition)) return false;
    VolcanoState candidate = VolcanoModelCreate(&definition);
    int existing = Find(definition.id);
    /* A cube-grid candidate may use a nearby integer map representative.
       Forcing that physical column must reuse its existing stable identity. */
    if (existing < 0) {
        for (unsigned i = 0u; i < recordCount; i++) {
            const VolcanoDefinition *d = &records[i].definition;
            if (d->address.bodyId == definition.address.bodyId &&
                d->seed == definition.seed && d->mapX == definition.mapX &&
                d->mapZ == definition.mapZ) {
                definition = *d;
                existing = (int)i;
                break;
            }
        }
    }
    if (existing >= 0) candidate = records[existing].state;
    if (!VolcanoModelForce(&candidate, intensity, ticks)) return false;
    int index = Admit(definition);
    if (index < 0) return false;
    records[index].state = candidate;
    return true;
}

bool VolcanoRuntimeStop(uint64_t id)
{
    int index = Find(id);
    if (index < 0) return false;
    VolcanoModelStop(&records[index].state);
    return true;
}

static double DistanceSquared(const VolcanoDefinition *d, Vector3 observer)
{
    SurfaceMapOffset offset = SurfaceShortestMapOffset(
        observer.x, observer.z, (float)d->mapX, (float)d->mapZ);
    double dy = (double)d->address.radial - observer.y;
    return (double)offset.x * offset.x + (double)offset.z * offset.z + dy * dy;
}

static bool ContextValid(const VolcanoContext *c)
{
    return c && c->valid && isfinite(c->observerMap.x) &&
        isfinite(c->observerMap.y) && isfinite(c->observerMap.z);
}

static void Discover(const VolcanoContext *c)
{
    if (!c->discover || !c->sample || !c->ready) return;
    SurfaceAddress center = SurfaceAddressFromMapCoordinates(
        c->bodyId, c->observerMap.x, c->observerMap.z, 0);
    VolcanoDefinition found[9];
    unsigned count = 0u;
    for (int z = -1; z <= 1; z++) {
        for (int x = -1; x <= 1; x++) {
            SurfaceAddress address = SurfaceAddressOffset(center,
                x * VOLCANO_CANDIDATE_SPACING, z * VOLCANO_CANDIDATE_SPACING, 0);
            VolcanoDefinition definition;
            if (!VolcanoCandidateAt(address, c->seed, c->sample, c->userData, &definition) ||
                DistanceSquared(&definition, c->observerMap) >
                    VOLCANO_ENTER_RADIUS * VOLCANO_ENTER_RADIUS ||
                !c->ready(&definition, c->userData)) continue;
            unsigned insert = count;
            while (insert > 0u && found[insert - 1u].id > definition.id) {
                found[insert] = found[insert - 1u];
                insert--;
            }
            found[insert] = definition;
            count++;
        }
    }
    for (unsigned i = 0u; i < count; i++) (void)Admit(found[i]);
}

void VolcanoRuntimeUpdate(double seconds, const VolcanoContext *c)
{
    if (!isfinite(seconds) || seconds < 0.0 || seconds > VOLCANO_STEP_SECONDS) return;
    if (!ContextValid(c)) {
        for (unsigned i = 0u; i < recordCount; i++) records[i].active = false;
        return;
    }
    if (discoveryBody != c->bodyId) {
        discoveryBody = c->bodyId;
        discoverySeconds = 0.0;
    }
    discoverySeconds += seconds;
    if (discoverySeconds >= 2.0) {
        discoverySeconds -= 2.0;
        Discover(c);
    }
    unsigned active = 0u;
    for (unsigned i = 0u; i < recordCount; i++) {
        VolcanoRecord *r = &records[i];
        if (r->definition.address.bodyId != c->bodyId || r->definition.seed != c->seed ||
            DistanceSquared(&r->definition, c->observerMap) >
                VOLCANO_LEAVE_RADIUS * VOLCANO_LEAVE_RADIUS ||
            !c->ready || !c->ready(&r->definition, c->userData)) r->active = false;
        if (r->active) active++;
    }
    while (active < VOLCANO_MAX_ACTIVE) {
        int best = -1;
        double distance = VOLCANO_ENTER_RADIUS * VOLCANO_ENTER_RADIUS;
        for (unsigned i = 0u; i < recordCount; i++) {
            VolcanoRecord *r = &records[i];
            if (r->active || r->definition.address.bodyId != c->bodyId ||
                r->definition.seed != c->seed || !c->ready ||
                !c->ready(&r->definition, c->userData)) continue;
            double candidate = DistanceSquared(&r->definition, c->observerMap);
            if (candidate < distance || (candidate == distance &&
                (best < 0 || r->definition.id < records[best].definition.id))) {
                distance = candidate;
                best = (int)i;
            }
        }
        if (best < 0) break;
        records[best].active = true;
        active++;
    }
    for (unsigned i = 0u; i < recordCount; i++) {
        VolcanoRecord *r = &records[i];
        if (!r->active) continue;
        r->accumulator += seconds;
        if (r->accumulator + 1e-9 >= VOLCANO_STEP_SECONDS) {
            r->accumulator = fmax(0.0, r->accumulator - VOLCANO_STEP_SECONDS);
            if (c->emitLava && r->state.reservoir > 0.0f &&
                (r->state.phase == VOLCANO_ERUPTING || r->state.phase == VOLCANO_WANING)) {
                unsigned requested = (unsigned)((r->definition.forced ? 128.0f : 2048.0f) * r->state.intensity *
                    (r->state.phase == VOLCANO_WANING ? 0.25f : 1.0f));
                unsigned inventory = (unsigned)(r->state.reservoir * 1024.0f);
                if (requested > inventory) requested = inventory;
                unsigned emitted = c->emitLava(&r->definition, requested, c->userData);
                if (emitted <= requested) r->state.reservoir -= (float)emitted / 1024.0f;
            }
            if (c->emitAsh && r->state.reservoir > 0.0f &&
                (r->state.phase == VOLCANO_ERUPTING || r->state.phase == VOLCANO_WANING)) {
                unsigned requested = (unsigned)(32.0f * r->state.intensity *
                    (r->state.phase == VOLCANO_WANING ? 0.25f : 1.0f));
                unsigned inventory = (unsigned)(r->state.reservoir * 1024.0f);
                if (requested > inventory) requested = inventory;
                unsigned emitted = c->emitAsh(&r->definition, requested, r->state.activeTicks, c->userData);
                if (emitted <= requested) r->state.reservoir -= (float)emitted / 1024.0f;
            }
            bool pending = r->state.phase == VOLCANO_COOLING && c->hasHotLava &&
                c->hasHotLava(&r->definition, c->userData);
            if (pending) {
                if (r->state.activeTicks != UINT64_MAX) r->state.activeTicks++;
                r->state.pressure *= 0.96f;
            } else VolcanoModelStep(&r->state, &r->definition);
        }
    }
}

void VolcanoRuntimeSolidifiedSource(uint32_t body, int x, int y, int z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell(x, z);
    for (unsigned i = 0; i < recordCount; i++) {
        VolcanoDefinition *d = &records[i].definition;
        if (d->address.bodyId == body && d->mapX == cell.x && d->mapZ == cell.z &&
            d->address.radial == y && y < 1000000) d->address.radial++;
    }
}

bool VolcanoRuntimeAt(unsigned index, VolcanoRecord *out)
{
    if (!out || index >= recordCount) return false;
    *out = records[index];
    return true;
}

bool VolcanoRuntimeNearest(uint32_t bodyId, Vector3 observer, VolcanoRecord *out)
{
    if (!out || !isfinite(observer.x) || !isfinite(observer.y) || !isfinite(observer.z)) return false;
    int best = -1;
    double distance = INFINITY;
    for (unsigned i = 0u; i < recordCount; i++) {
        if (records[i].definition.address.bodyId != bodyId) continue;
        double candidate = DistanceSquared(&records[i].definition, observer);
        if (candidate < distance || (candidate == distance &&
            (best < 0 || records[i].definition.id < records[best].definition.id))) {
            best = (int)i;
            distance = candidate;
        }
    }
    if (best < 0) return false;
    *out = records[best];
    return true;
}

VolcanoStats VolcanoRuntimeStats(void)
{
    VolcanoStats stats = { .count = recordCount, .rejectedAdmissions = rejectedAdmissions };
    for (unsigned i = 0u; i < recordCount; i++) stats.active += records[i].active;
    return stats;
}

/* Explicit little-endian scalar encoding: no pointers, struct padding or ABI. */
static bool WriteU64(FILE *f, uint64_t value)
{
    unsigned char bytes[8];
    for (unsigned i = 0u; i < 8u; i++) bytes[i] = (unsigned char)(value >> (8u * i));
    return fwrite(bytes, 1u, 8u, f) == 8u;
}
static bool ReadU64(FILE *f, uint64_t *out)
{
    unsigned char bytes[8];
    if (fread(bytes, 1u, 8u, f) != 8u) return false;
    *out = 0u;
    for (unsigned i = 0u; i < 8u; i++) *out |= (uint64_t)bytes[i] << (8u * i);
    return true;
}
static bool WriteReal(FILE *f, double value)
{
    uint64_t bits;
    memcpy(&bits, &value, sizeof(bits));
    return WriteU64(f, bits);
}
static bool ReadReal(FILE *f, double *value)
{
    uint64_t bits;
    if (!ReadU64(f, &bits)) return false;
    memcpy(value, &bits, sizeof(bits));
    return isfinite(*value);
}

bool VolcanoRuntimeSave(FILE *file)
{
    if (!file || sizeof(double) != 8u || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fwrite("VOLCANO1", 1u, 8u, file) != 8u ||
        !WriteU64(file, recordCount) || !WriteU64(file, rejectedAdmissions) ||
        !WriteU64(file, discoveryBody) || !WriteReal(file, discoverySeconds)) return false;
    for (unsigned i = 0u; i < recordCount; i++) {
        const VolcanoRecord *r = &records[i];
        const VolcanoDefinition *d = &r->definition;
        const VolcanoState *s = &r->state;
        uint64_t fields[] = { d->id, d->seed, d->address.bodyId, d->address.face,
            d->address.u, d->address.v, (uint32_t)d->address.radial,
            (uint32_t)d->mapX, (uint32_t)d->mapZ, d->forced, s->phase,
            s->phaseTicks, s->eruptionTicks, s->event, s->activeTicks, r->active };
        for (unsigned n = 0u; n < sizeof(fields) / sizeof(fields[0]); n++) {
            if (!WriteU64(file, fields[n])) return false;
        }
        if (!WriteReal(file, d->potential) || !WriteReal(file, s->intensity) ||
            !WriteReal(file, s->pressure) || !WriteReal(file, s->reservoir) ||
            !WriteReal(file, r->accumulator)) return false;
    }
    return !ferror(file);
}

bool VolcanoRuntimeLoad(FILE *file)
{
    char magic[8];
    uint64_t count, rejected, body;
    double discovery;
    VolcanoRecord loaded[VOLCANO_MAX_RECORDS] = { 0 };
    if (!file || sizeof(double) != 8u || DBL_MANT_DIG != 53 || DBL_MAX_EXP != 1024 ||
        fread(magic, 1u, 8u, file) != 8u ||
        memcmp(magic, "VOLCANO1", 8u) || !ReadU64(file, &count) ||
        count > VOLCANO_MAX_RECORDS || !ReadU64(file, &rejected) ||
        !ReadU64(file, &body) || body > UINT32_MAX || !ReadReal(file, &discovery) ||
        discovery < 0.0 || discovery >= 2.0) return false;
    unsigned active = 0u;
    for (unsigned i = 0u; i < count; i++) {
        uint64_t v[16];
        double r[5];
        for (unsigned n = 0u; n < 16u; n++) {
            if (!ReadU64(file, &v[n]) || (n != 0u && n != 14u && v[n] > UINT32_MAX)) return false;
        }
        for (unsigned n = 0u; n < 5u; n++) if (!ReadReal(file, &r[n])) return false;
        if (v[9] > 1u || v[15] > 1u || v[3] >= SURFACE_FACE_COUNT ||
            v[4] >= SURFACE_FACE_BLOCKS || v[5] >= SURFACE_FACE_BLOCKS ||
            v[10] > VOLCANO_RECOVERY || r[4] < 0.0 || r[4] >= VOLCANO_STEP_SECONDS) return false;
        VolcanoRecord *record = &loaded[i];
        record->definition = (VolcanoDefinition){
            .id = v[0], .seed = (uint32_t)v[1],
            .address = { (uint32_t)v[2], (SurfaceFace)v[3], (int)v[4], (int)v[5], (int32_t)v[6] },
            .mapX = (int32_t)v[7], .mapZ = (int32_t)v[8], .forced = v[9] != 0u,
            .potential = (float)r[0] };
        record->state = (VolcanoState){ .phase = (VolcanoPhase)v[10],
            .phaseTicks = (uint32_t)v[11], .eruptionTicks = (uint32_t)v[12],
            .event = (uint32_t)v[13], .activeTicks = v[14],
            .intensity = (float)r[1], .pressure = (float)r[2], .reservoir = (float)r[3] };
        record->active = v[15] != 0u;
        record->accumulator = r[4];
        if (!VolcanoDefinitionValid(&record->definition) || !VolcanoStateValid(&record->state)) return false;
        active += record->active;
        if (active > VOLCANO_MAX_ACTIVE) return false;
        for (unsigned previous = 0u; previous < i; previous++) {
            if (loaded[previous].definition.id == record->definition.id) return false;
        }
    }
    memcpy(records, loaded, sizeof(records));
    recordCount = (unsigned)count;
    rejectedAdmissions = rejected;
    discoveryBody = (uint32_t)body;
    discoverySeconds = discovery;
    return true;
}
