#include "world/volcano_history_cache.h"
#include "world/terrain.h"
#include "world/planet_landform_strategy.h"
#include "raymath.h"
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CANDIDATE_CAPACITY (SURFACE_FACE_COUNT * 8 * 8)
#define PROFILE_CAPACITY 4
typedef struct Candidate {
    VolcanoTerrainDefinition definition;
    Vector3 direction;
    float reach;
    bool selected;
} Candidate;
typedef struct ProfileEntry {
    bool used;
    uint32_t seed;
    uint64_t fingerprint, access;
    PlanetProfile profile;
    unsigned count;
    Candidate candidates[CANDIDATE_CAPACITY];
} ProfileEntry;
typedef struct HistoryEntry {
    bool used;
    uint32_t seed;
    uint64_t fingerprint, access;
    PlanetProfile profile;
    VolcanoHistory *history;
    VolcanoHistoricalTerrain summary;
    size_t bytes;
} HistoryEntry;
static pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
static ProfileEntry profiles[PROFILE_CAPACITY];
static HistoryEntry entries[VOLCANO_HISTORY_CACHE_CAPACITY];
static VolcanoHistoryCacheStats stats;
static uint64_t clockSequence;

/* Full fields plus deterministic fingerprint; padding can never distinguish
   equivalent independently constructed profiles. No hash-only equality. */
static PlanetProfile NormalizeProfile(const PlanetProfile *p)
{
    PlanetProfile n; memset(&n, 0, sizeof(n));
#define COPY(f) n.f = p->f
    COPY(seed); COPY(canonicalBodyId); COPY(style); COPY(atmosphereType);
    COPY(physicalRadiusKm); COPY(massKg); COPY(spaceProxyRadius); COPY(surfaceGravity);
    COPY(receivedIrradiance); COPY(radiativeTempK); COPY(equilibriumTempK);
    COPY(surfacePressureAtm); COPY(atmosphereDensity); COPY(oceanCoverage);
    COPY(iceCoverage); COPY(cloudCoverage); COPY(terrainRoughness); COPY(ageGyr);
    COPY(rotationRate); COPY(tidalLockFactor); COPY(ringTilt); COPY(albedo);
    COPY(greenhouseEffect); COPY(orbitalEccentricity); COPY(orbitalMeanAnomalyAtEpoch);
    COPY(axialTilt); COPY(seasonPhase); COPY(yearLength);
    COPY(seasonalTemperatureAmplitudeK); COPY(orbitalTemperatureAmplitudeK);
    COPY(polarIceVariability); COPY(seasonalHumidityBias); COPY(prevailingWindAngle);
    COPY(windStrength); COPY(volcanicActivity); COPY(impactRate);
    COPY(hasSolidSurface); COPY(hasRings); COPY(tidallyLocked);
#undef COPY
    return n;
}
static bool Supported(const PlanetProfile *p)
{
    return p && p->hasSolidSurface && isfinite(p->volcanicActivity) && p->volcanicActivity > 0 &&
        PlanetSeaLevelForProfile(p->style, p) < 0;
}
static bool SameProfile(uint32_t aSeed, uint64_t aKey, const PlanetProfile *a,
    uint32_t seed, uint64_t key, const PlanetProfile *p)
{
    return aSeed == seed && aKey == key && memcmp(a, p, sizeof(*p)) == 0;
}
static ProfileEntry *ProfileFor(uint32_t seed, PlanetProfile p, uint64_t key)
{
    ProfileEntry *slot = &profiles[0];
    for (unsigned i = 0; i < PROFILE_CAPACITY; i++) {
        ProfileEntry *entry = &profiles[i];
        if (entry->used && SameProfile(entry->seed, entry->fingerprint, &entry->profile, seed, key, &p)) {
            entry->access = ++clockSequence; return entry;
        }
        if (!entry->used || (slot->used && entry->access < slot->access)) slot = entry;
    }
    memset(slot, 0, sizeof(*slot));
    slot->used = true; slot->seed = seed; slot->profile = p; slot->fingerprint = key;
    slot->access = ++clockSequence;
    for (SurfaceFace face = 0; face < SURFACE_FACE_COUNT; face++)
        for (int v = VOLCANO_TERRAIN_OFFSET; v < SURFACE_FACE_BLOCKS; v += VOLCANO_TERRAIN_SPACING)
            for (int u = VOLCANO_TERRAIN_OFFSET; u < SURFACE_FACE_BLOCKS; u += VOLCANO_TERRAIN_SPACING) {
                VolcanoTerrainDefinition d;
                if (!VolcanoTerrainDefine(seed, p.volcanicActivity, (SurfaceAddress){0,face,u,v,0}, &d)) continue;
                double extent = ceil((d.radius * 1.12 + 80) / 4) * 4 + 2;
                slot->candidates[slot->count++] = (Candidate){d, SurfaceAddressDirection(d.center),
                    (float)(sqrt(2) * extent + 4), true};
            }
    /* Reject any candidate whose enclosing domain intersects a lower stable
       priority. Pairwise symmetry guarantees disjoint selected domains even
       at cube edges/corners. Discovery/query order cannot change selection. */
    for (unsigned i = 0; i < slot->count; i++) for (unsigned j = 0; j < slot->count; j++) {
        Candidate *a = &slot->candidates[i], *b = &slot->candidates[j];
        if (b->definition.seed > a->definition.seed ||
            (b->definition.seed == a->definition.seed && j >= i)) continue;
        float reach = a->reach + b->reach;
        if (Vector3DistanceSqr(a->direction, b->direction) * SURFACE_RADIUS_BLOCKS * SURFACE_RADIUS_BLOCKS < reach * reach)
            a->selected = false;
    }
    return slot;
}
static double InitialGround(Vector3 direction, void *user)
{
    const ProfileEntry *p = user;
    return TerrainPlanetBaselineAtGeographic(p->seed, &p->profile,
        atan2f(direction.z, direction.x), atan2f(direction.y, hypotf(direction.x, direction.z)), NULL) + 1;
}
static HistoryEntry *HistoryFor(const ProfileEntry *p, const Candidate *candidate)
{
    HistoryEntry *slot = &entries[0];
    for (unsigned i = 0; i < VOLCANO_HISTORY_CACHE_CAPACITY; i++) {
        HistoryEntry *entry = &entries[i];
        if (entry->used && SameProfile(entry->seed, entry->fingerprint, &entry->profile,
            p->seed, p->fingerprint, &p->profile) &&
            SurfaceAddressEqual(entry->summary.definition.center, candidate->definition.center)) {
            stats.hits++; entry->access = ++clockSequence; return entry;
        }
        if (!entry->used || (slot->used && entry->access < slot->access)) slot = entry;
    }
    if (slot->used) {
        stats.evictions++; stats.residentBytes -= slot->bytes; stats.residents--;
        VolcanoHistoryDestroy(slot->history);
    }
    memset(slot, 0, sizeof(*slot));
    clock_t start = clock();
    VolcanoHistoryConfig config = VolcanoHistoryDefaults();
    unsigned attempts = 0;
    for (; attempts < 4; attempts++) {
        VolcanoHistoryDestroy(slot->history);
        slot->history = VolcanoHistoryBuild(&candidate->definition, config, InitialGround, (void *)p);
        if (!slot->history) {
            fputs("fatal: unable to build immutable volcano history\n", stderr); abort();
        }
        if (VolcanoHistoryInspect(slot->history).ledger.boundaryVolume <= 1e-6) { attempts++; break; }
        config.cooling *= 2;
    }
    stats.buildSeconds += (double)(clock() - start) / CLOCKS_PER_SEC;
    stats.builds++;
    slot->summary = (VolcanoHistoricalTerrain){.domain = true, .fitAttempts = attempts, .definition = candidate->definition,
        .model = VolcanoHistoryInspect(slot->history)};
    VolcanoHistoryStats model = slot->summary.model;
    /* A history that reaches its closed boundary is not a valid world
       footprint. Reject the entire result deterministically; retain/report
       the model ledger for diagnosis, rather than exposing a dam at the edge. */
    slot->summary.boundaryRejected = model.ledger.boundaryVolume > 1e-6;
    if (slot->summary.boundaryRejected) stats.boundaryRejected++;
    for (unsigned y = 0; y < model.side; y++) for (unsigned x = 0; x < model.side; x++) {
        VolcanoHistoryCell cell;
        VolcanoHistoryRead(slot->history, model.events - 1, x, y, &cell);
        if (!slot->summary.boundaryRejected)
            slot->summary.representedVolume += floor(cell.depth) * model.cellSize * model.cellSize;
    }
    if (slot->summary.boundaryRejected) slot->summary.rejectedVolume = model.ledger.retainedVolume;
    else slot->summary.quantizationLoss = model.ledger.retainedVolume - slot->summary.representedVolume;
    slot->bytes = VolcanoHistoryBytes(slot->history);
    stats.residentBytes += slot->bytes; stats.residents++;
    size_t peak = stats.residentBytes - slot->bytes + model.peakBytes + sizeof(entries) + sizeof(profiles);
    if (peak > stats.peakBytes) stats.peakBytes = peak;
    slot->used = true; slot->seed = p->seed; slot->profile = p->profile;
    slot->fingerprint = p->fingerprint; slot->access = ++clockSequence;
    return slot;
}
VolcanoHistoricalTerrain VolcanoHistoricalTerrainAt(uint32_t seed, const PlanetProfile *input, Vector3 direction)
{
    VolcanoHistoricalTerrain result = {0};
    if (!Supported(input) || !isfinite(direction.x) || !isfinite(direction.y) || !isfinite(direction.z) ||
        Vector3LengthSqr(direction) < .5f) return result;
    direction = Vector3Normalize(direction);
    PlanetProfile p = NormalizeProfile(input);
    uint64_t key = PlanetLandformProfileFingerprint(&p);
    pthread_mutex_lock(&mutex);
    ProfileEntry *profile = ProfileFor(seed, p, key);
    for (unsigned i = 0; i < profile->count; i++) {
        Candidate *candidate = &profile->candidates[i];
        if (!candidate->selected || Vector3DistanceSqr(direction, candidate->direction) *
            SURFACE_RADIUS_BLOCKS * SURFACE_RADIUS_BLOCKS > candidate->reach * candidate->reach) continue;
        HistoryEntry *entry = HistoryFor(profile, candidate);
        VolcanoHistoryCell cell;
        if (!VolcanoHistorySample(entry->history, direction, &cell)) continue;
        result = entry->summary;
        result.depth = result.boundaryRejected ? 0 : (unsigned)floor(cell.depth);
        result.rock = cell.rock;
        break;
    }
    pthread_mutex_unlock(&mutex);
    return result;
}
bool VolcanoHistoricalTerrainEligible(uint32_t seed, const PlanetProfile *input, SurfaceAddress center)
{
    if (!Supported(input)) return false;
    PlanetProfile p = NormalizeProfile(input);
    uint64_t key = PlanetLandformProfileFingerprint(&p);
    pthread_mutex_lock(&mutex);
    ProfileEntry *profile = ProfileFor(seed, p, key);
    center.bodyId = 0; center.radial = 0;
    bool selected = false;
    for (unsigned i = 0; i < profile->count; i++) if (SurfaceAddressEqual(center, profile->candidates[i].definition.center)) {
        selected = profile->candidates[i].selected; break;
    }
    pthread_mutex_unlock(&mutex); return selected;
}
VolcanoHistoryCacheStats VolcanoHistoryCacheInspect(void)
{
    pthread_mutex_lock(&mutex); VolcanoHistoryCacheStats value = stats;
    pthread_mutex_unlock(&mutex); return value;
}
void VolcanoHistoryCacheReset(void)
{
    pthread_mutex_lock(&mutex);
    for (unsigned i = 0; i < VOLCANO_HISTORY_CACHE_CAPACITY; i++) VolcanoHistoryDestroy(entries[i].history);
    memset(entries, 0, sizeof(entries)); memset(profiles, 0, sizeof(profiles));
    stats = (VolcanoHistoryCacheStats){0}; clockSequence = 0;
    pthread_mutex_unlock(&mutex);
}
BlockType VolcanoHistoricalRockBlock(LavaRock rock)
{
    switch (rock) {
    case LAVA_ROCK_OBSIDIAN: return BLOCK_OBSIDIAN;
    case LAVA_ROCK_SCORIA: return BLOCK_SCORIA;
    case LAVA_ROCK_PUMICE: return BLOCK_PUMICE;
    case LAVA_ROCK_RHYOLITE: return BLOCK_RHYOLITE;
    default: return BLOCK_BASALT;
    }
}
