#include "world/chunks.h"
#include "world/terrain.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static uint32_t terrainSeed = DEFAULT_WORLD_SEED;

bool WorldIsSurfaceActive(void)
{
    return false;
}

uint32_t WorldCurrentSurfaceId(void)
{
    return 0u;
}

int WorldSurfaceMapOriginX(void)
{
    return 0;
}

int WorldSurfaceMapOriginZ(void)
{
    return 0;
}

uint32_t WorldGetSeed(void)
{
    return terrainSeed;
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_GLASS ||
           type == BLOCK_ICE || type == BLOCK_LEAVES;
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
    assert(first.elevation >= (float)HOME_BATHYMETRY_MIN_SEABED_Y &&
           first.elevation <= 240.0f);
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
            if (height <= HOME_SEA_LEVEL - 3000) deepOceanCount++;
            if (height <= HOME_SEA_LEVEL - 6000) trenchCount++;
            if (height >= HOME_SEA_LEVEL + 120) extremePeakCount++;
            if (height >= HOME_SEA_LEVEL + 2 &&
                height <= HOME_SEA_LEVEL + 25 && sample.slope <= 1.5f) {
                buildablePlainCount++;
            }
            sampleCount++;
        }
    }

    assert(minHeight <= HOME_SEA_LEVEL - 9000);
    assert(maxHeight >= HOME_SEA_LEVEL + 120);
    assert(deepOceanCount > sampleCount / 1000);
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
    assert(first.seabedY >= HOME_BATHYMETRY_MIN_SEABED_Y);
    assert(first.waterDepth >= 0 &&
           first.waterDepth <= HOME_BATHYMETRY_MAX_WATER_DEPTH);

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
            assert(sample.seabedY >= HOME_BATHYMETRY_MIN_SEABED_Y);
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
    assert(abs(boundaryLeft.seabedY - boundaryRight.seabedY) <= 256);

    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_SAND) == BLOCK_SAND);
    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_SEDIMENT) ==
           BLOCK_MUD);
    assert(BathymetryMaterialBlock(BATHYMETRY_MATERIAL_ROCK) == BLOCK_GRAVEL);

    terrainSeed = 1448040515u;
    BathymetrySample seeded = TerrainBathymetryAt(-2896, 16,
                                                  TERRAIN_VARIED);
    assert(seeded.waterDepth == 4379);
    assert(seeded.seabedY == -4299);
    assert(seeded.zone == BATHYMETRY_ZONE_ABYSSAL_PLAIN);
    assert(seeded.material == BATHYMETRY_MATERIAL_SEDIMENT);
    terrainSeed = DEFAULT_WORLD_SEED;
}

static void TestChunkSectionBoundaries(void)
{
    static const int heights[] = {
        SURFACE_MIN_Y, -17, -16, -1, 0, 15, 16,
        HOME_SEA_LEVEL, 240, SURFACE_MAX_Y_EXCLUSIVE - 1
    };
    Chunk chunk = { 0 };

    assert(SurfaceSectionYFromBlockY(-17) == -2);
    assert(SurfaceSectionLocalYFromBlockY(-17) == 15);
    assert(SurfaceSectionYFromBlockY(-16) == -1);
    assert(SurfaceSectionLocalYFromBlockY(-16) == 0);
    assert(SurfaceSectionYFromBlockY(-1) == -1);
    assert(SurfaceSectionLocalYFromBlockY(-1) == 15);

    for (size_t i = 0; i < sizeof(heights) / sizeof(heights[0]); i++) {
        int y = heights[i];
        BlockType type = (BlockType)(BLOCK_GRASS + (int)i);
        assert(ChunkSetLocalBlock(&chunk, 3, y, 11, type));
        assert(ChunkGetLocalBlock(&chunk, 3, y, 11) == type);
        assert(ChunkGetSectionConst(
            &chunk, SurfaceSectionYFromBlockY(y)));
    }

    assert(ChunkGetLocalBlock(&chunk, 3, SURFACE_MIN_Y - 1, 11) ==
           BLOCK_AIR);
    assert(ChunkGetLocalBlock(
               &chunk, 3, SURFACE_MAX_Y_EXCLUSIVE, 11) == BLOCK_AIR);
    assert(!ChunkSetLocalBlock(
        &chunk, 3, SURFACE_MIN_Y - 1, 11, BLOCK_STONE));
    assert(!ChunkSetLocalBlock(
        &chunk, 3, SURFACE_MAX_Y_EXCLUSIVE, 11, BLOCK_STONE));
    assert(!ChunkGetSectionConst(&chunk, 2));

    assert(ChunkSetLocalBlock(&chunk, 3, 16, 11, BLOCK_AIR));
    assert(ChunkGetLocalBlock(&chunk, 3, 16, 11) == BLOCK_AIR);
    ChunkClearBlockStorage(&chunk);
    assert(chunk.sections == NULL);
    assert(chunk.sectionCount == 0);
    assert(chunk.sectionCapacity == 0);
}

static void TestTerrainBaseBlockQueries(void)
{
    assert(TerrainBaseBlockAt(7, -1, -3, TERRAIN_FLAT) == BLOCK_AIR);
    assert(TerrainBaseBlockAt(7, 0, -3, TERRAIN_FLAT) == BLOCK_DIRT);
    assert(TerrainBaseBlockAt(7, 8, -3, TERRAIN_FLAT) == BLOCK_GRASS);
    assert(TerrainBaseBlockAt(7, 9, -3, TERRAIN_FLAT) == BLOCK_AIR);

    terrainSeed = 1448040515u;
    BathymetrySample deep = TerrainBathymetryAt(-2896, 16, TERRAIN_VARIED);
    assert(deep.seabedY == -4299);
    assert(TerrainBaseBlockAt(-2896, 0, 16, TERRAIN_VARIED) == BLOCK_WATER);
    assert(TerrainBaseBlockAt(
               -2896, SURFACE_MIN_Y, 16, TERRAIN_VARIED) == BLOCK_BEDROCK);
    BlockType seabed = TerrainBaseBlockAt(
        -2896, deep.seabedY, 16, TERRAIN_VARIED);
    assert(seabed == BathymetryMaterialBlock(deep.material) ||
           seabed == BLOCK_SILT);
    assert(TerrainBaseBlockAt(-2896, deep.seabedY + 1, 16,
                              TERRAIN_VARIED) == BLOCK_WATER);
    BlockType seaSurface = TerrainBaseBlockAt(
        -2896, HOME_SEA_LEVEL, 16, TERRAIN_VARIED);
    assert(seaSurface == BLOCK_WATER || seaSurface == BLOCK_ICE);
    assert(TerrainBaseBlockAt(-2896, HOME_SEA_LEVEL + 1, 16,
                              TERRAIN_VARIED) == BLOCK_AIR);
    terrainSeed = DEFAULT_WORLD_SEED;
}

static void TestNaturalGeologyDistribution(void)
{
    bool gravel = false;
    bool clay = false;
    bool mud = false;
    bool mossyStone = false;
    bool redSand = false;
    bool copperOre = false;
    bool granite = false;
    bool limestone = false;
    bool shale = false;
    bool marble = false;
    bool peat = false;
    bool permafrost = false;
    bool rockSalt = false;
    bool quartzOre = false;
    bool loam = false;
    bool podzol = false;
    bool silt = false;
    bool chalk = false;
    bool gneiss = false;
    bool laterite = false;
    bool tinOre = false;
    bool silverOre = false;
    bool nickelOre = false;

    terrainSeed = DEFAULT_WORLD_SEED;
    for (int z = -4096; z <= 4096; z += 32) {
        for (int x = -4096; x <= 4096; x += 32) {
            SurfaceTerrainSample surface = SurfaceTerrainAt(
                x, z, TERRAIN_VARIED);
            int height = (int)lroundf(surface.elevation);
            const int sampleY[] = { height, height - 1, height - 5, 40 };
            for (int index = 0; index < 4; index++) {
                int y = sampleY[index];
                if (y > height || y < SURFACE_MIN_Y ||
                    y >= SURFACE_MAX_Y_EXCLUSIVE) continue;
                BlockType type = TerrainBaseBlockAt(
                    x, y, z, TERRAIN_VARIED);
                assert(type == TerrainBaseBlockAt(
                    x, y, z, TERRAIN_VARIED));
                gravel = gravel || type == BLOCK_GRAVEL;
                clay = clay || type == BLOCK_CLAY;
                mud = mud || type == BLOCK_MUD;
                mossyStone = mossyStone || type == BLOCK_MOSSY_STONE;
                redSand = redSand || type == BLOCK_RED_SAND;
                copperOre = copperOre || type == BLOCK_COPPER_ORE;
                granite = granite || type == BLOCK_GRANITE;
                limestone = limestone || type == BLOCK_LIMESTONE;
                shale = shale || type == BLOCK_SHALE;
                marble = marble || type == BLOCK_MARBLE;
                peat = peat || type == BLOCK_PEAT;
                permafrost = permafrost || type == BLOCK_PERMAFROST;
                rockSalt = rockSalt || type == BLOCK_ROCK_SALT;
                quartzOre = quartzOre || type == BLOCK_QUARTZ_ORE;
                loam = loam || type == BLOCK_LOAM;
                podzol = podzol || type == BLOCK_PODZOL;
                silt = silt || type == BLOCK_SILT;
                chalk = chalk || type == BLOCK_CHALK;
                gneiss = gneiss || type == BLOCK_GNEISS;
                laterite = laterite || type == BLOCK_LATERITE;
                tinOre = tinOre || type == BLOCK_TIN_ORE;
                silverOre = silverOre || type == BLOCK_SILVER_ORE;
                nickelOre = nickelOre || type == BLOCK_NICKEL_ORE;
            }
        }
    }
    assert(gravel);
    assert(clay);
    assert(mud);
    assert(mossyStone);
    assert(redSand);
    assert(copperOre);
    assert(granite);
    assert(limestone);
    assert(shale);
    assert(marble);
    assert(peat);
    assert(permafrost);
    assert(rockSalt);
    assert(quartzOre);
    assert(loam);
    assert(podzol);
    assert(silt);
    assert(chalk);
    assert(gneiss);
    assert(laterite);
    assert(tinOre);
    assert(silverOre);
    assert(nickelOre);

    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_BASALT_PLAINS, 0, 1u) ==
           BLOCK_BASALT);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_BADLANDS, 0, 1u) ==
           BLOCK_RED_SAND);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_OASIS, 0, 1u) ==
           BLOCK_MUD);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_FOREST, 5, 4u) ==
           BLOCK_MOSSY_STONE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_FOREST, 5, 47u) ==
           BLOCK_COPPER_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_GAS, PLANET_BIOME_STORM_BANDS, 0, 7u) ==
           BLOCK_CRYSTAL);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_VOLCANIC_RIDGE, 0, 3u) ==
           BLOCK_VOLCANIC_ASH);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_LAVA_SEA, 0, 5u) ==
           BLOCK_PUMICE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_BASALT_PLAINS, 4, 43u) ==
           BLOCK_SULFUR_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_ICE, PLANET_BIOME_GLACIER, 0, 1u) ==
           BLOCK_PACKED_ICE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_ICE, PLANET_BIOME_ALPINE, 1, 1u) ==
           BLOCK_PERMAFROST);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 2, 11u) ==
           BLOCK_ROCK_SALT);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 8, 1u) ==
           BLOCK_LIMESTONE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_HIGHLANDS, 5, 89u) ==
           BLOCK_QUARTZ_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_HIGHLANDS, 5, 5u) ==
           BLOCK_GRANITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_OASIS, 1, 1u) ==
           BLOCK_PEAT);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_COAST, 5, 1u) ==
           BLOCK_SHALE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_LAVA_SEA, 0, 3u) ==
           BLOCK_SCORIA);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_BADLANDS, 0, 3u) ==
           BLOCK_LATERITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 0, 17u) ==
           BLOCK_SALT_CRUST);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_COAST, 0, 5u) ==
           BLOCK_SILT);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_FOREST, 0, 5u) ==
           BLOCK_PODZOL);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_PLAINS, 0, 11u) ==
           BLOCK_LOAM);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_IMPACT_BASIN, 0, 1u) ==
           BLOCK_REGOLITH);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_PLAINS, 6, 131u) ==
           BLOCK_TIN_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_ICE, PLANET_BIOME_ICE_SHEET, 8, 173u) ==
           BLOCK_SILVER_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_BASALT_PLAINS, 6, 127u) ==
           BLOCK_NICKEL_ORE);

    for (int z = -32; z <= 32; z += 8) {
        for (int x = -32; x <= 32; x += 8) {
            for (int y = 0; y <= 9; y++) {
                BlockType type = TerrainBaseBlockAt(x, y, z, TERRAIN_FLAT);
                assert(type < BLOCK_NATURAL_START ||
                       type > BLOCK_NATURAL_END);
            }
        }
    }
}

static void TestIndependentSectionBaseGeneration(void)
{
    Chunk flat = { 0 };
    assert(GenerateChunkTerrainSectionBase(
        &flat, 2, -3, 0, TERRAIN_FLAT));
    assert(flat.sectionCount == 1);
    assert(flat.sections[0]->sectionY == 0);
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < SURFACE_SECTION_HEIGHT; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int worldX = 2 * CHUNK_SIZE + lx;
                int worldZ = -3 * CHUNK_SIZE + lz;
                assert(ChunkGetLocalBlock(&flat, lx, y, lz) ==
                       TerrainBaseBlockAt(
                           worldX, y, worldZ, TERRAIN_FLAT));
            }
        }
    }
    assert(!GenerateChunkTerrainSectionBase(
        &flat, 2, -3, 0, TERRAIN_FLAT));
    assert(GenerateChunkTerrainSectionBase(
        &flat, 2, -3, 1, TERRAIN_FLAT));
    assert(flat.sectionCount == 1);
    assert(ChunkGetSectionConst(&flat, 1) == NULL);
    assert(ChunkTerrainSectionIsResolved(&flat, 1));
    assert(!GenerateChunkTerrainSectionBase(
        &flat, 2, -3, 1, TERRAIN_FLAT));

    assert(ChunkSetLocalBlock(&flat, 3, 4, 5, BLOCK_AIR));
    assert(ChunkGetLocalBlock(&flat, 3, 4, 5) == BLOCK_AIR);
    assert(ChunkGetLocalBlock(&flat, 4, 4, 5) == BLOCK_DIRT);
    ChunkClearBlockStorage(&flat);

    terrainSeed = 1448040515u;
    Chunk deep = { 0 };
    int cx = FloorDivInt(-2896, CHUNK_SIZE);
    int cz = FloorDivInt(16, CHUNK_SIZE);
    int seabedY = TerrainBathymetryAt(
        -2896, 16, TERRAIN_VARIED).seabedY;
    int seabedSectionY = SurfaceSectionYFromBlockY(seabedY);
    assert(GenerateChunkTerrainSectionBase(
        &deep, cx, cz, seabedSectionY, TERRAIN_VARIED));
    assert(deep.sectionCount == 1);
    assert(deep.sections[0]->sectionY == seabedSectionY);
    for (int y = seabedSectionY * SURFACE_SECTION_HEIGHT;
         y < (seabedSectionY + 1) * SURFACE_SECTION_HEIGHT; y++) {
        assert(ChunkGetLocalBlock(&deep, 0, y, 0) ==
               TerrainBaseBlockAt(-2896, y, 16, TERRAIN_VARIED));
    }
    ChunkClearBlockStorage(&deep);
    terrainSeed = DEFAULT_WORLD_SEED;
}

static void TestSparseChunkBootstrap(void)
{
    terrainSeed = 1448040515u;
    int cx = FloorDivInt(-2896, CHUNK_SIZE);
    int cz = FloorDivInt(16, CHUNK_SIZE);
    Chunk chunk = { 0 };
    TerrainTestBootstrapHomeChunk(&chunk, cx, cz, TERRAIN_VARIED);

    int seabedSectionY = SurfaceSectionYFromBlockY(
        TerrainBathymetryAt(-2896, 16, TERRAIN_VARIED).seabedY);
    assert(ChunkGetSectionConst(&chunk, 5));
    assert(ChunkGetSectionConst(&chunk, 4));
    assert(!ChunkGetSectionConst(&chunk, seabedSectionY));
    assert(chunk.sectionCount >= 2 && chunk.sectionCount <= 4);
    assert(ChunkGetLocalBlock(&chunk, 0, HOME_SEA_LEVEL, 0) ==
           TerrainBaseBlockAt(-2896, HOME_SEA_LEVEL, 16,
                              TERRAIN_VARIED));
    ChunkClearBlockStorage(&chunk);
    terrainSeed = DEFAULT_WORLD_SEED;
}

static void TestUndergroundFeaturesMaterializeTheirBase(void)
{
    bool found = false;
    for (int anchorX = -40; anchorX <= 40 && !found; anchorX++) {
        for (int anchorZ = -40; anchorZ <= 40 && !found; anchorZ++) {
            if (WorldHash2D(anchorX + 17, anchorZ + 29) % 100u >= 30u) {
                continue;
            }
            int wx = anchorX * 40;
            int wz = anchorZ * 40;
            int cx = FloorDivInt(wx, CHUNK_SIZE);
            int cz = FloorDivInt(wz, CHUNK_SIZE);
            int sampleX = cx * CHUNK_SIZE + CHUNK_SIZE - 1;
            int sampleZ = cz * CHUNK_SIZE + CHUNK_SIZE - 1;
            if (TerrainBathymetryAt(sampleX, sampleZ,
                                    TERRAIN_VARIED).waterDepth < 1000) {
                continue;
            }

            Chunk chunk = { 0 };
            TerrainTestBootstrapHomeChunk(
                &chunk, cx, cz, TERRAIN_VARIED);
            TerrainTestGenerateMineshaft(
                &chunk, cx, cz, TERRAIN_VARIED);
            const ChunkSection *featureSection = ChunkGetSectionConst(
                &chunk, SurfaceSectionYFromBlockY(8));
            assert(featureSection);
            BlockType expected = TerrainBaseBlockAt(
                sampleX, 1, sampleZ, TERRAIN_VARIED);
            assert(expected != BLOCK_AIR);
            assert(ChunkGetLocalBlock(
                       &chunk, CHUNK_SIZE - 1, 1, CHUNK_SIZE - 1) ==
                   expected);
            ChunkClearBlockStorage(&chunk);
            found = true;
        }
    }
    assert(found);
}

typedef struct TreePoint {
    int x;
    int z;
    int crownRadius;
} TreePoint;

typedef struct TreeShapeStats {
    int woodCount;
    int leafCount;
    int trunkHeight;
    int horizontalRadius;
    int minLeafY;
    int maxLeafY;
} TreeShapeStats;

static int CountChunkBlocks(const Chunk *chunk, BlockType type)
{
    int count = 0;
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                if (ChunkGetLocalBlock(chunk, lx, y, lz) == type) count++;
            }
        }
    }
    return count;
}

static TreeShapeStats AnalyzeTreeShape(const Chunk *chunk, int centerX,
                                       int baseY, int centerZ)
{
    TreeShapeStats stats = {
        .minLeafY = WORLD_HEIGHT,
        .maxLeafY = -1
    };
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                BlockType type = ChunkGetLocalBlock(chunk, lx, y, lz);
                if (type != BLOCK_WOOD && type != BLOCK_LEAVES) continue;
                int worldX = chunk->cx * CHUNK_SIZE + lx;
                int worldZ = chunk->cz * CHUNK_SIZE + lz;
                int radiusX = abs(worldX - centerX);
                int radiusZ = abs(worldZ - centerZ);
                int radius = radiusX > radiusZ ? radiusX : radiusZ;
                if (radius > stats.horizontalRadius) {
                    stats.horizontalRadius = radius;
                }
                if (type == BLOCK_WOOD) {
                    stats.woodCount++;
                } else {
                    stats.leafCount++;
                    if (y < stats.minLeafY) stats.minLeafY = y;
                    if (y > stats.maxLeafY) stats.maxLeafY = y;
                }
            }
        }
    }

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(centerX, centerZ, &cx, &cz, &lx, &lz);
    assert(cx == chunk->cx && cz == chunk->cz);
    while (baseY + stats.trunkHeight < WORLD_HEIGHT &&
           ChunkGetLocalBlock(chunk, lx, baseY + stats.trunkHeight, lz) ==
               BLOCK_WOOD) {
        stats.trunkHeight++;
    }
    return stats;
}

static void AssertChunkBlocksEqual(const Chunk *first, const Chunk *second)
{
    assert(first->cx == second->cx && first->cz == second->cz);
    for (int y = 0; y < WORLD_HEIGHT; y++) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                assert(ChunkGetLocalBlock(first, lx, y, lz) ==
                       ChunkGetLocalBlock(second, lx, y, lz));
            }
        }
    }
}

static void TestTreePlacementSpacing(void)
{
    terrainSeed = 1448040515u;
    TreePoint points[4096];
    int treeCount = 0;
    for (int z = -96; z <= 96; z++) {
        for (int x = -96; x <= 96; x++) {
            bool placed = ShouldPlaceTree(x, z, TERRAIN_VARIED);
            assert(placed == ShouldPlaceTree(x, z, TERRAIN_VARIED));
            if (!placed) continue;
            assert(treeCount < (int)(sizeof(points) / sizeof(points[0])));
            int crownRadius = TerrainTestHomeTreeCrownRadiusAt(x, z);
            for (int i = 0; i < treeCount; i++) {
                int requiredSpacing = crownRadius > points[i].crownRadius
                    ? crownRadius : points[i].crownRadius;
                if (requiredSpacing < 3) requiredSpacing = 3;
                int dx = abs(points[i].x - x);
                int dz = abs(points[i].z - z);
                int distance = dx > dz ? dx : dz;
                assert(distance > requiredSpacing);
            }
            points[treeCount++] = (TreePoint){ x, z, crownRadius };
        }
    }
    assert(treeCount > 20);
    terrainSeed = DEFAULT_WORLD_SEED;
}

static void TestHomeTreeVariantSelection(void)
{
    bool broadleafSeen[3] = { false };
    bool coniferSeen[2] = { false };
    for (int i = 0; i < 128; i++) {
        int x = i * 17 - 503;
        int z = 211 - i * 29;
        int broadleaf = TerrainTestHomeTreeVariantAt(x, z, false);
        int conifer = TerrainTestHomeTreeVariantAt(x, z, true);
        assert(broadleaf == TerrainTestHomeTreeVariantAt(x, z, false));
        assert(conifer == TerrainTestHomeTreeVariantAt(x, z, true));
        assert(broadleaf >= 0 && broadleaf < 3);
        assert(conifer >= 0 && conifer < 2);
        broadleafSeen[broadleaf] = true;
        coniferSeen[conifer] = true;
    }
    for (int i = 0; i < 3; i++) assert(broadleafSeen[i]);
    for (int i = 0; i < 2; i++) assert(coniferSeen[i]);
}

static void TestHomeGroundCoverSelection(void)
{
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_PLAINS, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 173u) ==
        BLOCK_FLOWER);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_FOREST, HOME_SEA_LEVEL, HOME_SEA_LEVEL, 397u) ==
        BLOCK_MUSHROOM);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_FOREST, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 13u) ==
        BLOCK_FERN);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_FOREST, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 19u) ==
        BLOCK_MOSS_CARPET);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_PLAINS, HOME_SEA_LEVEL + 4, HOME_SEA_LEVEL, 23u) ==
        BLOCK_REED);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_PLAINS, HOME_SEA_LEVEL + 5, HOME_SEA_LEVEL, 23u) ==
        BLOCK_AIR);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_PLAINS, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 7u) ==
        BLOCK_TALL_GRASS);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_SNOW, HOME_SEA_LEVEL, HOME_SEA_LEVEL, 7u) == BLOCK_AIR);
    assert(TerrainTestHomeGroundCoverBlock(
        BIOME_FOREST, HOME_SEA_LEVEL, HOME_SEA_LEVEL, 173u * 13u) ==
        BLOCK_FLOWER);
}

static void TestHomeTreeShapes(void)
{
    TreeShapeStats broadleafStats[3] = { 0 };
    int broadleafSignatures[3] = { 0 };
    for (int variant = 0; variant < 3; variant++) {
        Chunk tree = { .cx = 0, .cz = 0 };
        assert(ChunkSetLocalBlock(&tree, 8, 40, 8, BLOCK_FLOWER));
        TerrainTestPlaceHomeTree(&tree, 8, 40, 8, false, variant);
        broadleafStats[variant] = AnalyzeTreeShape(&tree, 8, 40, 8);
        TreeShapeStats stats = broadleafStats[variant];
        assert(ChunkGetLocalBlock(&tree, 8, 40, 8) == BLOCK_WOOD);
        assert(stats.trunkHeight >= 5 && stats.trunkHeight <= 11);
        assert(stats.woodCount > stats.trunkHeight);
        assert(stats.leafCount > 20);
        assert(stats.maxLeafY > 40 + stats.trunkHeight - 1);
        assert(stats.maxLeafY - stats.minLeafY >= 3);
        assert(stats.horizontalRadius >= 2 &&
               stats.horizontalRadius <= 4);
        broadleafSignatures[variant] =
            stats.woodCount * 10000 + stats.leafCount * 10 +
            stats.horizontalRadius;
        ChunkClearBlockStorage(&tree);
    }
    assert(broadleafStats[0].horizontalRadius >= 3);
    assert(broadleafStats[1].horizontalRadius >= 3);
    assert(broadleafStats[2].horizontalRadius <= 2);
    assert(broadleafSignatures[0] != broadleafSignatures[1]);
    assert(broadleafSignatures[0] != broadleafSignatures[2]);
    assert(broadleafSignatures[1] != broadleafSignatures[2]);

    TreeShapeStats coniferStats[2] = { 0 };
    for (int variant = 0; variant < 2; variant++) {
        Chunk tree = { .cx = 0, .cz = 0 };
        TerrainTestPlaceHomeTree(&tree, 8, 40, 8, true, variant);
        coniferStats[variant] = AnalyzeTreeShape(&tree, 8, 40, 8);
        TreeShapeStats stats = coniferStats[variant];
        assert(stats.trunkHeight >= 9 && stats.trunkHeight <= 15);
        assert(stats.woodCount > stats.trunkHeight);
        assert(stats.leafCount > 20);
        assert(stats.maxLeafY > 40 + stats.trunkHeight - 1);
        assert(stats.maxLeafY - stats.minLeafY >= 6);
        assert(stats.horizontalRadius >= 2 &&
               stats.horizontalRadius <= 4);
        ChunkClearBlockStorage(&tree);
    }
    assert(coniferStats[0].horizontalRadius >= 3);
    assert(coniferStats[1].horizontalRadius <= 3);
    assert(coniferStats[1].trunkHeight > coniferStats[0].trunkHeight);

    Chunk left = { .cx = 0, .cz = 0 };
    Chunk right = { .cx = 1, .cz = 0 };
    Chunk rightRepeat = { .cx = 1, .cz = 0 };
    TerrainTestPlaceHomeTree(&left, 15, 40, 8, false, 1);
    TerrainTestPlaceHomeTree(&right, 15, 40, 8, false, 1);
    TerrainTestPlaceHomeTree(&rightRepeat, 15, 40, 8, false, 1);
    assert(CountChunkBlocks(&right, BLOCK_LEAVES) > 0);
    AssertChunkBlocksEqual(&right, &rightRepeat);

    ChunkClearBlockStorage(&left);
    ChunkClearBlockStorage(&right);
    ChunkClearBlockStorage(&rightRepeat);
}

static void TestTerrainSectionExposureClassification(void)
{
    terrainSeed = 1448040515u;

    Chunk exposed = { .cx = -8, .cz = -18 };
    assert(GenerateChunkTerrainSectionBase(
        &exposed, -8, -18, 5, TERRAIN_VARIED));
    const ChunkSection *exposedSection = ChunkGetSectionConst(&exposed, 5);
    assert(exposedSection != NULL);
    assert(TerrainSectionHasExposedFaces(
        exposedSection, -8, -18, 5, TERRAIN_VARIED));

    Chunk enclosed = { .cx = 0, .cz = 0 };
    assert(GenerateChunkTerrainSectionBase(
        &enclosed, 0, 0, 1, TERRAIN_VARIED));
    const ChunkSection *enclosedSection = ChunkGetSectionConst(&enclosed, 1);
    assert(enclosedSection != NULL);
    assert(!TerrainSectionHasExposedFaces(
        enclosedSection, 0, 0, 1, TERRAIN_VARIED));

    ChunkClearBlockStorage(&exposed);
    ChunkClearBlockStorage(&enclosed);
    terrainSeed = DEFAULT_WORLD_SEED;
}

int main(void)
{
    assert(SURFACE_WORLD_HEIGHT == 256);
    assert(SURFACE_SECTION_HEIGHT == 16);
    assert(SURFACE_MIN_Y == -16384);
    assert(SURFACE_MAX_Y_EXCLUSIVE == 16384);
    assert(SURFACE_SECTION_COUNT == 2048);
    assert(SURFACE_GENERATION_SECTION_COUNT == 16);
    TestSurfaceSampleContracts();
    TestEarthScaleRelief();
    TestBathymetryContracts();
    TestChunkSectionBoundaries();
    TestTerrainBaseBlockQueries();
    TestNaturalGeologyDistribution();
    TestIndependentSectionBaseGeneration();
    TestSparseChunkBootstrap();
    TestUndergroundFeaturesMaterializeTheirBase();
    TestTreePlacementSpacing();
    TestHomeGroundCoverSelection();
    TestHomeTreeVariantSelection();
    TestHomeTreeShapes();
    TestTerrainSectionExposureClassification();
    puts("terrain scale tests passed");
    return 0;
}
