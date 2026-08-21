#include "world/chunks_internal.h"

static uint64_t chunkLodTargetChanges = 0u;
static bool chunkLodCoarseAllowed = false;

bool MeshPendingLookupVisit(MeshPendingLookup *lookup, int slot, int y,
                            ChunkLodLevel lod, bool insert)
{
    uint32_t hash = (uint32_t)slot * 0x9e3779b1u ^ (uint32_t)y ^
                    (uint32_t)lod * 0x85ebca6bu;
    for (int probe = 0; probe < MESH_PENDING_LOOKUP_CAPACITY; probe++) {
        int at = (int)((hash + (uint32_t)probe) %
                       MESH_PENDING_LOOKUP_CAPACITY);
        if (!lookup->used[at] && insert) {
            lookup->used[at] = true;
            lookup->slots[at] = slot;
            lookup->ys[at] = y;
            lookup->lods[at] = lod;
        }
        if (!lookup->used[at] ||
            (lookup->slots[at] == slot && lookup->ys[at] == y &&
             lookup->lods[at] == lod)) {
            return lookup->used[at];
        }
    }
    return false;
}

ChunkLodLevel ChunkLodSanitize(ChunkLodLevel lod)
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

    current = ChunkLodSanitize(current);
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

bool ChunkSectionLodReady(const ChunkSection *section, ChunkLodLevel lod)
{
    if (!section) return false;
    lod = ChunkLodSanitize(lod);
    if (lod == CHUNK_LOD_EXACT) {
        return section->exactModelReady &&
               section->exactModelStamp == section->dirtyStamp;
    }
    return section->lodModelReady &&
           section->lodModelStamp == section->dirtyStamp &&
           section->lodModelLevel == lod;
}

void ChunkRefreshActiveLod(Chunk *chunk)
{
    if (!chunk || !chunk->loaded || chunk->sectionCount == 0) return;
    ChunkLodLevel target = ChunkLodSanitize(chunk->targetLod);
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        if (!ChunkSectionLodReady(chunk->sections[sectionIndex], target)) {
            return;
        }
    }
    chunk->activeLod = target;
}

const Model *ChunksSectionSolidModel(const Chunk *chunk,
                                     const ChunkSection *section)
{
    if (!chunk || !section) return NULL;
    ChunkLodLevel active = ChunkLodSanitize(chunk->activeLod);
    if (active == CHUNK_LOD_EXACT) {
        if (ChunkSectionLodReady(section, active)) {
            return section->hasModel ? &section->model : NULL;
        }
        if (section->hasModel) return &section->model;
        return section->hasLodModel ? &section->lodModel : NULL;
    }
    if (ChunkSectionLodReady(section, active)) {
        return section->hasLodModel ? &section->lodModel : NULL;
    }
    if (section->hasLodModel && section->lodModelLevel == active) {
        return &section->lodModel;
    }
    if (section->hasModel) return &section->model;
    return section->hasLodModel ? &section->lodModel : NULL;
}

static void QueueChunkLodTarget(Chunk *chunk, ChunkLodLevel target)
{
    if (!chunk) return;
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        ChunkSection *section = chunk->sections[sectionIndex];
        if (ChunkSectionLodReady(section, target)) {
            section->dirty = false;
            section->dirtySinceMs = 0.0;
            continue;
        }
        if (!section->dirty) section->dirtySinceMs = ChunkNowMs();
        section->dirty = true;
    }
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
        ChunkLodLevel previous = ChunkLodSanitize(chunk->targetLod);
        ChunkLodLevel next = ChunkLodSelect(
            previous, chunk->lodInitialized,
            ChunkGridDistanceFrom(chunk, focusCx, focusCz), coarseAllowed);
        if (chunk->lodInitialized && previous != next) {
            chunkLodTargetChanges++;
        }
        chunk->targetLod = next;
        chunk->activeLod = ChunkLodSanitize(chunk->activeLod);
        chunk->lodInitialized = true;
        QueueChunkLodTarget(chunk, next);
        ChunkRefreshActiveLod(chunk);
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
        ChunkLodLevel target = ChunkLodSanitize(chunk->targetLod);
        ChunkLodLevel active = ChunkLodSanitize(chunk->activeLod);
        stats.targetChunks[target]++;
        stats.activeChunks[active]++;
        for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
             sectionIndex++) {
            const ChunkSection *section = chunk->sections[sectionIndex];
            for (ChunkLodLevel lod = CHUNK_LOD_EXACT;
                 lod < CHUNK_LOD_COUNT; lod++) {
                if (ChunkSectionLodReady(section, lod)) {
                    stats.readySections[lod]++;
                }
            }
        }
    }
    pthread_mutex_lock(&genMutex);
    for (int index = 0; index < MAX_MESH_JOBS; index++) {
        const MeshJob *job = &meshJobs[index];
        if (!job->inUse) continue;
        stats.pendingJobs[ChunkLodSanitize(job->lod)]++;
    }
    pthread_mutex_unlock(&genMutex);
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
