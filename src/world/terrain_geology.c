#include "world/terrain_geology_internal.h"

BlockType TerrainGeologyHomeStoneBlock(Biome biome, int depth, float region,
                                       float strata)
{
    if (biome == BIOME_FOREST && depth <= 12 && strata > 0.61f) {
        return BLOCK_MOSSY_STONE;
    }
    if (biome == BIOME_MOUNTAIN && depth >= 5 && depth <= 72 &&
        region > 0.55f && strata > 0.79f) {
        return BLOCK_MARBLE;
    }
    if ((biome == BIOME_MOUNTAIN && depth >= 4 && region > 0.40f) ||
        (depth >= 64 && region > 0.57f)) {
        return BLOCK_GRANITE;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_DESERT) &&
        depth >= 3 && depth <= 52 && region > 0.58f) {
        return BLOCK_LIMESTONE;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_FOREST ||
         biome == BIOME_SNOW) && depth >= 4 && depth <= 44 &&
        (region < 0.34f || strata < 0.30f)) {
        return BLOCK_SHALE;
    }
    return BLOCK_STONE;
}

static BlockType PlanetOreBlock(SolarBodyStyle style, int depth,
                                unsigned int hash)
{
    if (style == SOLAR_STYLE_TEMPERATE && depth >= 4 &&
        hash % 47u == 0u) {
        return BLOCK_COPPER_ORE;
    }
    if ((style == SOLAR_STYLE_TEMPERATE ||
         style == SOLAR_STYLE_CRATER || style == SOLAR_STYLE_GAS) &&
        depth >= 4 && hash % 89u == 0u) {
        return BLOCK_QUARTZ_ORE;
    }
    if (style == SOLAR_STYLE_LAVA && depth >= 2 && hash % 43u == 0u) {
        return BLOCK_SULFUR_ORE;
    }
    return BLOCK_AIR;
}

static BlockType PlanetSurfaceBiomeBlock(PlanetBiome biome,
                                          unsigned int hash)
{
    switch (biome) {
    case PLANET_BIOME_LAVA_SEA:
        if (hash % 5u == 0u) return BLOCK_PUMICE;
        return hash % 3u == 0u ? BLOCK_VOLCANIC_ASH : BLOCK_BASALT;
    case PLANET_BIOME_BASALT_PLAINS:
    case PLANET_BIOME_VOLCANIC_RIDGE:
        if (hash % 19u == 0u) return BLOCK_GLOWSTONE;
        return hash % 3u == 0u ? BLOCK_VOLCANIC_ASH : BLOCK_BASALT;
    case PLANET_BIOME_GLACIER:
        return BLOCK_PACKED_ICE;
    case PLANET_BIOME_ICE_SHEET:
    case PLANET_BIOME_ALPINE:
        return BLOCK_SNOW;
    case PLANET_BIOME_BADLANDS:
        return BLOCK_RED_SAND;
    case PLANET_BIOME_DUNES:
        return hash % 5u == 0u ? BLOCK_RED_SAND : BLOCK_SAND;
    case PLANET_BIOME_COAST:
    case PLANET_BIOME_OCEAN:
        return BLOCK_SAND;
    case PLANET_BIOME_OASIS:
        return BLOCK_MUD;
    case PLANET_BIOME_FOREST:
        return BLOCK_GRASS;
    case PLANET_BIOME_PLAINS:
        return hash % 7u == 0u ? BLOCK_MUD : BLOCK_GRASS;
    case PLANET_BIOME_IMPACT_BASIN:
        return hash % 9u == 0u ? BLOCK_METEORITE : BLOCK_MOON_SAND;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        return BLOCK_MOON_ROCK;
    case PLANET_BIOME_STORM_BANDS:
        return hash % 7u == 0u ? BLOCK_CRYSTAL : BLOCK_SOUL_SAND;
    default:
        return BLOCK_AIR;
    }
}

static BlockType PlanetLavaBlock(int depth, unsigned int hash)
{
    if (depth <= 3) {
        if (hash % 11u == 0u) return BLOCK_GLOWSTONE;
        return hash % 5u == 0u ? BLOCK_PUMICE : BLOCK_VOLCANIC_ASH;
    }
    return hash % 17u == 0u ? BLOCK_METEORITE : BLOCK_BASALT;
}

static BlockType PlanetIceBlock(PlanetBiome biome, int depth)
{
    if (depth == 0) return BLOCK_SNOW;
    if (depth <= 2 && biome == PLANET_BIOME_ALPINE) {
        return BLOCK_PERMAFROST;
    }
    if (depth <= 8) return BLOCK_PACKED_ICE;
    return BLOCK_MOON_ROCK;
}

static BlockType PlanetDesertBlock(PlanetBiome biome, int depth,
                                   unsigned int hash)
{
    if (depth <= 3) {
        if (hash % 11u == 0u) return BLOCK_ROCK_SALT;
        return biome == PLANET_BIOME_BADLANDS ? BLOCK_RED_SAND : BLOCK_SAND;
    }
    if (depth <= 16) return BLOCK_LIMESTONE;
    return BLOCK_SANDSTONE;
}

static BlockType PlanetGasBlock(int depth, unsigned int hash)
{
    if (depth <= 3) {
        if (hash % 13u == 0u) return BLOCK_CRYSTAL;
        return hash % 7u == 0u ? BLOCK_GLOWSTONE : BLOCK_SOUL_SAND;
    }
    return BLOCK_MOON_ROCK;
}

static BlockType PlanetCraterBlock(int depth, unsigned int hash)
{
    if (depth == 0) {
        return hash % 13u == 0u ? BLOCK_METEORITE : BLOCK_MOON_SAND;
    }
    return hash % 5u == 0u ? BLOCK_GRANITE : BLOCK_MOON_ROCK;
}

static BlockType PlanetTemperateBlock(PlanetBiome biome, int depth,
                                      unsigned int hash)
{
    if (depth == 0) return BLOCK_GRASS;
    if (depth <= 3) {
        if (biome == PLANET_BIOME_OASIS ||
            (biome == PLANET_BIOME_FOREST && hash % 3u == 0u)) {
            return BLOCK_PEAT;
        }
        return BLOCK_DIRT;
    }
    if (biome == PLANET_BIOME_FOREST && depth <= 8 &&
        hash % 4u == 0u) return BLOCK_MOSSY_STONE;
    if ((biome == PLANET_BIOME_COAST ||
         biome == PLANET_BIOME_OCEAN) && depth <= 18) {
        return BLOCK_SHALE;
    }
    if (hash % 7u == 1u) return BLOCK_LIMESTONE;
    if (hash % 7u == 2u) return BLOCK_SHALE;
    if (hash % 7u == 3u) return BLOCK_GRANITE;
    return BLOCK_STONE;
}

BlockType TerrainGeologyPlanetSubsurfaceBlock(SolarBodyStyle style,
                                               PlanetBiome biome, int depth,
                                               unsigned int hash)
{
    BlockType ore = PlanetOreBlock(style, depth, hash);
    if (ore != BLOCK_AIR) return ore;

    if (depth == 0) {
        BlockType surface = PlanetSurfaceBiomeBlock(biome, hash);
        if (surface != BLOCK_AIR) return surface;
    }

    switch (style) {
    case SOLAR_STYLE_LAVA:
        return PlanetLavaBlock(depth, hash);
    case SOLAR_STYLE_ICE:
        return PlanetIceBlock(biome, depth);
    case SOLAR_STYLE_DESERT:
        return PlanetDesertBlock(biome, depth, hash);
    case SOLAR_STYLE_GAS:
        return PlanetGasBlock(depth, hash);
    case SOLAR_STYLE_CRATER:
        return PlanetCraterBlock(depth, hash);
    case SOLAR_STYLE_TEMPERATE:
        return PlanetTemperateBlock(biome, depth, hash);
    default:
        return depth == 0 ? BLOCK_GRASS : BLOCK_STONE;
    }
}
