#include "world/terrain_geology_internal.h"

BlockType TerrainGeologyHomeStoneBlock(Biome biome, int depth, float region,
                                       float strata)
{
    if ((biome == BIOME_FOREST || biome == BIOME_SWAMP) &&
        depth <= 12 && strata > 0.61f) {
        return BLOCK_MOSSY_STONE;
    }
    if (biome == BIOME_MOUNTAIN && depth >= 5 && depth <= 72 &&
        region > 0.55f && strata > 0.79f) {
        return BLOCK_MARBLE;
    }
    if (depth >= 70 && region < 0.16f && strata > 0.58f) {
        return BLOCK_SERPENTINITE;
    }
    if ((biome == BIOME_MOUNTAIN && depth >= 28 && strata < 0.22f) ||
        (depth >= 104 && region >= 0.45f && region < 0.58f)) {
        return BLOCK_SCHIST;
    }
    if ((biome == BIOME_MOUNTAIN && depth >= 18 && strata < 0.54f) ||
        (depth >= 92 && region < 0.45f)) {
        return BLOCK_GNEISS;
    }
    if (biome == BIOME_MOUNTAIN && depth >= 16 && depth <= 76 &&
        region >= 0.44f && region < 0.52f && strata > 0.54f) {
        return BLOCK_DIORITE;
    }
    if ((biome == BIOME_MOUNTAIN && depth >= 4 && region > 0.40f) ||
        (depth >= 64 && region > 0.57f)) {
        return BLOCK_GRANITE;
    }
    if ((biome == BIOME_MOUNTAIN || biome == BIOME_DESERT) &&
        depth >= 3 && depth <= 22 && region > 0.82f && strata < 0.38f) {
        return BLOCK_TUFF;
    }
    if (biome == BIOME_MOUNTAIN && depth >= 3 && depth <= 38 &&
        region > 0.78f && strata >= 0.38f && strata < 0.62f) {
        return BLOCK_RHYOLITE;
    }
    if (biome == BIOME_MOUNTAIN && depth >= 5 && depth <= 62 &&
        region >= 0.28f && region < 0.40f && strata >= 0.40f) {
        return BLOCK_ANDESITE;
    }
    if (biome == BIOME_DESERT && depth >= 3 && depth <= 24 &&
        strata > 0.86f) {
        return BLOCK_GYPSUM;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_DESERT) &&
        depth >= 5 && depth <= 46 && region >= 0.50f && region < 0.58f &&
        strata >= 0.42f) {
        return BLOCK_DOLOMITE;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_SWAMP) &&
        depth >= 3 && depth <= 16 && region >= 0.44f && region < 0.50f &&
        strata > 0.62f) {
        return BLOCK_TRAVERTINE;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_DESERT) &&
        depth >= 8 && depth <= 36 && region >= 0.62f &&
        strata > 0.73f) {
        return BLOCK_PHOSPHATE_ROCK;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_DESERT ||
         biome == BIOME_SWAMP) &&
        depth >= 3 && depth <= 52 && region > 0.58f) {
        return BLOCK_LIMESTONE;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_DESERT) &&
        depth >= 3 && depth <= 28 && region > 0.48f && strata > 0.70f) {
        return BLOCK_CHALK;
    }
    if ((biome == BIOME_PLAINS || biome == BIOME_FOREST ||
         biome == BIOME_SWAMP ||
         biome == BIOME_SNOW) && depth >= 4 && depth <= 44 &&
        (region < 0.34f || strata < 0.30f)) {
        if (depth >= 18 && strata < 0.16f) return BLOCK_SLATE;
        return BLOCK_SHALE;
    }
    return BLOCK_STONE;
}

static BlockType PlanetOreBlock(SolarBodyStyle style, int depth,
                                unsigned int hash)
{
    if (style == SOLAR_STYLE_TEMPERATE && depth >= 6 &&
        hash % 131u == 0u) {
        return BLOCK_TIN_ORE;
    }
    if ((style == SOLAR_STYLE_CRATER || style == SOLAR_STYLE_ICE) &&
        depth >= 8 && hash % 173u == 0u) {
        return BLOCK_SILVER_ORE;
    }
    if ((style == SOLAR_STYLE_LAVA || style == SOLAR_STYLE_CRATER) &&
        depth >= 6 && hash % 127u == 0u) {
        return BLOCK_NICKEL_ORE;
    }
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
    if ((style == SOLAR_STYLE_LAVA || style == SOLAR_STYLE_CRATER) &&
        depth >= 8 && hash % 137u == 0u) {
        return BLOCK_MAGNETITE_ORE;
    }
    if ((style == SOLAR_STYLE_DESERT || style == SOLAR_STYLE_CRATER) &&
        depth >= 4 && hash % 109u == 0u) {
        return BLOCK_HEMATITE_ORE;
    }
    if ((style == SOLAR_STYLE_DESERT || style == SOLAR_STYLE_TEMPERATE) &&
        depth >= 2 && depth <= 18 && hash % 157u == 0u) {
        return BLOCK_BAUXITE;
    }
    if ((style == SOLAR_STYLE_TEMPERATE || style == SOLAR_STYLE_DESERT) &&
        depth >= 3 && depth <= 48 && hash % 149u == 0u) {
        return BLOCK_PHOSPHATE_ROCK;
    }
    return BLOCK_AIR;
}

static BlockType PlanetSurfaceBiomeBlock(PlanetBiome biome,
                                          unsigned int hash)
{
    switch (biome) {
    case PLANET_BIOME_LAVA_SEA:
        if (hash % 31u == 0u) return BLOCK_RHYOLITE;
        if (hash % 5u == 0u) return BLOCK_PUMICE;
        return hash % 3u == 0u ? BLOCK_SCORIA : BLOCK_BASALT;
    case PLANET_BIOME_BASALT_PLAINS:
    case PLANET_BIOME_VOLCANIC_RIDGE:
        if (hash % 37u == 0u) return BLOCK_TUFF;
        if (hash % 23u == 0u) return BLOCK_ANDESITE;
        if (hash % 19u == 0u) return BLOCK_GLOWSTONE;
        return hash % 3u == 0u ? BLOCK_VOLCANIC_ASH : BLOCK_BASALT;
    case PLANET_BIOME_GLACIER:
        return BLOCK_PACKED_ICE;
    case PLANET_BIOME_ICE_SHEET:
    case PLANET_BIOME_ALPINE:
        return BLOCK_SNOW;
    case PLANET_BIOME_BADLANDS:
        if (hash % 29u == 0u) return BLOCK_BAUXITE;
        if (hash % 17u == 0u) return BLOCK_TERRA_ROSSA;
        return hash % 3u == 0u ? BLOCK_LATERITE : BLOCK_RED_SAND;
    case PLANET_BIOME_DUNES:
        if (hash % 31u == 0u) return BLOCK_GYPSUM;
        if (hash % 17u == 0u) return BLOCK_SALT_CRUST;
        return hash % 5u == 0u ? BLOCK_RED_SAND : BLOCK_SAND;
    case PLANET_BIOME_COAST:
        if (hash % 29u == 0u) return BLOCK_CORAL_LIMESTONE;
        if (hash % 13u == 0u) return BLOCK_SHELL_BED;
        if (hash % 7u == 0u) return BLOCK_ALLUVIUM;
        return hash % 5u == 0u ? BLOCK_SILT : BLOCK_SAND;
    case PLANET_BIOME_OCEAN:
        if (hash % 31u == 0u) return BLOCK_CORAL_LIMESTONE;
        if (hash % 17u == 0u) return BLOCK_SHELL_BED;
        return hash % 3u == 0u ? BLOCK_SILT : BLOCK_SAND;
    case PLANET_BIOME_OASIS:
        return hash % 13u == 0u ? BLOCK_TRAVERTINE
                                : (hash % 7u == 0u ? BLOCK_ALLUVIUM
                                                   : BLOCK_MUD);
    case PLANET_BIOME_TEMPERATE_MARSH:
        if (hash % 13u == 0u) return BLOCK_HUMUS;
        if (hash % 7u == 0u) return BLOCK_ALLUVIUM;
        return hash % 5u == 0u ? BLOCK_PEAT
                               : (hash % 3u == 0u ? BLOCK_SILT : BLOCK_MUD);
    case PLANET_BIOME_SALT_MARSH:
        return hash % 4u == 0u ? BLOCK_SALT_CRUST
                               : (hash % 3u == 0u ? BLOCK_SILT : BLOCK_MUD);
    case PLANET_BIOME_FROZEN_MIRE:
        return hash % 4u == 0u ? BLOCK_PERMAFROST : BLOCK_PACKED_ICE;
    case PLANET_BIOME_MAGMA_MIRE:
        if (hash % 7u == 0u) return BLOCK_SULFUR_ORE;
        return hash % 3u == 0u ? BLOCK_PUMICE
                               : (hash % 2u == 0u ? BLOCK_SCORIA
                                                  : BLOCK_VOLCANIC_ASH);
    case PLANET_BIOME_CRATER_BOG:
        return hash % 5u == 0u ? BLOCK_REGOLITH
                               : (hash % 3u == 0u ? BLOCK_PACKED_ICE
                                                  : BLOCK_MUD);
    case PLANET_BIOME_FOREST:
        if (hash % 17u == 0u) return BLOCK_HUMUS;
        return hash % 5u == 0u ? BLOCK_PODZOL : BLOCK_GRASS;
    case PLANET_BIOME_PLAINS:
        if (hash % 13u == 0u) return BLOCK_CHERNOZEM;
        if (hash % 11u == 0u) return BLOCK_LOAM;
        return hash % 7u == 0u ? BLOCK_MUD : BLOCK_GRASS;
    case PLANET_BIOME_IMPACT_BASIN:
        return hash % 9u == 0u ? BLOCK_METEORITE : BLOCK_REGOLITH;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        return hash % 4u == 0u ? BLOCK_REGOLITH : BLOCK_MOON_ROCK;
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
        if (hash % 5u == 0u) return BLOCK_PUMICE;
        return hash % 3u == 0u ? BLOCK_SCORIA : BLOCK_VOLCANIC_ASH;
    }
    if (hash % 29u == 0u) return BLOCK_RHYOLITE;
    if (hash % 23u == 0u) return BLOCK_ANDESITE;
    return hash % 17u == 0u ? BLOCK_METEORITE : BLOCK_BASALT;
}

static BlockType PlanetIceBlock(PlanetBiome biome, int depth)
{
    if (depth == 0) {
        return biome == PLANET_BIOME_FROZEN_MIRE
            ? BLOCK_PACKED_ICE : BLOCK_SNOW;
    }
    if (biome == PLANET_BIOME_FROZEN_MIRE && depth <= 5) {
        return BLOCK_PERMAFROST;
    }
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
        if (biome == PLANET_BIOME_SALT_MARSH) {
            if (depth == 0 && hash % 4u == 0u) return BLOCK_SALT_CRUST;
            return hash % 3u == 0u ? BLOCK_SILT : BLOCK_MUD;
        }
        if (hash % 11u == 0u) return BLOCK_ROCK_SALT;
        if (hash % 17u == 0u) return BLOCK_SALT_CRUST;
        if (biome == PLANET_BIOME_BADLANDS && hash % 3u == 0u) {
            return BLOCK_LATERITE;
        }
        return biome == PLANET_BIOME_BADLANDS ? BLOCK_RED_SAND : BLOCK_SAND;
    }
    if (depth <= 8 && hash % 13u == 0u) return BLOCK_GYPSUM;
    if (depth <= 16) return hash % 7u == 0u ? BLOCK_DOLOMITE
                                            : BLOCK_LIMESTONE;
    return hash % 17u == 0u ? BLOCK_SLATE : BLOCK_SANDSTONE;
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
        return hash % 13u == 0u ? BLOCK_METEORITE : BLOCK_REGOLITH;
    }
    if (depth >= 16 && hash % 19u == 0u) return BLOCK_SERPENTINITE;
    if (depth >= 10 && hash % 13u == 0u) return BLOCK_SCHIST;
    if (hash % 11u == 0u) return BLOCK_DIORITE;
    if (hash % 5u == 0u) return BLOCK_GRANITE;
    return depth <= 3 ? BLOCK_REGOLITH : BLOCK_MOON_ROCK;
}

static BlockType PlanetTemperateBlock(PlanetBiome biome, int depth,
                                      unsigned int hash)
{
    if (depth == 0) {
        if (biome == PLANET_BIOME_TEMPERATE_MARSH) {
            return hash % 5u == 0u ? BLOCK_PEAT : BLOCK_MUD;
        }
        return BLOCK_GRASS;
    }
    if (depth <= 3) {
        if (biome == PLANET_BIOME_TEMPERATE_MARSH) {
            return hash % 3u == 0u ? BLOCK_SILT : BLOCK_PEAT;
        }
        if (biome == PLANET_BIOME_OASIS ||
            (biome == PLANET_BIOME_FOREST && hash % 3u == 0u)) {
            return BLOCK_PEAT;
        }
        return hash % 5u == 0u ? BLOCK_LOAM : BLOCK_DIRT;
    }
    if (biome == PLANET_BIOME_FOREST && depth <= 8 &&
        hash % 4u == 0u) return BLOCK_MOSSY_STONE;
    if ((biome == PLANET_BIOME_COAST ||
         biome == PLANET_BIOME_OCEAN) && depth <= 18) {
        return BLOCK_SHALE;
    }
    if (hash % 17u == 0u) return BLOCK_PHOSPHATE_ROCK;
    if (hash % 13u == 0u) return BLOCK_TRAVERTINE;
    if (hash % 11u == 0u) return BLOCK_DOLOMITE;
    if (hash % 7u == 1u) return BLOCK_LIMESTONE;
    if (hash % 7u == 2u) return depth >= 18 ? BLOCK_SLATE : BLOCK_SHALE;
    if (hash % 7u == 3u) return hash % 2u == 0u ? BLOCK_DIORITE
                                                 : BLOCK_GRANITE;
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
