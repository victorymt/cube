#include "world/cga_voxel.h"

#include <assert.h>
#include <stdio.h>

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

int main(void)
{
    TestSplitAndRepeat();
    TestFeatureIdAndHouseExpansion();
    puts("cga voxel tests passed");
    return 0;
}
