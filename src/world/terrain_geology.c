#include "world/terrain_geology_internal.h"

BlockType TerrainGeologyPlanetSubsurfaceBlock(SolarBodyStyle style,
                                               PlanetBiome biome, int depth,
                                               unsigned int hash)
{
    if (style == SOLAR_STYLE_TEMPERATE && depth >= 4 &&
        hash % 47u == 0u) {
        return BLOCK_COPPER_ORE;
    }
    if (depth == 0) {
        switch (biome) {
        case PLANET_BIOME_LAVA_SEA:
        case PLANET_BIOME_BASALT_PLAINS:
        case PLANET_BIOME_VOLCANIC_RIDGE:
            return hash % 19u == 0u ? BLOCK_GLOWSTONE : BLOCK_BASALT;
        case PLANET_BIOME_GLACIER:
            return BLOCK_ICE;
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
            break;
        }
    }

    switch (style) {
    case SOLAR_STYLE_LAVA:
        if (depth <= 3) {
            return hash % 11u == 0u ? BLOCK_GLOWSTONE : BLOCK_BASALT;
        }
        return hash % 17u == 0u ? BLOCK_METEORITE : BLOCK_BASALT;
    case SOLAR_STYLE_ICE:
        if (depth == 0) return BLOCK_SNOW;
        if (depth <= 3) return BLOCK_ICE;
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_DESERT:
        if (depth <= 3) {
            return biome == PLANET_BIOME_BADLANDS
                       ? BLOCK_RED_SAND : BLOCK_SAND;
        }
        return BLOCK_SANDSTONE;
    case SOLAR_STYLE_GAS:
        if (depth <= 3) {
            if (hash % 13u == 0u) return BLOCK_CRYSTAL;
            return hash % 7u == 0u ? BLOCK_GLOWSTONE : BLOCK_SOUL_SAND;
        }
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_CRATER:
        if (depth == 0) {
            return hash % 13u == 0u ? BLOCK_METEORITE : BLOCK_MOON_SAND;
        }
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_TEMPERATE:
        if (depth == 0) return BLOCK_GRASS;
        if (depth <= 3) return BLOCK_DIRT;
        if (biome == PLANET_BIOME_FOREST && depth <= 8 &&
            hash % 4u == 0u) return BLOCK_MOSSY_STONE;
        return BLOCK_STONE;
    default:
        return depth == 0 ? BLOCK_GRASS : BLOCK_STONE;
    }
}
