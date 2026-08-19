#include "world/chunks.h"
#include "world/terrain.h"
#include "world/terrain_geology_internal.h"
#include "world/terrain_home_materials_internal.h"
#include "ecology/flora_taxa.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

bool PlanetWorldIsActive(void)
{
    return false;
}

uint32_t PlanetWorldSeed(void)
{
    return terrainSeed;
}

SolarBodyStyle PlanetWorldStyle(void)
{
    return SOLAR_STYLE_TEMPERATE;
}

const PlanetProfile *PlanetWorldProfile(void)
{
    return NULL;
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_WATER || type == BLOCK_GLASS ||
           type == BLOCK_ICE || type == BLOCK_LEAVES;
}

static uint64_t TerrainChunkHash(const Chunk *chunk)
{
    uint64_t hash = UINT64_C(1469598103934665603);
    for (int sectionIndex = 0; sectionIndex < chunk->sectionCount;
         sectionIndex++) {
        const ChunkSection *section = chunk->sections[sectionIndex];
        uint32_t sectionY = (uint32_t)section->sectionY;
        hash ^= sectionY;
        hash *= UINT64_C(1099511628211);
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int ly = 0; ly < SURFACE_SECTION_HEIGHT; ly++) {
                for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                    hash ^= section->blocks[lx][ly][lz];
                    hash *= UINT64_C(1099511628211);
                }
            }
        }
    }
    return hash;
}

static void TestTerrainStructureBaselines(void)
{
    static const struct {
        int cx;
        int cz;
        uint64_t expectedHash;
    } baselines[] = {
        { -200, -200, UINT64_C(8452164288714417906) },
        { -100, -88, UINT64_C(1560829849132147165) },
        { -120, -105, UINT64_C(15945370240875556352) },
        { -225, 90, UINT64_C(5099965485900159630) }
    };

    terrainSeed = DEFAULT_WORLD_SEED;
    for (size_t index = 0;
         index < sizeof(baselines) / sizeof(baselines[0]); index++) {
        Chunk chunk = { 0 };
        TerrainTestBootstrapHomeChunk(
            &chunk, baselines[index].cx, baselines[index].cz,
            TERRAIN_VARIED);
        TerrainTestGenerateStructures(
            &chunk, baselines[index].cx, baselines[index].cz,
            TERRAIN_VARIED);
        assert(TerrainChunkHash(&chunk) == baselines[index].expectedHash);
        ChunkClearBlockStorage(&chunk);
    }
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
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_TEMPERATE_MARSH,
               0, 1u) == BLOCK_MUD);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_SALT_MARSH,
               0, 4u) == BLOCK_SALT_CRUST);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_ICE, PLANET_BIOME_FROZEN_MIRE,
               1, 1u) == BLOCK_PERMAFROST);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_MAGMA_MIRE,
               0, 7u) == BLOCK_SULFUR_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_BOG,
               0, 3u) == BLOCK_PACKED_ICE);

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

static void TestStage05GeologyRules(void)
{
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_MOUNTAIN, 5, 0.30f, 0.50f) == BLOCK_ANDESITE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_MOUNTAIN, 16, 0.48f, 0.60f) == BLOCK_DIORITE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_MOUNTAIN, 3, 0.80f, 0.50f) == BLOCK_RHYOLITE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_DESERT, 3, 0.90f, 0.20f) == BLOCK_TUFF);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_MOUNTAIN, 28, 0.50f, 0.10f) == BLOCK_SCHIST);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_FOREST, 18, 0.20f, 0.10f) == BLOCK_SLATE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_PLAINS, 70, 0.10f, 0.60f) == BLOCK_SERPENTINITE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_PLAINS, 5, 0.54f, 0.50f) == BLOCK_DOLOMITE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_DESERT, 3, 0.60f, 0.90f) == BLOCK_GYPSUM);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_PLAINS, 3, 0.47f, 0.70f) == BLOCK_TRAVERTINE);
    assert(TerrainGeologyHomeStoneBlock(
               BIOME_PLAINS, 8, 0.70f, 0.80f) == BLOCK_PHOSPHATE_ROCK);

    bool hematite = false;
    bool magnetite = false;
    for (int z = -128; z <= 128 && (!hematite || !magnetite); z++) {
        for (int x = -128; x <= 128; x++) {
            for (int y = 1; y <= 30; y++) {
                BlockType ore = OreAt(x, y, z);
                hematite |= ore == BLOCK_HEMATITE_ORE;
                magnetite |= ore == BLOCK_MAGNETITE_ORE;
            }
        }
    }
    assert(hematite);
    assert(magnetite);

    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_VOLCANIC_RIDGE, 0, 23u) ==
           BLOCK_ANDESITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_HIGHLANDS, 5, 11u) ==
           BLOCK_DIORITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_LAVA_SEA, 0, 31u) ==
           BLOCK_RHYOLITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_VOLCANIC_RIDGE, 0, 37u) ==
           BLOCK_TUFF);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_HIGHLANDS, 10, 13u) ==
           BLOCK_SCHIST);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 17, 17u) ==
           BLOCK_SLATE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_CRATER, PLANET_BIOME_CRATER_HIGHLANDS, 16, 19u) ==
           BLOCK_SERPENTINITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 10, 7u) ==
           BLOCK_DOLOMITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 8, 13u) ==
           BLOCK_GYPSUM);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_OASIS, 0, 13u) ==
           BLOCK_TRAVERTINE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_BADLANDS, 0, 29u) ==
           BLOCK_BAUXITE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_DESERT, PLANET_BIOME_DUNES, 4, 109u) ==
           BLOCK_HEMATITE_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_LAVA, PLANET_BIOME_BASALT_PLAINS, 8, 137u) ==
           BLOCK_MAGNETITE_ORE);
    assert(TerrainTestPlanetSubsurfaceBlock(
               SOLAR_STYLE_TEMPERATE, PLANET_BIOME_PLAINS, 3, 149u) ==
           BLOCK_PHOSPHATE_ROCK);
}

static SurfaceTerrainSample Stage05HomeSample(Biome biome, float elevation,
                                               float continentalness)
{
    return (SurfaceTerrainSample){
        .elevation = elevation,
        .seaLevel = HOME_SEA_LEVEL,
        .continentalness = continentalness,
        .biome = biome,
        .bathymetry = {
            .seabedY = (int)elevation,
            .seaLevel = HOME_SEA_LEVEL,
            .waterDepth = elevation < HOME_SEA_LEVEL
                ? HOME_SEA_LEVEL - (int)elevation : 0,
            .zone = elevation < HOME_SEA_LEVEL
                ? BATHYMETRY_ZONE_SHELF : BATHYMETRY_ZONE_LAND,
            .material = BATHYMETRY_MATERIAL_SEDIMENT
        }
    };
}

static void TestStage05HomeDeposits(void)
{
    bool found[BLOCK_STAGE05_BIOGENIC_END -
               BLOCK_STAGE05_BIOGENIC_START + 1] = { false };
    SurfaceTerrainSample plains = Stage05HomeSample(
        BIOME_PLAINS, 100.0f, 0.80f);
    SurfaceTerrainSample forest = Stage05HomeSample(
        BIOME_FOREST, 100.0f, 0.80f);
    SurfaceTerrainSample desert = Stage05HomeSample(
        BIOME_DESERT, 100.0f, 0.80f);
    SurfaceTerrainSample wet = Stage05HomeSample(
        BIOME_PLAINS, HOME_SEA_LEVEL + 4.0f, 0.40f);
    SurfaceTerrainSample marine = Stage05HomeSample(
        BIOME_PLAINS, HOME_SEA_LEVEL - 5.0f, 0.40f);

    for (int z = -512; z <= 512; z++) {
        for (int x = -512; x <= 512; x++) {
            const SurfaceTerrainSample *samples[] = {
                &plains, &forest, &desert, &wet, &marine
            };
            for (size_t sampleIndex = 0;
                 sampleIndex < sizeof(samples) / sizeof(samples[0]);
                 sampleIndex++) {
                int y = (int)samples[sampleIndex]->elevation;
                BlockType type = TerrainHomeBaseBlockFromSample(
                    x, y, z, TERRAIN_VARIED, samples[sampleIndex],
                    HOME_SEA_LEVEL);
                if (type >= BLOCK_STAGE05_BIOGENIC_START &&
                    type <= BLOCK_STAGE05_BIOGENIC_END) {
                    found[type - BLOCK_STAGE05_BIOGENIC_START] = true;
                }
            }
        }
    }
    found[BLOCK_LEAF_LITTER - BLOCK_STAGE05_BIOGENIC_START] =
        TerrainTestHomeGroundCoverBlock(
            BIOME_FOREST, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 3u) ==
        BLOCK_LEAF_LITTER;

    bool guano = false;
    SurfaceTerrainSample cave = Stage05HomeSample(
        BIOME_PLAINS, 100.0f, 0.80f);
    for (int z = -128; z <= 128 && !guano; z++) {
        for (int x = -128; x <= 128 && !guano; x++) {
            for (int y = 5; y < 96; y++) {
                if (TerrainHomeBaseBlockFromSample(
                        x, y, z, TERRAIN_VARIED, &cave,
                        HOME_SEA_LEVEL) == BLOCK_GUANO) {
                    guano = true;
                    break;
                }
            }
        }
    }
    found[BLOCK_GUANO - BLOCK_STAGE05_BIOGENIC_START] = guano;
    for (size_t index = 0; index < sizeof(found) / sizeof(found[0]);
         index++) {
        assert(found[index]);
    }
}

static void TestSwampTerrainGeneration(void)
{
    terrainSeed = DEFAULT_WORLD_SEED;
    int landSamples = 0;
    int swampSamples = 0;
    int poolSamples = 0;
    for (int z = -1024; z <= 1024; z += 16) {
        for (int x = -1024; x <= 1024; x += 16) {
            SurfaceTerrainSample sample = SurfaceTerrainAt(
                x, z, TERRAIN_VARIED);
            if (sample.bathymetry.waterDepth > 0) continue;
            landSamples++;
            if (sample.biome != BIOME_SWAMP) continue;
            swampSamples++;
            int height = (int)lroundf(sample.elevation);
            BlockType surface = TerrainBaseBlockAt(
                x, height, z, TERRAIN_VARIED);
            assert(surface == BLOCK_WATER || surface == BLOCK_MUD ||
                   surface == BLOCK_PEAT || surface == BLOCK_ALLUVIUM ||
                   surface == BLOCK_COMPOST);
            poolSamples += surface == BLOCK_WATER;
        }
    }
    float fraction = landSamples > 0
        ? (float)swampSamples / (float)landSamples : 0.0f;
    assert(fraction >= 0.01f && fraction <= 0.20f);
    assert(poolSamples > 0);
}

static void TestSubsurfaceLiquidSummary(void)
{
    terrainSeed = DEFAULT_WORLD_SEED;
    bool foundWater = false;
    for (int z = -256; z <= 256 && !foundWater; z += 8) {
        for (int x = -256; x <= 256; x += 8) {
            int surfaceHeight = TerrainHeight(x, z, TERRAIN_VARIED);
            TerrainSubsurfaceLiquidSummary summary =
                TerrainSubsurfaceLiquidSummaryAt(x, z, surfaceHeight);
            if (summary.kind == TERRAIN_SUBSURFACE_LIQUID_NONE) continue;
            assert(summary.kind == TERRAIN_SUBSURFACE_LIQUID_WATER);
            assert(summary.minY <= summary.maxY);
            assert(summary.floodedFraction > 0.0f &&
                   summary.floodedFraction <= 1.0f);
            foundWater = true;
            break;
        }
    }
    assert(foundWater);
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
                bool genericLog = type == BLOCK_WOOD;
                bool genericLeaf = type == BLOCK_LEAVES;
                bool taxonLog = type >= BLOCK_STAGE06_TREE_START &&
                    type <= BLOCK_STAGE06_TREE_END &&
                    (((int)type - BLOCK_STAGE06_TREE_START) % 2) == 0;
                bool taxonLeaf = type >= BLOCK_STAGE06_TREE_START &&
                    type <= BLOCK_STAGE06_TREE_END && !taxonLog;
                if (!genericLog && !genericLeaf && !taxonLog &&
                    !taxonLeaf) continue;
                int worldX = chunk->cx * CHUNK_SIZE + lx;
                int worldZ = chunk->cz * CHUNK_SIZE + lz;
                int radiusX = abs(worldX - centerX);
                int radiusZ = abs(worldZ - centerZ);
                int radius = radiusX > radiusZ ? radiusX : radiusZ;
                if (radius > stats.horizontalRadius) {
                    stats.horizontalRadius = radius;
                }
                if (genericLog || taxonLog) {
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
           (ChunkGetLocalBlock(chunk, lx, baseY + stats.trunkHeight, lz) ==
                BLOCK_WOOD ||
            (ChunkGetLocalBlock(chunk, lx,
                                baseY + stats.trunkHeight, lz) >=
                 BLOCK_STAGE06_TREE_START &&
             ChunkGetLocalBlock(chunk, lx,
                                baseY + stats.trunkHeight, lz) <=
                 BLOCK_STAGE06_TREE_END &&
             (((int)ChunkGetLocalBlock(
                   chunk, lx, baseY + stats.trunkHeight, lz) -
                BLOCK_STAGE06_TREE_START) % 2) == 0))) {
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
        BIOME_FOREST, HOME_SEA_LEVEL + 8, HOME_SEA_LEVEL, 3u) ==
        BLOCK_LEAF_LITTER);
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
        assert(ChunkGetLocalBlock(&tree, 8, 40, 8) >=
               BLOCK_STAGE06_TREE_START);
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
    assert(broadleafStats[2].horizontalRadius <= 3);
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
    assert(left.floraStructureCount == 1);
    assert(right.floraStructureCount == 1);
    assert(rightRepeat.floraStructureCount == 1);
    assert(memcmp(&left.floraStructures[0], &right.floraStructures[0],
                  sizeof(FloraStructureInstance)) == 0);
    assert(memcmp(&right.floraStructures[0], &rightRepeat.floraStructures[0],
                  sizeof(FloraStructureInstance)) == 0);
    assert(left.floraStructures[0].kind == FLORA_STRUCTURE_HOME_TREE);
    assert(left.floraStructures[0].taxonId == FLORA_TAXON_WILLOW);
    int boundaryLeaves = 0;
    for (int type = BLOCK_STAGE06_TREE_START + 1;
         type <= BLOCK_STAGE06_TREE_END; type += 2) {
        boundaryLeaves += CountChunkBlocks(&right, (BlockType)type);
    }
    assert(boundaryLeaves > 0);
    AssertChunkBlocksEqual(&right, &rightRepeat);

    ChunkClearBlockStorage(&left);
    ChunkClearBlockStorage(&right);
    ChunkClearBlockStorage(&rightRepeat);
}

static void TestNamedTaxonTreeShapes(void)
{
    int signatures[6] = { 0 };
    for (int taxonId = FLORA_TAXON_OAK; taxonId <= FLORA_TAXON_WILLOW;
         taxonId++) {
        const FloraTaxon *taxon = FloraTaxonAt((FloraTaxonId)taxonId);
        assert(taxon && FloraTaxonIsTree((FloraTaxonId)taxonId));
        Chunk tree = { .cx = 0, .cz = 0 };
        TerrainTestPlaceHomeTreeTaxon(&tree, 8, 40, 8, taxonId);
        assert(tree.floraStructureCount == 1);
        const FloraStructureInstance *structure = &tree.floraStructures[0];
        assert(structure->kind == FLORA_STRUCTURE_HOME_TREE);
        assert(structure->taxonId == taxonId);
        assert(structure->rootX == 8 && structure->groundY == 39 &&
               structure->rootZ == 8);
        assert(structure->primaryBlock == taxon->primaryBlock);
        assert(structure->accentBlock == taxon->accentBlock);
        assert(structure->windResponse == taxon->windResponse);

        int ownedCount = 0;
        int minX = INT_MAX;
        int minY = INT_MAX;
        int minZ = INT_MAX;
        int maxX = INT_MIN;
        int maxY = INT_MIN;
        int maxZ = INT_MIN;
        for (int x = structure->minX; x <= structure->maxX; x++) {
            for (int y = structure->minY; y <= structure->maxY; y++) {
                for (int z = structure->minZ; z <= structure->maxZ; z++) {
                    bool primary = ChunkFloraStructureOwnsBlock(
                        &tree, x, y, z, taxon->primaryBlock);
                    bool accent = ChunkFloraStructureOwnsBlock(
                        &tree, x, y, z, taxon->accentBlock);
                    assert(!(primary && accent));
                    int cx = 0;
                    int cz = 0;
                    int lx = 0;
                    int lz = 0;
                    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
                    assert(cx == 0 && cz == 0);
                    BlockType actual = ChunkGetLocalBlock(&tree, lx, y, lz);
                    BlockType expected = primary ? taxon->primaryBlock :
                        (accent ? taxon->accentBlock : BLOCK_AIR);
                    assert(actual == expected);
                    if (!primary && !accent) continue;
                    ownedCount++;
                    if (x < minX) minX = x;
                    if (y < minY) minY = y;
                    if (z < minZ) minZ = z;
                    if (x > maxX) maxX = x;
                    if (y > maxY) maxY = y;
                    if (z > maxZ) maxZ = z;
                }
            }
        }
        assert(ownedCount > 0);
        assert(minX == structure->minX && minY == structure->minY &&
               minZ == structure->minZ && maxX == structure->maxX &&
               maxY == structure->maxY && maxZ == structure->maxZ);
        TreeShapeStats stats = AnalyzeTreeShape(&tree, 8, 40, 8);
        assert(ChunkGetLocalBlock(&tree, 8, 40, 8) ==
               taxon->primaryBlock);
        assert(stats.trunkHeight >= (int)floorf(taxon->heightMin));
        assert(stats.trunkHeight <= (int)ceilf(taxon->heightMax));
        assert(stats.horizontalRadius <=
               (int)ceilf(taxon->crownRadius));
        assert(stats.woodCount > stats.trunkHeight);
        assert(stats.leafCount > 20);
        signatures[taxonId] = stats.trunkHeight * 1000000 +
            stats.woodCount * 1000 + stats.leafCount * 10 +
            stats.horizontalRadius;
        for (int prior = 0; prior < taxonId; prior++) {
            assert(signatures[taxonId] != signatures[prior]);
        }
        ChunkClearBlockStorage(&tree);
    }
}

static void TestFullColumnDecorationSectionsResolved(void)
{
    Chunk chunk = { .cx = 0, .cz = 0 };
    TerrainTestPlaceHomeTreeTaxon(
        &chunk, 8, 15, 8, FLORA_TAXON_ASPEN);
    assert(chunk.floraStructureCount == 1);
    assert(chunk.sectionCount >= 2);
    for (int index = 0; index < chunk.sectionCount; index++) {
        assert(!ChunkTerrainSectionIsResolved(
            &chunk, chunk.sections[index]->sectionY));
    }
    TerrainTestResolveMaterializedSections(&chunk);
    for (int index = 0; index < chunk.sectionCount; index++) {
        assert(ChunkTerrainSectionIsResolved(
            &chunk, chunk.sections[index]->sectionY));
    }
    ChunkClearBlockStorage(&chunk);
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
    TestTerrainStructureBaselines();
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
    TestStage05GeologyRules();
    TestStage05HomeDeposits();
    TestIndependentSectionBaseGeneration();
    TestSparseChunkBootstrap();
    TestUndergroundFeaturesMaterializeTheirBase();
    TestTreePlacementSpacing();
    TestHomeGroundCoverSelection();
    TestSwampTerrainGeneration();
    TestSubsurfaceLiquidSummary();
    TestHomeTreeVariantSelection();
    TestHomeTreeShapes();
    TestNamedTaxonTreeShapes();
    TestFullColumnDecorationSectionsResolved();
    TestTerrainSectionExposureClassification();
    puts("terrain scale tests passed");
    return 0;
}
