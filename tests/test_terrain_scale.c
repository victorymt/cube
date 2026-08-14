#include "chunks.h"
#include "terrain.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t terrainSeed = DEFAULT_WORLD_SEED;

uint32_t WorldGetSeed(void)
{
    return terrainSeed;
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
    BathymetrySample flatBathymetry = TerrainBathymetryAt(-91, 73,
                                                          TERRAIN_FLAT);
    assert(flatBathymetry.seaLevel == -1);
    assert(flatBathymetry.waterDepth == 0);
    assert(flatBathymetry.zone == BATHYMETRY_ZONE_LAND);
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

static void TestBathymetryContracts(void)
{
    BathymetrySample first = TerrainBathymetryAt(1234, -5678,
                                                 TERRAIN_VARIED);
    BathymetrySample repeat = TerrainBathymetryAt(1234, -5678,
                                                  TERRAIN_VARIED);
    assert(first.seaLevel == HOME_SEA_LEVEL);
    assert(first.seabedY == repeat.seabedY);
    assert(first.waterDepth == repeat.waterDepth);
    assert(first.zone == repeat.zone);
    assert(first.material == repeat.material);
    assert(first.seabedY >= BATHYMETRY_MIN_SEABED_Y);
    assert(first.waterDepth >= 0 &&
           first.waterDepth <= BATHYMETRY_MAX_WATER_DEPTH);

    int coastCount = 0;
    int shelfCount = 0;
    int slopeCount = 0;
    int abyssCount = 0;
    int trenchCount = 0;
    int seamountCount = 0;
    for (int z = -8192; z <= 8192; z += 32) {
        for (int x = -8192; x <= 8192; x += 32) {
            BathymetrySample sample = TerrainBathymetryAt(x, z,
                                                           TERRAIN_VARIED);
            assert(sample.seaLevel == HOME_SEA_LEVEL);
            assert(sample.seabedY >= BATHYMETRY_MIN_SEABED_Y);
            assert(sample.waterDepth == HOME_SEA_LEVEL - sample.seabedY ||
                   sample.waterDepth == 0);
            switch (sample.zone) {
            case BATHYMETRY_ZONE_COAST: coastCount++; break;
            case BATHYMETRY_ZONE_SHELF: shelfCount++; break;
            case BATHYMETRY_ZONE_SLOPE: slopeCount++; break;
            case BATHYMETRY_ZONE_ABYSSAL_PLAIN: abyssCount++; break;
            case BATHYMETRY_ZONE_TRENCH: trenchCount++; break;
            case BATHYMETRY_ZONE_SEAMOUNT: seamountCount++; break;
            case BATHYMETRY_ZONE_LAND: break;
            }
        }
    }
    assert(coastCount > 0);
    assert(shelfCount > 0);
    assert(slopeCount > 0);
    assert(abyssCount > 0);
    assert(trenchCount > 0);
    assert(seamountCount > 0);

    BathymetrySample boundaryLeft = TerrainBathymetryAt(15, 2048,
                                                        TERRAIN_VARIED);
    BathymetrySample boundaryRight = TerrainBathymetryAt(16, 2048,
                                                         TERRAIN_VARIED);
    assert(abs(boundaryLeft.seabedY - boundaryRight.seabedY) <= 8);

    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_SAND) == BLOCK_SAND);
    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_SEDIMENT) ==
           BLOCK_SANDSTONE);
    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_ROCK) == BLOCK_STONE);

    terrainSeed = 1448040515u;
    BathymetrySample seeded = TerrainBathymetryAt(-2896, 16,
                                                  TERRAIN_VARIED);
    assert(seeded.waterDepth == 46);
    assert(seeded.seabedY == 34);
    assert(seeded.zone == BATHYMETRY_ZONE_ABYSSAL_PLAIN);
    assert(seeded.material == BATHYMETRY_MATERIAL_ROCK);
    terrainSeed = DEFAULT_WORLD_SEED;
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
    TestBathymetryContracts();
    TestChunkSectionBoundaries();
    puts("terrain scale tests passed");
    return 0;
}
