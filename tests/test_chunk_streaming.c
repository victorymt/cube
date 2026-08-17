#include "world/chunks.h"

#define chunks (ChunksMutableForTesting())
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "world/world_extension.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

TerrainMode terrainMode = TERRAIN_VARIED;

static int sectionLoadedNotifications = 0;
static int sectionUnloadPreparations = 0;
static int lastSectionLoaded = 0;
static int lastSectionPrepared = 0;
static bool terrainSectionFacesExposed = true;

static void TestOnChunkSectionLoaded(Chunk *chunk, int sectionY)
{
    assert(chunk != NULL);
    sectionLoadedNotifications++;
    lastSectionLoaded = sectionY;
}

static bool TestPrepareChunkSectionUnload(Chunk *chunk, int sectionY)
{
    assert(chunk != NULL);
    sectionUnloadPreparations++;
    lastSectionPrepared = sectionY;
    return true;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 1u;
}

int WorldSurfaceMapOriginX(void)
{
    return 0;
}

int WorldSurfaceMapOriginZ(void)
{
    return 0;
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

bool TerrainSectionHasExposedFaces(const ChunkSection *section, int cx,
                                   int cz, int sectionY, TerrainMode mode)
{
    (void)cx;
    (void)cz;
    (void)mode;
    return section && section->sectionY == sectionY &&
           terrainSectionFacesExposed;
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
            SurfaceSectionYFromBlockY(edit.y) != sectionY) {
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

static void TestChunkFrustumUsesSparseSectionHeights(void)
{
    Camera3D camera = TestPerspectiveCamera(90.0f);
    camera.position = (Vector3){ 8.0f, -472.0f, 0.0f };
    camera.target = (Vector3){ 8.0f, -472.0f, 1.0f };
    Chunk chunk = { .cx = 0, .cz = 2 };
    ChunkSection *deep = ChunkGetSection(&chunk, -30, true);
    assert(deep != NULL);
    assert(ChunkSectionIntersectsCameraView(&chunk, deep, &camera));
    assert(ChunkIntersectsCameraView(&chunk, &camera));
    assert(!ChunkIntersectsCameraView(NULL, &camera));
    assert(!ChunkIntersectsCameraView(&chunk, NULL));
    ChunkClearBlockStorage(&chunk);
}

static void TestSingleChunkUsesSingleJobAndNearestFirst(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 4, 4, true, true);
    ChunksTestConfigureChunk(1, 0, 0, true, true);
    ChunksTestConfigureChunk(2, 2, 0, true, true);

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    const size_t boundaryBytes =
        (size_t)(CHUNK_SIZE + 2) * (SURFACE_SECTION_HEIGHT + 2) *
        (CHUNK_SIZE + 2) *
        (sizeof(unsigned short) + 2 * sizeof(unsigned char));
    assert(stats.meshSubmitted == 3);
    assert(stats.meshSnapshotBytes ==
           3u * (sizeof(unsigned short[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]) +
                 sizeof(unsigned char[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE]) +
                 boundaryBytes));
    assert(stats.syncRebuilds == 0);
    assert(ChunksTestMeshJobSlot(0) == 1);
    assert(ChunksTestMeshJobSlot(1) == 2);
    assert(ChunksTestMeshJobSlot(2) == 0);
}

static void TestFullQueueLeavesDirtyChunkForLater(void)
{
    ChunksTestResetScheduler();
    for (int i = 0; i < 64; i++) {
        ChunksTestSeedMeshJob(
            i, i % MAX_ACTIVE_CHUNKS, i, 0, 0, false);
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
    ChunksTestSeedMeshJob(0, -1, 0, 0, 0, true);
    ChunksTestSeedMeshJob(1, 1, 1, 1, 0, true);

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

static int BuildSparseWaterBoundaryMesh(bool vertical, bool resolved)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    ChunkSection *current = ChunkGetSection(&chunks[0], 0, false);
    assert(current);

    if (vertical) {
        current->blocks[8][SURFACE_SECTION_HEIGHT - 1][8] = BLOCK_WATER;
        if (resolved) {
            assert(ChunkMarkTerrainSectionResolved(&chunks[0], 1));
        }
    } else {
        ChunksTestConfigureChunk(1, 1, 0, true, false);
        current->blocks[CHUNK_SIZE - 1][1][8] = BLOCK_WATER;
        if (resolved) {
            assert(ChunkMarkTerrainSectionResolved(&chunks[1], 0));
        }
    }

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);
    return ChunksTestBuildWaterMeshJob(jobIndex);
}

static void TestSparseWaterBoundariesPreserveUnknownSections(void)
{
    assert(BuildSparseWaterBoundaryMesh(false, false) == 5 * 6);
    assert(BuildSparseWaterBoundaryMesh(false, true) == 6 * 6);
    assert(BuildSparseWaterBoundaryMesh(true, false) == 5 * 6);
    assert(BuildSparseWaterBoundaryMesh(true, true) == 6 * 6);
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
    assert(!ChunkTryGetLocalBlock(chunk, 2, SURFACE_MAX_Y_EXCLUSIVE, 3,
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

    assert(GetBlockAt(2, -1, 3) == BLOCK_AIR);
    assert(SetBlock(2, -1, 3, BLOCK_GLASS));
    assert(ChunkGetSectionConst(&chunks[0], -1) != NULL);
    assert(GetBlockAt(2, -1, 3) == BLOCK_GLASS);
    assert(GetBlockAt(3, -1, 3) == BLOCK_AIR);

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
    SurfaceAddress jobAddress = { 0 };
    assert(ChunksTestGenerationJobSurfaceAddress(0, &jobAddress));
    assert(SurfaceAddressEqual(jobAddress, chunks[0].surfaceAddress));
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

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 9, 0, true, false);
    chunks[0].generation = 22u;
    assert(RequestChunkTerrainSection(9, 0, 0));
    ChunksTestRunGenerationJob(0);
    chunks[0].surfaceAddress.bodyId++;
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], 0) == NULL);
    assert(ChunksGetStreamingStats().generationCanceled == 1u);
}

static void TestNearbySectionSchedulingPrioritizesPlayerSection(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    chunks[0].generation = 31u;

    int submitted = ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f }, 2);
    assert(submitted == 3);
    assert(ChunksTestGenerationJobSectionY(0) == 1);
    assert(ChunksTestGenerationJobSectionY(1) == 0);
    assert(ChunksTestGenerationJobSectionY(2) == 2);
    assert(ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f }, 2) == 0);

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    chunks[0].generation = 32u;
    submitted = ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, -0.5f, 0.5f }, 2);
    assert(submitted == 3);
    assert(ChunksTestGenerationJobSectionY(0) == -1);
    assert(ChunksTestGenerationJobSectionY(1) == -2);
    assert(ChunksTestGenerationJobSectionY(2) == 0);
}

static void TestNearbySectionSchedulingUsesRenderDistance(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    ChunksTestConfigureChunk(1, 2, 0, true, false);
    chunks[0].generation = 41u;
    chunks[1].generation = 42u;

    int submitted = ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f }, 2);
    assert(submitted == 6);
    assert(ChunksTestGenerationJobSlot(0) == 0);
    assert(ChunksTestGenerationJobSlot(1) == 0);
    assert(ChunksTestGenerationJobSlot(2) == 0);
    assert(ChunksTestGenerationJobSlot(3) == 1);
    assert(ChunksTestGenerationJobSlot(4) == 1);
    assert(ChunksTestGenerationJobSlot(5) == 1);

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    ChunksTestConfigureChunk(1, 3, 0, true, false);
    chunks[0].generation = 43u;
    chunks[1].generation = 44u;
    assert(ChunksTestScheduleTerrainSections(
        (Vector3){ 0.5f, 20.0f, 0.5f }, 2) == 3);
}

static void TestSectionExposureControlsMaterialization(void)
{
    terrainSectionFacesExposed = false;
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 30, 0, true, false);
    chunks[0].generation = 51u;
    assert(RequestChunkTerrainSection(30, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], 0) == NULL);
    assert(ChunkTerrainSectionIsResolved(&chunks[0], 0));

    terrainSectionFacesExposed = true;
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 31, 0, true, false);
    chunks[0].generation = 52u;
    assert(RequestChunkTerrainSection(31, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], 0) != NULL);
}

static void TestSavedEditForcesSectionMaterialization(void)
{
    const int cx = 40;
    assert(SetBlock(cx * CHUNK_SIZE + 2, 4, 3, BLOCK_GLASS));

    terrainSectionFacesExposed = false;
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, cx, 0, true, false);
    chunks[0].generation = 53u;
    assert(RequestChunkTerrainSection(cx, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();

    const ChunkSection *section = ChunkGetSectionConst(&chunks[0], 0);
    assert(section != NULL);
    assert(section->blocks[2][4][3] == BLOCK_GLASS);
    assert(section->blocks[3][4][3] == BLOCK_STONE);

    assert(SetBlock((cx + 1) * CHUNK_SIZE - 1, 4, 3, BLOCK_GLASS));
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, cx + 1, 0, true, false);
    chunks[0].generation = 54u;
    assert(RequestChunkTerrainSection(cx + 1, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    const ChunkSection *neighborSection = ChunkGetSectionConst(
        &chunks[0], 0);
    assert(neighborSection != NULL);
    assert(neighborSection->blocks[0][4][3] == BLOCK_STONE);
    terrainSectionFacesExposed = true;
}

static void TestNegativeSectionPruningKeepsVerticalWindow(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    Chunk *chunk = &chunks[0];

    ChunkSection *near = ChunkGetSection(chunk, -26, true);
    ChunkSection *far = ChunkGetSection(chunk, -27, true);
    ChunkSection *surface = ChunkGetSection(chunk, 2, true);
    assert(near && far && surface);
    assert(ChunkMarkTerrainSectionResolved(chunk, -26));
    assert(ChunkMarkTerrainSectionResolved(chunk, -27));
    assert(ChunkMarkTerrainSectionResolved(chunk, 2));

    assert(ChunksTestPruneTerrainSections(
        (Vector3){ 0.5f, -319.0f, 0.5f }) == 1);
    assert(ChunkGetSectionConst(chunk, -26) == near);
    assert(near->dirty);
    assert(ChunkGetSectionConst(chunk, -27) == NULL);
    assert(!ChunkTerrainSectionIsResolved(chunk, -27));
    assert(ChunkGetSectionConst(chunk, 2) == surface);
    assert(ChunkTerrainSectionIsResolved(chunk, 2));

    assert(RequestChunkTerrainSection(0, -27, 0));
    assert(ChunksTestGenerationJobSectionY(0) == -27);
}

static void TestNegativeSectionPruningPreservesRuntimeState(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    Chunk *chunk = &chunks[0];
    chunk->generation = 41u;

    ChunkSection *fluidRuntime = ChunkGetSection(chunk, -30, true);
    ChunkSection *fluidDirty = ChunkGetSection(chunk, -31, true);
    ChunkSection *meshPending = ChunkGetSection(chunk, -32, true);
    ChunkSection *plain = ChunkGetSection(chunk, -34, true);
    assert(fluidRuntime && fluidDirty && meshPending && plain);
    fluidRuntime->waterVolumes = malloc(1u);
    assert(fluidRuntime->waterVolumes != NULL);
    fluidDirty->fluidDirty = true;
    ChunksTestSeedMeshJob(0, 0, 0, 0, -32, false);

    assert(RequestChunkTerrainSection(0, -33, 0));
    ChunkSection *generationPending = ChunkGetSection(chunk, -33, true);
    assert(generationPending != NULL);

    assert(ChunksTestPruneTerrainSections(
        (Vector3){ 0.5f, -319.0f, 0.5f }) == 1);
    assert(ChunkGetSectionConst(chunk, -30) == fluidRuntime);
    assert(ChunkGetSectionConst(chunk, -31) == fluidDirty);
    assert(ChunkGetSectionConst(chunk, -32) == meshPending);
    assert(ChunkGetSectionConst(chunk, -33) == generationPending);
    assert(ChunkGetSectionConst(chunk, -34) == NULL);
}

static void TestDistantSectionJobsReleaseQueueCapacity(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    chunks[0].generation = 51u;

    assert(RequestChunkTerrainSection(0, -30, 0));
    ChunksTestRunGenerationJob(0);
    assert(RequestChunkTerrainSection(0, -31, 0));
    assert(RequestChunkTerrainSection(0, -20, 0));
    assert(RequestChunkTerrainSection(0, 2, 0));
    ChunksTestSetGenerationJobRunning(1, true);

    ChunksTestSeedMeshJob(0, 0, 0, 0, -32, false);
    ChunksTestSeedMeshJob(1, 0, 0, 0, -33, false);
    ChunksTestSeedMeshJob(2, 0, 0, 0, -21, false);
    ChunksTestSetMeshJobRunning(0, true);

    Vector3 playerPosition = { 0.5f, -319.0f, 0.5f };
    assert(ChunksTestCancelDistantSectionJobs(playerPosition) == 2);
    assert(ChunksTestGenerationJobSectionY(0) == INT_MIN);
    assert(ChunksTestGenerationJobSectionY(1) == -31);
    assert(ChunksTestGenerationJobSectionY(2) == -20);
    assert(ChunksTestGenerationJobSectionY(3) == 2);
    assert(ChunksTestMeshJobSlot(0) == 0);
    assert(ChunksTestMeshJobSlot(1) == -1);
    assert(ChunksTestMeshJobSlot(2) == 0);

    ChunksTestSetGenerationJobRunning(1, false);
    ChunksTestSetMeshJobRunning(0, false);
    assert(ChunksTestCancelDistantSectionJobs(playerPosition) == 2);
    assert(ChunksTestGenerationJobSectionY(1) == INT_MIN);
    assert(ChunksTestMeshJobSlot(0) == -1);
    assert(GetPendingGenJobCount() == 2);
    assert(GetPendingMeshJobCount() == 1);

    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], -30) == NULL);
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.generationCanceled == 2u);
    assert(stats.meshCanceled == 2u);
}

static void TestSectionLifecycleHooksPermitFluidRuntimePruning(void)
{
    ChunksTestResetScheduler();
    const WorldExtensionHooks hooks = {
        .onChunkSectionLoaded = TestOnChunkSectionLoaded,
        .prepareChunkSectionUnload = TestPrepareChunkSectionUnload
    };
    WorldInstallExtensionHooks(&hooks);
    sectionLoadedNotifications = 0;
    sectionUnloadPreparations = 0;

    ChunksTestConfigureChunk(0, 0, 0, true, false);
    Chunk *chunk = &chunks[0];
    chunk->generation = 61u;
    ChunkSection *runtime = ChunkGetSection(chunk, -30, true);
    assert(runtime != NULL);
    runtime->waterVolumes = malloc(1u);
    assert(runtime->waterVolumes != NULL);
    runtime->fluidDirty = true;

    assert(ChunksTestPruneTerrainSections(
        (Vector3){ 0.5f, -319.0f, 0.5f }) == 1);
    assert(sectionUnloadPreparations == 1);
    assert(lastSectionPrepared == -30);
    assert(ChunkGetSectionConst(chunk, -30) == NULL);

    assert(RequestChunkTerrainSection(0, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(sectionLoadedNotifications == 1);
    assert(lastSectionLoaded == 0);
    assert(ChunkGetSectionConst(chunk, 0) != NULL);

    WorldInstallExtensionHooks(NULL);
}

int main(void)
{
    memset(chunks, 0, sizeof(Chunk) * MAX_ACTIVE_CHUNKS);
    AssertFreshStats();
    TestFrustumSphereEdgesRemainVisible();
    TestFrustumNearPlaneAndRenderAspect();
    TestChunkFrustumUsesSparseSectionHeights();
    TestSingleChunkUsesSingleJobAndNearestFirst();
    TestFullQueueLeavesDirtyChunkForLater();
    TestFullGenerationQueueDoesNotFallBackToMainThread();
    TestBudgetAndInvalidSlotCleanup();
    TestEditDuringFlightKeepsSectionDirty();
    TestStaleJobDiscardedAfterChunkReload();
    TestWaterMeshUsesSnapshottedNeighborBoundary();
    TestSparseWaterBoundariesPreserveUnknownSections();
    TestSparseSignedSectionStorage();
    TestMaterializedAirDiffersFromMissingSection();
    TestImplicitTerrainLookupAndEditOverride();
    TestSectionGenerationJobsStageAndValidateResults();
    TestNearbySectionSchedulingPrioritizesPlayerSection();
    TestNearbySectionSchedulingUsesRenderDistance();
    TestSectionExposureControlsMaterialization();
    TestSavedEditForcesSectionMaterialization();
    TestNegativeSectionPruningKeepsVerticalWindow();
    TestNegativeSectionPruningPreservesRuntimeState();
    TestDistantSectionJobsReleaseQueueCapacity();
    TestSectionLifecycleHooksPermitFluidRuntimePruning();
    ChunksTestResetScheduler();
    puts("chunk streaming tests passed");
    return 0;
}
