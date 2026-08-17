#include "app/game_stream_audit.h"

#include "world/world.h"

#include <assert.h>
#include <stdio.h>

BlockType GetBlockAt(int x, int y, int z)
{
    if (y != 1 || z != 1) return BLOCK_AIR;
    if (x == 1) return BLOCK_STONE;
    if (x == 3) return BLOCK_WATER;
    if (x == 5) return BLOCK_TALL_GRASS;
    return BLOCK_AIR;
}

bool IsPlantBlock(BlockType type)
{
    return type == BLOCK_TALL_GRASS;
}

bool IsWaterBlock(BlockType type)
{
    return type == BLOCK_WATER;
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_AIR || type == BLOCK_WATER ||
           type == BLOCK_TALL_GRASS;
}

static void TestLayerClassification(void)
{
    int solid = 0;
    int water = 0;
    int flora = 0;
    GameStreamAuditCountExpectedForTest(0, 0, 0,
                                        &solid, &water, &flora);
    assert(solid == 1);
    assert(water == 1);
    assert(flora == 1);
    assert(GameStreamAuditLayerMissingForTest(solid, 0));
    assert(!GameStreamAuditLayerMissingForTest(solid, 6));
    assert(!GameStreamAuditLayerMissingForTest(0, 0));
}

static void TestSnapshotStaleness(void)
{
    GameStreamAuditSnapshot before = {
        .cx = 1,
        .cz = -2,
        .sectionY = 3,
        .generation = 4u,
        .dirtyStamp = 5u,
        .solidVertices = 6,
        .loaded = true,
        .resolved = true,
        .materialized = true
    };
    GameStreamAuditSnapshot after = before;
    assert(GameStreamAuditSnapshotsEqualForTest(&before, &after));
    after.dirtyStamp++;
    assert(!GameStreamAuditSnapshotsEqualForTest(&before, &after));
    after = before;
    after.solidVertices = 0;
    assert(!GameStreamAuditSnapshotsEqualForTest(&before, &after));
}

static void TestOneSectionCursor(void)
{
    GameStreamAuditState audit = {
        .radius = 1,
        .dx = -1,
        .dz = -1,
        .vertical = -1
    };
    GameStreamAuditAdvanceForTest(&audit);
    assert(audit.vertical == 0 && audit.dx == -1 && audit.dz == -1);
    GameStreamAuditAdvanceForTest(&audit);
    assert(audit.vertical == 1 && audit.dx == -1 && audit.dz == -1);
    GameStreamAuditAdvanceForTest(&audit);
    assert(audit.vertical == -1 && audit.dx == 0 && audit.dz == -1);
}

int main(void)
{
    TestLayerClassification();
    TestSnapshotStaleness();
    TestOneSectionCursor();
    puts("game stream audit tests passed");
    return 0;
}
