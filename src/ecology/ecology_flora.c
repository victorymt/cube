#include "ecology/ecology_internal.h"

#include "world/chunks.h"
#include "world/terrain.h"
#include "world/world.h"

#include <stdint.h>
#include <stdlib.h>

#define PLANET_FLORA_STRUCTURE_RADIUS 3

static void EcologySet(Chunk *chunk, int x, int y, int z, BlockType type)
{
    if (InHeight(y)) SetChunkLocalBlock(chunk, x, y, z, type);
}

static void RegisterFloraStructure(Chunk *chunk, int x, int z, int ground,
                                   const PlanetEcologyProfile *profile,
                                   PlanetFloraArchetype archetype,
                                   uint32_t hash)
{
    FloraStructureKind kind = FLORA_STRUCTURE_SPORE;
    switch (archetype) {
    case PLANET_FLORA_ALIEN_CANOPY:
        kind = FLORA_STRUCTURE_ALIEN_CANOPY;
        break;
    case PLANET_FLORA_CRYSTAL:
        kind = FLORA_STRUCTURE_CRYSTAL;
        break;
    case PLANET_FLORA_SPORE:
        kind = FLORA_STRUCTURE_SPORE;
        break;
    case PLANET_FLORA_THERMAL_VENT:
        kind = FLORA_STRUCTURE_THERMAL_VENT;
        break;
    }

    int base = ground + 1;
    FloraStructureInstance structure = {
        .kind = kind,
        .shapeHash = hash,
        .rootX = x,
        .groundY = ground,
        .rootZ = z,
        .minX = x,
        .minY = base,
        .minZ = z,
        .maxX = x,
        .maxY = base,
        .maxZ = z,
        .primaryBlock = profile->primaryBlock,
        .accentBlock = profile->accentBlock,
        .windResponse = 1.0f
    };
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = 3 + (int)(hash % 3u);
        structure.minX = x - 2;
        structure.maxX = x + 2;
        structure.minZ = z - 2;
        structure.maxZ = z + 2;
        structure.maxY = base + trunkHeight + 1;
        structure.windResponse = 1.0f;
    } break;
    case FLORA_STRUCTURE_CRYSTAL: {
        int height = 2 + (int)(hash % 4u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height - 1;
        structure.windResponse = 0.12f;
    } break;
    case FLORA_STRUCTURE_SPORE: {
        int stemHeight = 2 + (int)(hash % 2u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + stemHeight + 1;
        structure.windResponse = 0.65f;
    } break;
    case FLORA_STRUCTURE_THERMAL_VENT: {
        int height = 2 + (int)(hash % 3u);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height;
        structure.windResponse = 0.05f;
    } break;
    }

    int chunkMinX = chunk->cx * CHUNK_SIZE;
    int chunkMinZ = chunk->cz * CHUNK_SIZE;
    int chunkMaxX = chunkMinX + CHUNK_SIZE - 1;
    int chunkMaxZ = chunkMinZ + CHUNK_SIZE - 1;
    if (structure.maxX < chunkMinX || structure.minX > chunkMaxX ||
        structure.maxZ < chunkMinZ || structure.minZ > chunkMaxZ ||
        structure.maxY < SURFACE_GENERATION_MIN_Y ||
        structure.minY >= SURFACE_GENERATION_MAX_Y_EXCLUSIVE) return;

    for (int index = 0; index < chunk->floraStructureCount; index++) {
        const FloraStructureInstance *existing = &chunk->floraStructures[index];
        if (existing->kind == structure.kind &&
            existing->rootX == structure.rootX &&
            existing->rootZ == structure.rootZ) return;
    }
    if (chunk->floraStructureCount >= MAX_CHUNK_FLORA_STRUCTURES) return;
    chunk->floraStructures[chunk->floraStructureCount++] = structure;
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
    if (ground > SURFACE_GENERATION_MAX_Y_EXCLUSIVE - 7) return;

    PlanetFloraArchetype type = profile->flora;
    if (type == PLANET_FLORA_ALIEN_CANOPY && biome != PLANET_BIOME_FOREST &&
        biome != PLANET_BIOME_PLAINS && biome != PLANET_BIOME_OASIS) {
        type = PLANET_FLORA_SPORE;
    }
    RegisterFloraStructure(chunk, x, z, ground, profile, type, hash);
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
            PlanetEcologySuitability local = EcologyStaticSuitabilityForProfile(
                x, z, &profile);
            uint32_t localHash = EcologyMix(hash ^ 0x6d2b79f5u);
            float localRoll = (float)(localHash & 0x00ffffffu) / 16777215.0f;
            if (localRoll > local.carryingCapacity) continue;
            PlacePlanetFlora(chunk, x, z, &profile, hash);
        }
    }
}
