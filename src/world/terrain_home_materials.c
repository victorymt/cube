#include "world/terrain_home_materials_internal.h"

#include "raymath.h"
#include "world/chunks.h"
#include "world/terrain_geology_internal.h"

#include <math.h>
#include <stdbool.h>

static float HomeNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float HomeWorldHashUnit2D(int x, int z, unsigned int lane)
{
    unsigned int hash = WorldHash2DBits(
        (unsigned int)(x + (int)(lane * 101u)),
        (unsigned int)(z - (int)(lane * 173u)));
    return (float)(hash & 0x00ffffffu) / 16777215.0f;
}

static float HomeWorldValueNoise2D(float x, float z, unsigned int lane)
{
    int x0 = (int)floorf(x);
    int z0 = (int)floorf(z);
    float tx = HomeNoiseSmooth(x - (float)x0);
    float tz = HomeNoiseSmooth(z - (float)z0);
    float a = Lerp(HomeWorldHashUnit2D(x0, z0, lane),
                   HomeWorldHashUnit2D(x0 + 1, z0, lane), tx);
    float b = Lerp(HomeWorldHashUnit2D(x0, z0 + 1, lane),
                   HomeWorldHashUnit2D(x0 + 1, z0 + 1, lane), tx);
    return Lerp(a, b, tz);
}

static bool HomeRedSandAt(int worldX, int worldZ)
{
    return WorldHash2D(FloorDivInt(worldX, 10) + 947,
                       FloorDivInt(worldZ, 10) - 947) % 3u == 0u;
}

static bool HomeWetSoilAt(const SurfaceTerrainSample *surface)
{
    if (!surface) return false;
    return (surface->biome == BIOME_PLAINS ||
            surface->biome == BIOME_FOREST) &&
           surface->elevation <= (float)HOME_SEA_LEVEL + 6.0f &&
           surface->continentalness < 0.56f;
}

static float HomeGeologyField(int worldX, int y, int worldZ,
                              unsigned int lane)
{
    float fx = (float)worldX * 0.0085f + (float)y * 0.0041f;
    float fz = (float)worldZ * 0.0085f - (float)y * 0.0037f;
    return HomeWorldValueNoise2D(fx, fz, lane);
}

static bool HomeSaltDepositAt(int worldX, int worldZ)
{
    return HomeWorldValueNoise2D((float)worldX * 0.010f,
                                 (float)worldZ * 0.010f, 887u) > 0.74f;
}

static bool HomePeatDepositAt(int worldX, int worldZ)
{
    return HomeWorldValueNoise2D((float)worldX * 0.014f,
                                 (float)worldZ * 0.014f, 941u) > 0.48f;
}

static BlockType HomeRockBlock(int worldX, int y, int worldZ, int height,
                               Biome biome)
{
    BlockType type = StoneOrCaveBlock(worldX, y, worldZ, height);
    int depth = height - y;
    if (type != BLOCK_STONE) return type;
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
    bool peat = y <= height && wetSoil && y >= height - 4 &&
                HomePeatDepositAt(worldX, worldZ);
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
            if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                 surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                salt) {
                type = BLOCK_ROCK_SALT;
            } else if ((surface->bathymetry.zone == BATHYMETRY_ZONE_COAST ||
                        surface->bathymetry.zone == BATHYMETRY_ZONE_SHELF) &&
                surface->bathymetry.material == BATHYMETRY_MATERIAL_SAND &&
                WorldHash2D(FloorDivInt(worldX, 6) + 313,
                            FloorDivInt(worldZ, 6) - 197) % 5u == 0u) {
                type = BLOCK_CLAY;
            }
        } else {
            type = HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (y < height) {
        if (biome == BIOME_DESERT) {
            type = y > height - 3
                ? (salt ? BLOCK_ROCK_SALT
                        : (redSand ? BLOCK_RED_SAND : BLOCK_SAND))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_SNOW) {
            type = y > height - 3
                ? BLOCK_PERMAFROST
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else if (biome == BIOME_MOUNTAIN) {
            type = y > height - 4 && height < 24
                ? BLOCK_DIRT
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        } else {
            type = y > height - 4
                ? (peat ? BLOCK_PEAT : (wetSoil ? BLOCK_MUD : BLOCK_DIRT))
                : HomeRockBlock(worldX, y, worldZ, height, biome);
        }
    } else if (biome == BIOME_DESERT) {
        type = salt ? BLOCK_ROCK_SALT
                    : (redSand ? BLOCK_RED_SAND : BLOCK_SAND);
    } else if (biome == BIOME_SNOW) {
        type = BLOCK_SNOW;
    } else if (biome == BIOME_MOUNTAIN) {
        type = height >= 165 ? BLOCK_SNOW
                             : (height >= 125 ? BLOCK_STONE : BLOCK_GRASS);
    } else {
        type = peat ? BLOCK_PEAT : (wetSoil ? BLOCK_MUD : BLOCK_GRASS);
    }

    if (mode != TERRAIN_FLAT && y == height - 2 &&
        CaveWaterAt(worldX, y, worldZ, height) &&
        CaveAt(worldX, y, worldZ, height) &&
        !CaveAt(worldX, y - 1, worldZ, height)) {
        return BLOCK_WATER;
    }
    return type;
}
