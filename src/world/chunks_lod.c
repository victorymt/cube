#include "world/chunks_internal.h"

static uint64_t chunkLodTargetChanges = 0u;
static bool chunkLodCoarseAllowed = false;

static ChunkLodLevel SanitizeChunkLod(ChunkLodLevel lod)
{
    if (lod < CHUNK_LOD_EXACT || lod >= CHUNK_LOD_COUNT) {
        return CHUNK_LOD_EXACT;
    }
    return lod;
}

ChunkLodLevel ChunkLodSelect(ChunkLodLevel current, bool initialized,
                             int distance, bool coarseAllowed)
{
    if (!coarseAllowed) return CHUNK_LOD_EXACT;
    if (distance < 0) distance = 0;

    ChunkLodLevel base = distance <= CHUNK_LOD_EXACT_MAX_DISTANCE
        ? CHUNK_LOD_EXACT
        : (distance <= CHUNK_LOD_HALF_MAX_DISTANCE
               ? CHUNK_LOD_HALF : CHUNK_LOD_QUARTER);
    if (!initialized) return base;

    current = SanitizeChunkLod(current);
    if (current == CHUNK_LOD_EXACT) {
        return distance > CHUNK_LOD_EXACT_MAX_DISTANCE +
                              CHUNK_LOD_HYSTERESIS
            ? base : CHUNK_LOD_EXACT;
    }
    if (current == CHUNK_LOD_HALF) {
        if (distance < CHUNK_LOD_EXACT_MAX_DISTANCE) {
            return CHUNK_LOD_EXACT;
        }
        if (distance > CHUNK_LOD_HALF_MAX_DISTANCE +
                           CHUNK_LOD_HYSTERESIS) {
            return CHUNK_LOD_QUARTER;
        }
        return CHUNK_LOD_HALF;
    }
    return distance < CHUNK_LOD_HALF_MAX_DISTANCE
        ? base : CHUNK_LOD_QUARTER;
}

void ChunksUpdateLodTargets(Vector3 focusPosition, bool coarseAllowed)
{
    int focusCx = 0;
    int focusCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal((int)floorf(focusPosition.x),
                      (int)floorf(focusPosition.z),
                      &focusCx, &focusCz, &localX, &localZ);
    chunkLodCoarseAllowed = coarseAllowed;

    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        ChunkLodLevel previous = SanitizeChunkLod(chunk->targetLod);
        ChunkLodLevel next = ChunkLodSelect(
            previous, chunk->lodInitialized,
            ChunkGridDistanceFrom(chunk, focusCx, focusCz), coarseAllowed);
        if (chunk->lodInitialized && previous != next) {
            chunkLodTargetChanges++;
        }
        chunk->targetLod = next;
        chunk->activeLod = SanitizeChunkLod(chunk->activeLod);
        chunk->lodInitialized = true;
    }
}

ChunkLodStats ChunksGetLodStats(void)
{
    ChunkLodStats stats = {
        .targetChanges = chunkLodTargetChanges,
        .coarseAllowed = chunkLodCoarseAllowed
    };
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        const Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        ChunkLodLevel target = SanitizeChunkLod(chunk->targetLod);
        ChunkLodLevel active = SanitizeChunkLod(chunk->activeLod);
        stats.targetChunks[target]++;
        stats.activeChunks[active]++;
    }
    return stats;
}

void ChunksResetLodState(void)
{
    chunkLodTargetChanges = 0u;
    chunkLodCoarseAllowed = false;
}

#ifdef CHUNKS_TESTING
void ChunksTestUpdateLodTargets(Vector3 focusPosition, bool coarseAllowed)
{
    ChunksUpdateLodTargets(focusPosition, coarseAllowed);
}
#endif
