#include "world/terrain_home_materials_internal.h"

#include "world/chunks.h"
#include "world/terrain_geology_internal.h"

#include <math.h>
#include <stdbool.h>

static bool HomeRedSandAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.10f, 947u) > 0.67f;
}

static bool HomeWetSoilAt(const SurfaceTerrainSample *surface)
{
    if (!surface) return false;
    return (surface->biome == BIOME_SWAMP ||
            surface->biome == BIOME_PLAINS ||
            surface->biome == BIOME_FOREST) &&
           surface->elevation <= (float)HOME_SEA_LEVEL + 6.0f &&
           surface->continentalness < 0.56f;
}

static bool HomeSwampPoolAt(int worldX, int worldZ,
                            const SurfaceTerrainSample *surface)
{
    if (!surface || surface->biome != BIOME_SWAMP) return false;
    float pools = HomeSurfaceNoiseAt(worldX, worldZ, 0.075f, 1151u);
    float edges = HomeSurfaceNoiseAt(worldX, worldZ, 0.19f, 1171u);
    return pools > 0.59f && edges > 0.34f;
}

static float HomeGeologyField(int worldX, int y, int worldZ,
                              unsigned int lane)
{
    return HomeVolumeNoiseAt(
        worldX, y, worldZ, 0.0085f, 0.004f, lane);
}

static bool HomeSaltDepositAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.010f, 887u) > 0.74f;
}

static bool HomePeatDepositAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.014f, 941u) > 0.48f;
}

static bool HomePodzolAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.018f, 997u) > 0.55f;
}

static bool HomeLoamAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.012f, 1031u) > 0.43f;
}

static bool HomeLateriteAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.009f, 1063u) > 0.72f;
}

static bool HomeChernozemAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.010f, 1201u) > 0.46f;
}

static bool HomeHumusAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.016f, 1217u) > 0.40f;
}

static bool HomeCompostAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.025f, 1231u) > 0.78f;
}

static bool HomeAlluviumAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.013f, 1249u) > 0.38f;
}

static bool HomeTerraRossaAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.011f, 1277u) > 0.70f;
}

static bool HomeBauxiteAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.008f, 1291u) > 0.78f;
}

static bool HomeShellBedAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.018f, 1303u) > 0.68f;
}

static bool HomeCoralLimestoneAt(int worldX, int worldZ)
{
    return HomeSurfaceNoiseAt(worldX, worldZ, 0.009f, 1321u) > 0.74f;
}

static BlockType HomeRockBlock(int worldX, int y, int worldZ, int height,
                               Biome biome)
{
    BlockType type = StoneOrCaveBlock(worldX, y, worldZ, height);
    int depth = height - y;
    if (type != BLOCK_STONE) return type;
    if (depth >= 2 && CaveAt(worldX, y + 1, worldZ, height) &&
        !CaveWaterAt(worldX, y + 1, worldZ, height) &&
        HomeVolumeHashAt(worldX, FloorDivInt(y, 2), worldZ, 1517u) % 47u ==
            0u) {
        return BLOCK_GUANO;
    }
    return TerrainGeologyHomeStoneBlock(
        biome, depth, HomeGeologyField(worldX, y, worldZ, 733u),
        HomeGeologyField(worldX, y, worldZ, 811u));
}

BlockType TerrainHomeBaseBlockFromSample(
    int worldX, int y, int worldZ, TerrainMode mode,
    const SurfaceTerrainSample *surface, int seaLevel)
{
    if (!surface || !InHeight(y) ||
        y >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE ||
        (mode == TERRAIN_FLAT && y < SURFACE_GENERATION_MIN_Y)) {
        return BLOCK_AIR;
    }
    int height = (int)lroundf(surface->elevation);
    Biome biome = surface->biome;
    bool submerged = seaLevel >= 0 && height < seaLevel;
    bool redSand = biome == BIOME_DESERT &&
                   HomeRedSandAt(worldX, worldZ);
    bool wetSoil = HomeWetSoilAt(surface);
    bool swampPool = HomeSwampPoolAt(worldX, worldZ, surface);
    bool peat = y <= height && wetSoil && y >= height - 4 &&
                HomePeatDepositAt(worldX, worldZ);
    bool podzol = biome == BIOME_FOREST &&
                  HomePodzolAt(worldX, worldZ);
    bool loam = (biome == BIOME_PLAINS || biome == BIOME_FOREST) &&
                HomeLoamAt(worldX, worldZ);
    bool laterite = biome == BIOME_DESERT &&
                    HomeLateriteAt(worldX, worldZ);
    bool chernozem = biome == BIOME_PLAINS &&
                     HomeChernozemAt(worldX, worldZ);
    bool humus = biome == BIOME_FOREST &&
                 HomeHumusAt(worldX, worldZ);
    bool compost = (biome == BIOME_FOREST || biome == BIOME_SWAMP) &&
                   HomeCompostAt(worldX, worldZ);
    bool alluvium = wetSoil && HomeAlluviumAt(worldX, worldZ);
    bool terraRossa = biome == BIOME_DESERT &&
                      HomeTerraRossaAt(worldX, worldZ);
    bool bauxite = biome == BIOME_DESERT && laterite &&
                   HomeBauxiteAt(worldX, worldZ);
    bool shallowMarine = submerged &&
                         surface->bathymetry.waterDepth > 0 &&
                         surface->bathymetry.waterDepth <= 14;
    bool shellBed = shallowMarine &&
                    HomeShellBedAt(worldX, worldZ);
    bool coralLimestone = shallowMarine && biome != BIOME_SNOW &&
                          HomeCoralLimestoneAt(worldX, worldZ);
    bool salt = y <= height && (biome == BIOME_DESERT || submerged) &&
                y >= height - 6 &&
                HomeSaltDepositAt(worldX, worldZ);

    if (y > height) {
        if (!submerged || y > seaLevel) return BLOCK_AIR;
        return biome == BIOME_SNOW && y == seaLevel
            ? BLOCK_ICE : BLOCK_WATER;
    }

    BlockType type = BLOCK_STONE;
    if (mode == TERRAIN_FLAT) {
        type = y == height ? BLOCK_GRASS : BLOCK_DIRT;
    } else if (y == SURFACE_MIN_Y || (!submerged && y == 0)) {
        type = BLOCK_BEDROCK;
    } else if (submerged) {
        int sedimentDepth = 2;
        if (surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
            surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) {
            sedimentDepth = 6;
        } else if (surface->bathymetry.zone == BATHYMETRY_ZONE_SLOPE ||
                   surface->bathymetry.zone ==
                       BATHYMETRY_ZONE_ABYSSAL_PLAIN) {
            sedimentDepth = 4;
        }
        if (y > height - sedimentDepth) {
            type = BathymetryMaterialBlock(surface->bathymetry.material);
            if (coralLimestone && y >= height - 2) {
                type = BLOCK_CORAL_LIMESTONE;
            } else if (shellBed && y >= height - 1) {
                type = BLOCK_SHELL_BED;
            } else if (alluvium && y >= height - 2) {
                type = BLOCK_ALLUVIUM;
            } else if ((surface->bathymetry.zone == BATHYMETRY_ZONE_SLOPE ||
                 surface->bathymetry.zone ==
                     BATHYMETRY_ZONE_ABYSSAL_PLAIN) &&
                HomeSurfaceNoiseAt(
                    worldX, worldZ, 0.011f, 1097u) > 0.42f) {
                type = BLOCK_SILT;
            }
            if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                 surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                salt) {
                type = BLOCK_ROCK_SALT;
            } else if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                        surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                HomeSurfaceNoiseAt(
                    worldX, worldZ, 1.0f / 6.0f, 313u) > 0.80f) {
                type = BLOCK_CLAY;
            }
        } else {
            type = HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (y < height) {
        if (biome == BIOME_DESERT) {
            type = y > height - 3
                ? (bauxite ? BLOCK_BAUXITE
                           : (terraRossa ? BLOCK_TERRA_ROSSA
                              : (salt ? BLOCK_ROCK_SALT
                                 : (laterite ? BLOCK_LATERITE
                                    : (redSand ? BLOCK_RED_SAND
                                               : BLOCK_SAND)))))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_SNOW) {
            type = y > height - 3
                ? BLOCK_PERMAFROST
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_MOUNTAIN) {
            type = y > height - 4 && height < 24
                ? BLOCK_DIRT
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_SWAMP) {
            type = y > height - 5
                ? (compost ? BLOCK_COMPOST
                           : (peat ? BLOCK_PEAT
                              : (alluvium ? BLOCK_ALLUVIUM
                                 : (y >= height - 2 ? BLOCK_MUD
                                                    : BLOCK_SILT))))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else {
            type = y > height - 4
                ? (compost ? BLOCK_COMPOST
                   : (humus ? BLOCK_HUMUS
                      : (chernozem ? BLOCK_CHERNOZEM
                         : (alluvium ? BLOCK_ALLUVIUM
                            : (peat ? BLOCK_PEAT
                               : (podzol ? BLOCK_PODZOL
                                  : (wetSoil ? BLOCK_MUD
                                     : (loam ? BLOCK_LOAM
                                             : BLOCK_DIRT))))))))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (biome == BIOME_DESERT) {
        type = bauxite ? BLOCK_BAUXITE
                       : (terraRossa ? BLOCK_TERRA_ROSSA
                          : (salt ? BLOCK_ROCK_SALT
                             : (laterite ? BLOCK_LATERITE
                                : (redSand ? BLOCK_RED_SAND : BLOCK_SAND))));
    } else if (biome == BIOME_SNOW) {
        type = BLOCK_SNOW;
    } else if (biome == BIOME_MOUNTAIN) {
        type = height >= 165 ? BLOCK_SNOW
                             : (height >= 125 ? BLOCK_STONE : BLOCK_GRASS);
    } else if (biome == BIOME_SWAMP) {
        type = swampPool ? BLOCK_WATER
                         : (compost ? BLOCK_COMPOST
                            : (peat ? BLOCK_PEAT
                               : (alluvium ? BLOCK_ALLUVIUM : BLOCK_MUD)));
    } else {
        type = compost ? BLOCK_COMPOST
                       : (humus ? BLOCK_HUMUS
                          : (chernozem ? BLOCK_CHERNOZEM
                             : (alluvium ? BLOCK_ALLUVIUM
                                : (peat ? BLOCK_PEAT
                                   : (podzol ? BLOCK_PODZOL
                                      : (wetSoil ? BLOCK_MUD
                                                 : BLOCK_GRASS))))));
    }

    if (mode != TERRAIN_FLAT && y == height - 2 &&
        CaveWaterAt(worldX, y, worldZ, height) &&
        CaveAt(worldX, y, worldZ, height) &&
        !CaveAt(worldX, y - 1, worldZ, height)) {
        return BLOCK_WATER;
    }
    return type;
}
