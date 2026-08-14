#include "chunks.h"
#include "world_environment.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

TerrainMode terrainMode = TERRAIN_VARIED;

void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    (void)chunk;
    (void)cx;
    (void)cz;
    (void)mode;
    assert(!"generation queue overflow fell back to synchronous terrain");
}

void ApplyEditsToChunk(Chunk *chunk)
{
    (void)chunk;
}

WorldBlockRegion WorldBlockRegionAt(int y)
{
    (void)y;
    return WORLD_BLOCK_REGION_SURFACE;
}

bool WorldCanAccessBlockY(int y)
{
    (void)y;
    return true;
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

BlockType NetherBlockAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
    return BLOCK_AIR;
}

static void AssertFreshStats(void)
{
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.generationSubmitted == 0);
    assert(stats.meshSubmitted == 0);
    assert(stats.meshSnapshotBytes == 0);
    assert(stats.syncRebuilds == 0);
}

static void TestSingleChunkUsesSingleJobAndNearestFirst(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 4, 4, true, true);
    ChunksTestConfigureChunk(1, 0, 0, true, true);
    ChunksTestConfigureChunk(2, 2, 0, true, true);

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    const size_t waterBoundaryBytes =
        (size_t)(4 * SURFACE_SECTION_HEIGHT * CHUNK_SIZE +
                 2 * CHUNK_SIZE * CHUNK_SIZE) *
        (sizeof(unsigned short) + sizeof(unsigned char));
    assert(stats.meshSubmitted == 3);
    assert(stats.meshSnapshotBytes ==
           3u * (sizeof(unsigned short[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]) +
                 sizeof(unsigned char[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]) +
                 waterBoundaryBytes));
    assert(stats.syncRebuilds == 0);
    assert(ChunksTestMeshJobSlot(0) == 1);
    assert(ChunksTestMeshJobSlot(1) == 2);
    assert(ChunksTestMeshJobSlot(2) == 0);
}

static void TestFullQueueLeavesDirtyChunkForLater(void)
{
    ChunksTestResetScheduler();
    for (int i = 0; i < 64; i++) {
        ChunksTestSeedMeshJob(i, i % MAX_ACTIVE_CHUNKS, i, 0, false);
    }
    ChunksTestConfigureChunk(0, 0, 0, true, true);

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.meshSubmitted == 0);
    assert(stats.syncRebuilds == 0);
    assert(ChunksTestChunkDirty(0));
}

static void TestFullGenerationQueueDoesNotFallBackToMainThread(void)
{
    ChunksTestResetScheduler();
    ChunksTestFillGenerationQueue();

    UpdateChunks((Vector3){ 0.5f, 80.0f, 0.5f },
                 MIN_RENDER_DISTANCE_CHUNKS);

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.generationSubmitted == 0);
    assert(stats.generationCompleted == 0);
    assert(stats.generationQueuePeak == MAX_CHUNK_GEN_JOBS);
    assert(GetActiveChunkCount() == 0);
}

static void TestBudgetAndInvalidSlotCleanup(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(1, 1, 1, true, false);
    ChunksTestSeedMeshJob(0, -1, 0, 0, true);
    ChunksTestSeedMeshJob(1, 1, 1, 1, true);

    ProcessFinishedMeshJobs(0.0);

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.meshCanceled == 1);
    assert(stats.uploadedMeshes == 0);
    assert(stats.uploadBudgetDeferrals == 1);
    assert(ChunksTestMeshJobSlot(0) == -1);
    assert(ChunksTestMeshJobSlot(1) == 1);
}

// H-1: an edit made after a mesh job snapshot must keep the section dirty so
// the edit is not silently lost when the stale snapshot is uploaded.
static int FindPendingMeshJobFor(int slotIndex, int sectionY)
{
    for (int i = 0; i < MAX_CHUNK_MESH_JOBS; i++) {
        if (ChunksTestMeshJobSlot(i) == slotIndex &&
            ChunksTestMeshJobSectionY(i) == sectionY) return i;
    }
    return -1;
}

static void TestEditDuringFlightKeepsSectionDirty(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);

    // The player edits the section after the snapshot was taken: the content
    // revision bumps (MarkSectionDirty semantics) while dirty stays set.
    assert(chunks[0].sections[0] != NULL);
    chunks[0].sections[0]->dirtyStamp++;
    assert(chunks[0].sections[0]->dirty);

    ChunksTestCompleteMeshJob(jobIndex);
    ProcessFinishedMeshJobs(0.0);

    // The uploaded mesh predates the edit, so the dirty flag must survive to
    // trigger a rebuild with the new content on the next frame. The stale
    // snapshot is discarded so it cannot flash an obsolete water border.
    assert(ChunksTestMeshJobSlot(jobIndex) == -1);
    assert(ChunksTestChunkDirty(0));
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.uploadedMeshes == 0);
    assert(stats.meshCanceled >= 1);
}

// H-3: a completed mesh job from a previous chunk incarnation must be
// discarded instead of uploaded over freshly generated terrain at the same
// coordinates, and it must not clear the new section's dirty flag.
static void TestStaleJobDiscardedAfterChunkReload(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    chunks[0].generation = 1u;  // the slot was used before

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);

    // Evict the chunk and regenerate it into the same slot with the same
    // coordinates (new incarnation).
    ChunkClearBlockStorage(&chunks[0]);
    chunks[0].generation = 2u;
    chunks[0].loaded = true;
    ChunkSection *fresh = ChunkGetSection(&chunks[0], 0, true);
    assert(fresh != NULL);
    fresh->dirty = true;

    ChunksTestCompleteMeshJob(jobIndex);
    ProcessFinishedMeshJobs(0.0);

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(ChunksTestMeshJobSlot(jobIndex) == -1);
    assert(stats.meshCanceled >= 1);
    // The stale job must not have cleared the fresh section's dirty flag.
    assert(ChunksTestChunkDirty(0));
}

typedef struct WaterMeshBuildProbe {
    int jobIndex;
    bool passed;
} WaterMeshBuildProbe;

static void *BuildWaterMeshRepeatedly(void *argument)
{
    WaterMeshBuildProbe *probe = argument;
    probe->passed = true;
    for (int iteration = 0; iteration < 64; iteration++) {
        if (ChunksTestBuildWaterMeshJob(probe->jobIndex) != 5 * 6) {
            probe->passed = false;
            break;
        }
    }
    return NULL;
}

static void TestWaterMeshUsesSnapshottedNeighborBoundary(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    ChunksTestConfigureChunk(1, 1, 0, true, false);
    ChunkSection *current = ChunkGetSection(&chunks[0], 0, true);
    ChunkSection *east = ChunkGetSection(&chunks[1], 0, true);
    assert(current && east);
    current->blocks[CHUNK_SIZE - 1][1][8] = BLOCK_WATER;
    east->blocks[0][1][8] = BLOCK_WATER;

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);

    ChunkClearBlockStorage(&chunks[1]);
    chunks[1].loaded = true;
    assert(ChunksTestBuildWaterMeshJob(jobIndex) == 5 * 6);

    WaterMeshBuildProbe probe = { .jobIndex = jobIndex };
    pthread_t builder;
    assert(pthread_create(&builder, NULL, BuildWaterMeshRepeatedly, &probe) == 0);
    for (int iteration = 0; iteration < 64; iteration++) {
        ChunkClearBlockStorage(&chunks[1]);
        chunks[1].loaded = true;
        east = ChunkGetSection(&chunks[1], 0, true);
        assert(east);
        east->blocks[0][1][8] = BLOCK_WATER;
    }
    assert(pthread_join(builder, NULL) == 0);
    assert(probe.passed);
}

int main(void)
{
    memset(chunks, 0, sizeof(chunks));
    AssertFreshStats();
    TestSingleChunkUsesSingleJobAndNearestFirst();
    TestFullQueueLeavesDirtyChunkForLater();
    TestFullGenerationQueueDoesNotFallBackToMainThread();
    TestBudgetAndInvalidSlotCleanup();
    TestEditDuringFlightKeepsSectionDirty();
    TestStaleJobDiscardedAfterChunkReload();
    TestWaterMeshUsesSnapshottedNeighborBoundary();
    ChunksTestResetScheduler();
    puts("chunk streaming tests passed");
    return 0;
}
