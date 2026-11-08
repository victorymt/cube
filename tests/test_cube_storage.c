#include "world/chunks_internal.h"
#include "world/terrain_cube.h"

#include <assert.h>
#include <stdio.h>

static void TestIndexNamespaces(void)
{
    ChunkIndexReset();
    for (int face = 0; face < 6; face++) {
        chunks[face] = (Chunk){.loaded = true, .spherical = true, .cubeGrid = true,
            .cubeKey = {7, (SurfaceFace)face, 128, 128}};
        ChunkIndexInsert(&chunks[face]);
    }
    chunks[6] = (Chunk){.loaded = true, .spherical = true, .cubeGrid = true,
        .cubeKey = {8, SURFACE_FACE_POS_Y, 128, 128}};
    ChunkIndexInsert(&chunks[6]);
    chunks[7] = (Chunk){.loaded = true, .spherical = true};
    ChunkIndexInsert(&chunks[7]);
    chunks[8] = (Chunk){.loaded = true};
    ChunkIndexInsert(&chunks[8]);
    for (int face = 0; face < 6; face++)
        assert(FindCubeChunk((SurfaceCubeChunkKey){7, (SurfaceFace)face, 128, 128}) == &chunks[face]);
    assert(FindCubeChunk((SurfaceCubeChunkKey){8, SURFACE_FACE_POS_Y, 128, 128}) == &chunks[6]);
    assert(ChunkIndexFindSurface((SurfaceChunkKey){0}) == &chunks[7]);
    assert(ChunkIndexFindFlat(0, 0) == &chunks[8]);
    assert(!FindCubeChunk((SurfaceCubeChunkKey){9, SURFACE_FACE_POS_Y, 128, 128}));
    assert(!FindCubeChunk((SurfaceCubeChunkKey){7, SURFACE_FACE_POS_Y, -1, 128}));
    // A missed explicit removal cannot turn an old hash entry into another
    // body's chunk when a slot is recycled.
    SurfaceCubeChunkKey old = chunks[0].cubeKey;
    chunks[0].cubeKey.bodyId = 99;
    assert(!FindCubeChunk(old));
    assert(FindCubeChunk(chunks[0].cubeKey) == &chunks[0]);
    chunks[0].loaded = false;
    assert(!FindCubeChunk(chunks[0].cubeKey));
    memset(chunks, 0, sizeof(chunks));
    ChunkIndexReset();
}

static void TestRealChunkGeneration(void)
{
    WorldSurfaceSnapshot snapshot = {.context = {.active = true, .kind = WORLD_SURFACE_HOME,
        .topology = {SURFACE_TOPOLOGY_CUBE_GRID, 1}, .bodyId = 0, .seed = 123}};
    TerrainCubeContext *context = TerrainCubeContextCreate(&snapshot, TERRAIN_FLAT, 3);
    assert(context);
    for (int face = 0; face < 6; face++) {
        Chunk staging = {0};
        SurfaceCubeChunkKey key = {0, (SurfaceFace)face, 255, 0};
        assert(TerrainCubeGenerateChunk(context, key, &staging));
        assert(staging.cubeGrid && SurfaceCubeChunkKeyEqual(staging.cubeKey, key));
        assert(staging.sectionCount == 2); // Ground -1/0; upper section is implicit air.
        assert(ChunkTerrainSectionIsResolved(&staging, 1));
        assert(!ChunkGetSectionConst(&staging, 1));
        assert(ChunkGetLocalBlock(&staging, 0, 8, 0) == BLOCK_GRASS);
        assert(ChunkGetLocalBlock(&staging, 15, 7, 15) == BLOCK_DIRT);
        assert(TerrainCubeGenerateChunkSection(context, &staging, -128));
        assert(ChunkGetLocalBlock(&staging, 3, TERRAIN_CUBE_MIN_RADIAL, 12) == BLOCK_BEDROCK);
        assert(!TerrainCubeGenerateChunkSection(context, &staging, -128));
        ChunkClearBlockStorage(&staging);
    }
    TerrainCubeContextRelease(context);
}

static void TestCanonicalCellStorage(void)
{
    Chunk chunk = { 0 };
    const int x = 3;
    const int y = 5;
    const int z = 7;

    assert(ChunkSetLocalCell(
        &chunk, x, y, z,
        (WorldCell){ .block = BLOCK_COMMON_REED, .waterVolume = 77u }));
    WorldCell cell = ChunkGetLocalCell(&chunk, x, y, z);
    assert(cell.block == BLOCK_COMMON_REED);
    assert(cell.waterVolume == 77u);
    assert(ChunkGetLocalBlock(&chunk, x, y, z) == BLOCK_COMMON_REED);
    assert(ChunkGetLocalWaterVolume(&chunk, x, y, z) == 77u);

    assert(ChunkSetLocalBlock(&chunk, x, y, z, BLOCK_STONE));
    cell = ChunkGetLocalCell(&chunk, x, y, z);
    assert(cell.block == BLOCK_STONE);
    assert(cell.waterVolume == 0u);
    assert(!ChunkSetLocalWaterVolume(&chunk, x, y, z, 10u));

    assert(ChunkSetLocalBlock(&chunk, x, y, z, BLOCK_COMMON_REED));
    assert(ChunkSetLocalWaterVolume(&chunk, x, y, z, 42u));
    assert(ChunkGetLocalWaterVolume(&chunk, x, y, z) == 42u);

    assert(ChunkSetLocalCell(
        &chunk, x, y, z,
        (WorldCell){ .block = BLOCK_STONE, .waterVolume = 1u }) == false);
    assert(ChunkGetLocalWaterVolume(&chunk, x, y, z) == 42u);
    ChunkClearBlockStorage(&chunk);
}

static void TestLegacyWaterNormalization(void)
{
    Chunk chunk = { 0 };
    ChunkSection *section = ChunkGetSection(&chunk, 0, true);
    assert(section);
    section->blocks[2][2][2] = BLOCK_WATER;

    WorldCell cell = ChunkGetLocalCell(&chunk, 2, 2, 2);
    assert(cell.block == BLOCK_AIR);
    assert(cell.waterVolume == WORLD_CELL_WATER_FULL);

    assert(ChunkSetLocalWaterVolume(&chunk, 2, 2, 2, 33u));
    assert(section->blocks[2][2][2] == BLOCK_AIR);
    cell = ChunkGetLocalCell(&chunk, 2, 2, 2);
    assert(cell.block == BLOCK_AIR);
    assert(cell.waterVolume == 33u);

    section->blocks[4][2][2] = BLOCK_WATER;
    assert(ChunkSetLocalWaterVolume(&chunk, 4, 2, 2, 0u));
    assert(section->blocks[4][2][2] == BLOCK_AIR);
    assert(ChunkGetLocalWaterVolume(&chunk, 4, 2, 2) == 0u);
    ChunkClearBlockStorage(&chunk);
}

int main(void)
{
    TestIndexNamespaces();
    TestRealChunkGeneration();
    TestCanonicalCellStorage();
    TestLegacyWaterNormalization();
    puts("cube chunk storage tests passed");
    return 0;
}
