#include "world/chunks_internal.h"
#include "world/terrain_cube.h"

#include <assert.h>
#include <stdio.h>

// The worker/terrain/storage implementation is real. The surrounding game
// environment stays neutral; these tests do not activate the runtime policy.
bool WorldIsSurfaceActive(void) { return false; }
int WorldSurfaceMapOriginX(void) { return 0; }
int WorldSurfaceMapOriginZ(void) { return 0; }
uint32_t WorldCurrentSurfaceId(void) { return 0; }
WorldSurfaceContext WorldCurrentSurfaceContext(void)
{ return WorldCurrentSurfaceSnapshot()->context; }
WorldBlockRegion WorldBlockRegionAt(int y)
{ (void)y; assert(!"unexpected legacy block query"); return WORLD_BLOCK_REGION_NONE; }
bool WorldCanAccessBlockY(int y) { return InHeight(y); }
BlockType SpaceBlockAt(int x, int y, int z)
{ (void)x; (void)y; (void)z; assert(false); return BLOCK_AIR; }
BlockType NetherBlockAt(int x, int y, int z)
{ (void)x; (void)y; (void)z; assert(false); return BLOCK_AIR; }

static void Reset(void)
{
    ChunksTestResetScheduler();
    // The general scheduler fixture fakes one worker; this fixture starts
    // actual pthread workers and must not inherit that simulated state.
    genWorkerThreadsStarted = 0;
    genWorkerThreadsConfigured = 0;
}

static TerrainCubeContext *Context(void)
{
    WorldSurfaceSnapshot snapshot = {.context = {.active = true,
        .kind = WORLD_SURFACE_HOME, .topology = {SURFACE_TOPOLOGY_CUBE_GRID, 1},
        .bodyId = 0, .seed = 1234}};
    TerrainCubeContext *context = TerrainCubeContextCreate(
        &snapshot, TERRAIN_FLAT, WorldCurrentGeneration());
    assert(context);
    return context;
}

static ChunkGenJob Prepare(TerrainCubeContext *context, int slot,
                           SurfaceCubeChunkKey key, ChunkGenScope scope, int sectionY)
{
    ChunkGenJob job = {.scope = scope, .sectionY = sectionY, .slotIndex = slot};
    assert(TerrainCubePrepareJob(context, key, &job));
    if (scope == CHUNK_GEN_SCOPE_COLUMN) {
        chunks[slot] = (Chunk){.generating = true, .spherical = true,
            .cubeGrid = true, .cubeKey = key, .generation = 1,
            .cx = key.u, .cz = key.v};
    }
    job.chunkGeneration = chunks[slot].generation;
    return job;
}

static void TestIdentityAndLifetime(void)
{
    TerrainCubeContext *context = Context();
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_POS_Y, 255, 0};
    ChunkGenJob job = Prepare(context, 0, key, CHUNK_GEN_SCOPE_COLUMN, 0);
    assert(ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    chunks[0].cubeKey.face = SURFACE_FACE_NEG_Y;
    assert(!ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    chunks[0].cubeKey = key;
    chunks[0].cubeKey.bodyId++;
    assert(!ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    chunks[0].cubeKey = key;
    chunks[0].cubeGrid = false;
    assert(!ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    chunks[0].cubeGrid = true;
    chunks[0].generation++;
    assert(!ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    chunks[0].generation--;
    // A worker can finish after the world relinquishes its own reference.
    TerrainCubeContextRelease(context);
    GenerateChunkJobPayload(&job, false);
    assert(job.succeeded && job.columnResult);
    assert(ChunkGetLocalBlock(job.columnResult, 0, 8, 0) == BLOCK_GRASS);
    assert(WorldInstallSurfaceSnapshot(NULL));
    assert(!ChunkGenJobTargetMatchesChunk(&job, &chunks[0]));
    CompleteChunkGenJob(&job);
    assert(!chunks[0].loaded && chunks[0].sectionCount == 0);
    assert(!job.columnResult && !job.cubeContext);
    ReleaseChunkGenJob(&job);
    Reset();
}

static void TestWorkerQueue(void)
{
    Reset();
    assert(ChunksConfigureWorkerCount(2));
    assert(ChunksStartGenThread());
    TerrainCubeContext *context = Context();
    for (int face = 0; face < 6; face++) {
        SurfaceCubeChunkKey key = {0, (SurfaceFace)face, 255, 0};
        ChunkGenJob job = Prepare(context, face, key, CHUNK_GEN_SCOPE_COLUMN, 0);
        assert(QueueCubeChunkGenJob(&job, &chunks[face]));
        assert(!job.cubeContext); // ownership moved into the queue
        ChunkGenJob duplicate = {.scope = CHUNK_GEN_SCOPE_COLUMN};
        assert(TerrainCubePrepareJob(context, key, &duplicate));
        assert(!QueueCubeChunkGenJob(&duplicate, &chunks[face]));
        ReleaseChunkGenJob(&duplicate);
    }
    TerrainCubeContextRelease(context);
    DrainChunkGen();
    for (int face = 0; face < 6; face++) {
        assert(chunks[face].loaded && !chunks[face].generating);
        assert(FindCubeChunk(chunks[face].cubeKey) == &chunks[face]);
        assert(ChunkGetLocalBlock(&chunks[face], 15, 8, 15) == BLOCK_GRASS);
        assert(ChunkTerrainSectionIsResolved(&chunks[face], 1));
        assert(!ChunkGetSectionConst(&chunks[face], 1));
    }
    context = Context();
    ChunkGenJob deep = Prepare(context, 0, chunks[0].cubeKey, CHUNK_GEN_SCOPE_SECTION, -128);
    assert(QueueCubeChunkGenJob(&deep, &chunks[0]));
    ChunkGenJob air = Prepare(context, 1, chunks[1].cubeKey, CHUNK_GEN_SCOPE_SECTION, 3);
    assert(QueueCubeChunkGenJob(&air, &chunks[1]));
    TerrainCubeContextRelease(context);
    DrainChunkGen();
    assert(ChunkGetLocalBlock(&chunks[0], 0, TERRAIN_CUBE_MIN_RADIAL, 0) == BLOCK_BEDROCK);
    assert(ChunkTerrainSectionIsResolved(&chunks[1], 3));
    assert(!ChunkGetSectionConst(&chunks[1], 3));
    ChunksShutdownGenThread();
    Reset();
}

static void TestSubmissionFailure(void)
{
    TerrainCubeContext *context = Context();
    ChunkGenJob job = Prepare(context, 0,
        (SurfaceCubeChunkKey){0, SURFACE_FACE_POS_Y, 0, 0}, CHUNK_GEN_SCOPE_COLUMN, 0);
    assert(!QueueCubeChunkGenJob(&job, &chunks[0])); // no workers
    assert(job.cubeContext == context);
    TerrainCubeContextRelease(context);
    ReleaseChunkGenJob(&job);
    Reset();
}

static void TestFailedAndStaleResults(void)
{
    TerrainCubeContext *context = Context();
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_NEG_Y, 0, 0};
    ChunkGenJob job = Prepare(context, 0, key, CHUNK_GEN_SCOPE_COLUMN, 0);
    ChunksTestFailNextColumnAllocation();
    GenerateChunkJobPayload(&job, false);
    assert(!job.succeeded);
    CompleteChunkGenJob(&job);
    assert(!job.cubeContext && !chunks[0].generating && !chunks[0].loaded);

    job = Prepare(context, 0, key, CHUNK_GEN_SCOPE_COLUMN, 0);
    GenerateChunkJobPayload(&job, false);
    assert(job.succeeded);
    chunks[0].cubeKey.face = SURFACE_FACE_POS_Y;
    CompleteChunkGenJob(&job);
    assert(!chunks[0].loaded && !chunks[0].sectionCount && !job.cubeContext);
    assert(chunks[0].cubeKey.face == SURFACE_FACE_POS_Y);

    chunks[0].loaded = true;
    chunks[0].generating = false;
    job = Prepare(context, 0, chunks[0].cubeKey, CHUNK_GEN_SCOPE_SECTION, -128);
    GenerateChunkJobPayload(&job, false);
    assert(job.succeeded && job.hasSectionBlocks);
    chunks[0].generation++;
    CompleteChunkGenJob(&job);
    assert(!ChunkTerrainSectionIsResolved(&chunks[0], -128));
    assert(!ChunkGetSectionConst(&chunks[0], -128) && !job.cubeContext);
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestQueuedCancellationAndCapacity(void)
{
    Reset();
    WorldSurfaceSnapshot home = {.context = {.active = true,
        .kind = WORLD_SURFACE_HOME, .topology = {SURFACE_TOPOLOGY_LEGACY_MAP, 1}}};
    assert(WorldInstallSurfaceSnapshot(&home));
    TerrainCubeContext *context = Context();
    // Deterministically hold work pending using the scheduler's simulated
    // worker fixture. TestWorkerQueue separately exercises real pthreads.
    genWorkerThreadsStarted = 1;
    chunks[0] = (Chunk){.loaded = true, .spherical = true, .cubeGrid = true,
        .cubeKey = {0, SURFACE_FACE_POS_Y, 0, 0}, .generation = 1};
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        ChunkGenJob job = Prepare(context, 0, chunks[0].cubeKey,
            CHUNK_GEN_SCOPE_SECTION, -128 + i);
        assert(QueueCubeChunkGenJob(&job, &chunks[0]));
    }
    ChunkGenJob extra = Prepare(context, 1,
        (SurfaceCubeChunkKey){0, SURFACE_FACE_NEG_Y, 0, 0}, CHUNK_GEN_SCOPE_COLUMN, 0);
    assert(!QueueCubeChunkGenJob(&extra, &chunks[1]));
    ReleaseChunkGenJob(&extra);
    TerrainCubeContextRelease(context);
    assert(CancelDistantNegativeSectionJobs(0) == MAX_CHUNK_GEN_JOBS);
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++)
        assert(!chunkGenJobs[i].cubeContext && !chunkGenJobs[i].inUse);
    Reset();
    assert(WorldInstallSurfaceSnapshot(NULL));
}

static void TestCrossFaceDirtyPropagation(void)
{
    Reset();
    SurfaceCubeChunkKey source = {0, SURFACE_FACE_POS_Y, 255, 0}, neighbor;
    assert(SurfaceCubeChunkNeighbor(source, SURFACE_CUBE_U_PLUS, &neighbor, NULL));
    assert(source.face != neighbor.face);
    for (int i = 0; i < 3; i++) {
        SurfaceCubeChunkKey key = i == 0 ? source : neighbor;
        if (i == 2) key.bodyId++;
        chunks[i] = (Chunk){.loaded = true, .spherical = true, .cubeGrid = true,
            .cubeKey = key, .cx = key.u, .cz = key.v};
        assert(ChunkSetLocalBlock(&chunks[i], 0, 0, 0, BLOCK_STONE));
        ChunkGetSection(&chunks[i], 0, false)->dirty = false;
        ChunkIndexInsert(&chunks[i]);
    }
    MarkGeneratedSectionAndNeighborsDirty(&chunks[0], 0);
    assert(ChunkGetSection(&chunks[0], 0, false)->dirty);
    assert(ChunkGetSection(&chunks[1], 0, false)->dirty);
    assert(!ChunkGetSection(&chunks[2], 0, false)->dirty);
    Reset();
}

int main(void)
{
    TestIdentityAndLifetime();
    TestSubmissionFailure();
    TestFailedAndStaleResults();
    TestQueuedCancellationAndCapacity();
    TestCrossFaceDirtyPropagation();
    TestWorkerQueue();
    puts("cube generation queue tests passed");
    return 0;
}
