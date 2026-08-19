#include "ecology/ecology_internal.h"

#include "space/space_state.h"
#include "world/chunks.h"
#include "world/terrain.h"
#include "world/world.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>

#define PLANET_FLORA_STRUCTURE_RADIUS 3

static float FloraProfileScale(const PlanetEcologyProfile *profile)
{
    float scale = profile ? profile->organismScale : 1.0f;
    if (!isfinite(scale) || scale <= 0.0f) scale = 1.0f;
    if (scale < 0.65f) scale = 0.65f;
    if (scale > 1.60f) scale = 1.60f;
    return scale;
}

static int FloraScaledHeight(int baseHeight,
                             const PlanetEcologyProfile *profile)
{
    int height = (int)lroundf((float)baseHeight * FloraProfileScale(profile));
    if (height < 2) height = 2;
    if (height > 9) height = 9;
    return height;
}

static void EcologySet(Chunk *chunk, int x, int y, int z, BlockType type)
{
    if (InHeight(y)) SetChunkLocalBlock(chunk, x, y, z, type);
}

static void EcologySetIfAir(Chunk *chunk, int x, int y, int z,
                            BlockType type)
{
    int lx = x - chunk->cx * CHUNK_SIZE;
    int lz = z - chunk->cz * CHUNK_SIZE;
    if (lx < 0 || lx >= CHUNK_SIZE || lz < 0 || lz >= CHUNK_SIZE ||
        !InHeight(y) || ChunkGetLocalBlock(chunk, lx, y, lz) != BLOCK_AIR) {
        return;
    }
    SetChunkLocalBlock(chunk, x, y, z, type);
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
        .taxonId = -1,
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
        .primaryBlock = BLOCK_FUNGAL_STEM,
        .accentBlock = BLOCK_SPORE_CAP,
        .windResponse = 1.0f
    };
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY: {
        int trunkHeight = FloraScaledHeight(3 + (int)(hash % 3u), profile);
        int radius = FloraProfileScale(profile) > 1.25f ? 3 : 2;
        structure.minX = x - radius;
        structure.maxX = x + radius;
        structure.minZ = z - radius;
        structure.maxZ = z + radius;
        structure.maxY = base + trunkHeight + 1;
        structure.windResponse = 0.72f +
            (profile ? profile->floraDensity : 0.5f) * 0.28f;
    } break;
    case FLORA_STRUCTURE_CRYSTAL: {
        int height = FloraScaledHeight(2 + (int)(hash % 4u), profile);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height - 1;
        structure.windResponse = 0.12f;
    } break;
    case FLORA_STRUCTURE_SPORE: {
        int stemHeight = FloraScaledHeight(2 + (int)(hash % 2u), profile);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.minZ = z - 1;
        structure.maxZ = z + 1;
        structure.maxY = base + stemHeight + 1;
        structure.windResponse = 0.45f +
            (profile ? profile->floraDensity : 0.5f) * 0.30f;
    } break;
    case FLORA_STRUCTURE_THERMAL_VENT: {
        int height = FloraScaledHeight(2 + (int)(hash % 3u), profile);
        structure.minX = x - 1;
        structure.maxX = x + 1;
        structure.maxZ = z + 1;
        structure.maxY = base + height;
        structure.windResponse = 0.05f;
    } break;
    case FLORA_STRUCTURE_HOME_TREE:
        return;
    }
    switch (kind) {
    case FLORA_STRUCTURE_ALIEN_CANOPY:
        structure.primaryBlock = BLOCK_LIVING_STEM;
        structure.accentBlock = BLOCK_CANOPY_FROND;
        break;
    case FLORA_STRUCTURE_CRYSTAL:
        structure.primaryBlock = BLOCK_CRYSTAL_BLOOM;
        structure.accentBlock = BLOCK_CRYSTAL_BLOOM;
        break;
    case FLORA_STRUCTURE_SPORE:
        structure.primaryBlock = BLOCK_FUNGAL_STEM;
        structure.accentBlock = BLOCK_SPORE_CAP;
        break;
    case FLORA_STRUCTURE_THERMAL_VENT:
        structure.primaryBlock = BLOCK_VENT_CHIMNEY;
        structure.accentBlock = BLOCK_CHEMO_MAT;
        break;
    case FLORA_STRUCTURE_HOME_TREE:
        return;
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
    int trunkHeight = FloraScaledHeight(3 + (int)(hash % 3u), profile);
    int radius = FloraProfileScale(profile) > 1.25f ? 3 : 2;
    for (int y = base; y < base + trunkHeight; y++) {
        EcologySet(chunk, x, y, z, BLOCK_LIVING_STEM);
    }
    for (int ox = -radius; ox <= radius; ox++) {
        for (int oz = -radius; oz <= radius; oz++) {
            int distance = abs(ox) + abs(oz);
            if (distance > radius + 1) continue;
            EcologySet(chunk, x + ox, base + trunkHeight - 1, z + oz,
                       BLOCK_CANOPY_FROND);
            if (distance < 2) {
                EcologySet(chunk, x + ox, base + trunkHeight, z + oz,
                           BLOCK_CANOPY_FROND);
            }
        }
    }
    EcologySet(chunk, x, base + trunkHeight + 1, z, BLOCK_LUMINOUS_POD);
}

static void PlaceCrystal(Chunk *chunk, int x, int z, int ground,
                         const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = FloraScaledHeight(2 + (int)(hash % 4u), profile);
    for (int y = base; y < base + height; y++) {
        EcologySet(chunk, x, y, z, BLOCK_CRYSTAL_BLOOM);
    }
    if (hash & 1u) {
        EcologySet(chunk, x - 1, base + 1, z, BLOCK_CRYSTAL_BLOOM);
        EcologySet(chunk, x + 1, base, z, BLOCK_CRYSTAL_BLOOM);
    } else {
        EcologySet(chunk, x, base + 1, z - 1, BLOCK_CRYSTAL_BLOOM);
        EcologySet(chunk, x, base, z + 1, BLOCK_CRYSTAL_BLOOM);
    }
}
static void PlaceSpore(Chunk *chunk, int x, int z, int ground,
                       const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int stemHeight = FloraScaledHeight(2 + (int)(hash % 2u), profile);
    for (int y = base; y < base + stemHeight; y++) {
        EcologySet(chunk, x, y, z, BLOCK_FUNGAL_STEM);
    }
    for (int ox = -1; ox <= 1; ox++) {
        for (int oz = -1; oz <= 1; oz++) {
            if (abs(ox) + abs(oz) <= 1) {
                EcologySet(chunk, x + ox, base + stemHeight, z + oz,
                           BLOCK_SPORE_CAP);
            }
        }
    }
    EcologySet(chunk, x, base + stemHeight + 1, z, BLOCK_LUMINOUS_POD);
}

static void PlaceThermalVent(Chunk *chunk, int x, int z, int ground,
                             const PlanetEcologyProfile *profile, uint32_t hash)
{
    int base = ground + 1;
    int height = FloraScaledHeight(2 + (int)(hash % 3u), profile);
    for (int y = base; y < base + height; y++) {
        EcologySet(chunk, x, y, z, BLOCK_VENT_CHIMNEY);
    }
    EcologySet(chunk, x - 1, base, z, BLOCK_SCORIA);
    EcologySet(chunk, x + 1, base, z, BLOCK_SCORIA);
    EcologySet(chunk, x, base + height, z, BLOCK_LUMINOUS_POD);
    EcologySet(chunk, x, base + height, z + 1, BLOCK_CHEMO_MAT);
}

static BlockType PlanetGroundCoverBlock(
    PlanetBiomassClass biomass, PlanetFloraArchetype flora,
    PlanetBiome biome, uint32_t hash)
{
    BlockType type = BLOCK_LICHEN;
    bool wetland = biome == PLANET_BIOME_TEMPERATE_MARSH ||
                   biome == PLANET_BIOME_SALT_MARSH ||
                   biome == PLANET_BIOME_FROZEN_MIRE ||
                   biome == PLANET_BIOME_MAGMA_MIRE ||
                   biome == PLANET_BIOME_CRATER_BOG;
    switch (biomass) {
    case PLANET_BIOMASS_MICROBIAL:
        type = hash % 4u == 0u ? BLOCK_LICHEN : BLOCK_MICROBIAL_MAT;
        break;
    case PLANET_BIOMASS_FUNGAL:
        type = hash % 5u == 0u ? BLOCK_LICHEN : BLOCK_MYCELIUM;
        break;
    case PLANET_BIOMASS_CRYSTALLINE:
        type = hash % 3u == 0u ? BLOCK_LICHEN : BLOCK_MICROBIAL_MAT;
        break;
    case PLANET_BIOMASS_ANOMALOUS:
        type = hash % 3u == 0u ? BLOCK_MYCELIUM : BLOCK_MICROBIAL_MAT;
        break;
    case PLANET_BIOMASS_LUSH:
        type = biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_OASIS
                   ? BLOCK_MOSS_CARPET
                   : (hash % 3u == 0u ? BLOCK_FERN : BLOCK_LICHEN);
        break;
    case PLANET_BIOMASS_BARREN:
    default:
        return BLOCK_AIR;
    }
    if (flora == PLANET_FLORA_THERMAL_VENT) {
        type = BLOCK_CHEMO_MAT;
    } else if (wetland && biomass == PLANET_BIOMASS_LUSH) {
        if (biome == PLANET_BIOME_TEMPERATE_MARSH) {
            type = hash % 3u == 0u ? BLOCK_REED
                                   : (hash % 2u == 0u ? BLOCK_FERN
                                                      : BLOCK_MOSS_CARPET);
        } else if (biome == PLANET_BIOME_SALT_MARSH) {
            type = hash % 3u == 0u ? BLOCK_REED : BLOCK_LICHEN;
        } else if (biome == PLANET_BIOME_FROZEN_MIRE) {
            type = hash % 3u == 0u ? BLOCK_MOSS_CARPET : BLOCK_LICHEN;
        } else if (biome == PLANET_BIOME_CRATER_BOG) {
            type = hash % 2u == 0u ? BLOCK_REED : BLOCK_MOSS_CARPET;
        }
    }
    return type;
}

static void PlacePlanetGroundCover(Chunk *chunk, int x, int z,
                                   const PlanetEcologyProfile *profile,
                                   uint32_t hash)
{
    PlanetBiome biome = PlanetBiomeAt(x, z);
    if (biome == PLANET_BIOME_OCEAN || biome == PLANET_BIOME_LAVA_SEA ||
        biome == PLANET_BIOME_STORM_BANDS) return;
    int ground = PlanetTerrainHeight(x, z);
    if (!InHeight(ground + 1)) return;

    BlockType type = PlanetGroundCoverBlock(
        profile->biomass, profile->flora, biome, hash);
    if (type == BLOCK_AIR) return;
    EcologySetIfAir(chunk, x, ground + 1, z, type);
}

#ifdef CHUNKS_TESTING
BlockType PlanetEcologyTestGroundCoverBlock(
    PlanetBiomassClass biomass, PlanetFloraArchetype flora,
    int biome, uint32_t hash)
{
    return PlanetGroundCoverBlock(
        biomass, flora, (PlanetBiome)biome, hash);
}
#endif

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
        biome != PLANET_BIOME_PLAINS && biome != PLANET_BIOME_OASIS &&
        biome != PLANET_BIOME_TEMPERATE_MARSH &&
        biome != PLANET_BIOME_CRATER_BOG) {
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

static void PlanetEcologyApplyProfileToChunk(
    Chunk *chunk, int chunkX, int chunkZ,
    const PlanetEcologyProfile *profile)
{
    if (!chunk || !profile || profile->biomass == PLANET_BIOMASS_BARREN ||
        profile->floraDensity <= 0.0f) return;
    int coverDivisor = 46 - (int)(profile->floraDensity * 32.0f);
    if (coverDivisor < 12) coverDivisor = 12;
    int chunkStartX = chunkX * CHUNK_SIZE;
    int chunkStartZ = chunkZ * CHUNK_SIZE;
    for (int x = chunkStartX; x < chunkStartX + CHUNK_SIZE; x++) {
        for (int z = chunkStartZ; z < chunkStartZ + CHUNK_SIZE; z++) {
            uint32_t hash = EcologyHash(x, z, 0x5f3759u);
            if (hash % (uint32_t)coverDivisor != 0u) continue;
            PlanetEcologySuitability local = EcologyStaticSuitabilityForProfile(
                x, z, profile);
            uint32_t localHash = EcologyMix(hash ^ 0x9e3779b9u);
            float localRoll = (float)(localHash & 0x00ffffffu) / 16777215.0f;
            if (localRoll > local.carryingCapacity) continue;
            PlacePlanetGroundCover(chunk, x, z, profile, hash);
        }
    }

    if (profile->biomass == PLANET_BIOMASS_MICROBIAL) return;
    int divisor = 920 - (int)(profile->floraDensity * 760.0f);
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
                x, z, profile);
            uint32_t localHash = EcologyMix(hash ^ 0x6d2b79f5u);
            float localRoll = (float)(localHash & 0x00ffffffu) / 16777215.0f;
            if (localRoll > local.carryingCapacity) continue;
            PlacePlanetFlora(chunk, x, z, profile, hash);
        }
    }
}

void PlanetEcologyApplyToChunk(Chunk *chunk, int chunkX, int chunkZ)
{
    if (!PlanetWorldIsActive()) return;
    PlanetEcologyProfile profile = PlanetEcologyCurrent();
    PlanetEcologyApplyProfileToChunk(chunk, chunkX, chunkZ, &profile);
}

#ifdef CHUNKS_TESTING
void PlanetEcologyTestApplyProfileToChunk(
    Chunk *chunk, int chunkX, int chunkZ,
    const PlanetEcologyProfile *profile)
{
    PlanetEcologyApplyProfileToChunk(chunk, chunkX, chunkZ, profile);
}
#endif
