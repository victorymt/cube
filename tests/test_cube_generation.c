#include "world/chunks_internal.h"
#include "world/terrain_cube.h"
#include "world/chunks_cube.h"
#include "world/world_cube.h"
#include "presentation/surface_render_transform.h"
#include "world/surface_geometry.h"

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
    WorldCubeClearEdits();
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

static WorldBlockAddress Address(uint32_t body, SurfaceFace face, int u, int v, int y)
{
    return (WorldBlockAddress){.dimension = body ? WORLD_DIMENSION_PLANET : WORLD_DIMENSION_HOME,
        .at.surface = {body, face, u, v, y}};
}

static void TestCubeEditIndex(void)
{
    Reset();
    BlockType type = BLOCK_GOLD_ORE;
    for (int i = 0; i < 2048; i++) {
        WorldBlockAddress a = Address((unsigned)i % 3, (SurfaceFace)(i % 6),
                                      i, 4095 - i, -i);
        assert(WorldCubeSetBlock(a, BLOCK_BRICK));
    }
    assert(WorldCubeEditCount() == 2048);
    for (int i = 0; i < 2048; i++) {
        WorldBlockAddress a = Address((unsigned)i % 3, (SurfaceFace)(i % 6),
                                      i, 4095 - i, -i);
        assert(WorldCubeGetEdit(a, &type) && type == BLOCK_BRICK);
        assert(WorldCubeSetBlock(a, BLOCK_AIR));
        assert(WorldCubeGetEdit(a, &type) && type == BLOCK_AIR);
    }
    assert(WorldCubeEditCount() == 2048);
    WorldBlockAddress invalid = Address(0, SURFACE_FACE_POS_Y, 0, 0, -2049);
    assert(!WorldCubeSetBlock(invalid, BLOCK_BRICK));
    invalid = Address(0, SURFACE_FACE_POS_Y, 0, 0, 5);
    assert(!WorldCubeSetBlock(invalid, (BlockType)-1));
    invalid.dimension = WORLD_DIMENSION_SPACE;
    assert(!WorldCubeSetBlock(invalid, BLOCK_BRICK));
    invalid = Address(1, SURFACE_FACE_POS_Y, 0, 0, 5);
    invalid.dimension = WORLD_DIMENSION_HOME;
    assert(!WorldCubeSetBlock(invalid, BLOCK_BRICK));
    assert(WorldCubeEditCount() == 2048);
    WorldCubeClearEdits();
    assert(!WorldCubeEditCount());
    assert(!WorldCubeGetEdit(Address(0, SURFACE_FACE_POS_Y, 0, 0, 0), &type));
}

static void TestCubeRuntimeReload(void)
{
    Reset();
    TerrainCubeContext *context = Context();
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_POS_Y, 255, 0};
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
    WorldBlockAddress ground = Address(0, key.face, 4095, 0, 8);
    WorldBlockAddress deep = ground; deep.at.surface.radial = -1000;
    WorldBlockAddress air = ground; air.at.surface.radial = 50;
    BlockType type = BLOCK_GOLD_ORE;
    assert(WorldCubeBlockAt(ground, &type) && type == BLOCK_GRASS);
    WorldCubeTestFailNextReserve();
    assert(!WorldCubeSetBlock(ground, BLOCK_AIR));
    assert(!WorldCubeEditCount());
    assert(WorldCubeBlockAt(ground, &type) && type == BLOCK_GRASS);
    assert(!WorldCubeBlockAt(deep, &type) && type == BLOCK_GRASS);
    assert(WorldCubeSetBlock(ground, BLOCK_AIR));
    assert(WorldCubeSetBlock(deep, BLOCK_DIAMOND_ORE));
    assert(!ChunkGetSectionConst(FindCubeChunk(key), SurfaceSectionYFromBlockY(-1000)));
    assert(ChunksRequestCubeSection(context, key, SurfaceSectionYFromBlockY(-1000)) == CUBE_CHUNK_READY);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 15, -1000, 0) == BLOCK_DIAMOND_ORE);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 14, -1000, 0) == BLOCK_DIRT);
    assert(WorldCubeSetBlock(air, BLOCK_BRICK));
    assert(ChunksRequestCubeSection(context, key, 3) == CUBE_CHUNK_READY);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 15, 50, 0) == BLOCK_BRICK);
    assert(ChunksReleaseCubeColumn(key));
    assert(!FindCubeChunk(key));
    assert(WorldCubeEditCount() == 3);
    assert(WorldCubeBlockAt(ground, &type) && type == BLOCK_AIR);
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 15, 8, 0) == BLOCK_AIR);
    assert(ChunksRequestCubeSection(context, key, 3) == CUBE_CHUNK_READY);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 15, 50, 0) == BLOCK_BRICK);
    assert(ChunksRequestCubeSection(context, key, SurfaceSectionYFromBlockY(-1000)) == CUBE_CHUNK_READY);
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 15, -1000, 0) == BLOCK_DIAMOND_ORE);

    WorldBlockAddress across;
    assert(WorldBlockNeighbor(ground, WORLD_BLOCK_U_PLUS, &across, NULL));
    assert(across.at.surface.face != ground.at.surface.face);
    SurfaceCubeChunkKey neighbor;
    assert(SurfaceCubeChunkAt(across.at.surface, &neighbor));
    assert(WorldCubeSetBlock(across, BLOCK_GOLD_ORE));
    assert(ChunksRequestCubeColumn(context, neighbor, false) == CUBE_CHUNK_READY);
    assert(WorldCubeBlockAt(across, &type) && type == BLOCK_GOLD_ORE);
    assert(WorldCubeBlockAt(ground, &type) && type == BLOCK_AIR);
    WorldBlockAddress otherBody = across;
    otherBody.dimension = WORLD_DIMENSION_PLANET; otherBody.at.surface.bodyId = 77;
    assert(WorldCubeSetBlock(otherBody, BLOCK_RED));
    assert(WorldCubeBlockAt(across, &type) && type == BLOCK_GOLD_ORE);
    assert(WorldCubeBlockAt(otherBody, &type) && type == BLOCK_RED);

    assert(WorldInstallSurfaceSnapshot(NULL));
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_UNAVAILABLE);
    TerrainCubeContextRelease(context);
    context = Context();
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
    assert(WorldCubeBlockAt(ground, &type) && type == BLOCK_AIR);
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestCubeRuntimeAsyncEdits(void)
{
    Reset();
    assert(ChunksConfigureWorkerCount(2) && ChunksStartGenThread());
    TerrainCubeContext *context = Context();
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_NEG_Y, 0, 255};
    WorldBlockAddress cell = Address(0, key.face, 0, 4095, 8);
    assert(ChunksRequestCubeColumn(context, key, true) == CUBE_CHUNK_PENDING);
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_PENDING);
    assert(WorldCubeSetBlock(cell, BLOCK_BRICK));
    assert(WorldCubeSetBlock(cell, BLOCK_GOLD_ORE));
    DrainChunkGen();
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 0, 8, 15) == BLOCK_GOLD_ORE);
    assert(!FindCubeChunk(key)->preloaded);
    cell.at.surface.radial = 50;
    assert(ChunksRequestCubeSection(context, key, 3) == CUBE_CHUNK_PENDING);
    assert(WorldCubeSetBlock(cell, BLOCK_BLUE));
    DrainChunkGen();
    assert(ChunkGetLocalBlock(FindCubeChunk(key), 0, 50, 15) == BLOCK_BLUE);
    assert(ChunksReleaseCubeColumn(key));
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_PENDING);
    // Slot can be released and reused while its old job is in flight.
    assert(ChunksReleaseCubeColumn(key));
    SurfaceCubeChunkKey replacement = key; replacement.face = SURFACE_FACE_POS_Y;
    assert(ChunksRequestCubeColumn(context, replacement, false) == CUBE_CHUNK_PENDING);
    DrainChunkGen();
    assert(!FindCubeChunk(key));
    assert(FindCubeChunk(replacement));
    assert(ChunkGetLocalBlock(FindCubeChunk(replacement), 0, 8, 15) == BLOCK_GRASS);
    TerrainCubeContextRelease(context);
    ChunksShutdownGenThread();
    Reset();
}

static void TestCubePoolAdmission(void)
{
    Reset();
    TerrainCubeContext *context = Context();
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++)
        chunks[i] = (Chunk){.loaded = true, .generation = 17};
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_POS_Y, 0, 0};
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_UNAVAILABLE);
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++)
        assert(chunks[i].loaded && !chunks[i].cubeGrid && chunks[i].generation == 17);
    chunks[0].loaded = false;
    ChunksTestFailNextColumnAllocation();
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_UNAVAILABLE);
    assert(!chunks[0].generating && !chunks[0].loaded);
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestPhysicalCoverage(void)
{
    Reset();
    TerrainCubeContext *context = Context();
    SurfaceVector north = {0, SURFACE_RADIUS_BLOCKS + 10, 0};
    CubeCoverageStatus status;
    assert(ChunksUpdateCubeCoverage(context, north, 24, 2, &status));
    assert(status.submitted == 2 && status.ready == 2 && status.missing > 0);
    unsigned expected = status.admitted;
    assert(status.requested == expected && !status.saturated);
    for (int i = 0; i < 30 && status.ready < expected; i++)
        assert(ChunksUpdateCubeCoverage(context, north, 24, 3, &status));
    assert(status.ready == expected && status.pending == 0 && status.missing == 0);
    assert(ChunksUpdateCubeCoverage(context, north, 24, 0, &status));
    assert(status.submitted == 0 && status.released == 0 && status.ready == expected);
    SurfaceVector south = {0, -SURFACE_RADIUS_BLOCKS - 10, 0};
    assert(ChunksUpdateCubeCoverage(context, south, 24, 1, &status));
    assert(status.released == expected && status.ready == 1 && status.submitted == 1);
    CubeCoverageStatus before = status;
    assert(!ChunksUpdateCubeCoverage(context, south, NAN, 100, &status));
    assert(memcmp(&before, &status, sizeof(status)) == 0 && GetActiveChunkCount() == 1);
    SurfaceVector corner = {1, 1, 1};
    corner = SurfaceVectorScale(SurfaceVectorNormalize(corner), SURFACE_RADIUS_BLOCKS + 10);
    assert(ChunksUpdateCubeCoverage(context, corner, 208, 0, &status));
    assert(status.saturated && status.requested > MAX_ACTIVE_CHUNKS);
    assert(status.admitted == MAX_ACTIVE_CHUNKS && status.missing == status.admitted);
    assert(status.ready == 0 && status.submitted == 0);
    // A stale immutable input may not evict or submit any columns.
    assert(WorldInstallSurfaceSnapshot(NULL));
    before = status;
    assert(!ChunksUpdateCubeCoverage(context, north, 24, 10, &status));
    assert(memcmp(&before, &status, sizeof(status)) == 0);
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestCubeMeshHalo(void)
{
    Reset();
    TerrainCubeContext *context = Context();
    for (int face = 0; face < 6; face++) for (int edge = 0; edge < 4; edge++) {
        SurfaceAddress cell = {0, (SurfaceFace)face,
            edge == 0 ? 0 : edge == 1 ? SURFACE_FACE_BLOCKS-1 : 2048,
            edge == 2 ? 0 : edge == 3 ? SURFACE_FACE_BLOCKS-1 : 2048, 8};
        SurfaceAddress across;
        SurfaceCubeChunkKey key, neighbor;
        assert(SurfaceCubeCellNeighbor(cell, (SurfaceCubeEdge)edge, &across, NULL));
        assert(SurfaceCubeChunkAt(cell, &key) && SurfaceCubeChunkAt(across, &neighbor));
        assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
        assert(ChunksRequestCubeColumn(context, neighbor, false) == CUBE_CHUNK_READY);
        WorldBlockAddress edit = {.dimension = WORLD_DIMENSION_HOME, .at.surface = across};
        assert(WorldCubeSetBlock(edit, BLOCK_GLASS));
        SurfaceBoundarySnapshot halo;
        CaptureCubeBoundary(&halo, key, 0);
        int x = cell.u % CHUNK_SIZE + (edge == 0 ? -1 : edge == 1 ? 1 : 0);
        int z = cell.v % CHUNK_SIZE + (edge == 2 ? -1 : edge == 3 ? 1 : 0);
        assert(halo.known[x+1][9][z+1] && halo.blocks[x+1][9][z+1] == BLOCK_GLASS);
        assert(ChunksReleaseCubeColumn(neighbor));
        CaptureCubeBoundary(&halo, key, 0);
        assert(!halo.known[x+1][9][z+1]);
        assert(ChunksReleaseCubeColumn(key));
    }
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestCubeMeshIdentity(void)
{
    Reset();
    TerrainCubeContext *context = Context();
    SurfaceCubeChunkKey key = {0, SURFACE_FACE_POS_Y, 128, 128};
    assert(ChunksRequestCubeColumn(context, key, false) == CUBE_CHUNK_READY);
    Chunk *chunk = FindCubeChunk(key);
    ChunkSection *section = ChunkGetSection(chunk, 0, false);
    assert(section);
    for (int fault = 0; fault < 4; fault++) {
        section->dirty = true;
        section->exactModelReady = false;
        ChunksTestSeedMeshJob(0, (int)(chunk - chunks), key.u, key.v, 0, true);
        MeshJob *job = &meshJobs[0];
        job->cubeGrid = job->spherical = true;
        job->cubeKey = key;
        job->cubeWorldGeneration = WorldCurrentGeneration();
        job->chunkGeneration = chunk->generation;
        job->sectionStamp = section->dirtyStamp;
        if (fault == 0) job->cubeKey.face = SURFACE_FACE_NEG_Y;
        if (fault == 1) job->cubeWorldGeneration++;
        if (fault == 2) job->cubeGrid = false;
        ProcessFinishedMeshJobs(100);
        assert(section->exactModelReady == (fault == 3));
        assert(section->dirty == (fault != 3));
    }
    TerrainCubeContextRelease(context);
    Reset();
}

static void TestCubeRenderTransform(void)
{
    for (int face = 0; face < 6; face++) {
        Chunk chunk = {.cubeGrid = true, .spherical = true,
            .cubeKey = {0, (SurfaceFace)face, 128, 128}};
        SurfaceVector anchor;
        assert(SurfacePointFromCoordinates((SurfaceCoordinates){(SurfaceFace)face, 2048, 2048, 0}, &anchor));
        Vector3 direction = {(float)anchor.x, (float)anchor.y, (float)anchor.z};
        direction = Vector3Normalize(direction);
        Vector3 camera = {atan2f(direction.z, direction.x) * SURFACE_RADIUS_BLOCKS,
            18, asinf(direction.y) * SURFACE_RADIUS_BLOCKS};
        Matrix transform = SurfaceChunkRenderTransform(&chunk, 0, camera, 0, 0);
        SurfaceFrame frame = SurfaceFrameAtMapCoordinates(0, camera.x, camera.z, 0);
        Vector3 center;
        float radius;
        assert(SurfaceChunkRenderBounds(&chunk, 0, &center, &radius));
        for (int u = 0; u <= 16; u += 4) for (int v = 0; v <= 16; v += 4)
            for (int y = 0; y <= SURFACE_SECTION_HEIGHT; y += 4) {
                SurfaceVector point;
                assert(SurfacePointFromCoordinates((SurfaceCoordinates){(SurfaceFace)face, 2048+u, 2048+v, y}, &point));
                SurfaceVector delta = SurfaceVectorSubtract(point, anchor);
                Vector3 local = {(float)delta.x, (float)delta.y, (float)delta.z};
                assert(Vector3Distance(local, center) <= radius);
                Vector3 actual = Vector3Transform(local, transform);
                Vector3 expected = SurfaceFramePlanetToLocal(&frame,
                    (Vector3){(float)point.x, (float)point.y, (float)point.z});
                expected.x += camera.x; expected.z += camera.z;
                assert(Vector3Distance(actual, expected) < .002f);
            }
    }
}

static void TestCubeMeshProjection(void)
{
    for (int face = 0; face < 6; face++) {
        SurfaceCubeChunkKey key = {0, (SurfaceFace)face, 0, 255};
        MeshJob *job = calloc(1, sizeof(*job));
        assert(job);
        job->cubeGrid = job->spherical = true;
        job->cubeKey = key;
        job->cx = key.u; job->cz = key.v; job->sectionY = 0;
        job->blocks[0][8][15] = BLOCK_STONE;
        memset(job->boundary.known, 1, sizeof(job->boundary.known));
        BuildMeshJobPayload(job);
        assert(job->hasMesh && job->mesh.vertexCount == 36);
        SurfaceVector anchor;
        assert(SurfacePointFromCoordinates((SurfaceCoordinates){key.face, 0, 4080, 0}, &anchor));
        for (int i = 0; i < job->mesh.vertexCount; i++) {
            float *v = job->mesh.vertices + i*3;
            SurfaceVector point = SurfaceVectorAdd(anchor, (SurfaceVector){v[0], v[1], v[2]});
            // Every rendered vertex belongs to the exact projected cell,
            // including the last row of each face. No map-coordinate warp.
            bool matched = false;
            for (int corner = 0; corner < 8; corner++) {
                SurfaceVector expected;
                assert(SurfacePointFromCoordinates((SurfaceCoordinates){key.face,
                    corner & 1 ? 1 : 0, corner & 2 ? 4096 : 4095,
                    corner & 4 ? 9 : 8}, &expected));
                if (SurfaceVectorLength(SurfaceVectorSubtract(point, expected)) < .0001) matched = true;
            }
            assert(matched);
            float *n = job->mesh.normals + i*3;
            assert(fabsf(n[0]*n[0] + n[1]*n[1] + n[2]*n[2] - 1) < .0001);
        }
        SurfaceTriangle triangles[12];
        assert(SurfaceCellTriangles((SurfaceAddress){0, key.face, 0, 4095, 8},
            SURFACE_CELL_BOX_FULL, triangles));
        for (int i = 0; i < job->mesh.vertexCount; i += 3) {
            const float *v = job->mesh.vertices + i*3;
            SurfaceVector a = SurfaceVectorAdd(anchor, (SurfaceVector){v[0], v[1], v[2]});
            SurfaceVector b = SurfaceVectorAdd(anchor, (SurfaceVector){v[3], v[4], v[5]});
            SurfaceVector c = SurfaceVectorAdd(anchor, (SurfaceVector){v[6], v[7], v[8]});
            SurfaceVector centroid = SurfaceVectorScale(SurfaceVectorAdd(a, SurfaceVectorAdd(b, c)), 1.0/3);
            if (fabs(SurfaceVectorLength(a) - (SURFACE_RADIUS_BLOCKS + 9)) > .0001 ||
                fabs(SurfaceVectorLength(b) - (SURFACE_RADIUS_BLOCKS + 9)) > .0001 ||
                fabs(SurfaceVectorLength(c) - (SURFACE_RADIUS_BLOCKS + 9)) > .0001) continue;
            bool found = false;
            for (int j = 10; j < 12; j++) {
                SurfaceVector expected = SurfaceVectorScale(SurfaceVectorAdd(triangles[j].a,
                    SurfaceVectorAdd(triangles[j].b, triangles[j].c)), 1.0/3);
                if (SurfaceVectorLength(SurfaceVectorSubtract(centroid, expected)) < .0001) found = true;
            }
            assert(found);
        }
        FreeMeshData(&job->mesh);
        FreeMeshData(&job->waterMesh);
        FreeMeshData(&job->floraMesh);
        free(job->floraInstances);
        free(job);
    }
    ChunkGreedyMeshWorkerScratchShutdown();
    ChunkMeshWorkerScratchShutdown();
}

int main(void)
{
    TestCubeMeshIdentity();
    TestCubeRenderTransform();
    TestPhysicalCoverage();
    TestCubeMeshHalo();
    TestCubeMeshProjection();
    TestIdentityAndLifetime();
    TestSubmissionFailure();
    TestFailedAndStaleResults();
    TestQueuedCancellationAndCapacity();
    TestCrossFaceDirtyPropagation();
    TestWorkerQueue();
    TestCubeEditIndex();
    TestCubeRuntimeReload();
    TestCubeRuntimeAsyncEdits();
    TestCubePoolAdmission();
    puts("cube generation queue tests passed");
    return 0;
}
