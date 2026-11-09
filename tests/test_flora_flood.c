/* Flood mortality must remove only the dead plant structure, preserve the
 * shared cell water volume and leave unrelated blocks untouched. The pure
 * tolerance curve is covered in test_flora_taxa; this fixture covers the
 * loaded-chunk integration boundary. */
#include "world/chunks.h"
#include "world/chunks_internal.h"
#include "world/flora_taxa.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

bool WorldIsSurfaceActive(void) { return true; }
uint32_t WorldCurrentSurfaceId(void) { return 0u; }
int WorldSurfaceMapOriginX(void) { return 0; }
int WorldSurfaceMapOriginZ(void) { return 0; }

static void TestFloodRemovalPreservesWater(void)
{
    memset(chunks, 0, sizeof(chunks));
    ChunkIndexReset();
    Chunk *chunk = &chunks[0];
    chunk->loaded = true;
    chunk->spherical = true;
    chunk->cx = 0;
    chunk->cz = 0;
    assert(ChunkGetSection(chunk, 0, true));
    assert(ChunkSetLocalBlock(chunk, 1, 1, 1, BLOCK_BIG_BLUESTEM));
    assert(ChunkSetLocalWaterVolume(chunk, 1, 1, 1, 255u));
    chunk->floraStructures[0] = (FloraStructureInstance){
        .kind = FLORA_STRUCTURE_CRYSTAL,
        .taxonId = -1,
        .rootX = 1,
        .groundY = 0,
        .rootZ = 1,
        .minX = 1,
        .minY = 1,
        .minZ = 1,
        .maxX = 1,
        .maxY = 1,
        .maxZ = 1,
        .primaryBlock = BLOCK_BIG_BLUESTEM,
        .accentBlock = BLOCK_BIG_BLUESTEM,
        .floodExposureSeconds = 2.1f,
        .floodDamage = 0.99f
    };
    chunk->floraStructureCount = 1;

    ChunksAdvanceFloraFlood(1.0f);
    assert(ChunkGetLocalBlock(chunk, 1, 1, 1) == BLOCK_AIR);
    assert(ChunkGetLocalWaterVolume(chunk, 1, 1, 1) == 255u);
    assert(chunk->floraStructureCount == 0);
    assert(ChunksGetFloraFloodStats().dead == 1u);
    memset(chunks, 0, sizeof(chunks));
    ChunkIndexReset();
}

int main(void)
{
    TestFloodRemovalPreservesWater();
    puts("flora flood tests passed");
    return 0;
}
