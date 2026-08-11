#include "space_query_cache.h"

#include <math.h>
#include <pthread.h>
#include <string.h>

#define SPACE_QUERY_CACHE_WAYS 4
#define SPACE_QUERY_DEFINITION_CACHE_SETS 512
#define SPACE_QUERY_RUNTIME_CACHE_SETS 128

typedef struct SpaceQueryDefinitionCacheEntry {
    bool valid;
    uint32_t worldSeed;
    int anchorX;
    int anchorZ;
    uint64_t age;
    SolarSystemDef value;
} SpaceQueryDefinitionCacheEntry;

typedef struct SpaceQueryRuntimeCacheEntry {
    bool valid;
    uint32_t worldSeed;
    int anchorX;
    int anchorZ;
    uint64_t systemSignature;
    double simulationTime;
    uint64_t age;
    SolarSystemRuntimeState value;
} SpaceQueryRuntimeCacheEntry;

static SpaceQueryDefinitionCacheEntry definitionCache[
    SPACE_QUERY_DEFINITION_CACHE_SETS * SPACE_QUERY_CACHE_WAYS];
static SpaceQueryRuntimeCacheEntry runtimeCache[
    SPACE_QUERY_RUNTIME_CACHE_SETS * SPACE_QUERY_CACHE_WAYS];
static SpaceQueryCacheStats cacheStats;
static uint64_t cacheAge;
static pthread_mutex_t cacheMutex = PTHREAD_MUTEX_INITIALIZER;

static uint32_t SpaceQueryCacheHash(uint32_t worldSeed, int anchorX,
                                    int anchorZ)
{
    uint32_t hash = worldSeed ^ (uint32_t)anchorX * 0x9e3779b9u ^
                    (uint32_t)anchorZ * 0x85ebca6bu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16);
}

static uint64_t SpaceQueryCacheNextAge(void)
{
    cacheAge++;
    if (cacheAge == 0) cacheAge = 1;
    return cacheAge;
}

static SpaceQueryDefinitionCacheEntry *SpaceQueryDefinitionSlot(
    uint32_t worldSeed, int anchorX, int anchorZ, bool *hit)
{
    uint32_t set = SpaceQueryCacheHash(worldSeed, anchorX, anchorZ) &
                   (SPACE_QUERY_DEFINITION_CACHE_SETS - 1u);
    SpaceQueryDefinitionCacheEntry *base =
        &definitionCache[set * SPACE_QUERY_CACHE_WAYS];
    SpaceQueryDefinitionCacheEntry *oldest = &base[0];
    for (int way = 0; way < SPACE_QUERY_CACHE_WAYS; way++) {
        SpaceQueryDefinitionCacheEntry *entry = &base[way];
        if (entry->valid && entry->worldSeed == worldSeed &&
            entry->anchorX == anchorX && entry->anchorZ == anchorZ) {
            *hit = true;
            return entry;
        }
        if (!entry->valid || entry->age < oldest->age) oldest = entry;
    }
    *hit = false;
    return oldest;
}

static SpaceQueryRuntimeCacheEntry *SpaceQueryRuntimeSlot(
    uint32_t worldSeed, int anchorX, int anchorZ, uint64_t systemSignature,
    double simulationTime, bool *hit)
{
    uint32_t signatureHash = (uint32_t)systemSignature ^
                             (uint32_t)(systemSignature >> 32);
    uint32_t set = (SpaceQueryCacheHash(worldSeed, anchorX, anchorZ) ^
                    signatureHash) &
                   (SPACE_QUERY_RUNTIME_CACHE_SETS - 1u);
    SpaceQueryRuntimeCacheEntry *base =
        &runtimeCache[set * SPACE_QUERY_CACHE_WAYS];
    SpaceQueryRuntimeCacheEntry *oldest = &base[0];
    for (int way = 0; way < SPACE_QUERY_CACHE_WAYS; way++) {
        SpaceQueryRuntimeCacheEntry *entry = &base[way];
        if (entry->valid && entry->worldSeed == worldSeed &&
            entry->anchorX == anchorX && entry->anchorZ == anchorZ &&
            entry->systemSignature == systemSignature &&
            entry->simulationTime == simulationTime) {
            *hit = true;
            return entry;
        }
        if (!entry->valid || entry->age < oldest->age) oldest = entry;
    }
    *hit = false;
    return oldest;
}

bool SpaceQueryDefinitionCacheGet(uint32_t worldSeed, int anchorX,
                                  int anchorZ, SolarSystemDef *out)
{
    if (!out) return false;
    *out = (SolarSystemDef){ 0 };
    pthread_mutex_lock(&cacheMutex);
    bool hit = false;
    SpaceQueryDefinitionCacheEntry *entry = SpaceQueryDefinitionSlot(
        worldSeed, anchorX, anchorZ, &hit);
    if (hit) {
        *out = entry->value;
        entry->age = SpaceQueryCacheNextAge();
        cacheStats.definitionHits++;
    } else {
        cacheStats.definitionMisses++;
    }
    pthread_mutex_unlock(&cacheMutex);
    return hit;
}

void SpaceQueryDefinitionCachePut(uint32_t worldSeed, int anchorX,
                                  int anchorZ, const SolarSystemDef *value)
{
    if (!value) return;
    pthread_mutex_lock(&cacheMutex);
    bool hit = false;
    SpaceQueryDefinitionCacheEntry *entry = SpaceQueryDefinitionSlot(
        worldSeed, anchorX, anchorZ, &hit);
    entry->valid = true;
    entry->worldSeed = worldSeed;
    entry->anchorX = anchorX;
    entry->anchorZ = anchorZ;
    entry->age = SpaceQueryCacheNextAge();
    entry->value = *value;
    pthread_mutex_unlock(&cacheMutex);
}

bool SpaceQueryRuntimeCacheGet(uint32_t worldSeed, int anchorX, int anchorZ,
                               uint64_t systemSignature, double simulationTime,
                               SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!isfinite(simulationTime)) return false;
    pthread_mutex_lock(&cacheMutex);
    bool hit = false;
    SpaceQueryRuntimeCacheEntry *entry = SpaceQueryRuntimeSlot(
        worldSeed, anchorX, anchorZ, systemSignature, simulationTime, &hit);
    if (hit) {
        *out = entry->value;
        entry->age = SpaceQueryCacheNextAge();
        cacheStats.runtimeHits++;
    } else {
        cacheStats.runtimeMisses++;
    }
    pthread_mutex_unlock(&cacheMutex);
    return hit;
}

void SpaceQueryRuntimeCachePut(uint32_t worldSeed, int anchorX, int anchorZ,
                               uint64_t systemSignature, double simulationTime,
                               const SolarSystemRuntimeState *value)
{
    if (!value || !isfinite(simulationTime)) return;
    pthread_mutex_lock(&cacheMutex);
    bool hit = false;
    SpaceQueryRuntimeCacheEntry *entry = SpaceQueryRuntimeSlot(
        worldSeed, anchorX, anchorZ, systemSignature, simulationTime, &hit);
    entry->valid = true;
    entry->worldSeed = worldSeed;
    entry->anchorX = anchorX;
    entry->anchorZ = anchorZ;
    entry->systemSignature = systemSignature;
    entry->simulationTime = simulationTime;
    entry->age = SpaceQueryCacheNextAge();
    entry->value = *value;
    pthread_mutex_unlock(&cacheMutex);
}

void SpaceQueryCacheClear(void)
{
    pthread_mutex_lock(&cacheMutex);
    memset(definitionCache, 0, sizeof(definitionCache));
    memset(runtimeCache, 0, sizeof(runtimeCache));
    memset(&cacheStats, 0, sizeof(cacheStats));
    cacheAge = 0;
    pthread_mutex_unlock(&cacheMutex);
}

SpaceQueryCacheStats SpaceQueryCacheGetStats(void)
{
    pthread_mutex_lock(&cacheMutex);
    SpaceQueryCacheStats result = cacheStats;
    pthread_mutex_unlock(&cacheMutex);
    return result;
}
