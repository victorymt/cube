#include "world/chunks.h"

#define chunks (ChunksMutableForTesting())
#include "world/terrain.h"
#include "world/world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int terrainBaseBlockLookups = 0;

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

bool WorldIsSurfaceActive(void)
{
    return true;
}

bool HomeWorldSurfaceIsActive(void)
{
    return true;
}

BlockType TerrainBaseBlockAt(int x, int y, int z, TerrainMode mode)
{
    terrainBaseBlockLookups++;
    (void)x;
    (void)y;
    (void)z;
    (void)mode;
    return BLOCK_AIR;
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

static void FreeTestMesh(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

typedef enum TestMeshPartition {
    TEST_MESH_SOLID = 0,
    TEST_MESH_WATER,
    TEST_MESH_FLORA
} TestMeshPartition;

static const int TEST_CHUNK_FACES[6][3] = {
    { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
    { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
};

static bool BuildTestMesh(
    const unsigned short (*blocks)[CHUNK_SIZE], TestMeshPartition partition,
    Mesh *mesh)
{
    switch (partition) {
    case TEST_MESH_SOLID:
        return BuildSurfaceSolidMeshData(
            blocks, WORLD_HEIGHT, 0, 0, 0, TEST_CHUNK_FACES, NULL, 0, mesh);
    case TEST_MESH_WATER:
        return BuildSurfaceWaterMeshData(
            blocks, NULL, WORLD_HEIGHT, 0, 0, 0, TEST_CHUNK_FACES, NULL, 0,
            mesh);
    case TEST_MESH_FLORA:
        return BuildFloraMeshData(
            blocks, WORLD_HEIGHT, 0, 0, 0, TEST_CHUNK_FACES, NULL, 0, mesh);
    }
    return false;
}

static void AssertMeshWellFormed(const Mesh *mesh, int expectedVertexCount)
{
    assert(mesh->vertexCount == expectedVertexCount);
    assert(mesh->vertexCount % 6 == 0);
    assert(mesh->triangleCount == mesh->vertexCount / 3);
    assert(mesh->vertices != NULL);
    assert(mesh->normals != NULL);
    assert(mesh->texcoords != NULL);
    assert(mesh->texcoords2 != NULL);
    assert(mesh->colors != NULL);

    for (int vertex = 0; vertex < mesh->vertexCount; vertex++) {
        for (int component = 0; component < 3; component++) {
            assert(isfinite(mesh->vertices[vertex * 3 + component]));
            assert(isfinite(mesh->normals[vertex * 3 + component]));
        }
        for (int component = 0; component < 2; component++) {
            assert(isfinite(mesh->texcoords[vertex * 2 + component]));
            assert(isfinite(mesh->texcoords2[vertex * 2 + component]));
        }
        assert(mesh->colors[vertex * 4 + 3] > 0);
    }
}

static void AssertSpecialBlockMeshContracts(void)
{
    typedef struct SpecialBlockCase {
        BlockType type;
        TestMeshPartition partition;
        int expectedVertexCount;
    } SpecialBlockCase;
    static const SpecialBlockCase cases[] = {
        { BLOCK_TORCH, TEST_MESH_WATER, 36 },
        { BLOCK_ALBUM, TEST_MESH_WATER, 30 },
        { BLOCK_SLAB, TEST_MESH_SOLID, 36 },
        { BLOCK_DOOR, TEST_MESH_SOLID, 30 },
        { BLOCK_DOOR_OPEN, TEST_MESH_SOLID, 30 },
        { BLOCK_STONE_STAIRS, TEST_MESH_SOLID, 72 },
        { BLOCK_WOOD_STAIRS, TEST_MESH_SOLID, 72 },
        { BLOCK_FENCE_GATE, TEST_MESH_SOLID, 18 },
        { BLOCK_FENCE_GATE_OPEN, TEST_MESH_SOLID, 18 },
        { BLOCK_GLASS_PANE, TEST_MESH_WATER, 30 },
        { BLOCK_FLOWER, TEST_MESH_FLORA, 12 },
        { BLOCK_MUSHROOM, TEST_MESH_FLORA, 12 },
        { BLOCK_TALL_GRASS, TEST_MESH_FLORA, 12 },
        { BLOCK_FERN, TEST_MESH_FLORA, 12 },
        { BLOCK_REED, TEST_MESH_FLORA, 12 },
        { BLOCK_MOSS_CARPET, TEST_MESH_FLORA, 12 },
        { BLOCK_LICHEN, TEST_MESH_FLORA, 12 },
        { BLOCK_MICROBIAL_MAT, TEST_MESH_FLORA, 12 },
        { BLOCK_MYCELIUM, TEST_MESH_FLORA, 12 },
        { BLOCK_CHEMO_MAT, TEST_MESH_FLORA, 12 },
        { BLOCK_LIVING_STEM, TEST_MESH_SOLID, 36 },
        { BLOCK_CANOPY_FROND, TEST_MESH_WATER, 36 },
        { BLOCK_LUMINOUS_POD, TEST_MESH_WATER, 36 },
        { BLOCK_FUNGAL_STEM, TEST_MESH_SOLID, 36 },
        { BLOCK_SPORE_CAP, TEST_MESH_WATER, 36 },
        { BLOCK_CRYSTAL_BLOOM, TEST_MESH_SOLID, 36 },
        { BLOCK_VENT_CHIMNEY, TEST_MESH_SOLID, 36 },
        { BLOCK_SPACESHIP, TEST_MESH_SOLID, 576 },
        { BLOCK_SPACESHIP_CORE_NORTH, TEST_MESH_SOLID, 576 },
        { BLOCK_SPACESHIP_CORE_EAST, TEST_MESH_SOLID, 576 },
        { BLOCK_SPACESHIP_CORE_SOUTH, TEST_MESH_SOLID, 576 },
        { BLOCK_SPACESHIP_CORE_WEST, TEST_MESH_SOLID, 576 }
    };
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];

    for (int index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); index++) {
        memset(blocks, 0, sizeof(blocks));
        blocks[5][6][7] = (unsigned short)cases[index].type;
        Mesh mesh = { 0 };
        assert(BuildTestMesh(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            cases[index].partition, &mesh));
        AssertMeshWellFormed(&mesh, cases[index].expectedVertexCount);
        FreeTestMesh(&mesh);
    }

    memset(blocks, 0, sizeof(blocks));
    blocks[5][6][7] = BLOCK_SPACESHIP_OCCUPIED;
    Mesh occupied = { 0 };
    assert(!BuildTestMesh(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        TEST_MESH_SOLID, &occupied));
    assert(occupied.vertexCount == 0);
    assert(occupied.vertices == NULL);
}

static void AssertEcologyPlantMeshShapes(void)
{
    typedef struct PlantShapeCase {
        BlockType type;
        float expectedHeight;
        bool carpet;
    } PlantShapeCase;
    static const PlantShapeCase cases[] = {
        { BLOCK_TALL_GRASS, 0.78f, false },
        { BLOCK_FERN, 0.62f, false },
        { BLOCK_REED, 0.96f, false },
        { BLOCK_MOSS_CARPET, 0.0f, true },
        { BLOCK_LICHEN, 0.30f, false },
        { BLOCK_MICROBIAL_MAT, 0.0f, true },
        { BLOCK_MYCELIUM, 0.0f, true },
        { BLOCK_CHEMO_MAT, 0.0f, true }
    };
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    for (size_t index = 0; index < sizeof(cases) / sizeof(cases[0]); index++) {
        memset(blocks, 0, sizeof(blocks));
        blocks[5][6][7] = (unsigned short)cases[index].type;
        Mesh mesh = { 0 };
        assert(BuildTestMesh(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            TEST_MESH_FLORA, &mesh));
        AssertMeshWellFormed(&mesh, 12);
        float minX = INFINITY;
        float minY = INFINITY;
        float minZ = INFINITY;
        float maxX = -INFINITY;
        float maxY = -INFINITY;
        float maxZ = -INFINITY;
        for (int vertex = 0; vertex < mesh.vertexCount; vertex++) {
            minX = fminf(minX, mesh.vertices[vertex * 3]);
            minY = fminf(minY, mesh.vertices[vertex * 3 + 1]);
            minZ = fminf(minZ, mesh.vertices[vertex * 3 + 2]);
            maxX = fmaxf(maxX, mesh.vertices[vertex * 3]);
            maxY = fmaxf(maxY, mesh.vertices[vertex * 3 + 1]);
            maxZ = fmaxf(maxZ, mesh.vertices[vertex * 3 + 2]);
        }
        assert(fabsf((maxY - minY) - cases[index].expectedHeight) < 0.0001f);
        if (cases[index].carpet) {
            assert(maxX - minX > 0.9f);
            assert(maxZ - minZ > 0.9f);
        } else {
            assert(maxX - minX < 0.6f);
            assert(maxZ - minZ < 0.6f);
        }
        FreeTestMesh(&mesh);
    }
}

static void AssertFenceMeshContracts(void)
{
    typedef struct FenceCase {
        BlockType neighbor;
        int expectedVertexCount;
    } FenceCase;
    static const FenceCase cases[] = {
        { BLOCK_AIR, 36 },
        { BLOCK_FENCE, 72 },
        { BLOCK_FENCE_GATE, 54 },
        { BLOCK_FENCE_GATE_OPEN, 54 },
        { BLOCK_STONE, 60 }
    };
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];

    for (int index = 0; index < (int)(sizeof(cases) / sizeof(cases[0])); index++) {
        memset(blocks, 0, sizeof(blocks));
        blocks[5][6][7] = BLOCK_FENCE;
        blocks[6][6][7] = (unsigned short)cases[index].neighbor;
        Mesh mesh = { 0 };
        assert(BuildTestMesh(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            TEST_MESH_SOLID, &mesh));
        AssertMeshWellFormed(&mesh, cases[index].expectedVertexCount);
        FreeTestMesh(&mesh);
    }
}

static void AssertStandardBlockCullingAndPartitions(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    blocks[2][4][2] = BLOCK_STONE;
    blocks[3][4][2] = BLOCK_STONE;
    blocks[8][4][8] = BLOCK_WATER;
    blocks[11][4][11] = BLOCK_FLOWER;

    Mesh solid = { 0 };
    Mesh water = { 0 };
    Mesh flora = { 0 };
    assert(BuildTestMesh(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        TEST_MESH_SOLID, &solid));
    assert(BuildTestMesh(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        TEST_MESH_WATER, &water));
    assert(BuildTestMesh(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        TEST_MESH_FLORA, &flora));
    AssertMeshWellFormed(&solid, 60);
    AssertMeshWellFormed(&water, 36);
    AssertMeshWellFormed(&flora, 12);
    FreeTestMesh(&solid);
    FreeTestMesh(&water);
    FreeTestMesh(&flora);
}

static void AssertSolidFacesRemainVisibleUnderwater(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    blocks[4][4][4] = BLOCK_STONE;
    blocks[5][4][4] = BLOCK_WATER;

    Mesh solid = { 0 };
    Mesh water = { 0 };
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_SOLID, &solid));
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_WATER, &water));
    AssertMeshWellFormed(&solid, 36);
    AssertMeshWellFormed(&water, 30);
    FreeTestMesh(&solid);
    FreeTestMesh(&water);
}

static void AssertUnknownWaterNeighborsAreConservative(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    memset(chunks, 0, sizeof(Chunk) * MAX_ACTIVE_CHUNKS);
    blocks[CHUNK_SIZE - 1][4][8] = BLOCK_WATER;

    Mesh water = { 0 };
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_WATER, &water));
    // The east face is hidden while the streamed neighbor is unknown.
    AssertMeshWellFormed(&water, 30);
    FreeTestMesh(&water);

    chunks[0].loaded = true;
    chunks[0].spherical = true;
    chunks[0].cx = 1;
    chunks[0].cz = 0;
    chunks[0].surfaceAddress = ChunkSurfaceAddressAt(1, 0);
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_WATER, &water));
    // A loaded chunk with an unresolved section is still unknown.
    AssertMeshWellFormed(&water, 30);
    FreeTestMesh(&water);

    assert(ChunkMarkTerrainSectionResolved(&chunks[0], 0));
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_WATER, &water));
    // A resolved empty section makes the actual exposed face visible.
    AssertMeshWellFormed(&water, 36);
    FreeTestMesh(&water);

    ChunkSection *neighbor = ChunkGetSection(&chunks[0], 0, true);
    assert(neighbor != NULL);
    neighbor->blocks[0][4][8] = BLOCK_WATER;
    assert(BuildTestMesh((const unsigned short (*)[CHUNK_SIZE])blocks,
                         TEST_MESH_WATER, &water));
    // Matching water on both sides of the chunk border has no internal face.
    AssertMeshWellFormed(&water, 30);
    FreeTestMesh(&water);
    ChunkClearBlockStorage(&chunks[0]);
    memset(chunks, 0, sizeof(Chunk) * MAX_ACTIVE_CHUNKS);
}

static void AssertLightingVertexData(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    memset(blocks, 0, sizeof(blocks));
    blocks[4][4][4] = BLOCK_STONE;
    blocks[5][5][4] = BLOCK_STONE;
    blocks[4][5][5] = BLOCK_STONE;
    blocks[5][5][5] = BLOCK_STONE;
    Mesh mesh = { 0 };
    assert(BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &mesh));
    assert(mesh.texcoords2 != NULL);
    bool sawOpenCorner = false;
    bool sawOccludedCorner = false;
    for (int vertex = 0; vertex < mesh.vertexCount; vertex++) {
        float ao = mesh.texcoords2[vertex * 2];
        float localLight = mesh.texcoords2[vertex * 2 + 1];
        assert(ao >= 0.48f && ao <= 1.0f);
        assert(localLight == 0.0f);
        if (ao > 0.99f) sawOpenCorner = true;
        if (ao < 0.60f) sawOccludedCorner = true;
    }
    assert(sawOpenCorner);
    assert(sawOccludedCorner);
    FreeTestMesh(&mesh);
}

static void AssertSurfaceFloraMeshPartition(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    memset(blocks, 0, sizeof(blocks));
    blocks[2][4][3] = BLOCK_STONE;
    blocks[5][5][6] = BLOCK_FLOWER;
    blocks[9][6][9] = BLOCK_MUSHROOM;
    blocks[8][4][8] = BLOCK_WATER;

    Mesh legacySolid = { 0 };
    Mesh legacyTransparent = { 0 };
    Mesh solid = { 0 };
    Mesh water = { 0 };
    Mesh flora = { 0 };
    assert(BuildMeshData((const unsigned short (*)[CHUNK_SIZE])blocks,
                         WORLD_HEIGHT, 0, 0, 0, false, faces,
                         NULL, 0, &legacySolid));
    assert(BuildMeshData((const unsigned short (*)[CHUNK_SIZE])blocks,
                         WORLD_HEIGHT, 0, 0, 0, true, faces,
                         NULL, 0, &legacyTransparent));
    assert(BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &solid));
    assert(BuildSurfaceWaterMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        NULL, WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &water));
    assert(BuildFloraMeshData((const unsigned short (*)[CHUNK_SIZE])blocks,
                              WORLD_HEIGHT, 0, 0, 0, faces,
                              NULL, 0, &flora));
    assert(flora.vertexCount == 24);
    unsigned char baseColors[24 * 4];
    memcpy(baseColors, flora.colors, sizeof(baseColors));
    for (int group = 0; group < 2; group++) {
        int firstVertex = group * 12;
        float centerX = flora.vertices[firstVertex * 3] + 0.16f;
        float centerZ = flora.vertices[firstVertex * 3 + 2] + 0.16f;
        for (int vertex = firstVertex; vertex < firstVertex + 12; vertex++) {
            assert(fabsf(flora.vertices[vertex * 3] - centerX) < 0.33f);
            assert(fabsf(flora.vertices[vertex * 3 + 2] - centerZ) < 0.33f);
        }
    }
    const float presence[2] = { 0.0f, 1.0f };
    assert(ApplyFloraMeshPresenceColors(
        flora.colors, baseColors, flora.vertexCount, presence, 2, 1.0f));
    for (int vertex = 0; vertex < 12; vertex++) {
        int color = vertex * 4;
        assert(flora.colors[color] ==
               (unsigned char)lroundf((float)baseColors[color] * 0.55f));
        assert(flora.colors[color + 1] ==
               (unsigned char)lroundf((float)baseColors[color + 1] * 0.42f));
        assert(flora.colors[color + 2] ==
               (unsigned char)lroundf((float)baseColors[color + 2] * 0.32f));
        assert(flora.colors[color + 3] == baseColors[color + 3]);
    }
    for (int vertex = 12; vertex < 24; vertex++) {
        int color = vertex * 4;
        assert(memcmp(&flora.colors[color], &baseColors[color], 4) == 0);
    }
    assert(!ApplyFloraMeshPresenceColors(
        flora.colors, baseColors, flora.vertexCount, presence, 2, 1.0f));

    memcpy(flora.colors, baseColors, sizeof(baseColors));
    const FloraVisualInstance instances[2] = {
        { .firstVertex = 0, .vertexCount = 6,
          .anchor = { 0 }, .height = 0.4f, .windResponse = 1.0f },
        { .firstVertex = 6, .vertexCount = 18,
          .anchor = { 0 }, .height = 2.5f, .windResponse = 0.25f }
    };
    assert(ApplyFloraMeshInstancePresenceColors(
        flora.colors, baseColors, flora.vertexCount, presence,
        instances, 2, 1.0f));
    for (int vertex = 0; vertex < 6; vertex++) {
        int color = vertex * 4;
        assert(flora.colors[color] ==
               (unsigned char)lroundf((float)baseColors[color] * 0.55f));
        assert(flora.colors[color + 1] ==
               (unsigned char)lroundf((float)baseColors[color + 1] * 0.42f));
        assert(flora.colors[color + 2] ==
               (unsigned char)lroundf((float)baseColors[color + 2] * 0.32f));
        assert(flora.colors[color + 3] == baseColors[color + 3]);
    }
    assert(memcmp(&flora.colors[6 * 4], &baseColors[6 * 4],
                  (size_t)18 * 4u) == 0);
    assert(!ApplyFloraMeshInstancePresenceColors(
        flora.colors, baseColors, flora.vertexCount, presence,
        instances, 2, 1.0f));
    assert(legacySolid.vertexCount == solid.vertexCount);
    assert(legacyTransparent.vertexCount == water.vertexCount + flora.vertexCount);

    FreeTestMesh(&legacySolid);
    FreeTestMesh(&legacyTransparent);
    FreeTestMesh(&solid);
    FreeTestMesh(&water);
    FreeTestMesh(&flora);
}

static void AssertPartialWaterVolumeHeight(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static unsigned char volumes[CHUNK_SIZE * WORLD_HEIGHT * CHUNK_SIZE];
    memset(blocks, 0, sizeof(blocks));
    memset(volumes, 0, sizeof(volumes));
    const int lx = 4;
    const int y = 10;
    const int lz = 4;
    blocks[lx][y][lz] = BLOCK_WATER;
    volumes[(lx * WORLD_HEIGHT + y) * CHUNK_SIZE + lz] = 127u;

    Mesh mesh = { 0 };
    assert(BuildSurfaceWaterMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks, volumes,
        WORLD_HEIGHT, 0, 0, 0, TEST_CHUNK_FACES, NULL, 0, &mesh));
    AssertMeshWellFormed(&mesh, 36);
    float expectedTop = (float)y + 127.0f / 255.0f;
    float maximumY = -INFINITY;
    for (int vertex = 0; vertex < mesh.vertexCount; vertex++) {
        float vertexY = mesh.vertices[vertex * 3 + 1];
        if (vertexY > maximumY) maximumY = vertexY;
        assert(vertexY <= expectedTop + 0.0001f);
        assert(vertexY >= (float)y - 0.0001f);
    }
    assert(fabsf(maximumY - expectedTop) < 0.0001f);
    FreeTestMesh(&mesh);
}

static void AssertStairsMeshCapacity(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    memset(blocks, 0, sizeof(blocks));
    blocks[4][5][7] = BLOCK_STONE_STAIRS;

    Mesh mesh = { 0 };
    assert(BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &mesh));
    assert(mesh.vertexCount == 12 * 6);
    assert(mesh.triangleCount == 12 * 2);
    FreeTestMesh(&mesh);
}

static void AssertDoorMeshCapacityWithOcclusion(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    memset(blocks, 0, sizeof(blocks));
    blocks[4][5][7] = BLOCK_DOOR;
    blocks[4][5][6] = BLOCK_STONE;

    Mesh mesh = { 0 };
    assert(BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &mesh));
    assert(mesh.vertexCount == (4 + 5) * 6);
    FreeTestMesh(&mesh);
}

static void AssertSpaceshipMeshCapacityAndBounds(void)
{
    static unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    const int blockX = 4;
    const int blockY = 5;
    const int blockZ = 7;
    memset(blocks, 0, sizeof(blocks));
    blocks[blockX][blockY][blockZ] = BLOCK_SPACESHIP;

    Mesh mesh = { 0 };
    assert(BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &mesh));
    assert(mesh.vertexCount == 96 * 6);
    assert(mesh.triangleCount == 96 * 2);

    float minX = INFINITY;
    float minY = INFINITY;
    float minZ = INFINITY;
    float maxX = -INFINITY;
    float maxY = -INFINITY;
    float maxZ = -INFINITY;
    for (int vertex = 0; vertex < mesh.vertexCount; vertex++) {
        float x = mesh.vertices[vertex * 3 + 0];
        float y = mesh.vertices[vertex * 3 + 1];
        float z = mesh.vertices[vertex * 3 + 2];
        minX = fminf(minX, x);
        minY = fminf(minY, y);
        minZ = fminf(minZ, z);
        maxX = fmaxf(maxX, x);
        maxY = fmaxf(maxY, y);
        maxZ = fmaxf(maxZ, z);

        float nx = mesh.normals[vertex * 3 + 0];
        float ny = mesh.normals[vertex * 3 + 1];
        float nz = mesh.normals[vertex * 3 + 2];
        assert(fabsf(nx * nx + ny * ny + nz * nz - 1.0f) < 0.001f);
    }
    assert(minX >= (float)blockX && maxX <= (float)blockX + 1.0f);
    assert(minY >= (float)blockY && maxY <= (float)blockY + 1.0f);
    assert(minZ >= (float)blockZ && maxZ <= (float)blockZ + 1.0f);
    assert(maxX - minX > 0.85f);
    assert(maxY - minY < 0.90f);
    assert(maxZ - minZ > 0.80f);
    FreeTestMesh(&mesh);

    for (int direction = 0; direction < 4; direction++) {
        memset(blocks, 0, sizeof(blocks));
        blocks[blockX][blockY][blockZ] =
            (unsigned short)(BLOCK_SPACESHIP_CORE_NORTH + direction);
        blocks[blockX + 1][blockY][blockZ] = BLOCK_SPACESHIP_OCCUPIED;
        assert(BuildSurfaceSolidMeshData(
            (const unsigned short (*)[CHUNK_SIZE])blocks,
            WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &mesh));
        assert(mesh.vertexCount == 96 * 6);

        minX = minY = minZ = INFINITY;
        maxX = maxY = maxZ = -INFINITY;
        for (int vertex = 0; vertex < mesh.vertexCount; vertex++) {
            float x = mesh.vertices[vertex * 3];
            float y = mesh.vertices[vertex * 3 + 1];
            float z = mesh.vertices[vertex * 3 + 2];
            minX = fminf(minX, x);
            minY = fminf(minY, y);
            minZ = fminf(minZ, z);
            maxX = fmaxf(maxX, x);
            maxY = fmaxf(maxY, y);
            maxZ = fmaxf(maxZ, z);
            float nx = mesh.normals[vertex * 3];
            float ny = mesh.normals[vertex * 3 + 1];
            float nz = mesh.normals[vertex * 3 + 2];
            assert(fabsf(nx * nx + ny * ny + nz * nz - 1.0f) < 0.001f);
        }
        assert(minX >= (float)blockX - 1.0f);
        assert(maxX <= (float)blockX + 3.0f);
        assert(minY >= (float)blockY);
        assert(maxY <= (float)blockY + 2.0f);
        assert(minZ >= (float)blockZ - 1.0f);
        assert(maxZ <= (float)blockZ + 3.0f);
        assert(maxX - minX > 3.70f);
        assert(maxZ - minZ > 3.70f);
        FreeTestMesh(&mesh);
    }
}

static void AssertFloraStructureInstancePartition(void)
{
    static Chunk chunk;
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    memset(&chunk, 0, sizeof(chunk));
    chunk.loaded = true;
    chunk.floraStructureCount = 4;
    chunk.floraStructures[0] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_ALIEN_CANOPY,
        .shapeHash = 0u,
        .rootX = 2, .groundY = 3, .rootZ = 2,
        .minX = 0, .minY = 4, .minZ = 0,
        .maxX = 4, .maxY = 8, .maxZ = 4,
        .primaryBlock = BLOCK_LIVING_STEM,
        .accentBlock = BLOCK_CANOPY_FROND,
        .windResponse = 1.0f
    };
    chunk.floraStructures[1] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_CRYSTAL,
        .shapeHash = 1u,
        .rootX = 8, .groundY = 3, .rootZ = 2,
        .minX = 7, .minY = 4, .minZ = 1,
        .maxX = 9, .maxY = 6, .maxZ = 3,
        .primaryBlock = BLOCK_CRYSTAL_BLOOM,
        .accentBlock = BLOCK_CRYSTAL_BLOOM,
        .windResponse = 0.12f
    };
    chunk.floraStructures[2] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_SPORE,
        .shapeHash = 0u,
        .rootX = 2, .groundY = 3, .rootZ = 9,
        .minX = 1, .minY = 4, .minZ = 8,
        .maxX = 3, .maxY = 8, .maxZ = 10,
        .primaryBlock = BLOCK_FUNGAL_STEM,
        .accentBlock = BLOCK_SPORE_CAP,
        .windResponse = 0.65f
    };
    chunk.floraStructures[3] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_THERMAL_VENT,
        .shapeHash = 0u,
        .rootX = 8, .groundY = 3, .rootZ = 9,
        .minX = 7, .minY = 4, .minZ = 9,
        .maxX = 9, .maxY = 7, .maxZ = 10,
        .primaryBlock = BLOCK_VENT_CHIMNEY,
        .accentBlock = BLOCK_CHEMO_MAT,
        .windResponse = 0.05f
    };

    ChunkSetLocalBlock(&chunk, 2, 6, 2, BLOCK_CANOPY_FROND);
    ChunkSetLocalBlock(&chunk, 2, 8, 2, BLOCK_LUMINOUS_POD);
    ChunkSetLocalBlock(&chunk, 2, 5, 2, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 4, 4, 4, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 8, 4, 2, BLOCK_CRYSTAL_BLOOM);
    ChunkSetLocalBlock(&chunk, 8, 5, 2, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 9, 5, 3, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 2, 6, 9, BLOCK_SPORE_CAP);
    ChunkSetLocalBlock(&chunk, 2, 5, 9, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 3, 4, 10, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 8, 4, 9, BLOCK_VENT_CHIMNEY);
    ChunkSetLocalBlock(&chunk, 8, 5, 9, BLOCK_STONE);
    ChunkSetLocalBlock(&chunk, 9, 5, 10, BLOCK_STONE);

    Mesh flora = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    assert(BuildChunkFloraMeshData(
        &chunk, faces, NULL, 0, &flora, &instances, &instanceCount));
    assert(instanceCount == 4);
    const int expectedVertexCounts[4] = { 72, 36, 36, 36 };
    int firstVertex = 0;
    for (int index = 0; index < instanceCount; index++) {
        assert(instances[index].firstVertex == firstVertex);
        assert(instances[index].vertexCount == expectedVertexCounts[index]);
        assert(instances[index].anchor.x ==
               (float)chunk.floraStructures[index].rootX + 0.5f);
        assert(instances[index].anchor.y ==
               (float)chunk.floraStructures[index].groundY + 1.0f);
        assert(instances[index].anchor.z ==
               (float)chunk.floraStructures[index].rootZ + 0.5f);
        assert(instances[index].height ==
               (float)(chunk.floraStructures[index].maxY -
                       chunk.floraStructures[index].groundY));
        assert(instances[index].windResponse ==
               chunk.floraStructures[index].windResponse);
        firstVertex += expectedVertexCounts[index];
    }
    assert(flora.vertexCount == firstVertex);

    const ChunkSection *section = ChunkGetSectionConst(&chunk, 0);
    assert(section);
    Mesh rawTransparent = { 0 };
    assert(BuildSurfaceWaterMeshData(
        (const unsigned short (*)[CHUNK_SIZE])section->blocks, NULL,
        SURFACE_SECTION_HEIGHT, 0, 0, 0, faces, NULL, 0,
        &rawTransparent));
    AssertMeshWellFormed(&rawTransparent, 96);
    FreeTestMesh(&rawTransparent);
    Mesh waterWithoutFlora = { 0 };
    assert(!ChunksTestBuildSurfaceWaterMeshDataWithSnapshot(
        section->blocks, NULL, 0, 0, 0,
        chunk.floraStructures, chunk.floraStructureCount,
        faces, NULL, 0, NULL, &waterWithoutFlora));
    assert(waterWithoutFlora.vertexCount == 0);

    free(instances);
    FreeTestMesh(&flora);
}

static void AssertSolidSnapshotDoesNotReadLiveChunks(void)
{
    UnloadAllChunks();

    unsigned short blocks[CHUNK_SIZE]
                         [SURFACE_SECTION_HEIGHT]
                         [CHUNK_SIZE] = { 0 };
    blocks[CHUNK_SIZE - 1][8][8] = BLOCK_STONE;

    ChunkTestBoundarySnapshot boundary = { 0 };
    boundary.blocks[CHUNK_SIZE + 1][8 + 1][8 + 1] = BLOCK_STONE;
    boundary.blocks[0][8 + 1][8 + 1] = BLOCK_WATER;
    boundary.volumes[0][8 + 1][8 + 1] = WATER_VOLUME_CAPACITY;
    boundary.known[0][8 + 1][8 + 1] = 1u;

    BlockType boundaryBlock = BLOCK_AIR;
    unsigned char boundaryVolume = 0u;
    assert(ChunksTestSurfaceBoundaryCellAt(
        &boundary, -1, 8, 8, &boundaryBlock, &boundaryVolume));
    assert(boundaryBlock == BLOCK_WATER);
    assert(boundaryVolume == WATER_VOLUME_CAPACITY);

    terrainBaseBlockLookups = 0;
    Mesh mesh = { 0 };
    assert(ChunksTestBuildMeshDataFilteredWithSnapshot(
        (const unsigned short (*)[CHUNK_SIZE])blocks,
        SURFACE_SECTION_HEIGHT, 0, 0, 0,
        false, false, false, false, TEST_CHUNK_FACES,
        NULL, 0, &boundary, &mesh));
    AssertMeshWellFormed(&mesh, 30);
    assert(terrainBaseBlockLookups == 0);
    FreeTestMesh(&mesh);
}

int main(void)
{
    AssertSpecialBlockMeshContracts();
    AssertEcologyPlantMeshShapes();
    AssertFenceMeshContracts();
    AssertStandardBlockCullingAndPartitions();
    AssertSolidFacesRemainVisibleUnderwater();
    AssertUnknownWaterNeighborsAreConservative();
    AssertSurfaceFloraMeshPartition();
    AssertPartialWaterVolumeHeight();
    AssertLightingVertexData();
    AssertStairsMeshCapacity();
    AssertDoorMeshCapacityWithOcclusion();
    AssertSpaceshipMeshCapacityAndBounds();
    AssertFloraStructureInstancePartition();
    AssertSolidSnapshotDoesNotReadLiveChunks();
    puts("chunk atlas tests passed");
    return 0;
}
