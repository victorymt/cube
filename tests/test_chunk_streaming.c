#include "world/chunks.h"
#include "app/game_debug_lod.h"

#define chunks (ChunksMutableForTesting())
#include "world/terrain.h"
#include "world/world.h"
#include "world/world_environment.h"
#include "world/world_extension.h"
#include "world/world_persistence.h"

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
static pthread_mutex_t generationProbeMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t generationProbeCond = PTHREAD_COND_INITIALIZER;
static bool generationProbeEnabled = false;
static bool generationProbeRelease = false;
static int generationProbeActive = 0;
static int generationProbeMaxActive = 0;
static bool allowColumnGeneration = false;

static int FindPendingMeshJobFor(int slotIndex, int sectionY);

static void AssertLodDslNumber(const char *name, double expected)
{
    DebugDslValue value = { 0 };
    assert(GameDebugLodDslResolve(name, &value));
    assert(value.type == DEBUG_DSL_VALUE_NUMBER);
    assert(value.as.number == expected);
}

static bool WaitForGenerationProbe(int target)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_REALTIME, &deadline) == 0);
    deadline.tv_sec += 3;
    pthread_mutex_lock(&generationProbeMutex);
    while (generationProbeActive < target) {
        if (pthread_cond_timedwait(&generationProbeCond,
                                   &generationProbeMutex,
                                   &deadline) != 0) break;
    }
    bool reached = generationProbeActive >= target;
    pthread_mutex_unlock(&generationProbeMutex);
    return reached;
}

static void ReleaseGenerationProbe(void)
{
    pthread_mutex_lock(&generationProbeMutex);
    generationProbeRelease = true;
    pthread_cond_broadcast(&generationProbeCond);
    pthread_mutex_unlock(&generationProbeMutex);
}

static void ResetGenerationProbe(void)
{
    pthread_mutex_lock(&generationProbeMutex);
    generationProbeEnabled = true;
    generationProbeRelease = false;
    generationProbeActive = 0;
    generationProbeMaxActive = 0;
    pthread_mutex_unlock(&generationProbeMutex);
}

static void TestAdaptiveWorkerCountPolicy(void)
{
    assert(ChunksTestWorkerCountForCpu(1, 0) == 1);
    assert(ChunksTestWorkerCountForCpu(2, 0) == 1);
    assert(ChunksTestWorkerCountForCpu(4, 0) == 2);
    assert(ChunksTestWorkerCountForCpu(24, 0) ==
           MAX_CHUNK_WORKER_THREADS);
    assert(ChunksTestWorkerCountForCpu(128, 0) ==
           MAX_CHUNK_WORKER_THREADS);
    assert(ChunksTestWorkerCountForCpu(24, 3) == 3);
}

static void TestFailedColumnGenerationReleasesSlot(void)
{
    ChunksTestResetScheduler();
    ChunksTestFailNextColumnAllocation();
    assert(ChunksTestEnsureChunk(79, 0));

    int slotIndex = -1;
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        if (chunks[index].generating && chunks[index].cx == 79 &&
            chunks[index].cz == 0) {
            slotIndex = index;
            break;
        }
    }
    assert(slotIndex >= 0);
    assert(ChunksTestNextGenerationJobIndex() == 0);
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(!chunks[slotIndex].generating);
    assert(!chunks[slotIndex].loaded);
    assert(ChunksGetStreamingStats().generationCanceled == 1);

    assert(ChunksTestEnsureChunk(79, 0));
    assert(chunks[slotIndex].generating);
    ChunksTestResetScheduler();
}

static void TestShutdownWithoutWorkersCancelsQueuedJobs(void)
{
    ChunksTestResetScheduler();
    ChunksTestSeedGenerationJob(0, 12, 34, 0, false);
    ChunksTestReleaseScheduler();
    ChunksShutdownGenThread();
    assert(!ChunksTestFindPendingGenerationJob(12, 34));
    assert(ChunksStartedWorkerCount() == 0);
    ChunksTestResetScheduler();
}

static void TestWorkerPoolRunsJobsConcurrently(void)
{
    ChunksTestReleaseScheduler();
    assert(ChunksConfigureWorkerCount(4));
    assert(ChunksStartGenThread());
    assert(ChunksConfiguredWorkerCount() == 4);
    assert(ChunksStartedWorkerCount() == 4);

    for (int index = 0; index < 4; index++) {
        ChunksTestConfigureChunk(index, 40 + index, 0, true, false);
        chunks[index].generation = (uint32_t)(100 + index);
    }
    ResetGenerationProbe();
    for (int index = 0; index < 4; index++) {
        assert(RequestChunkTerrainSection(40 + index, 0, 0));
    }
    assert(WaitForGenerationProbe(4));
    assert(ChunksActiveWorkerCount() == 4);
    assert(generationProbeMaxActive == 4);
    ReleaseGenerationProbe();
    DrainChunkGen();
    for (int index = 0; index < 4; index++) {
        assert(ChunkGetSectionConst(&chunks[index], 0) != NULL);
    }

    ResetGenerationProbe();
    allowColumnGeneration = true;
    assert(ChunksTestEnsureChunk(80, 0));
    assert(WaitForGenerationProbe(1));
    Chunk *destination = NULL;
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        if (chunks[index].generating && chunks[index].cx == 80 &&
            chunks[index].cz == 0) {
            destination = &chunks[index];
            break;
        }
    }
    assert(destination != NULL);
    assert(destination->sectionCount == 0);
    ReleaseGenerationProbe();
    DrainChunkGen();
    destination = FindChunk(80, 0);
    assert(destination != NULL && destination->loaded);
    assert(ChunkGetSectionConst(destination, 0) != NULL);

    generationProbeEnabled = false;
    allowColumnGeneration = false;
    for (int index = 0; index < 4; index++) {
        ChunksTestConfigureChunk(index, 44 + index, 0, true, false);
        chunks[index].generation = (uint32_t)(200 + index);
        assert(RequestChunkTerrainSection(44 + index, 0, 0));
    }
    ChunksTestSeedMeshJob(0, 0, 44, 0, 0, false);
    assert(ChunksRestartGenThreads(2));
    assert(ChunksConfiguredWorkerCount() == 2);
    assert(ChunksStartedWorkerCount() == 2);
    for (int index = 0; index < 4; index++) {
        assert(ChunkGetSectionConst(&chunks[index], 0) != NULL);
    }
    assert(ChunksTestMeshJobSlot(0) == -1);
    ChunksShutdownGenThread();
    assert(ChunksStartedWorkerCount() == 0);
    assert(ChunksActiveWorkerCount() == 0);
    ChunksTestResetScheduler();
}

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

void WorldSetNetherActive(bool active)
{
    (void)active;
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
    if (generationProbeEnabled) {
        pthread_mutex_lock(&generationProbeMutex);
        generationProbeActive++;
        if (generationProbeActive > generationProbeMaxActive) {
            generationProbeMaxActive = generationProbeActive;
        }
        pthread_cond_broadcast(&generationProbeCond);
        while (!generationProbeRelease) {
            pthread_cond_wait(&generationProbeCond, &generationProbeMutex);
        }
        generationProbeActive--;
        pthread_mutex_unlock(&generationProbeMutex);
    }
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
    if (allowColumnGeneration) {
        assert(GenerateChunkTerrainSectionBase(chunk, cx, cz, 0, mode));
        return;
    }
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

static void TestChunkLodSelectionUsesRingsAndHysteresis(void)
{
    assert(ChunkLodSelect(CHUNK_LOD_EXACT, false, 0, true) ==
           CHUNK_LOD_EXACT);
    assert(ChunkLodSelect(CHUNK_LOD_EXACT, false, 5, true) ==
           CHUNK_LOD_HALF);
    assert(ChunkLodSelect(CHUNK_LOD_EXACT, false, 9, true) ==
           CHUNK_LOD_QUARTER);

    assert(ChunkLodSelect(CHUNK_LOD_EXACT, true, 5, true) ==
           CHUNK_LOD_EXACT);
    assert(ChunkLodSelect(CHUNK_LOD_EXACT, true, 6, true) ==
           CHUNK_LOD_HALF);
    assert(ChunkLodSelect(CHUNK_LOD_HALF, true, 4, true) ==
           CHUNK_LOD_HALF);
    assert(ChunkLodSelect(CHUNK_LOD_HALF, true, 3, true) ==
           CHUNK_LOD_EXACT);
    assert(ChunkLodSelect(CHUNK_LOD_HALF, true, 9, true) ==
           CHUNK_LOD_HALF);
    assert(ChunkLodSelect(CHUNK_LOD_HALF, true, 10, true) ==
           CHUNK_LOD_QUARTER);
    assert(ChunkLodSelect(CHUNK_LOD_QUARTER, true, 8, true) ==
           CHUNK_LOD_QUARTER);
    assert(ChunkLodSelect(CHUNK_LOD_QUARTER, true, 7, true) ==
           CHUNK_LOD_HALF);
    assert(ChunkLodSelect(CHUNK_LOD_QUARTER, true, 12, false) ==
           CHUNK_LOD_EXACT);
}

static void TestChunkLodTargetsAndStatsTrackLoadedChunks(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, false);
    ChunksTestConfigureChunk(1, 5, 0, true, false);
    ChunksTestConfigureChunk(2, 9, 0, true, false);
    ChunksTestConfigureChunk(3, 12, 0, false, false);

    ChunksTestUpdateLodTargets((Vector3){ 0.5f, 80.0f, 0.5f }, true);
    ChunkLodStats stats = ChunksGetLodStats();
    assert(stats.coarseAllowed);
    assert(stats.targetChunks[CHUNK_LOD_EXACT] == 1u);
    assert(stats.targetChunks[CHUNK_LOD_HALF] == 1u);
    assert(stats.targetChunks[CHUNK_LOD_QUARTER] == 1u);
    assert(stats.activeChunks[CHUNK_LOD_EXACT] == 3u);
    assert(stats.readySections[CHUNK_LOD_EXACT] == 0u);
    assert(stats.readySections[CHUNK_LOD_HALF] == 0u);
    assert(stats.readySections[CHUNK_LOD_QUARTER] == 0u);
    assert(stats.pendingJobs[CHUNK_LOD_EXACT] == 0u);
    assert(stats.pendingJobs[CHUNK_LOD_HALF] == 0u);
    assert(stats.pendingJobs[CHUNK_LOD_QUARTER] == 0u);
    assert(stats.targetChanges == 0u);

    DebugDslValue value = { 0 };
    assert(GameDebugLodDslResolve("stream.lod_coarse_allowed", &value));
    assert(value.type == DEBUG_DSL_VALUE_BOOL);
    assert(value.as.boolean);
    AssertLodDslNumber("stream.lod_target_changes", 0.0);
    AssertLodDslNumber("stream.lod_target_exact", 1.0);
    AssertLodDslNumber("stream.lod_target_half", 1.0);
    AssertLodDslNumber("stream.lod_target_quarter", 1.0);
    AssertLodDslNumber("stream.lod_active_exact", 3.0);
    AssertLodDslNumber("stream.lod_active_half", 0.0);
    AssertLodDslNumber("stream.lod_active_quarter", 0.0);
    AssertLodDslNumber("stream.lod_ready_exact_sections", 0.0);
    AssertLodDslNumber("stream.lod_ready_half_sections", 0.0);
    AssertLodDslNumber("stream.lod_ready_quarter_sections", 0.0);
    AssertLodDslNumber("stream.lod_jobs_exact", 0.0);
    AssertLodDslNumber("stream.lod_jobs_half", 0.0);
    AssertLodDslNumber("stream.lod_jobs_quarter", 0.0);
    assert(!GameDebugLodDslResolve("stream.surface_ready", &value));

    ChunksTestUpdateLodTargets((Vector3){ 16.5f, 80.0f, 0.5f }, true);
    stats = ChunksGetLodStats();
    assert(stats.targetChunks[CHUNK_LOD_EXACT] == 1u);
    assert(stats.targetChunks[CHUNK_LOD_HALF] == 1u);
    assert(stats.targetChunks[CHUNK_LOD_QUARTER] == 1u);
    assert(stats.targetChanges == 0u);

    ChunksTestUpdateLodTargets((Vector3){ 0.5f, -80.0f, 0.5f }, false);
    stats = ChunksGetLodStats();
    assert(!stats.coarseAllowed);
    assert(stats.targetChunks[CHUNK_LOD_EXACT] == 3u);
    assert(stats.targetChanges == 2u);
}

static void TestChunkLodSwitchKeepsPreviousLevelUntilCacheIsReady(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 9, 0, true, true);
    ChunkSection *section = ChunkGetSection(&chunks[0], 0, false);
    assert(section != NULL);
    section->lodModelReady = true;
    section->lodModelStamp = section->dirtyStamp;
    section->lodModelLevel = CHUNK_LOD_QUARTER;

    ChunksTestUpdateLodTargets((Vector3){ 0.5f, 0.0f, 0.5f }, true);
    ChunkLodStats stats = ChunksGetLodStats();
    assert(stats.targetChunks[CHUNK_LOD_QUARTER] == 1u);
    assert(stats.activeChunks[CHUNK_LOD_QUARTER] == 1u);
    assert(stats.readySections[CHUNK_LOD_QUARTER] == 1u);
    assert(!section->dirty);

    ChunksTestUpdateLodTargets((Vector3){ 144.5f, 0.0f, 0.5f }, true);
    stats = ChunksGetLodStats();
    assert(stats.targetChunks[CHUNK_LOD_EXACT] == 1u);
    assert(stats.activeChunks[CHUNK_LOD_QUARTER] == 1u);
    assert(section->dirty);

    section->exactModelReady = true;
    section->exactModelStamp = section->dirtyStamp;
    ChunksTestUpdateLodTargets((Vector3){ 144.5f, 0.0f, 0.5f }, true);
    stats = ChunksGetLodStats();
    assert(stats.activeChunks[CHUNK_LOD_EXACT] == 1u);
    assert(stats.readySections[CHUNK_LOD_EXACT] == 1u);
    assert(stats.readySections[CHUNK_LOD_QUARTER] == 1u);
    assert(!section->dirty);
}

static void TestChunkLodRenderModelUsesActiveLevelAndFallbacks(void)
{
    Chunk chunk = { .loaded = true, .activeLod = CHUNK_LOD_EXACT };
    ChunkSection section = { 0 };
    assert(ChunksSectionSolidModel(&chunk, &section) == NULL);

    section.hasLodModel = true;
    section.lodModelLevel = CHUNK_LOD_QUARTER;
    assert(ChunksSectionSolidModel(&chunk, &section) == &section.lodModel);

    section.hasModel = true;
    assert(ChunksSectionSolidModel(&chunk, &section) == &section.model);
    chunk.activeLod = CHUNK_LOD_QUARTER;
    assert(ChunksSectionSolidModel(&chunk, &section) == &section.lodModel);

    section.lodModelReady = true;
    section.lodModelStamp = section.dirtyStamp;
    section.hasLodModel = false;
    assert(ChunksSectionSolidModel(&chunk, &section) == NULL);
}

static void TestMeshJobsIncludeLodIdentityAndPrioritizeExactUpgrades(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 9, 0, true, true);

    ChunksTestUpdateLodTargets((Vector3){ 0.5f, 0.0f, 0.5f }, true);
    RebuildDirtyChunkMeshes((Vector3){ 0.5f, 0.0f, 0.5f });
    assert(ChunksTestMeshJobSlot(0) == 0);
    assert(ChunksTestMeshJobLod(0) == CHUNK_LOD_QUARTER);
    assert(!ChunksTestMeshJobPriority(0));

    chunks[0].activeLod = CHUNK_LOD_QUARTER;
    ChunksTestUpdateLodTargets((Vector3){ 144.5f, 0.0f, 0.5f }, true);
    RebuildDirtyChunkMeshes((Vector3){ 144.5f, 0.0f, 0.5f });
    assert(GetPendingMeshJobCount() == 2);
    assert(ChunksTestMeshJobSlot(1) == 0);
    assert(ChunksTestMeshJobLod(1) == CHUNK_LOD_EXACT);
    assert(ChunksTestMeshJobPriority(1));
    assert(ChunksTestNextMeshJobIndex() == 1);
    ChunkLodStats stats = ChunksGetLodStats();
    assert(stats.pendingJobs[CHUNK_LOD_EXACT] == 1u);
    assert(stats.pendingJobs[CHUNK_LOD_HALF] == 0u);
    assert(stats.pendingJobs[CHUNK_LOD_QUARTER] == 1u);
    AssertLodDslNumber("stream.lod_jobs_exact", 1.0);
    AssertLodDslNumber("stream.lod_jobs_half", 0.0);
    AssertLodDslNumber("stream.lod_jobs_quarter", 1.0);

    ChunksTestCompleteMeshJob(0);
    ProcessFinishedMeshJobs(0.0);
    assert(ChunksTestMeshJobSlot(0) == -1);
    assert(ChunksTestChunkDirty(0));
    assert(ChunksGetStreamingStats().meshCanceled >= 1u);
}

static void TestSingleChunkUsesSingleJobAndNearestFirst(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 4, 4, true, true);
    ChunksTestConfigureChunk(1, 0, 0, true, true);
    ChunksTestConfigureChunk(2, 2, 0, true, true);

    ChunkStreamingStats stats = { 0 };
    for (int frame = 0; frame < 4 && stats.meshSubmitted < 3; frame++) {
        RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
        stats = ChunksGetStreamingStats();
    }
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

static void TestReusedLowSlotsDoNotStarveOlderJobs(void)
{
    ChunksTestResetScheduler();
    ChunksTestSeedGenerationJob(63, 10, 20, 5, false);
    ChunksTestSeedGenerationJob(0, 11, 20, 5, false);
    assert(ChunksTestNextGenerationJobIndex() == 63);

    ChunksTestSeedMeshJob(63, 1, 10, 20, 5, false);
    ChunksTestSeedMeshJob(0, 2, 11, 20, 5, false);
    assert(ChunksTestNextMeshJobIndex() == 63);

    ChunksTestResetScheduler();
    ChunksTestSeedMeshJob(63, 1, 10, 20, 5, true);
    ChunksTestSeedMeshJob(0, 2, 11, 20, 5, true);
    ProcessFinishedMeshJobs(0.0);
    assert(ChunksTestMeshJobSlot(63) == -1);
    assert(ChunksTestMeshJobSlot(0) == 2);
}

static void TestPriorityMeshJobsBypassBackgroundWork(void)
{
    ChunksTestResetScheduler();
    ChunksTestSeedMeshJob(63, 1, 10, 20, 5, false);
    ChunksTestSeedMeshJob(0, 2, 11, 20, 5, false);
    ChunksTestSetMeshJobPriority(0, true);
    assert(ChunksTestNextMeshJobIndex() == 0);

    ChunksTestResetScheduler();
    ChunksTestSeedMeshJob(63, 1, 10, 20, 5, true);
    ChunksTestSeedMeshJob(0, 2, 11, 20, 5, true);
    ChunksTestSetMeshJobPriority(0, true);
    ProcessFinishedMeshJobs(0.0);
    assert(ChunksTestMeshJobSlot(0) == -1);
    assert(ChunksTestMeshJobSlot(63) == 1);
}

static void TestPriorityEditSupersedesInFlightSnapshot(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    RebuildDirtyChunkMeshes((Vector3){ 0 });
    assert(ChunksTestMeshJobSlot(0) == 0);
    assert(!ChunksTestMeshJobPriority(0));

    MarkChunkDirtyAtBlock(0, 0, 0);
    assert(RebuildDirtyChunkMeshAt(0, 0, 0));
    assert(GetPendingMeshJobCount() == 2);
    assert(ChunksTestMeshJobSlot(1) == 0);
    assert(ChunksTestMeshJobPriority(1));
    assert(ChunksTestNextMeshJobIndex() == 1);
}

static void TestPriorityEditFallsBackWhenBackgroundQueueIsSaturated(void)
{
    ChunksTestResetScheduler();
    for (int i = 0; i < MAX_CHUNK_MESH_JOBS - 1; i++) {
        ChunksTestSeedMeshJob(i, 1, 100 + i, 0, 0, false);
    }
    ChunksTestConfigureChunk(0, 0, 0, true, true);

    assert(RebuildDirtyChunkMeshAt(0, 0, 0));
    assert(GetPendingMeshJobCount() == MAX_CHUNK_MESH_JOBS - 1);
    assert(ChunksTestMeshJobSlot(MAX_CHUNK_MESH_JOBS - 1) == -1);
    assert(!ChunksTestChunkDirty(0));
    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(stats.meshSubmitted == 0);
    assert(stats.syncRebuilds == 1);
}

static void TestSectionPipelineReportsMeshWaitStage(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    ChunkSection *section = ChunkGetSection(&chunks[0], 0, false);
    assert(section != NULL);
    MarkChunkDirty(0, 0);

    RebuildDirtyChunkMeshes((Vector3){ 0.5f, 1.0f, 0.5f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);
    ChunkSectionPipelineInfo info = { 0 };
    assert(ChunksGetSectionPipelineInfo(0, 0, 0, &info));
    assert(info.stage == CHUNK_PIPELINE_MESH_QUEUED);
    assert(info.stageAgeMs >= 0.0);
    assert(info.snapshotStamp == section->dirtyStamp);
    assert(info.currentStamp == section->dirtyStamp);

    ChunksTestSetMeshJobRunning(jobIndex, true);
    assert(ChunksGetSectionPipelineInfo(0, 0, 0, &info));
    assert(info.stage == CHUNK_PIPELINE_MESH_RUNNING);
    ChunksTestSetMeshJobRunning(jobIndex, false);
    ChunksTestCompleteMeshJob(jobIndex);
    assert(ChunksGetSectionPipelineInfo(0, 0, 0, &info));
    assert(info.stage == CHUNK_PIPELINE_MESH_DONE);
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
    RebuildDirtyChunkMeshes((Vector3){ 0 });
    assert(GetPendingMeshJobCount() == 1);

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

static void TestStaleMeshJobDiscardedAfterSurfaceKeyChanges(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, 0, true, true);
    chunks[0].generation = 3u;

    RebuildDirtyChunkMeshes((Vector3){ 0.0f, 0.0f, 0.0f });
    int jobIndex = FindPendingMeshJobFor(0, 0);
    assert(jobIndex >= 0);

    chunks[0].surfaceKey.bodyId++;
    ChunksTestCompleteMeshJob(jobIndex);
    ProcessFinishedMeshJobs(0.0);

    ChunkStreamingStats stats = ChunksGetStreamingStats();
    assert(ChunksTestMeshJobSlot(jobIndex) == -1);
    assert(stats.meshCanceled >= 1);
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
    assert(!SurfaceBlockReadyAt(2, 4, 3));
    ChunksTestConfigureChunk(0, 0, 0, true, false);

    assert(SurfaceBlockReadyAt(2, 4, 3));
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
    assert(ChunkGetSectionConst(&chunks[0], 0) != NULL);
    assert(ChunksGetStreamingStats().generationCanceled == 0u);

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 10, 0, true, false);
    chunks[0].generation = 23u;
    assert(RequestChunkTerrainSection(10, 0, 0));
    ChunksTestRunGenerationJob(0);
    chunks[0].surfaceKey.bodyId++;
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

static void TestNonEmptySectionsMaterialize(void)
{
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 30, 0, true, false);
    chunks[0].generation = 51u;
    assert(RequestChunkTerrainSection(30, 0, 0));
    ChunksTestRunGenerationJob(0);
    ProcessFinishedChunkJobs();
    assert(ChunkGetSectionConst(&chunks[0], 0) != NULL);
    assert(ChunkTerrainSectionIsResolved(&chunks[0], 0));

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
}

static void TestExpandedBlockEditInstallationIsTransactional(void)
{
    const BlockEdit edits[] = {
        { 701, 4, 3, BLOCK_CHARRED_WOOD },
        { 702, 5, 3, BLOCK_CHARCOAL },
        { 703, 6, 3, BLOCK_FIRE_ASH },
        { 704, 7, 3, BLOCK_OAK_LOG },
        { 705, 8, 3, BLOCK_WILLOW_LEAVES },
        { 706, 9, 3, BLOCK_FIREWEED },
        { 707, 10, 3, BLOCK_SAGUARO }
    };
    const uint32_t dimensions[] = { 1u, 1u, 1u, 1u, 1u, 1u, 1u };
    SurfaceAddress addresses[7];
    SurfaceMapCell mapCells[7];
    for (int index = 0; index < 7; index++) {
        addresses[index] = SurfaceAddressFromMapCoordinates(
            1u, (float)edits[index].x, (float)edits[index].z,
            edits[index].y);
        mapCells[index] = SurfaceCanonicalMapCell(
            (float)edits[index].x, (float)edits[index].z);
    }
    assert(WorldPersistenceEditsValid(edits, 7));
    assert(WorldPersistenceInstallEdits(
        edits, dimensions, addresses, mapCells, 7));
    assert(WorldGetEditCount() == 7);
    for (int index = 0; index < 7; index++) {
        const BlockEdit *loaded = WorldGetEditAt(index);
        assert(loaded != NULL);
        assert(loaded->x == edits[index].x);
        assert(loaded->y == edits[index].y);
        assert(loaded->z == edits[index].z);
        assert(loaded->type == edits[index].type);
    }

    BlockEdit invalid = { 708, 4, 3, (BlockType)200 };
    uint32_t invalidDimension = 1u;
    SurfaceAddress invalidAddress = SurfaceAddressFromMapCoordinates(
        1u, (float)invalid.x, (float)invalid.z, invalid.y);
    assert(!WorldPersistenceEditsValid(&invalid, 1));
    assert(!WorldPersistenceInstallEdits(
        &invalid, &invalidDimension, &invalidAddress, mapCells, 1));
    assert(WorldGetEditCount() == 7);
    assert(WorldGetEditAt(6)->type == BLOCK_SAGUARO);

    invalid = (BlockEdit){
        708, SURFACE_MAX_Y_EXCLUSIVE, 3, BLOCK_ANDESITE
    };
    assert(!WorldPersistenceEditsValid(&invalid, 1));
    assert(!WorldPersistenceInstallEdits(
        &invalid, &invalidDimension, &invalidAddress, mapCells, 1));
    assert(WorldGetEditCount() == 7);
    assert(WorldGetEditAt(0)->type == BLOCK_CHARRED_WOOD);
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

static void TestWrappedBlockEditUsesCanonicalIdentity(void)
{
    WorldReset(DEFAULT_WORLD_SEED);
    const int x = 73;
    const int y = 4;
    const int z = -211;
    assert(SetBlock(x, y, z, BLOCK_GLASS));
    assert(WorldGetEditCount() == 1);
    assert(GetBlockAt(x + SURFACE_EQUATOR_BLOCKS, y, z) == BLOCK_GLASS);
    assert(SetBlock(x + SURFACE_EQUATOR_BLOCKS, y, z, BLOCK_BRICK));
    assert(WorldGetEditCount() == 1);
    assert(GetBlockAt(x, y, z) == BLOCK_BRICK);
}

static void AssertChunkAliasCanonicalizes(int rawCx, int rawCz)
{
    SurfaceChunkKey rawKey = ChunkSurfaceKeyAt(rawCx, rawCz);
    int canonicalCx = rawCx;
    int canonicalCz = rawCz;
    CanonicalizeSurfaceChunkCoordinates(&canonicalCx, &canonicalCz);
    assert(SurfaceChunkKeyEqual(
        rawKey, ChunkSurfaceKeyAt(canonicalCx, canonicalCz)));

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, canonicalCx, canonicalCz, true, false);
    assert(FindChunk(rawCx, rawCz) == &chunks[0]);
    assert(!ChunksTestEnsureChunk(rawCx, rawCz));
    assert(GetPendingGenJobCount() == 0);
}

static void TestSurfaceChunkAliasesUseOneCanonicalIdentity(void)
{
    const int equatorChunks = SURFACE_EQUATOR_BLOCKS / CHUNK_SIZE;
    const int poleChunk =
        SURFACE_POLE_TO_POLE_BLOCKS / (2 * CHUNK_SIZE);

    AssertChunkAliasCanonicalizes(17 + equatorChunks, -23);
    AssertChunkAliasCanonicalizes(17, poleChunk);
    AssertChunkAliasCanonicalizes(-19, -poleChunk - 1);

    ChunksTestResetScheduler();
    assert(ChunksTestEnsureChunk(31, 12));
    assert(GetPendingGenJobCount() == 1);
    assert(ChunksTestFindPendingGenerationJob(
        31 + equatorChunks, 12));
    assert(!ChunksTestEnsureChunk(31 + equatorChunks, 12));
    assert(GetPendingGenJobCount() == 1);
}

static void TestPolarChunkNeighborsBecomeDirty(void)
{
    const int pole = SURFACE_POLE_TO_POLE_BLOCKS / 2;
    const int poleChunk = pole / CHUNK_SIZE;
    int neighborCx = 0;
    int neighborCz = poleChunk;
    CanonicalizeSurfaceChunkCoordinates(&neighborCx, &neighborCz);

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, poleChunk - 1, true, false);
    ChunksTestConfigureChunk(
        1, neighborCx, neighborCz, true, false);
    assert(ChunkGetSection(&chunks[0], 0, true));
    assert(ChunkGetSection(&chunks[1], 0, true));
    assert(FindHorizontalChunkNeighbor(
               0, poleChunk - 1, 0, 1) == &chunks[1]);

    MarkChunkAndHorizontalNeighborsDirty(0, poleChunk - 1);
    assert(ChunksTestChunkDirty(0));
    assert(ChunksTestChunkDirty(1));

    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 0, poleChunk - 1, true, false);
    ChunksTestConfigureChunk(
        1, neighborCx, neighborCz, true, false);
    assert(ChunkGetSection(&chunks[0], 0, true));
    assert(ChunkGetSection(&chunks[1], 0, true));
    MarkChunkDirtyAtBlock(3, 4, pole - 1);
    assert(ChunksTestChunkDirty(0));
    assert(ChunksTestChunkDirty(1));
}

static void TestCanonicalChunkIdentityStatsDetectAliases(void)
{
    const int equatorChunks = SURFACE_EQUATOR_BLOCKS / CHUNK_SIZE;
    ChunksTestResetScheduler();
    ChunksTestConfigureChunk(0, 23, -7, true, false);
    ChunksTestConfigureChunk(1, 23 + equatorChunks, -7, true, false);
    ChunksTestConfigureChunk(2, 24, -7, true, false);
    ChunksTestConfigureChunk(3, 25, -7, false, false);

    ChunkCanonicalIdentityStats stats = ChunksGetCanonicalIdentityStats();
    assert(stats.loaded == 3);
    assert(stats.unique == 2);
    assert(stats.duplicates == 1);
}

int main(void)
{
    memset(chunks, 0, sizeof(Chunk) * MAX_ACTIVE_CHUNKS);
    TestAdaptiveWorkerCountPolicy();
    TestFailedColumnGenerationReleasesSlot();
    TestShutdownWithoutWorkersCancelsQueuedJobs();
    AssertFreshStats();
    TestFrustumSphereEdgesRemainVisible();
    TestFrustumNearPlaneAndRenderAspect();
    TestChunkFrustumUsesSparseSectionHeights();
    TestChunkLodSelectionUsesRingsAndHysteresis();
    TestChunkLodTargetsAndStatsTrackLoadedChunks();
    TestChunkLodSwitchKeepsPreviousLevelUntilCacheIsReady();
    TestChunkLodRenderModelUsesActiveLevelAndFallbacks();
    TestMeshJobsIncludeLodIdentityAndPrioritizeExactUpgrades();
    TestSingleChunkUsesSingleJobAndNearestFirst();
    TestFullQueueLeavesDirtyChunkForLater();
    TestReusedLowSlotsDoNotStarveOlderJobs();
    TestPriorityMeshJobsBypassBackgroundWork();
    TestPriorityEditSupersedesInFlightSnapshot();
    TestPriorityEditFallsBackWhenBackgroundQueueIsSaturated();
    TestSectionPipelineReportsMeshWaitStage();
    TestFullGenerationQueueDoesNotFallBackToMainThread();
    TestBudgetAndInvalidSlotCleanup();
    TestEditDuringFlightKeepsSectionDirty();
    TestStaleJobDiscardedAfterChunkReload();
    TestStaleMeshJobDiscardedAfterSurfaceKeyChanges();
    TestWaterMeshUsesSnapshottedNeighborBoundary();
    TestSparseWaterBoundariesPreserveUnknownSections();
    TestSparseSignedSectionStorage();
    TestMaterializedAirDiffersFromMissingSection();
    TestImplicitTerrainLookupAndEditOverride();
    TestSectionGenerationJobsStageAndValidateResults();
    TestNearbySectionSchedulingPrioritizesPlayerSection();
    TestNearbySectionSchedulingUsesRenderDistance();
    TestNonEmptySectionsMaterialize();
    TestSavedEditForcesSectionMaterialization();
    TestExpandedBlockEditInstallationIsTransactional();
    TestNegativeSectionPruningKeepsVerticalWindow();
    TestNegativeSectionPruningPreservesRuntimeState();
    TestDistantSectionJobsReleaseQueueCapacity();
    TestSectionLifecycleHooksPermitFluidRuntimePruning();
    TestWrappedBlockEditUsesCanonicalIdentity();
    TestSurfaceChunkAliasesUseOneCanonicalIdentity();
    TestPolarChunkNeighborsBecomeDirty();
    TestCanonicalChunkIdentityStatsDetectAliases();
    TestWorkerPoolRunsJobsConcurrently();
    ChunksTestResetScheduler();
    puts("chunk streaming tests passed");
    return 0;
}
