#include "app/game_stream_audit.h"

#include "world/chunks.h"
#include "world/world.h"

#include <assert.h>
#include <stdio.h>

BlockType GetBlockAt(int x, int y, int z)
{
    if (y != 1 || z != 1) return BLOCK_AIR;
    if (x == 1) return BLOCK_STONE;
    if (x == 3) return BLOCK_WATER;
    if (x == 5) return BLOCK_TALL_GRASS;
    if (x == 7) return BLOCK_OAK_LOG;
    return BLOCK_AIR;
}

Chunk *FindChunk(int cx, int cz)
{
    static Chunk chunk = { .loaded = true };
    return cx == 0 && cz == 0 ? &chunk : NULL;
}

bool ChunkFloraStructureOwnsBlock(
    const Chunk *chunk, int worldX, int y, int worldZ, BlockType block)
{
    return chunk && worldX == 7 && y == 1 && worldZ == 1 &&
           block == BLOCK_OAK_LOG;
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
    assert(flora == 2);
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

static void TestWaitProgress(void)
{
    assert(GameStreamWaitStageSettledForTest(CHUNK_PIPELINE_READY));
    assert(GameStreamWaitStageSettledForTest(CHUNK_PIPELINE_IMPLICIT));
    assert(!GameStreamWaitStageSettledForTest(CHUNK_PIPELINE_DIRTY_WAIT));
    assert(!GameStreamWaitStageSettledForTest(CHUNK_PIPELINE_MESH_RUNNING));

    GameStreamAuditState audit = { 0 };
    audit.wait.timeoutFrames = 3u;
    assert(!GameStreamWaitAdvanceForTest(&audit, true));
    assert(GameStreamWaitAdvanceForTest(&audit, true));
    assert(audit.wait.elapsedFrames == 2u);
    assert(audit.wait.settledFrames == 2u);

    audit = (GameStreamAuditState){ 0 };
    audit.wait.timeoutFrames = 2u;
    assert(!GameStreamWaitAdvanceForTest(&audit, true));
    assert(GameStreamWaitAdvanceForTest(&audit, false));
    assert(audit.wait.elapsedFrames == 2u);
    assert(audit.wait.settledFrames == 0u);
}

int main(void)
{
    TestLayerClassification();
    TestSnapshotStaleness();
    TestOneSectionCursor();
    TestWaitProgress();
    puts("game stream audit tests passed");
    return 0;
}
