#include "chunks.h"
#include "terrain.h"
#include "world.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

TerrainMode terrainMode = TERRAIN_VARIED;

uint32_t WorldCurrentSurfaceId(void)
{
    return 1u;
}

bool HomeWorldSurfaceIsActive(void)
{
    return true;
}

bool WorldIsSurfaceActive(void)
{
    return true;
}

bool ShipBlockIsParkedCore(BlockType type)
{
    (void)type;
    return false;
}

void ShipForgetParkedAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
}

void ShipTrackParkedAt(int x, int y, int z)
{
    (void)x;
    (void)y;
    (void)z;
}

BlockType TerrainBaseBlockAt(int x, int y, int z, TerrainMode mode)
{
    (void)x;
    (void)z;
    (void)mode;
    return y >= 0 && y <= 8 ? BLOCK_STONE : BLOCK_AIR;
}

bool GenerateChunkTerrainSectionBase(
    Chunk *chunk, int cx, int cz, int sectionY, TerrainMode mode)
{
    if (!chunk || ChunkGetSectionConst(chunk, sectionY)) return false;
    int firstY = sectionY * SURFACE_SECTION_HEIGHT;
    int lastY = firstY + SURFACE_SECTION_HEIGHT;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = firstY; y < lastY; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                BlockType type = TerrainBaseBlockAt(
                    cx * CHUNK_SIZE + lx, y, cz * CHUNK_SIZE + lz, mode);
                if (type != BLOCK_AIR &&
                    !ChunkSetLocalBlock(chunk, lx, y, lz, type)) {
                    return false;
                }
            }
        }
    }
    return ChunkMarkTerrainSectionResolved(chunk, sectionY);
}

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

void ApplyEditsToChunkSection(Chunk *chunk, int sectionY)
{
    for (int index = 0; index < WorldGetEditCount(); index++) {
        BlockEdit edit;
        if (!WorldGetEditForCurrentDimension(index, &edit) ||
            edit.y / SURFACE_SECTION_HEIGHT != sectionY) {
            continue;
        }
        int cx = 0;
        int cz = 0;
        int lx = 0;
        int lz = 0;
        WorldToChunkLocal(edit.x, edit.z, &cx, &cz, &lx, &lz);
        if (cx == chunk->cx && cz == chunk->cz) {
            assert(ChunkSetLocalBlock(chunk, lx, edit.y, lz, edit.type));
        }
    }
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

void SpaceSetBlock(int x, int y, int z, BlockType type)
{
    (void)x;
    (void)y;
    (void)z;
    (void)type;
}

void NetherSetBlock(int x, int y, int z, BlockType type)
{
    (void)x;
    (void)y;
    (void)z;
    (void)type;
}

static void AssertFreshStats(void)
{
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.generationSubmitted == 0);
    assert(stats.meshSubmitted == 0);
    assert(stats.meshSnapshotBytes == 0);
    assert(stats.syncRebuilds == 0);
}

static Camera3D TestPerspectiveCamera(float fovY)
{
    return (Camera3D){
        .position = { 0.0f, 0.0f, 0.0f },
        .target = { 0.0f, 0.0f, 1.0f },
        .up = { 0.0f, 1.0f, 0.0f },
        .fovy = fovY,
        .projection = CAMERA_PERSPECTIVE
    };
}

static void TestFrustumSphereEdgesRemainVisible(void)
{
    Camera3D camera = TestPerspectiveCamera(90.0f);
    const float aspect = 16.0f / 9.0f;
    const float radius = sqrtf(8.0f * 8.0f * 3.0f);
    const float depth = 32.0f;
    const float verticalTan = tanf(camera.fovy * DEG2RAD * 0.5f);
    const float horizontalTan = verticalTan * aspect;
    const float horizontalLimit = depth * horizontalTan +
        radius * sqrtf(1.0f + horizontalTan * horizontalTan);
    const float verticalLimit = depth * verticalTan +
        radius * sqrtf(1.0f + verticalTan * verticalTan);

    assert(ChunksTestSphereInFrustum(
        &camera, (Vector3){ horizontalLimit - 0.05f, 0.0f, depth },
        radius, aspect));
    assert(!ChunksTestSphereInFrustum(
        &camera, (Vector3){ horizontalLimit + 0.05f, 0.0f, depth },
        radius, aspect));
    assert(ChunksTestSphereInFrustum(
        &camera, (Vector3){ 0.0f, verticalLimit - 0.05f, depth },
        radius, aspect));
    assert(!ChunksTestSphereInFrustum(
        &camera, (Vector3){ 0.0f, verticalLimit + 0.05f, depth },
        radius, aspect));
}

static void TestFrustumNearPlaneAndRenderAspect(void)
{
    Camera3D camera = TestPerspectiveCamera(90.0f);
    assert(!ChunksTestSphereInFrustum(
        &camera, (Vector3){ 0.0f, 0.0f, -2.0f }, 1.0f, 1.0f));
    assert(ChunksTestSphereInFrustum(
        &camera, (Vector3){ 0.0f, 0.0f, -0.5f }, 1.0f, 1.0f));

    const float logicalAspect = 1280.0f / 720.0f;
    const float renderAspect = 1692.0f / 926.0f;
    Vector3 edgeCenter = { 182.0f, 0.0f, 100.0f };
    assert(!ChunksTestSphereInFrustum(
        &camera, edgeCenter, 1.0f, logicalAspect));
    assert(ChunksTestSphereInFrustum(
        &camera, edgeCenter, 1.0f, renderAspect));
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
    ChunkSection *editedSection = ChunkGetSection(&chunks[0], 0, false);
    assert(editedSection != NULL);
    editedSection->dirtyStamp++;
    assert(editedSection->dirty);

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

static void TestSparseSignedSectionStorage(void)
{
    ChunksTestResetScheduler();
    Chunk *chunk = &chunks[0];

    ChunkSection *high = ChunkGetSection(chunk, 700, true);
    ChunkSection *below = ChunkGetSection(chunk, -4, true);
    ChunkSection *middle = ChunkGetSection(chunk, 3, true);
    assert(high && below && middle);
    assert(chunk->sectionCount == 3);
    assert(chunk->sections[0] == below);
    assert(chunk->sections[1] == middle);
    assert(chunk->sections[2] == high);
    assert(ChunkGetSection(chunk, -4, false) == below);
    assert(ChunkGetSectionConst(chunk, 700) == high);
    assert(ChunkGetSectionConst(chunk, 699) == NULL);

    assert(ChunkMarkTerrainSectionResolved(chunk, 700));
    assert(ChunkMarkTerrainSectionResolved(chunk, -4));
    assert(ChunkMarkTerrainSectionResolved(chunk, 3));
    assert(ChunkMarkTerrainSectionResolved(chunk, 3));
    assert(chunk->resolvedTerrainSectionCount == 3);
    assert(chunk->resolvedTerrainSectionYs[0] == -4);
    assert(chunk->resolvedTerrainSectionYs[1] == 3);
    assert(chunk->resolvedTerrainSectionYs[2] == 700);

    chunk->loaded = true;
    below->dirty = true;
    high->dirty = true;
    RebuildDirtyChunkMeshes((Vector3){ 0.0f, -63.0f, 0.0f });
    assert(ChunksTestMeshJobSectionY(0) == -4);
    assert(ChunksTestMeshJobSectionY(1) == 700);

    ChunkClearBlockStorage(chunk);
    assert(chunk->sections == NULL);
    assert(chunk->sectionCount == 0);
    assert(chunk->sectionCapacity == 0);
    assert(chunk->resolvedTerrainSectionYs == NULL);
    assert(chunk->resolvedTerrainSectionCount == 0);
    assert(chunk->resolvedTerrainSectionCapacity == 0);
}

static void TestMaterializedAirDiffersFromMissingSection(void)
{
    ChunksTestResetScheduler();
    Chunk *chunk = &chunks[0];
    BlockType block = BLOCK_STONE;

    assert(!ChunkTryGetLocalBlock(chunk, 2, 17, 3, &block));
    assert(block == BLOCK_STONE);
    assert(ChunkGetLocalBlock(chunk, 2, 17, 3) == BLOCK_AIR);

    ChunkSection *section = ChunkGetSection(chunk, 1, true);
    assert(section != NULL);
    assert(ChunkTryGetLocalBlock(chunk, 2, 17, 3, &block));
    assert(block == BLOCK_AIR);

    section->blocks[2][1][3] = BLOCK_DIRT;
    assert(ChunkTryGetLocalBlock(chunk, 2, 17, 3, &block));
    assert(block == BLOCK_DIRT);

    assert(!ChunkTryGetLocalBlock(chunk, -1, 17, 3, &block));
    assert(!ChunkTryGetLocalBlock(chunk, 2, SURFACE_WORLD_HEIGHT, 3,
                                  &block));
    assert(!ChunkTryGetLocalBlock(chunk, 2, 17, 3, NULL));
}

static void TestImplicitTerrainLookupAndEditOverride(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);

    assert(ChunkGetSectionConst(&chunks[0], 0) == NULL);
    assert(GetBlockAt(2, 4, 3) == BLOCK_STONE);
    assert(GetBlockAt(2, 12, 3) == BLOCK_AIR);

    assert(SetBlock(2, 4, 3, BLOCK_AIR));
    assert(ChunkGetSectionConst(&chunks[0], 0) != NULL);
    assert(GetBlockAt(2, 4, 3) == BLOCK_AIR);
    assert(GetBlockAt(3, 4, 3) == BLOCK_STONE);

    assert(SetBlock(2 * CHUNK_SIZE, 4, 0, BLOCK_GLASS));
    ChunksTestConfigureChunk(1, 2, 0, true, false);
    assert(ChunkGetSectionConst(&chunks[1], 0) == NULL);
    assert(GetBlockAt(2 * CHUNK_SIZE, 4, 0) == BLOCK_GLASS);
    assert(GetBlockAt(2 * CHUNK_SIZE + 1, 4, 0) == BLOCK_STONE);

    assert(SetBlock(2 * CHUNK_SIZE + 1, 4, 0, BLOCK_AIR));
    assert(ChunkGetSectionConst(&chunks[1], 0) != NULL);
    assert(GetBlockAt(2 * CHUNK_SIZE, 4, 0) == BLOCK_GLASS);
    assert(GetBlockAt(2 * CHUNK_SIZE + 1, 4, 0) == BLOCK_AIR);
    assert(GetBlockAt(2 * CHUNK_SIZE + 2, 4, 0) == BLOCK_STONE);
}

static void TestSectionGenerationJobsStageAndValidateResults(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 7, 0, true, false);
    chunks[0].generation = 11u;

    assert(RequestChunkTerrainSection(7, 0, 0));
    assert(!RequestChunkTerrainSection(7, 0, 0));
    assert(GetPendingGenJobCount() == 1);
    assert(ChunksTestGenerationJobSectionY(0) == 0);
    ChunksTestRunGenerationJob(0);
    assert(ChunkGetSectionConst(&chunks[0], 0) == NULL);
    ProcessFinishedChunkJobs();

    const ChunkSection *generated = ChunkGetSectionConst(&chunks[0], 0);
    assert(generated != NULL);
    assert(generated->blocks[4][4][5] == BLOCK_STONE);
    assert(generated->dirty);
    assert(GetPendingGenJobCount() == 0);

    assert(RequestChunkTerrainSection(7, 1, 0));
    assert(ChunksTestGenerationJobSectionY(0) == 1);
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], 1) == NULL);
    assert(ChunkTerrainSectionIsResolved(&chunks[0], 1));
    assert(!RequestChunkTerrainSection(7, 1, 0));

    ChunksTestConfigureChunk(1, 8, 0, true, false);
    chunks[1].generation = 21u;
    assert(RequestChunkTerrainSection(8, 0, 0));
    ChunksTestRunGenerationJob(0);
    chunks[1].generation++;
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[1], 0) == NULL);
    assert(ChunksGetStreamingStats().generationCanceled == 1u);
}

static void TestNearbySectionSchedulingPrioritizesPlayerSection(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    chunks[0].generation = 31u;

    int submitted = ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f });
    assert(submitted == 3);
    assert(ChunksTestGenerationJobSectionY(0) == 1);
    assert(ChunksTestGenerationJobSectionY(1) == 0);
    assert(ChunksTestGenerationJobSectionY(2) == 2);
    assert(ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f }) == 0);
}

int main(void)
{
    memset(chunks, 0, sizeof(chunks));
    AssertFreshStats();
    TestFrustumSphereEdgesRemainVisible();
    TestFrustumNearPlaneAndRenderAspect();
    TestSingleChunkUsesSingleJobAndNearestFirst();
    TestFullQueueLeavesDirtyChunkForLater();
    TestFullGenerationQueueDoesNotFallBackToMainThread();
    TestBudgetAndInvalidSlotCleanup();
    TestEditDuringFlightKeepsSectionDirty();
    TestStaleJobDiscardedAfterChunkReload();
    TestWaterMeshUsesSnapshottedNeighborBoundary();
    TestSparseSignedSectionStorage();
    TestMaterializedAirDiffersFromMissingSection();
    TestImplicitTerrainLookupAndEditOverride();
    TestSectionGenerationJobsStageAndValidateResults();
    TestNearbySectionSchedulingPrioritizesPlayerSection();
    ChunksTestResetScheduler();
    puts("chunk streaming tests passed");
    return 0;
}
