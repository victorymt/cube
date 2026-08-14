#include "chunks.h"
#include "terrain.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

uint32_t WorldGetSeed(void)
{
    return DEFAULT_WORLD_SEED;
}

static void TestSurfaceSampleContracts(void)
{
    SurfaceTerrainSample first = SurfaceTerrainAt(1234, -5678,
                                                   TERRAIN_VARIED);
    SurfaceTerrainSample repeat = SurfaceTerrainAt(1234, -5678,
                                                    TERRAIN_VARIED);
    assert(first.elevation == repeat.elevation);
    assert(first.slope == repeat.slope);
    assert(isfinite(first.elevation));
    assert(isfinite(first.seaLevel));
    assert(isfinite(first.continentalness));
    assert(isfinite(first.erosion));
    assert(isfinite(first.ridge));
    assert(isfinite(first.peak));
    assert(isfinite(first.trench));
    assert(isfinite(first.slope));
    assert(first.elevation >= 5.0f && first.elevation <= 240.0f);
    assert(first.seaLevel == (float)HOME_SEA_LEVEL);
    assert(first.continentalness >= 0.0f && first.continentalness <= 1.0f);
    assert(first.erosion >= 0.0f && first.erosion <= 1.0f);
    assert(first.ridge >= 0.0f && first.ridge <= 1.0f);
    assert(first.peak >= 0.0f && first.peak <= 1.0f);
    assert(first.trench >= 0.0f && first.trench <= 1.0f);
    assert(first.slope >= 0.0f);
    assert(first.biome >= BIOME_PLAINS && first.biome <= BIOME_MOUNTAIN);

    SurfaceTerrainSample flat = SurfaceTerrainAt(-91, 73, TERRAIN_FLAT);
    assert(flat.elevation == 8.0f);
    assert(flat.seaLevel == -1.0f);
    assert(flat.slope == 0.0f);
    assert(flat.biome == BIOME_PLAINS);
    assert(TerrainHeight(-91, 73, TERRAIN_FLAT) == 8);
    assert(TerrainSeaLevel(TERRAIN_FLAT) == -1);
}

static void TestEarthScaleRelief(void)
{
    int minHeight = WORLD_HEIGHT;
    int maxHeight = 0;
    int deepOceanCount = 0;
    int trenchCount = 0;
    int extremePeakCount = 0;
    int buildablePlainCount = 0;
    int sampleCount = 0;

    for (int z = -8192; z <= 8192; z += 32) {
        for (int x = -8192; x <= 8192; x += 32) {
            SurfaceTerrainSample sample = SurfaceTerrainAt(x, z,
                                                           TERRAIN_VARIED);
            int height = TerrainHeight(x, z, TERRAIN_VARIED);
            if (height < minHeight) minHeight = height;
            if (height > maxHeight) maxHeight = height;
            if (height <= HOME_SEA_LEVEL - 40) deepOceanCount++;
            if (height <= HOME_SEA_LEVEL - 60) trenchCount++;
            if (height >= HOME_SEA_LEVEL + 120) extremePeakCount++;
            if (height >= HOME_SEA_LEVEL + 2 &&
                height <= HOME_SEA_LEVEL + 25 && sample.slope <= 1.5f) {
                buildablePlainCount++;
            }
            sampleCount++;
        }
    }

    assert(minHeight <= HOME_SEA_LEVEL - 60);
    assert(maxHeight >= HOME_SEA_LEVEL + 120);
    assert(deepOceanCount > sampleCount / 100);
    assert(trenchCount > 0);
    assert(extremePeakCount > 0);
    assert(buildablePlainCount > sampleCount / 10);
}

static void TestChunkSectionBoundaries(void)
{
    static const int heights[] = { 0, 15, 16, HOME_SEA_LEVEL, 240, 255 };
    Chunk chunk = { 0 };

    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
        int y = heights[i];
        BlockType type = (BlockType)(BLOCK_GRASS + (int)i);
        assert(ChunkSetLocalBlock(&chunk, 3, y, 11, type));
        assert(ChunkGetLocalBlock(&chunk, 3, y, 11) == type);
        assert(ChunkGetSectionConst(&chunk, y / SURFACE_SECTION_HEIGHT));
    }

    assert(ChunkGetLocalBlock(&chunk, 3, -1, 11) == BLOCK_AIR);
    assert(ChunkGetLocalBlock(&chunk, 3, WORLD_HEIGHT, 11) == BLOCK_AIR);
    assert(!ChunkSetLocalBlock(&chunk, 3, -1, 11, BLOCK_STONE));
    assert(!ChunkSetLocalBlock(&chunk, 3, WORLD_HEIGHT, 11, BLOCK_STONE));
    assert(!ChunkGetSectionConst(&chunk, 2));

    assert(ChunkSetLocalBlock(&chunk, 3, 16, 11, BLOCK_AIR));
    assert(ChunkGetLocalBlock(&chunk, 3, 16, 11) == BLOCK_AIR);
    ChunkClearBlockStorage(&chunk);
    for (int sectionY = 0; sectionY < SURFACE_SECTION_COUNT; sectionY++) {
        assert(!ChunkGetSectionConst(&chunk, sectionY));
    }
}

int main(void)
{
    assert(SURFACE_WORLD_HEIGHT == 256);
    assert(SURFACE_SECTION_HEIGHT == 16);
    assert(SURFACE_SECTION_COUNT == 16);
    TestSurfaceSampleContracts();
    TestEarthScaleRelief();
    TestChunkSectionBoundaries();
    puts("terrain scale tests passed");
    return 0;
}
