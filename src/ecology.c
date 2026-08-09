#include "ecology.h"

#include "chunks.h"
#include "space.h"
#include "terrain.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#define PLANET_FLORA_STRUCTURE_RADIUS 3

static uint32_t EcologyMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static uint32_t EcologyHash(int x, int z, uint32_t salt)
{
    uint32_t hash = PlanetWorldSeed() ^ salt;
    hash ^= (uint32_t)x * 0x9e3779b9u;
    hash ^= (uint32_t)z * 0x85ebca6bu;
    return EcologyMix(hash);
}

static float EcologyClamp(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile result = { 0 };
    if (!PlanetWorldIsActive()) return result;

    const PlanetProfile *planet = PlanetWorldProfile();
    float temperatureComfort = 1.0f -
        EcologyClamp(fabsf(planet->equilibriumTempK - 288.0f) / 230.0f);
    float atmosphere = EcologyClamp(planet->atmosphereDensity);
    float water = EcologyClamp(planet->oceanCoverage);
    float life = temperatureComfort * (0.18f + atmosphere * 0.72f) *
                 (0.70f + water * 0.30f);

    switch (planet->style) {
    case SOLAR_STYLE_TEMPERATE:
        life += 0.22f;
        result.flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    case SOLAR_STYLE_ICE:
        life *= 0.62f;
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_DESERT:
        life *= 0.50f;
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_LAVA:
        life *= 0.06f;
        result.flora = PLANET_FLORA_THERMAL_VENT;
        break;
    case SOLAR_STYLE_CRATER:
        life *= 0.12f;
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_GAS:
        life *= 0.18f;
        result.flora = PLANET_FLORA_SPORE;
        break;
    default:
        result.flora = PLANET_FLORA_SPORE;
        break;
    }

    if (planet->atmosphereType == PLANET_ATMOSPHERE_NONE) life *= 0.08f;
    else if (planet->atmosphereType == PLANET_ATMOSPHERE_CORROSIVE) life *= 0.28f;
    else if (planet->atmosphereType == PLANET_ATMOSPHERE_DENSE) life *= 0.72f;

    result.lifeDensity = EcologyClamp(life);
    result.floraDensity = EcologyClamp(0.10f + result.lifeDensity * 0.78f);
    if (planet->style == SOLAR_STYLE_LAVA || planet->style == SOLAR_STYLE_CRATER) {
        result.floraDensity = EcologyClamp(0.15f + result.lifeDensity * 0.35f);
    }
    result.faunaDensity = EcologyClamp((result.lifeDensity - 0.16f) * 1.12f);

    uint32_t paletteHash = EcologyHash(0, 0, 0x72a31u);
    int primary = 20 + (int)(paletteHash % 196u);
    int accent = 20 + (int)((paletteHash >> 8) % 196u);
    if (accent == primary) accent = (accent + 47) % 196 + 20;
    result.primaryBlock = (BlockType)(BLOCK_COLOR_START + primary);
    result.accentBlock = (BlockType)(BLOCK_COLOR_START + accent);
    return result;
}

float PlanetEcologyFaunaDensity(void)
{
    return PlanetEcologyCurrent().faunaDensity;
}

const char *PlanetEcologyLifeName(void)
{
    float density = PlanetEcologyCurrent().lifeDensity;
    if (density < 0.05f) return "Sterile";
    if (density < 0.20f) return "Trace life";
    if (density < 0.42f) return "Sparse life";
    if (density < 0.68f) return "Flourishing";
    return "Abundant life";
}

static void EcologySet(Chunk *chunk, int x, int y, int z, BlockType type)
{
    if (InHeight(y)) SetChunkLocalBlock(chunk, x, y, z, type);
}

static void PlaceAlienCanopy(Chunk *chunk, int x, int z, int ground,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int trunkHeight = 3 + (int)(hash % 3u);
    for (int y = base; y < base + trunkHeight; y++) {
        EcologySet(chunk, x, y, z, profile->primaryBlock);
    }
    for (int ox = -2; ox <= 2; ox++) {
        for (int oz = -2; oz <= 2; oz++) {
            int distance = abs(ox) + abs(oz);
            if (distance > 3) continue;
            EcologySet(chunk, x + ox, base + trunkHeight - 1, z + oz,
                       profile->accentBlock);
            if (distance < 2) {
                EcologySet(chunk, x + ox, base + trunkHeight, z + oz,
                           profile->accentBlock);
            }
        }
    }
    EcologySet(chunk, x, base + trunkHeight + 1, z, BLOCK_GLOWSTONE);
}

static void PlaceCrystal(Chunk *chunk, int x, int z, int ground,
                         const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = 2 + (int)(hash % 4u);
    for (int y = base; y < base + height; y++) {
        EcologySet(chunk, x, y, z,
                   y == base + height - 1 ? profile->accentBlock : profile->primaryBlock);
    }
    if (hash & 1u) {
        EcologySet(chunk, x - 1, base + 1, z, profile->accentBlock);
        EcologySet(chunk, x + 1, base, z, profile->accentBlock);
    } else {
        EcologySet(chunk, x, base + 1, z - 1, profile->accentBlock);
        EcologySet(chunk, x, base, z + 1, profile->accentBlock);
    }
}

static void PlaceSpore(Chunk *chunk, int x, int z, int ground,
                       const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int stemHeight = 2 + (int)(hash % 2u);
    for (int y = base; y < base + stemHeight; y++) {
        EcologySet(chunk, x, y, z, BLOCK_MUSHROOM);
    }
    for (int ox = -1; ox <= 1; ox++) {
        for (int oz = -1; oz <= 1; oz++) {
            if (abs(ox) + abs(oz) <= 1) {
                EcologySet(chunk, x + ox, base + stemHeight, z + oz,
                           profile->accentBlock);
            }
        }
    }
    EcologySet(chunk, x, base + stemHeight + 1, z, profile->primaryBlock);
}

static void PlaceThermalVent(Chunk *chunk, int x, int z, int ground,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = 2 + (int)(hash % 3u);
    for (int y = base; y < base + height; y++) EcologySet(chunk, x, y, z, BLOCK_NETHERRACK);
    EcologySet(chunk, x - 1, base, z, BLOCK_OBSIDIAN);
    EcologySet(chunk, x + 1, base, z, BLOCK_OBSIDIAN);
    EcologySet(chunk, x, base + height, z, BLOCK_GLOWSTONE);
    EcologySet(chunk, x, base + height, z + 1, profile->accentBlock);
}

static void PlacePlanetFlora(Chunk *chunk, int x, int z,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    if (biome == PLANET_BIOME_OCEAN || biome == PLANET_BIOME_LAVA_SEA ||
        biome == PLANET_BIOME_STORM_BANDS) return;
    int ground = PlanetTerrainHeight(x, z);
    if (ground > WORLD_HEIGHT - 7) return;

    PlanetFloraArchetype type = profile->flora;
    if (type == PLANET_FLORA_ALIEN_CANOPY && biome != PLANET_BIOME_FOREST &&
        biome != PLANET_BIOME_PLAINS && biome != PLANET_BIOME_OASIS) {
        type = PLANET_FLORA_SPORE;
    }
    switch (type) {
    case PLANET_FLORA_ALIEN_CANOPY:
        PlaceAlienCanopy(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_CRYSTAL:
        PlaceCrystal(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_SPORE:
        PlaceSpore(chunk, x, z, ground, profile, hash);
        break;
    case PLANET_FLORA_THERMAL_VENT:
    default:
        PlaceThermalVent(chunk, x, z, ground, profile, hash);
        break;
    }
}

void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ)
{
    if (!PlanetWorldIsActive()) return;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    int divisor = 920 - (int)(profile.floraDensity * 760.0f);
    if (divisor < 140) divisor = 140;
    int startX = chunkX * CHUNK_SIZE - PLANET_FLORA_STRUCTURE_RADIUS;
    int startZ = chunkZ * CHUNK_SIZE - PLANET_FLORA_STRUCTURE_RADIUS;
    int endX = chunkX * CHUNK_SIZE + CHUNK_SIZE + PLANET_FLORA_STRUCTURE_RADIUS;
    int endZ = chunkZ * CHUNK_SIZE + CHUNK_SIZE + PLANET_FLORA_STRUCTURE_RADIUS;
    for (int x = startX; x < endX; x++) {
        for (int z = startZ; z < endZ; z++) {
            uint32_t hash = EcologyHash(x, z, 0x314159u);
            if (hash % (uint32_t)divisor != 0u) continue;
            PlacePlanetFlora(chunk, x, z, &profile, hash);
        }
    }
}
