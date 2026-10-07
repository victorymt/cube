#include "world/landform_runtime.h"

#include "world/terrain_generation.h"
#include "world/world.h"
#include "world/world_surface_snapshot.h"

#include "raymath.h"

#include <math.h>
#include <string.h>

#define LANDFORM_RUNTIME_CACHE_CAPACITY 4

typedef struct LandformRuntimeKey {
    WorldSurfaceKind surfaceKind;
    uint32_t bodyId;
    uint32_t seed;
    uint32_t terrainVersion;
    uint32_t strategyId;
    uint64_t profileFingerprint;
    uint64_t physicsFingerprint;
    uint64_t strategyFingerprint;
} LandformRuntimeKey;

typedef struct LandformRuntimeCacheEntry {
    LandformAtlas *atlas;
    PlanetLandformStrategy strategy;
    LandformRuntimeKey key;
} LandformRuntimeCacheEntry;

static LandformRuntimeCacheEntry cache[LANDFORM_RUNTIME_CACHE_CAPACITY];
static LandformRuntimeCacheEntry *active;
static unsigned replacement;

static LandformRuntimeKey KeyForInput(const LandformRuntimeInput *input)
{
    return (LandformRuntimeKey){
        .surfaceKind = input->surfaceKind,
        .bodyId = input->bodyId,
        .seed = input->seed,
        .terrainVersion = input->terrainVersion,
        .strategyId = input->strategy.id,
        .profileFingerprint = PlanetLandformProfileFingerprint(
            &input->strategy.profile),
        .physicsFingerprint = LandformPhysicsFingerprint(
            input->strategy.physics),
        .strategyFingerprint = PlanetLandformStrategyFingerprint(&input->strategy)
    };
}

static bool KeyEqual(LandformRuntimeKey a, LandformRuntimeKey b)
{
    return a.surfaceKind == b.surfaceKind && a.bodyId == b.bodyId &&
           a.seed == b.seed && a.terrainVersion == b.terrainVersion &&
           a.strategyId == b.strategyId &&
           a.profileFingerprint == b.profileFingerprint &&
           a.physicsFingerprint == b.physicsFingerprint &&
           a.strategyFingerprint == b.strategyFingerprint;
}

static bool ContextMatchesKey(const WorldSurfaceContext *context,
                              const LandformRuntimeKey *key)
{
    if (!context || !key) return false;
    if (context->kind == WORLD_SURFACE_NONE) {
        return key->surfaceKind == WORLD_SURFACE_HOME &&
               key->bodyId == 0 && key->seed == WorldGetSeed();
    }
    return context->active && context->kind == key->surfaceKind &&
           context->bodyId == key->bodyId && context->seed == key->seed;
}

static bool CurrentContextMatchesKey(const LandformRuntimeKey *key)
{
    if (!key) return false;
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    WorldSurfaceContext context = snapshot->context;
    if (!ContextMatchesKey(&context, key)) return false;
    WorldSurfaceKind kind = context.kind == WORLD_SURFACE_NONE
        ? WORLD_SURFACE_HOME : context.kind;
    uint32_t seed = context.kind == WORLD_SURFACE_NONE
        ? key->seed : context.seed;
    const PlanetProfile *profile = kind == WORLD_SURFACE_PLANET
        ? &snapshot->planetProfile : NULL;
    PlanetLandformStrategy strategy = PlanetLandformStrategyForSurface(
        kind, seed, profile);
    return strategy.id == key->strategyId &&
           PlanetLandformProfileFingerprint(&strategy.profile) ==
               key->profileFingerprint &&
           LandformPhysicsFingerprint(strategy.physics) ==
               key->physicsFingerprint;
}

static LandformRuntimeCacheEntry *CurrentEntry(void)
{
    if (!active) return NULL;
    if (TerrainGenerationVersion() < TERRAIN_GENERATION_LANDFORMS ||
        active->key.terrainVersion != TerrainGenerationVersion()) return NULL;
    if (active->key.surfaceKind == WORLD_SURFACE_HOME &&
        WorldTerrainMode() == TERRAIN_FLAT) return NULL;
    if (!CurrentContextMatchesKey(&active->key)) return NULL;
    return active;
}

bool LandformRuntimePrepareSurface(const LandformRuntimeInput *input)
{
    active = NULL;
    if (!input || input->terrainVersion < TERRAIN_GENERATION_LANDFORMS ||
        !input->enabled || input->surfaceKind == WORLD_SURFACE_NONE ||
        input->strategy.id == LANDFORM_STRATEGY_NONE ||
        !(input->strategy.capabilities & LANDFORM_HAS_DRAINAGE)) {
        return true;
    }

    LandformRuntimeKey key = KeyForInput(input);
    for (unsigned i = 0; i < LANDFORM_RUNTIME_CACHE_CAPACITY; i++) {
        if (cache[i].atlas && KeyEqual(cache[i].key, key)) {
            active = &cache[i];
            return true;
        }
    }

    PlanetLandformStrategy strategy = input->strategy;
    LandformBuildInput build = PlanetLandformBuildInput(&strategy);
    LandformAtlas *atlas = LandformBuildWithInput(&build);
    if (!atlas) return false;
    atlas->terraces = input->terrainVersion >= 8;

    unsigned slot = LANDFORM_RUNTIME_CACHE_CAPACITY;
    for (unsigned i = 0; i < LANDFORM_RUNTIME_CACHE_CAPACITY; i++) {
        if (!cache[i].atlas) {
            slot = i;
            break;
        }
    }
    if (slot == LANDFORM_RUNTIME_CACHE_CAPACITY) {
        slot = replacement++ % LANDFORM_RUNTIME_CACHE_CAPACITY;
    }
    LandformDestroy(cache[slot].atlas);
    cache[slot] = (LandformRuntimeCacheEntry){
        .atlas = atlas, .strategy = strategy, .key = key
    };
    active = &cache[slot];
    return true;
}

bool LandformRuntimePrepare(uint32_t seed, uint32_t version, bool enabled)
{
    PlanetLandformStrategy strategy = HomeworldLandformStrategy(seed);
    return LandformRuntimePrepareSurface(&(LandformRuntimeInput){
        .surfaceKind = WORLD_SURFACE_HOME,
        .bodyId = 0,
        .seed = seed,
        .terrainVersion = version,
        .enabled = enabled,
        .strategy = strategy
    });
}

const LandformAtlas *LandformRuntimeAtlas(void)
{
    LandformRuntimeCacheEntry *entry = CurrentEntry();
    return entry ? entry->atlas : NULL;
}

const PlanetLandformStrategy *LandformRuntimeStrategy(void)
{
    LandformRuntimeCacheEntry *entry = CurrentEntry();
    return entry ? &entry->strategy : NULL;
}

uint32_t LandformRuntimeCapabilities(void)
{
    const PlanetLandformStrategy *strategy = LandformRuntimeStrategy();
    return strategy ? strategy->capabilities : LANDFORM_CAPABILITY_NONE;
}

Vector3 LandformRuntimeDirectionAt(int x, int z)
{
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)x, (float)z);
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    if (snapshot->context.kind != WORLD_SURFACE_PLANET) {
        return SurfaceDirectionFromMapCoordinates(
            (float)cell.x + .5f, (float)cell.z + .5f);
    }
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(cell.x, cell.z, &longitude, &latitude);
    float cosine = cosf(latitude);
    return (Vector3){ cosine * cosf(longitude), sinf(latitude),
                      cosine * sinf(longitude) };
}

bool LandformRuntimePhysicsDiagnosticAt(int x, int z,
                                        LandformPhysicsDiagnostic *out)
{
    if (!out) return false;
    *out = (LandformPhysicsDiagnostic){ 0 };
    const LandformAtlas *atlas = LandformRuntimeAtlas();
    if (!atlas) return false;
    Vector3 direction = LandformRuntimeDirectionAt(x, z);
    uint32_t cellId = LandformCellAtDirection(direction);
    if (cellId >= LANDFORM_CELL_COUNT) return false;
    const LandformCell *cell = &atlas->cells[cellId];
    uint32_t downstream = cell->downstream == LANDFORM_NONE
        ? cellId : cell->downstream;
    const LandformCell *next = &atlas->cells[downstream];
    double blockScale = atlas->physics.metersPerBlock;
    double area = fmax((double)cell->area, 0.0) * blockScale * blockScale;
    double discharge = LandformDischarge(
        area, fmax((double)cell->runoff, 0.0) * 1e-5);
    double width = fmax((double)cell->width, 1.0) * blockScale;
    double depth = fmax(2.0 * blockScale, 0.01);
    double reach = Vector3Distance(cell->direction, next->direction) *
                   atlas->physics.radiusMeters;
    double latitude = asin(Clamp(direction.y, -1.0f, 1.0f));
    *out = LandformPhysicsDiagnose(atlas->physics, latitude, discharge,
                                   width, depth, reach);
    return out->valid;
}

void LandformRuntimeReset(void)
{
    active = NULL;
    for (unsigned i = 0; i < LANDFORM_RUNTIME_CACHE_CAPACITY; i++) {
        LandformDestroy(cache[i].atlas);
    }
    memset(cache, 0, sizeof(cache));
    replacement = 0;
}

void LandformApplyTerrainSample(int x, int z, SurfaceTerrainSample *sample)
{
    if (!sample) return;
    const LandformAtlas *atlas = LandformRuntimeAtlas();
    if (!atlas) return;
    SurfaceMapCell cell = SurfaceCanonicalMapCell((float)x, (float)z);
    float baseline = sample->elevation;
    const WorldSurfaceSnapshot *snapshot = WorldCurrentSurfaceSnapshot();
    if (snapshot->context.kind == WORLD_SURFACE_PLANET) {
        sample->seaLevel = atlas->seaLevel;
    }
    sample->landform = LandformSampleAt(
        atlas, LandformRuntimeDirectionAt(cell.x, cell.z), baseline);
    if (snapshot->context.kind == WORLD_SURFACE_PLANET &&
        !(active->strategy.capabilities & LANDFORM_HAS_SEDIMENT))
        sample->landform.sedimentDepth = 0;
    sample->elevation = sample->landform.elevation;
}
