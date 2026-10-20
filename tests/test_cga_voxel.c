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
    assert(workspace.opCount == 24);
    assert(!workspace.overflow);
    assert(workspace.ops[0].stage == CGA_STAGE_BASE);
    assert(workspace.ops[2].stage == CGA_STAGE_CARVE);
    assert(workspace.ops[workspace.opCount - 1].stage == CGA_STAGE_PAINT);
    bool hasStoneBase = false;
    bool hasWhiteWalls = false;
    bool hasRedTimber = false;
    bool hasSteppedRoof = false;
    for (size_t i = 0; i < workspace.opCount; i++) {
        const CgaVoxelOp *op = &workspace.ops[i];
        hasStoneBase |= op->material == BLOCK_STONE_BRICKS;
        hasWhiteWalls |= op->material == BLOCK_WHITE;
        hasRedTimber |= op->material == BLOCK_RED;
        hasSteppedRoof |= op->material == BLOCK_STONE_STAIRS;
    }
    assert(hasStoneBase && hasWhiteWalls && hasRedTimber && hasSteppedRoof);
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
    assert(observed[0][14][33][1] == BLOCK_RED);
    assert(observed[1][0][33][1] == BLOCK_WOOD);
    for (int orientation = 0; orientation < 4; orientation++) {
        CgaWorkspace rotated;
        CgaWorkspaceReset(&rotated);
        feature.scope.orientation = (CgaOrientation)orientation;
        feature.scope.sizeX = (orientation & 1) ? 13 : 17;
        feature.scope.sizeZ = (orientation & 1) ? 17 : 13;
        assert(CgaTestExpandHouse(&rotated, feature));
        assert(rotated.opCount == 24);
        assert(CgaScopeValid(rotated.ops[0].scope));
        assert(rotated.ops[0].material == BLOCK_STONE_BRICKS);
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


static void TestOpsEvaluationOrder(void)
{
    CgaFeature featureA = {
        5u, CGA_HOUSE_GRAMMAR_VERSION, 1u, 0, 0,
        {0, 32, 0, 1, 1, 1, CGA_ORIENTATION_0}
    };
    CgaFeature featureB = featureA;
    featureB.id = 3u;
    CgaScope scope = {0, 32, 0, 1, 1, 1, CGA_ORIENTATION_0};
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    assert(CgaAppendOp(&workspace, CGA_OP_FILL, CGA_STAGE_BASE, 10,
                       featureA, scope, BLOCK_STONE));
    assert(CgaAppendOp(&workspace, CGA_OP_FILL, CGA_STAGE_BASE, 10,
                       featureB, scope, BLOCK_DIRT));
    assert(CgaAppendOp(&workspace, CGA_OP_FILL, CGA_STAGE_BASE, 20,
                       featureB, scope, BLOCK_WOOD));
    assert(CgaAppendOp(&workspace, CGA_OP_CARVE, CGA_STAGE_CARVE, 5,
                       featureA, scope, BLOCK_AIR));
    assert(CgaAppendOp(&workspace, CGA_OP_PAINT, CGA_STAGE_PAINT, 1,
                       featureA, scope, BLOCK_SNOW));
    assert(workspace.opCount == 5);
    CgaSortOps(&workspace);
    assert(workspace.ops[0].sequence == 1);
    assert(workspace.ops[1].sequence == 0);
    assert(workspace.ops[2].sequence == 2);
    assert(workspace.ops[3].sequence == 3);
    assert(workspace.ops[4].sequence == 4);
}

static void TestClassicalComplexExpansion(void)
{
    CgaFeature complex = {
        CgaFeatureId(0x564f5843u, CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION, 4, -2),
        CGA_CLASSICAL_COMPLEX_GRAMMAR_VERSION, 0x564f5843u, 4, -2,
        {120, 48, -90, 93, 28, 101, CGA_ORIENTATION_0}
    };
    CgaWorkspace workspace;
    CgaWorkspaceReset(&workspace);
    assert(CgaExpandFeature(&workspace, complex));
    assert(!workspace.overflow);
    assert(workspace.opCount >= 300);
    assert(workspace.opCount <= CGA_CLASSICAL_COMPLEX_OP_BUDGET);

    bool hasTerrace = false;
    bool hasPlaster = false;
    bool hasRedColumns = false;
    bool hasLattice = false;
    bool hasRoofTile = false;
    bool hasPaintedBracketBand = false;
    bool hasRidge = false;
    bool hasPlaque = false;
    for (size_t i = 0; i < workspace.opCount; i++) {
        const CgaVoxelOp *op = &workspace.ops[i];
        assert(CgaScopeValid(op->scope));
        hasTerrace |= op->material == BLOCK_DRESSED_STONE;
        hasPlaster |= op->material == BLOCK_WHITE_PLASTER;
        hasRedColumns |= op->material == BLOCK_RED_LACQUER_WOOD;
        hasLattice |= op->material == BLOCK_WOOD_LATTICE;
        hasRoofTile |= op->material == BLOCK_BLUE_ROOF_TILE;
        hasPaintedBracketBand |= op->material == BLOCK_GREEN ||
                                  op->material == BLOCK_YELLOW;
        hasRidge |= op->material == BLOCK_BLUE_BRICK;
        hasPlaque |= op->material == BLOCK_BLACK;
    }
    assert(hasTerrace && hasPlaster && hasRedColumns && hasLattice);
    assert(hasRoofTile && hasPaintedBracketBand && hasRidge && hasPlaque);

    size_t baseOpCount = workspace.opCount;
    for (int orientation = 0; orientation < 4; orientation++) {
        CgaWorkspace rotated;
        CgaWorkspaceReset(&rotated);
        complex.scope.orientation = (CgaOrientation)orientation;
        complex.scope.sizeX = (orientation & 1) ? 101 : 93;
        complex.scope.sizeZ = (orientation & 1) ? 93 : 101;
        assert(CgaExpandFeature(&rotated, complex));
        assert(!rotated.overflow);
        assert(rotated.opCount <= CGA_CLASSICAL_COMPLEX_OP_BUDGET);
        assert(rotated.opCount == baseOpCount);
        for (size_t i = 0; i < rotated.opCount; i++) {
            assert(CgaScopeValid(rotated.ops[i].scope));
        }
    }
}

static void TestAllStructureGrammarsExpand(void)
{
    CgaFeature features[] = {
        TestFeature(40, 0, 0, 13, 13),
        TestFeature(41, 0, 0, 7, 7),
        TestFeature(42, 0, 0, 9, 9)
    };
    features[0].grammarVersion = CGA_MINESHAFT_GRAMMAR_VERSION;
    features[0].scope.minY = 8;
    features[0].scope.sizeY = 4;
    features[1].grammarVersion = CGA_DUNGEON_GRAMMAR_VERSION;
    features[1].scope.minY = 10;
    features[1].scope.sizeY = 4;
    features[2].grammarVersion = CGA_DESERT_TEMPLE_GRAMMAR_VERSION;
    features[2].anchorX = 0;
    features[2].anchorZ = 0;
    features[2].scope.minY = 4;
    features[2].scope.sizeY = 9;

    CgaWorkspace workspace;
    for (size_t i = 0; i < sizeof(features) / sizeof(features[0]); i++) {
        CgaWorkspaceReset(&workspace);
        assert(CgaExpandFeature(&workspace, features[i]));
        assert(workspace.opCount > 0);
        assert(!workspace.overflow);
    }
}

int main(void)
{
    TestSplitAndRepeat();
    TestFeatureIdAndHouseExpansion();
    TestChunkOrderInvariant();
    TestFeatureConflictResolution();
    TestOpsEvaluationOrder();
    TestClassicalComplexExpansion();
    TestAllStructureGrammarsExpand();
    puts("cga voxel tests passed");
    return 0;
}
