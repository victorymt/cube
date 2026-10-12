#include "world/cga_voxel.h"
#include "world/chunks.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned short observed[2][CHUNK_SIZE][SURFACE_GENERATION_MAX_Y_EXCLUSIVE][CHUNK_SIZE];
static Chunk testChunks[2];

static CgaFeature TestFeature(uint64_t id, int minX, int minZ,
                              int sizeX, int sizeZ)
{
    return (CgaFeature){id, CGA_HOUSE_GRAMMAR_VERSION, 1u, minX, minZ,
                        {minX, 32, minZ, sizeX, 12, sizeZ,
                         CGA_ORIENTATION_0}};
}

static bool RejectEveryRegion(void *userData, CgaScope region,
                              uint64_t excludedFeature)
{
    (void)userData;
    (void)region;
    (void)excludedFeature;
    return false;
}

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    *cx = x >= 0 ? x / CHUNK_SIZE : -((-x + CHUNK_SIZE - 1) / CHUNK_SIZE);
    *cz = z >= 0 ? z / CHUNK_SIZE : -((-z + CHUNK_SIZE - 1) / CHUNK_SIZE);
    *lx = x - *cx * CHUNK_SIZE;
    *lz = z - *cz * CHUNK_SIZE;
}

bool ChunkSetLocalBlock(Chunk *chunk, int lx, int y, int lz, BlockType type)
{
    int index = chunk == &testChunks[1] ? 1 : 0;
    if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        y < 0 || y >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE) return false;
    observed[index][lx][y][lz] = (unsigned short)type;
    return true;
}

static void TestSplitAndRepeat(void)
{
    CgaScope parent = {20, 64, 30, 17, 12, 13, CGA_ORIENTATION_0};
    CgaScope parts[5];
    int sizes[] = {2, 4, 5, 4, 2};
    assert(CgaScopeSplit(parent, CGA_AXIS_X, sizes, 5, parts));
    assert(parts[0].minX == 20 && parts[0].sizeX == 2);
    assert(parts[4].minX == 35 && parts[4].sizeX == 2);
    assert(parts[0].minX + parts[0].sizeX == parts[1].minX);

    CgaScope repeated[8];
    size_t count = CgaScopeRepeat(parent, CGA_AXIS_X, 4, repeated, 8);
    assert(count == 4);
    assert(repeated[0].sizeX + repeated[1].sizeX +
           repeated[2].sizeX + repeated[3].sizeX == 17);
    assert(repeated[0].sizeX >= 4 && repeated[3].sizeX >= 4);
    assert(repeated[0].minX + repeated[0].sizeX == repeated[1].minX);
}

static void TestFeatureIdAndHouseExpansion(void)
{
    CgaFeature feature = {
        CgaFeatureId(0x564f5843u, CGA_HOUSE_GRAMMAR_VERSION, 2, -1),
        CGA_HOUSE_GRAMMAR_VERSION, 0x564f5843u, 2, -1,
        {120, 64, -70, 17, 12, 13, CGA_ORIENTATION_0}
    };
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    assert(CgaTestExpandHouse(&workspace, feature));
    assert(workspace.opCount == 13);
    assert(!workspace.overflow);
    assert(workspace.ops[0].stage == CGA_STAGE_BASE);
    assert(workspace.ops[1].stage == CGA_STAGE_CARVE);
    assert(workspace.ops[workspace.opCount - 1].stage == CGA_STAGE_PAINT);
    assert(CgaFeatureId(1, 1, 2, 3) == CgaFeatureId(1, 1, 2, 3));
    assert(CgaFeatureId(1, 1, 2, 3) != CgaFeatureId(1, 1, 3, 2));
}

static void TestChunkOrderInvariant(void)
{
    CgaFeature feature = {
        CgaFeatureId(7, CGA_HOUSE_GRAMMAR_VERSION, 0, 0),
        CGA_HOUSE_GRAMMAR_VERSION, 7, 0, 0,
        {14, 32, 1, 17, 12, 13, CGA_ORIENTATION_0}
    };
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    assert(CgaTestExpandHouse(&workspace, feature));

    memset(observed, 0, sizeof(observed));
    assert(CgaTestEvaluateWorkspace(&testChunks[0], 0, 0, &workspace));
    assert(CgaTestEvaluateWorkspace(&testChunks[1], 1, 0, &workspace));
    unsigned short forward[2][CHUNK_SIZE][SURFACE_GENERATION_MAX_Y_EXCLUSIVE][CHUNK_SIZE];
    memcpy(forward, observed, sizeof(forward));

    memset(observed, 0, sizeof(observed));
    assert(CgaTestEvaluateWorkspace(&testChunks[1], 1, 0, &workspace));
    assert(CgaTestEvaluateWorkspace(&testChunks[0], 0, 0, &workspace));
    assert(memcmp(forward, observed, sizeof(forward)) == 0);
    assert(observed[0][14][33][1] == BLOCK_PLANK);
    assert(observed[1][0][33][1] == BLOCK_PLANK);
    for (int orientation = 0; orientation < 4; orientation++) {
        CgaWorkspace rotated;
        CgaWorkspaceReset(&rotated);
        feature.scope.orientation = (CgaOrientation)orientation;
        feature.scope.sizeX = (orientation & 1) ? 13 : 17;
        feature.scope.sizeZ = (orientation & 1) ? 17 : 13;
        assert(CgaTestExpandHouse(&rotated, feature));
        assert(rotated.opCount == 13);
        assert(rotated.ops[0].scope.minX == 14);
        assert(rotated.ops[0].scope.minZ == 1);
        assert(rotated.ops[0].scope.sizeX == (orientation & 1 ? 13 : 17));
        assert(rotated.ops[0].scope.sizeZ == (orientation & 1 ? 17 : 13));
    }
}

static void TestFeatureConflictResolution(void)
{
    CgaFeature features[] = {
        TestFeature(20, 0, 0, 10, 10),
        TestFeature(10, 5, 5, 10, 10),
        TestFeature(30, 40, 40, 10, 10)
    };
    size_t count = CgaTestResolveFeatureConflicts(features, 3, NULL);
    assert(count == 2);
    assert(features[0].id == 10);
    assert(features[1].id == 30);

    CgaFeature reversed[] = {
        TestFeature(30, 40, 40, 10, 10),
        TestFeature(20, 0, 0, 10, 10),
        TestFeature(10, 5, 5, 10, 10)
    };
    count = CgaTestResolveFeatureConflicts(reversed, 3, NULL);
    assert(count == 2);
    assert(reversed[0].id == 10);
    assert(reversed[1].id == 30);

    CgaContext blocked = {NULL, RejectEveryRegion};
    assert(CgaTestResolveFeatureConflicts(features, count, &blocked) == 0);
}

int main(void)
{
    TestSplitAndRepeat();
    TestFeatureIdAndHouseExpansion();
    TestChunkOrderInvariant();
    TestFeatureConflictResolution();
    puts("cga voxel tests passed");
    return 0;
}
