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

static int EcologyPaletteIndex(PlanetChemistry chemistry, uint32_t hash, bool accent)
{
    unsigned r;
    unsigned g;
    unsigned b;
    unsigned shift = accent ? 11u : 0u;
    switch (chemistry) {
    case PLANET_CHEMISTRY_SILICON:
        r = 3u + ((hash >> shift) & 1u);
        g = 2u + ((hash >> (shift + 3u)) & 2u);
        b = 2u + ((hash >> (shift + 5u)) & 1u);
        break;
    case PLANET_CHEMISTRY_SULFUR:
        r = 6u + ((hash >> shift) & 1u);
        g = 4u + ((hash >> (shift + 3u)) & 3u);
        b = (hash >> (shift + 5u)) & 1u;
        break;
    case PLANET_CHEMISTRY_CARBON:
    default:
        r = 1u + ((hash >> shift) & 2u);
        g = 4u + ((hash >> (shift + 3u)) & 3u);
        b = (hash >> (shift + 5u)) & 2u;
        break;
    }
    return (int)((r << 5u) | (g << 2u) | b);
}

PlanetEcologyProfile PlanetEcologyCurrent(void)
{
    PlanetEcologyProfile result = { 0 };
    if (!PlanetWorldIsActive()) return result;

    const PlanetProfile *planet = PlanetWorldProfile();
    float temperature = planet->equilibriumTempK;
    float temperatureComfort = 1.0f - EcologyClamp(fabsf(temperature - 288.0f) / 230.0f);
    float atmosphere = EcologyClamp(planet->atmosphereDensity);
    float water = EcologyClamp(planet->oceanCoverage);
    bool darkSide = PlanetWorldIsDarkSide();
    float atmosphereSupport = 0.0f;
    switch (planet->atmosphereType) {
    case PLANET_ATMOSPHERE_NONE:       atmosphereSupport = 0.03f; break;
    case PLANET_ATMOSPHERE_THIN:       atmosphereSupport = 0.22f; break;
    case PLANET_ATMOSPHERE_BREATHABLE: atmosphereSupport = 0.88f; break;
    case PLANET_ATMOSPHERE_DENSE:      atmosphereSupport = 0.96f; break;
    case PLANET_ATMOSPHERE_CORROSIVE:  atmosphereSupport = 0.38f; break;
    default: break;
    }
    float life = temperatureComfort * atmosphereSupport * (0.28f + water * 0.72f);
    if (planet->style == SOLAR_STYLE_TEMPERATE) life += 0.16f * (0.5f + water);
    if (planet->style == SOLAR_STYLE_ICE) life *= 0.66f;
    if (planet->style == SOLAR_STYLE_DESERT) life *= 0.52f;
    if (planet->style == SOLAR_STYLE_LAVA) life = fmaxf(life * 0.20f, 0.075f);
    if (planet->style == SOLAR_STYLE_CRATER) life *= 0.18f;
    if (planet->style == SOLAR_STYLE_GAS || !planet->hasSolidSurface) life = 0.0f;

    uint32_t seedHash = EcologyHash(0, 0, 0x72a31u);
    PlanetLifeHistory lifeHistory = PlanetLifeHistoryDerive(
        planet->seed, planet->ageGyr, life, planet->hasSolidSurface);
    result.planetAgeGyr = lifeHistory.planetAgeGyr;
    result.lifeOriginProbability = lifeHistory.originProbability;
    result.complexLifeProbability = lifeHistory.complexLifeProbability;
    result.evolutionProgress = lifeHistory.evolutionProgress;
    result.lifeOriginated = lifeHistory.lifeOriginated;
    result.hasComplexLife = lifeHistory.hasComplexLife;
    life = PlanetLifeHistoryDensity(&lifeHistory, life);

    float chemistryRoll = (float)(seedHash & 0xffffu) / 65535.0f;
    if (temperature > 365.0f) {
        result.chemistry = chemistryRoll < 0.64f ?
                           PLANET_CHEMISTRY_SULFUR : PLANET_CHEMISTRY_SILICON;
    } else if (chemistryRoll < 0.28f) {
        result.chemistry = PLANET_CHEMISTRY_SILICON;
    } else if (chemistryRoll < 0.48f) {
        result.chemistry = PLANET_CHEMISTRY_SULFUR;
    } else {
        result.chemistry = PLANET_CHEMISTRY_CARBON;
    }

    switch (planet->style) {
    case SOLAR_STYLE_TEMPERATE:
        result.flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    case SOLAR_STYLE_ICE:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_DESERT:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_LAVA:
        result.flora = PLANET_FLORA_THERMAL_VENT;
        break;
    case SOLAR_STYLE_CRATER:
        result.flora = PLANET_FLORA_CRYSTAL;
        break;
    case SOLAR_STYLE_GAS:
        result.flora = PLANET_FLORA_SPORE;
        break;
    default:
        result.flora = PLANET_FLORA_SPORE;
        break;
    }

    result.lifeDensity = EcologyClamp(life);
    if (!result.lifeOriginated || !planet->hasSolidSurface ||
        result.lifeDensity < 0.055f) {
        result.biomass = PLANET_BIOMASS_BARREN;
    } else if (!result.hasComplexLife) {
        result.biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (temperature > 360.0f && atmosphere > 0.16f) {
        result.biomass = PLANET_BIOMASS_CRYSTALLINE;
    } else if (darkSide && result.lifeDensity >= 0.12f) {
        result.biomass = PLANET_BIOMASS_ANOMALOUS;
    } else if (result.lifeDensity < 0.20f || planet->atmosphereType == PLANET_ATMOSPHERE_NONE) {
        result.biomass = PLANET_BIOMASS_MICROBIAL;
    } else if (result.lifeDensity > 0.60f && water > 0.25f) {
        result.biomass = PLANET_BIOMASS_LUSH;
    } else if ((seedHash % 5u) == 0u || water < 0.10f) {
        result.biomass = PLANET_BIOMASS_FUNGAL;
    } else {
        result.biomass = PLANET_BIOMASS_LUSH;
    }

    result.floraDensity = EcologyClamp(result.lifeDensity * 0.92f);
    result.faunaDensity = EcologyClamp((result.lifeDensity - 0.14f) * 1.12f);
    switch (result.biomass) {
    case PLANET_BIOMASS_BARREN:
        result.floraDensity = 0.0f;
        result.faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_MICROBIAL:
        result.floraDensity = EcologyClamp(result.lifeDensity * 0.18f);
        result.faunaDensity = 0.0f;
        break;
    case PLANET_BIOMASS_FUNGAL:
        result.flora = PLANET_FLORA_SPORE;
        result.floraDensity = EcologyClamp(0.12f + result.lifeDensity * 0.66f);
        break;
    case PLANET_BIOMASS_CRYSTALLINE:
        result.flora = PLANET_FLORA_CRYSTAL;
        result.faunaDensity = EcologyClamp(result.lifeDensity * 0.48f);
        break;
    case PLANET_BIOMASS_ANOMALOUS:
        result.flora = PLANET_FLORA_SPORE;
        result.faunaDensity = EcologyClamp(result.lifeDensity * 0.92f);
        break;
    case PLANET_BIOMASS_LUSH:
    default:
        result.flora = PLANET_FLORA_ALIEN_CANOPY;
        break;
    }

    float gravity = EcologyClamp((planet->surfaceGravity - 0.35f) / 1.45f) * 1.45f + 0.35f;
    result.organismScale = EcologyClamp(1.10f / sqrtf(gravity));
    if (result.organismScale < 0.48f) result.organismScale = 0.48f;
    if (result.organismScale > 2.20f) result.organismScale = 2.20f;
    result.bodyArmor = EcologyClamp((gravity - 0.76f) / 0.88f);
    result.supportsFlight = result.hasComplexLife && planet->hasSolidSurface &&
                           (planet->atmosphereDensity >= 0.72f || gravity <= 0.68f);
    result.darkSideColony = result.hasComplexLife && planet->hasSolidSurface &&
                            darkSide && result.lifeDensity >= 0.12f;
    if (result.darkSideColony) {
        result.bodyPlan = PLANET_BODY_COLONY;
        result.niche = PLANET_NICHE_BIOLUMINESCENT_COLONY;
    } else if (result.biomass == PLANET_BIOMASS_CRYSTALLINE) {
        result.bodyPlan = PLANET_BODY_HEXAPOD;
        result.niche = PLANET_NICHE_CRYSTAL_GRAZER;
    } else if (result.supportsFlight) {
        result.bodyPlan = PLANET_BODY_FLOATING;
        result.niche = PLANET_NICHE_FILTER_FEEDER;
    } else if (result.biomass == PLANET_BIOMASS_MICROBIAL) {
        result.bodyPlan = PLANET_BODY_SERPENTINE;
        result.niche = PLANET_NICHE_MICROBIAL;
    } else if (gravity < 0.70f) {
        result.bodyPlan = PLANET_BODY_BIPED;
        result.niche = PLANET_NICHE_GRAZER;
    } else if (gravity > 1.20f) {
        result.bodyPlan = PLANET_BODY_HEXAPOD;
        result.niche = PLANET_NICHE_GRAZER;
    } else {
        switch (seedHash % 3u) {
        case 0: result.bodyPlan = PLANET_BODY_QUADRUPED; break;
        case 1: result.bodyPlan = PLANET_BODY_BIPED; break;
        default: result.bodyPlan = PLANET_BODY_SERPENTINE; break;
        }
        result.niche = result.biomass == PLANET_BIOMASS_FUNGAL ?
                       PLANET_NICHE_DECOMPOSER : PLANET_NICHE_GRAZER;
    }
    result.limbCount = result.bodyPlan == PLANET_BODY_HEXAPOD ? 6 :
                       result.bodyPlan == PLANET_BODY_BIPED ? 2 :
                       result.bodyPlan == PLANET_BODY_QUADRUPED ? 4 : 0;
    float speedScale = 0.86f / sqrtf(gravity);
    if (result.bodyPlan == PLANET_BODY_FLOATING) speedScale *= 0.85f;
    if (result.bodyPlan == PLANET_BODY_COLONY || result.biomass == PLANET_BIOMASS_CRYSTALLINE) {
        speedScale *= 0.34f;
    }
    result.movementSpeed = EcologyClamp(speedScale * 0.70f);
    if (result.movementSpeed < 0.18f && result.faunaDensity > 0.0f) result.movementSpeed = 0.18f;
    result.temperament = EcologyClamp((float)((seedHash >> 17) & 255u) / 255.0f * 0.72f +
                                      (result.biomass == PLANET_BIOMASS_ANOMALOUS ? 0.22f : 0.0f));

    int primary = EcologyPaletteIndex(result.chemistry, seedHash, false);
    int accent = EcologyPaletteIndex(result.chemistry, seedHash, true);
    if (accent == primary) accent = (accent + 37) & 255;
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

const char *PlanetEcologyBiomassName(void)
{
    switch (PlanetEcologyCurrent().biomass) {
    case PLANET_BIOMASS_MICROBIAL:    return "Microbial";
    case PLANET_BIOMASS_FUNGAL:       return "Fungal";
    case PLANET_BIOMASS_CRYSTALLINE:  return "Crystalline";
    case PLANET_BIOMASS_LUSH:         return "Lush";
    case PLANET_BIOMASS_ANOMALOUS:    return "Anomalous";
    case PLANET_BIOMASS_BARREN:
    default:                          return "Barren";
    }
}

const char *PlanetEcologyChemistryName(void)
{
    switch (PlanetEcologyCurrent().chemistry) {
    case PLANET_CHEMISTRY_SILICON: return "Silicon";
    case PLANET_CHEMISTRY_SULFUR:  return "Sulfur";
    case PLANET_CHEMISTRY_CARBON:
    default:                        return "Carbon";
    }
}

const char *PlanetEcologyBodyPlanName(void)
{
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    if (!profile.lifeOriginated) return "None";
    if (!profile.hasComplexLife) return "Microscopic";
    switch (profile.bodyPlan) {
    case PLANET_BODY_BIPED:      return "Biped";
    case PLANET_BODY_HEXAPOD:    return "Hexapod";
    case PLANET_BODY_SERPENTINE: return "Serpentine";
    case PLANET_BODY_FLOATING:   return "Floating";
    case PLANET_BODY_COLONY:     return "Colony";
    case PLANET_BODY_QUADRUPED:
    default:                     return "Quadruped";
    }
}

const char *PlanetEcologyNicheName(void)
{
    switch (PlanetEcologyCurrent().niche) {
    case PLANET_NICHE_MICROBIAL:             return "Microbial mat";
    case PLANET_NICHE_DECOMPOSER:            return "Decomposer";
    case PLANET_NICHE_CRYSTAL_GRAZER:       return "Crystal rock-eater";
    case PLANET_NICHE_FILTER_FEEDER:        return "Floating grazer";
    case PLANET_NICHE_BIOLUMINESCENT_COLONY: return "Bioluminescent colony";
    case PLANET_NICHE_GRAZER:
    default:                                 return "Grazer";
    }
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
    if (profile.biomass == PLANET_BIOMASS_BARREN ||
        profile.floraDensity <= 0.0f) return;
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
