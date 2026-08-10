#include "chunks.h"
#include "world_environment.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static bool ColorsEqual(Color a, Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

static void AssertAtlasCoordinates(void)
{
    const float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    const float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    assert(ATLAS_CELL_SIZE == 32);
    assert(ATLAS_TILE_PADDING >= ATLAS_TILE_SIZE / 2);

    for (int i = 0; i < TEX_COUNT; i++) {
        BlockTexture texture = (BlockTexture)i;
        int column = i % ATLAS_COLUMNS;
        int row = i / ATLAS_COLUMNS;
        Rectangle source = AtlasSourceRect(texture);
        assert(source.x == column * ATLAS_CELL_SIZE + ATLAS_TILE_PADDING);
        assert(source.y == row * ATLAS_CELL_SIZE + ATLAS_TILE_PADDING);
        assert(source.width == ATLAS_TILE_SIZE);
        assert(source.height == ATLAS_TILE_SIZE);

        Vector2 uvs[6];
        AtlasUVs(texture, uvs);
        float minU = (source.x + 0.25f) / atlasWidth;
        float maxU = (source.x + source.width - 0.25f) / atlasWidth;
        float minV = (source.y + 0.25f) / atlasHeight;
        float maxV = (source.y + source.height - 0.25f) / atlasHeight;
        for (int vertex = 0; vertex < 6; vertex++) {
            assert(fabsf(uvs[vertex].x - minU) < 0.000001f ||
                   fabsf(uvs[vertex].x - maxU) < 0.000001f);
            assert(fabsf(uvs[vertex].y - minV) < 0.000001f ||
                   fabsf(uvs[vertex].y - maxV) < 0.000001f);
        }
    }
}

static void AssertTilePadding(Image image, BlockTexture texture)
{
    Rectangle source = AtlasSourceRect(texture);
    int left = (int)source.x;
    int top = (int)source.y;
    int right = left + ATLAS_TILE_SIZE - 1;
    int bottom = top + ATLAS_TILE_SIZE - 1;
    int cellLeft = left - ATLAS_TILE_PADDING;
    int cellTop = top - ATLAS_TILE_PADDING;
    int cellRight = right + ATLAS_TILE_PADDING;
    int cellBottom = bottom + ATLAS_TILE_PADDING;

    for (int y = 0; y < ATLAS_TILE_SIZE; y++) {
        Color leftEdge = GetImageColor(image, left, top + y);
        Color rightEdge = GetImageColor(image, right, top + y);
        assert(ColorsEqual(GetImageColor(image, cellLeft, top + y), leftEdge));
        assert(ColorsEqual(GetImageColor(image, cellRight, top + y), rightEdge));
    }
    for (int x = 0; x < ATLAS_TILE_SIZE; x++) {
        Color topEdge = GetImageColor(image, left + x, top);
        Color bottomEdge = GetImageColor(image, left + x, bottom);
        assert(ColorsEqual(GetImageColor(image, left + x, cellTop), topEdge));
        assert(ColorsEqual(GetImageColor(image, left + x, cellBottom), bottomEdge));
    }
    assert(ColorsEqual(GetImageColor(image, cellLeft, cellTop),
                       GetImageColor(image, left, top)));
    assert(ColorsEqual(GetImageColor(image, cellRight, cellBottom),
                       GetImageColor(image, right, bottom)));
}

static void AssertMipSafePadding(void)
{
    Image image = GenImageColor(ATLAS_CELL_SIZE * ATLAS_COLUMNS,
                                ATLAS_CELL_SIZE * ATLAS_ROWS, BLANK);
    assert(IsImageValid(image));
    const BlockTexture samples[] = {
        TEX_GRASS_TOP,
        TEX_GLASS,
        TEX_FLOWER,
        (BlockTexture)(TEX_COUNT - 1)
    };
    for (int i = 0; i < (int)(sizeof(samples) / sizeof(samples[0])); i++) {
        DrawAtlasTile(&image, samples[i]);
        AssertTilePadding(image, samples[i]);
    }
    UnloadImage(image);
}

static void FreeTestMesh(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
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
        WORLD_HEIGHT, 0, 0, 0, faces, NULL, 0, &water));
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
        .primaryBlock = BLOCK_GREEN, .accentBlock = BLOCK_CYAN,
        .windResponse = 1.0f
    };
    chunk.floraStructures[1] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_CRYSTAL,
        .shapeHash = 1u,
        .rootX = 8, .groundY = 3, .rootZ = 2,
        .minX = 7, .minY = 4, .minZ = 1,
        .maxX = 9, .maxY = 6, .maxZ = 3,
        .primaryBlock = BLOCK_BLUE, .accentBlock = BLOCK_PURPLE,
        .windResponse = 0.12f
    };
    chunk.floraStructures[2] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_SPORE,
        .shapeHash = 0u,
        .rootX = 2, .groundY = 3, .rootZ = 9,
        .minX = 1, .minY = 4, .minZ = 8,
        .maxX = 3, .maxY = 8, .maxZ = 10,
        .primaryBlock = BLOCK_GREEN, .accentBlock = BLOCK_PURPLE,
        .windResponse = 0.65f
    };
    chunk.floraStructures[3] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_THERMAL_VENT,
        .shapeHash = 0u,
        .rootX = 8, .groundY = 3, .rootZ = 9,
        .minX = 7, .minY = 4, .minZ = 9,
        .maxX = 9, .maxY = 7, .maxZ = 10,
        .primaryBlock = BLOCK_ORANGE, .accentBlock = BLOCK_YELLOW,
        .windResponse = 0.05f
    };

    chunk.blocks[2][4][2] = BLOCK_GREEN;
    chunk.blocks[2][5][2] = BLOCK_STONE;
    chunk.blocks[4][4][4] = BLOCK_STONE;
    chunk.blocks[8][4][2] = BLOCK_BLUE;
    chunk.blocks[8][5][2] = BLOCK_STONE;
    chunk.blocks[9][5][3] = BLOCK_STONE;
    chunk.blocks[2][4][9] = BLOCK_MUSHROOM;
    chunk.blocks[2][5][9] = BLOCK_STONE;
    chunk.blocks[3][4][10] = BLOCK_STONE;
    chunk.blocks[8][4][9] = BLOCK_NETHERRACK;
    chunk.blocks[8][5][9] = BLOCK_STONE;
    chunk.blocks[9][5][10] = BLOCK_STONE;

    Mesh flora = { 0 };
    FloraVisualInstance *instances = NULL;
    int instanceCount = 0;
    assert(BuildChunkFloraMeshData(
        &chunk, faces, NULL, 0, &flora, &instances, &instanceCount));
    assert(instanceCount == 4);
    const int expectedVertexCounts[4] = { 36, 36, 12, 36 };
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

    free(instances);
    FreeTestMesh(&flora);
}

int main(void)
{
    AssertAtlasCoordinates();
    AssertMipSafePadding();
    AssertSurfaceFloraMeshPartition();
    AssertFloraStructureInstancePartition();
    puts("chunk atlas tests passed");
    return 0;
}
