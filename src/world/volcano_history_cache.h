#ifndef VOXELCRAFT_VOLCANO_HISTORY_CACHE_H
#define VOXELCRAFT_VOLCANO_HISTORY_CACHE_H
#include "world/volcano_history.h"
#include "space/planet_profile.h"
#include "world/world_types.h"

#define VOLCANO_HISTORY_CACHE_CAPACITY 8u
#define TERRAIN_GENERATION_VOLCANO_HISTORY 11u
typedef struct VolcanoHistoricalTerrain {
    bool domain, boundaryRejected;
    unsigned depth;
    unsigned fitAttempts;
    LavaRock rock;
    VolcanoTerrainDefinition definition;
    VolcanoHistoryStats model;
    /* Constant-height cells quantized downward. Volumes are measured in the
       generator's tangent-plane block units, not spherical physical metres. */
    double representedVolume, quantizationLoss, rejectedVolume;
} VolcanoHistoricalTerrain;
typedef struct VolcanoHistoryCacheStats {
    uint64_t hits, builds, evictions, boundaryRejected;
    size_t residentBytes, peakBytes;
    unsigned residents;
    double buildSeconds;
} VolcanoHistoryCacheStats;

/* Synchronous memoization of immutable values. No runtime/world/origin reads.
   Only dry solid planets are supported. Results are copied under the cache
   lock; no borrowed reference can outlive its entry. Allocation failure is
   fatal rather than publishing a transient terrain without its deposits. */
VolcanoHistoricalTerrain VolcanoHistoricalTerrainAt(uint32_t seed,
    const PlanetProfile *profile, Vector3 direction);
bool VolcanoHistoricalTerrainEligible(uint32_t seed, const PlanetProfile *profile,
    SurfaceAddress center);
VolcanoHistoryCacheStats VolcanoHistoryCacheInspect(void);
/* Safe even with concurrent readers: serialized with build/publication. */
void VolcanoHistoryCacheReset(void);
BlockType VolcanoHistoricalRockBlock(LavaRock rock);

#endif
