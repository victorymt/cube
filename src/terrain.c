#include "terrain.h"

#include "subsurface.h"

#include "discovery.h"
#include "ecology.h"

#include "raymath.h"
#include "chunks.h"
#include "space.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "chunks.h"
#include "world.h"
static unsigned int Hash2DBits(unsigned int xBits, unsigned int zBits)
{
    unsigned int h = 2166136261u;
    h = (h ^ xBits) * 16777619u;
    h = (h ^ (zBits * 374761393u)) * 16777619u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
}

unsigned int Hash2D(int x, int z)
{
    return Hash2DBits((unsigned int)x, (unsigned int)z);
}

static unsigned int MixWorldSeed(unsigned int hash)
{
    uint32_t seed = WorldGetSeed();
    if (seed == DEFAULT_WORLD_SEED) return hash;
    hash ^= seed + 0x9e3779b9u + (hash << 6) + (hash >> 2);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    return hash ^ (hash >> 13);
}

static float WorldSeedCoordinateOffset(unsigned int lane)
{
    uint32_t seed = WorldGetSeed();
    if (seed == DEFAULT_WORLD_SEED) return 0.0f;
    unsigned int hash = seed ^ (lane * 0x9e3779b9u);
    hash ^= hash >> 16;
    hash *= 2246822519u;
    int centered = (int)(hash % 200001u) - 100000;
    return (float)centered * 0.01f;
}

unsigned int WorldHash2D(int x, int z)
{
    return WorldHash2DBits((unsigned int)x, (unsigned int)z);
}

unsigned int WorldHash2DBits(unsigned int xBits, unsigned int zBits)
{
    return MixWorldSeed(Hash2DBits(xBits, zBits));
}

unsigned int WorldHash3D(int x, int y, int z)
{
    return MixWorldSeed(Hash3D(x, y, z));
}

static float WorldHashUnit2D(int x, int z, unsigned int lane)
{
    unsigned int h = WorldHash2DBits((unsigned int)(x + (int)(lane * 101u)),
                                     (unsigned int)(z - (int)(lane * 173u)));
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static float NoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float WorldValueNoise2D(float x, float z, unsigned int lane)
{
    int x0 = (int)floorf(x);
    int z0 = (int)floorf(z);
    float tx = NoiseSmooth(x - (float)x0);
    float tz = NoiseSmooth(z - (float)z0);
    float a = Lerp(WorldHashUnit2D(x0, z0, lane),
                   WorldHashUnit2D(x0 + 1, z0, lane), tx);
    float b = Lerp(WorldHashUnit2D(x0, z0 + 1, lane),
                   WorldHashUnit2D(x0 + 1, z0 + 1, lane), tx);
    return Lerp(a, b, tz);
}

static float WorldFractalNoise2D(float x, float z, unsigned int lane)
{
    float value = 0.0f;
    float amplitude = 0.58f;
    float total = 0.0f;
    for (int octave = 0; octave < 5; octave++) {
        value += WorldValueNoise2D(x, z, lane + (unsigned int)octave * 17u) *
                 amplitude;
        total += amplitude;
        x = x * 2.03f + 11.3f;
        z = z * 2.03f - 7.9f;
        amplitude *= 0.49f;
    }
    return value / total;
}

static float SmoothRange(float low, float high, float value)
{
    if (value <= low) return 0.0f;
    if (value >= high) return 1.0f;
    return NoiseSmooth((value - low) / (high - low));
}

static float HomeTerrainElevationCore(int x, int z,
                                      SurfaceTerrainSample *sample)
{
    float fx = (float)x + WorldSeedCoordinateOffset(1u);
    float fz = (float)z + WorldSeedCoordinateOffset(2u);
    float continentalness = WorldFractalNoise2D(fx * 0.00135f,
                                                fz * 0.00135f, 21u);
    float erosion = WorldFractalNoise2D(fx * 0.0038f, fz * 0.0038f, 79u);
    float ridgeNoise = WorldFractalNoise2D(fx * 0.0026f, fz * 0.0026f, 131u);
    float ridge = 1.0f - fabsf(ridgeNoise * 2.0f - 1.0f);
    ridge = SmoothRange(0.56f, 0.94f, ridge);
    float peakNoise = WorldFractalNoise2D(fx * 0.0065f, fz * 0.0065f, 211u);
    float peak = SmoothRange(0.69f, 0.91f, peakNoise) * ridge;
    float trenchBoundary = 1.0f - fabsf(
        WorldFractalNoise2D(fx * 0.0019f, fz * 0.0019f, 307u) * 2.0f - 1.0f);
    float trench = SmoothRange(0.80f, 0.97f, trenchBoundary);
    float detail = WorldFractalNoise2D(fx * 0.021f, fz * 0.021f, 401u) - 0.5f;
    const float coast = 0.50f;
    float elevation = 0.0f;
    if (continentalness < coast) {
        float ocean = Clamp((coast - continentalness) / coast, 0.0f, 1.0f);
        float shelf = SmoothRange(0.0f, 0.18f, ocean);
        elevation = (float)HOME_SEA_LEVEL - 4.0f - shelf * 14.0f -
                    powf(ocean, 1.35f) * 43.0f;
        elevation -= trench * SmoothRange(0.20f, 0.72f, ocean) * 34.0f;
        elevation += detail * 4.0f;
    } else {
        float land = Clamp((continentalness - coast) / (1.0f - coast),
                           0.0f, 1.0f);
        float mountainMask = SmoothRange(0.08f, 0.64f, land) *
                             (1.0f - erosion * 0.48f);
        elevation = (float)HOME_SEA_LEVEL + 3.0f + land * 34.0f;
        elevation += ridge * mountainMask * 92.0f;
        elevation += peak * mountainMask * 48.0f;
        elevation += detail * (5.0f + land * 8.0f);
    }
    elevation = Clamp(elevation, 8.0f, 240.0f);
    if (sample) {
        sample->continentalness = continentalness;
        sample->erosion = erosion;
        sample->ridge = ridge;
        sample->peak = peak;
        sample->trench = trench;
    }
    return elevation;
}

SurfaceTerrainSample SurfaceTerrainAt(int x, int z, TerrainMode mode)
{
    SurfaceTerrainSample sample = { 0 };
    sample.seaLevel = mode == TERRAIN_FLAT ? -1.0f : (float)HOME_SEA_LEVEL;
    if (mode == TERRAIN_FLAT) {
        sample.elevation = 8.0f;
        sample.continentalness = 1.0f;
        sample.biome = BIOME_PLAINS;
        return sample;
    }
    sample.elevation = HomeTerrainElevationCore(x, z, &sample);
    float east = HomeTerrainElevationCore(x + 1, z, NULL);
    float west = HomeTerrainElevationCore(x - 1, z, NULL);
    float north = HomeTerrainElevationCore(x, z - 1, NULL);
    float south = HomeTerrainElevationCore(x, z + 1, NULL);
    sample.slope = fmaxf(fabsf(east - west), fabsf(north - south)) * 0.5f;
    float climate = WorldFractalNoise2D(
        ((float)x + WorldSeedCoordinateOffset(3u)) * 0.0018f,
        ((float)z + WorldSeedCoordinateOffset(4u)) * 0.0018f, 503u);
    if (sample.elevation >= 132.0f || sample.ridge > 0.58f) {
        sample.biome = BIOME_MOUNTAIN;
    } else if (climate < 0.25f) {
        sample.biome = BIOME_SNOW;
    } else if (climate < 0.43f) {
        sample.biome = BIOME_DESERT;
    } else if (climate > 0.64f) {
        sample.biome = BIOME_FOREST;
    } else {
        sample.biome = BIOME_PLAINS;
    }
    return sample;
}

float TerrainNoise(float x, float z)
{
    return HomeTerrainElevationCore((int)floorf(x), (int)floorf(z), NULL) -
           (float)HOME_SEA_LEVEL;
}

float BiomeNoise(int x, int z)
{
    SurfaceTerrainSample sample = SurfaceTerrainAt(x, z, TERRAIN_VARIED);
    return sample.continentalness * 2.0f - 1.0f;
}

Biome BiomeAt(int x, int z)
{
    return SurfaceTerrainAt(x, z, TERRAIN_VARIED).biome;
}

int TerrainSeaLevel(TerrainMode mode)
{
    return mode == TERRAIN_FLAT ? -1 : HOME_SEA_LEVEL;
}

int TerrainHeight(int x, int z, TerrainMode mode)
{
    if (mode == TERRAIN_FLAT) return 8;
    return (int)lroundf(HomeTerrainElevationCore(x, z, NULL));
}

bool ShouldPlaceTree(int x, int z, TerrainMode mode)
{
    if (mode == TERRAIN_FLAT) return false;

    int height = TerrainHeight(x, z, mode);
    if (height <= TerrainSeaLevel(mode) || height > WORLD_HEIGHT - 8) return false;
    unsigned int hash = WorldHash2D(x, z);
    switch (BiomeAt(x, z)) {
    case BIOME_FOREST:   return hash % 31u == 0u;
    case BIOME_PLAINS:   return hash % 107u == 0u;
    case BIOME_MOUNTAIN: return hash % 139u == 0u;
    case BIOME_SNOW:     return hash % 53u == 0u;
    default:             return false;
    }
}

bool CaveAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    return SubsurfaceSampleAt(&params, x, y, z, height).cave;
}

bool CaveWaterAt(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    return SubsurfaceSampleAt(&params, x, y, z, height).flooded;
}

BlockType OreAt(int x, int y, int z)
{
    unsigned int h = WorldHash3D(x, y, z);
    if (y <= 11 && (h % 281u) == 0u) return BLOCK_DIAMOND_ORE;
    if (y <= 16 && (h % 149u) == 0u) return BLOCK_GOLD_ORE;
    if (y <= 26 && (h % 71u) == 0u) return BLOCK_IRON_ORE;
    if (y <= 30 && (h % 43u) == 0u) return BLOCK_COAL_ORE;
    return BLOCK_STONE;
}

BlockType StoneOrCaveBlock(int x, int y, int z, int height)
{
    SubsurfaceParams params = {
        .seed = WorldGetSeed(),
        .activity = 1.0f,
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = 0.68f
    };
    SubsurfaceSample cave = SubsurfaceSampleAt(&params, x, y, z, height);
    if (cave.cave) return cave.flooded ? BLOCK_WATER : BLOCK_AIR;
    return OreAt(x, y, z);
}

bool ShouldPlacePond(int x, int z, int height)
{
    if (height > 6) return false;
    Biome biome = BiomeAt(x, z);
    if (biome == BIOME_DESERT || biome == BIOME_MOUNTAIN) return false;
    return WorldHash2D(x, z) % 97u == 0u;
}

void SetChunkLocalBlock(Chunk *chunk, int worldX, int y, int worldZ, BlockType type)
{
    if (!InHeight(y)) return;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(worldX, worldZ, &cx, &cz, &lx, &lz);
    if (chunk->cx == cx && chunk->cz == cz) {
        ChunkSetLocalBlock(chunk, lx, y, lz, type);
    }
}



static void GenerateMineshaft(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    (void)mode;
    const int spacing = 40;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 16, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 16, spacing);
    int minAnchorZ = FloorDivInt(startZ - 16, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 16, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 17, anchorZ + 29) % 100u >= 30u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 8 + (int)(WorldHash2D(anchorX + 3, anchorZ + 5) % 5u);
            int dx = (WorldHash2D(anchorX + 7, anchorZ + 11) % 2u) ? 1 : -1;
            int dz = (WorldHash2D(anchorX + 13, anchorZ + 19) % 2u) ? 1 : -1;
            int length = 12 + (int)(WorldHash2D(anchorX + 23, anchorZ + 31) % 9u);

            for (int i = 0; i < length; i++) {
                int bx = wx + dx * i;
                int bz = wz + dz * i;
                for (int ly = 0; ly <= 2; ly++) {
                    SetChunkLocalBlock(chunk, bx, wy + ly, bz, BLOCK_AIR);
                    if (i % 3 == 0 && ly == 0) {
                        SetChunkLocalBlock(chunk, bx, wy, bz, BLOCK_AIR);
                    }
                }
                if (i % 4 == 0) {
                    for (int ly = 0; ly <= 2; ly++) {
                        SetChunkLocalBlock(chunk, bx, wy + ly, bz, BLOCK_WOOD);
                    }
                    SetChunkLocalBlock(chunk, bx, wy + 3, bz, BLOCK_WOOD);
                }
            }
        }
    }
}

static void GenerateDungeon(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    (void)mode;
    const int spacing = 80;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 6, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 6, spacing);
    int minAnchorZ = FloorDivInt(startZ - 6, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 6, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 41, anchorZ + 53) % 100u >= 25u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 10 + (int)(WorldHash2D(anchorX + 2, anchorZ + 4) % 4u);

            for (int ox = -3; ox <= 3; ox++) {
                for (int oz = -3; oz <= 3; oz++) {
                    bool wall = abs(ox) == 3 || abs(oz) == 3;
                    bool door = (ox >= -1 && ox <= 0) && (oz == -3);
                    int bx = wx + ox;
                    int bz = wz + oz;
                    if (wall && !door) {
                        for (int oy = 0; oy <= 3; oy++) {
                            SetChunkLocalBlock(chunk, bx, wy + oy, bz, BLOCK_STONE_BRICKS);
                        }
                    }
                }
            }
            for (int oy = 0; oy <= 3; oy++) {
                SetChunkLocalBlock(chunk, wx - 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(chunk, wx + 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(chunk, wx, wy + oy, wz + 3, BLOCK_STONE_BRICKS);
            }
        }
    }
}

static void GenerateDesertTemple(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    const int spacing = 90;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 7, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 7, spacing);
    int minAnchorZ = FloorDivInt(startZ - 7, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 7, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            if (BiomeAt(wx, wz) != BIOME_DESERT) continue;
            if (WorldHash2D(anchorX + 67, anchorZ + 79) % 100u >= 20u) continue;

            int base = TerrainHeight(wx, wz, mode);

            for (int ox = -2; ox <= 2; ox++) {
                for (int oz = -2; oz <= 2; oz++) {
                    for (int oy = 0; oy <= 2; oy++) {
                        SetChunkLocalBlock(chunk, wx + ox, base + 1 + oy, wz + oz, BLOCK_SANDSTONE);
                    }
                }
            }
            SetChunkLocalBlock(chunk, wx, base + 4, wz, BLOCK_SANDSTONE);

            for (int oy = 1; oy <= 4; oy++) {
                SetChunkLocalBlock(chunk, wx - 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx + 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx, base + oy, wz - 4, BLOCK_SANDSTONE);
                SetChunkLocalBlock(chunk, wx, base + oy, wz + 4, BLOCK_SANDSTONE);
            }
            for (int ox = -1; ox <= 1; ox++) {
                for (int oz = -1; oz <= 1; oz++) {
                    for (int oy = 0; oy <= 3; oy++) {
                        SetChunkLocalBlock(chunk, wx + ox, base - 1 - oy, wz + oz, BLOCK_SANDSTONE);
                    }
                }
            }
        }
    }
}
#define VILLAGE_SPACING 48
#define VILLAGE_PROBABILITY 35u
#define VILLAGE_HALF_WIDTH 3
#define VILLAGE_HALF_DEPTH 2

static void SetVillageBlock(Chunk *chunk, int x, int y, int z, BlockType type)
{
    SetChunkLocalBlock(chunk, x, y, z, type);
}

static void GenerateVillage(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    int minAnchorX = FloorDivInt(startX - VILLAGE_HALF_WIDTH - 1, VILLAGE_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + VILLAGE_HALF_WIDTH, VILLAGE_SPACING);
    int minAnchorZ = FloorDivInt(startZ - VILLAGE_HALF_DEPTH - 1, VILLAGE_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + VILLAGE_HALF_DEPTH, VILLAGE_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * VILLAGE_SPACING;
            int wz = anchorZ * VILLAGE_SPACING;
            if (WorldHash2D(anchorX, anchorZ) % 100u >= VILLAGE_PROBABILITY) continue;
            if (BiomeAt(wx, wz) == BIOME_DESERT) continue;
            if (TerrainHeight(wx, wz, mode) < 4) continue;

            int minX = wx - VILLAGE_HALF_WIDTH;
            int maxX = wx + VILLAGE_HALF_WIDTH;
            int minZ = wz - VILLAGE_HALF_DEPTH;
            int maxZ = wz + VILLAGE_HALF_DEPTH;
            int base = TerrainHeight(wx, wz, mode);

            for (int x = minX; x <= maxX; x++) {
                for (int z = minZ; z <= maxZ; z++) {
                    if (x < startX || x >= startX + CHUNK_SIZE ||
                        z < startZ || z >= startZ + CHUNK_SIZE) continue;

                    for (int y = 0; y <= 5; y++) {
                        BlockType type = BLOCK_AIR;
                        bool edge = x == minX || x == maxX || z == minZ || z == maxZ;
                        bool corner = (x == minX || x == maxX) && (z == minZ || z == maxZ);
                        bool front = z == maxZ;

                        if (y == 0) {
                            type = BLOCK_PLANK;
                        } else if (y >= 1 && y <= 2 && edge) {
                            if (front && (x == wx - 1 || x == wx + 0)) type = BLOCK_AIR;
                            else if (y == 2 && edge && (x == wx || z == wz) &&
                                     !(z == minZ || z == maxZ) && x != minX && x != maxX) {
                                type = BLOCK_GLASS;
                            } else {
                                type = corner ? BLOCK_WOOD : BLOCK_PLANK;
                            }
                        } else if (y == 3) {
                            type = edge ? BLOCK_PLANK : BLOCK_AIR;
                        } else if (y == 4) {
                            type = BLOCK_AIR;
                            if (x > minX && x < maxX && z > minZ && z < maxZ) type = BLOCK_PLANK;
                        } else if (y == 5) {
                            type = BLOCK_AIR;
                            if (x >= minX + 2 && x <= maxX - 2 && z == wz) type = BLOCK_PLANK;
                        }
                        if (type != BLOCK_AIR) SetVillageBlock(chunk, x, base + y, z, type);
                    }
                }
            }
        }
    }
}

static unsigned int PlanetHash2D(int localX, int localZ, unsigned int lane)
{
    int globalX = localX + PlanetWorldOriginX();
    int globalZ = localZ + PlanetWorldOriginZ();
    unsigned int h = Hash2D(globalX + (int)(lane * 101u),
                            globalZ - (int)(lane * 173u));
    h ^= PlanetWorldSeed() + 0x9e3779b9u + (h << 6) + (h >> 2);
    h ^= h >> 16;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

static float PlanetHashUnit2D(int x, int z, unsigned int lane)
{
    return (float)(PlanetHash2D(x, z, lane) & 0x00ffffffu) / 16777215.0f;
}

static float PlanetNoiseSmooth(float value)
{
    return value * value * (3.0f - 2.0f * value);
}

static float PlanetValueNoise2D(float x, float z, unsigned int lane)
{
    int x0 = (int)floorf(x);
    int z0 = (int)floorf(z);
    float tx = PlanetNoiseSmooth(x - (float)x0);
    float tz = PlanetNoiseSmooth(z - (float)z0);
    float a = Lerp(PlanetHashUnit2D(x0, z0, lane),
                   PlanetHashUnit2D(x0 + 1, z0, lane), tx);
    float b = Lerp(PlanetHashUnit2D(x0, z0 + 1, lane),
                   PlanetHashUnit2D(x0 + 1, z0 + 1, lane), tx);
    return Lerp(a, b, tz);
}

static float PlanetFractalNoise2D(float x, float z, unsigned int lane)
{
    float value = 0.0f;
    float amplitude = 0.58f;
    float total = 0.0f;
    for (int octave = 0; octave < 4; octave++) {
        value += PlanetValueNoise2D(x, z, lane + (unsigned int)octave * 17u) * amplitude;
        total += amplitude;
        x = x * 2.07f + 11.3f;
        z = z * 2.07f - 7.9f;
        amplitude *= 0.48f;
    }
    return value / total;
}

static void PlanetSurfaceCoordinates(int x, int z, float *outX, float *outZ)
{
    *outX = (float)PlanetWorldOriginX() + (float)x;
    *outZ = (float)PlanetWorldOriginZ() + (float)z;
}

static void PlanetMapCoordinatesToLatLon(float mapX, float mapZ,
                                          float *outLongitude, float *outLatitude)
{
    float longitude = mapX * (2.0f * PI / PLANET_GLOBAL_CIRCUMFERENCE_BLOCKS);
    longitude = fmodf(longitude + PI, 2.0f * PI);
    if (longitude < 0.0f) longitude += 2.0f * PI;
    *outLongitude = longitude - PI;
    *outLatitude = Clamp(mapZ * (PI / PLANET_GLOBAL_POLE_TO_POLE_BLOCKS),
                         -0.5f * PI, 0.5f * PI);
}

void PlanetSurfaceLatLonAt(int x, int z, float *outLongitude, float *outLatitude)
{
    float mapX = 0.0f;
    float mapZ = 0.0f;
    PlanetSurfaceCoordinates(x, z, &mapX, &mapZ);
    PlanetMapCoordinatesToLatLon(mapX, mapZ, outLongitude, outLatitude);
}

PlanetSurfaceSample PlanetSurfaceBaselineAt(int x, int z)
{
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceBaseline(PlanetWorldSeed(), PlanetWorldProfile(),
                                             longitude, latitude);
}

PlanetSurfaceSample PlanetSurfaceAtTime(int x, int z, double simulationTime)
{
    float longitude = 0.0f;
    float latitude = 0.0f;
    PlanetSurfaceLatLonAt(x, z, &longitude, &latitude);
    return PlanetSampleGlobalSurfaceAtTime(PlanetWorldSeed(), PlanetWorldProfile(),
                                           longitude, latitude, simulationTime);
}

static PlanetSurfaceSample PlanetSampleLocalSurface(int x, int z, float *outX, float *outZ)
{
    float mapX = 0.0f;
    float mapZ = 0.0f;
    PlanetSurfaceCoordinates(x, z, &mapX, &mapZ);
    if (outX) *outX = mapX;
    if (outZ) *outZ = mapZ;
    return PlanetSurfaceBaselineAt(x, z);
}

static int PlanetSeaLevelForProfile(SolarBodyStyle style,
                                    const PlanetProfile *profile)
{
    if (profile->oceanCoverage <= 0.05f) return -1;
    if (style == SOLAR_STYLE_LAVA || style == SOLAR_STYLE_ICE ||
        style == SOLAR_STYLE_TEMPERATE) return 80;
    return -1;
}

int PlanetTerrainSeaLevel(void)
{
    if (!PlanetWorldIsActive()) return TerrainSeaLevel(terrainMode);
    return PlanetSeaLevelForProfile(PlanetWorldStyle(), PlanetWorldProfile());
}

PlanetBiome PlanetBiomeAt(int x, int z)
{
    if (!PlanetWorldIsActive()) return PLANET_BIOME_PLAINS;
    return PlanetSampleLocalSurface(x, z, NULL, NULL).biome;
}

int PlanetTerrainHeight(int x, int z)
{
    if (!PlanetWorldIsActive()) return TerrainHeight(x, z, terrainMode);

    const PlanetProfile *profile = PlanetWorldProfile();
    float fx = 0.0f;
    float fz = 0.0f;
    PlanetSurfaceSample surface = PlanetSampleLocalSurface(x, z, &fx, &fz);
    float roughness = Clamp(profile->terrainRoughness, 0.35f, 1.55f);
    float continents = surface.continentalness;
    float localDetail = PlanetFractalNoise2D(fx * 0.024f, fz * 0.024f, 47u);
    float hills = surface.regionalness * 0.62f + surface.detail * 0.23f +
                  localDetail * 0.15f;
    float coast = 0.27f + profile->oceanCoverage * 0.36f;
    float height;
    if (continents < coast && PlanetSeaLevelForProfile(
            profile->style, profile) >= 0) {
        float ocean = Clamp((coast - continents) / fmaxf(coast, 0.08f),
                            0.0f, 1.0f);
        height = 76.0f - SmoothRange(0.0f, 0.20f, ocean) * 14.0f -
                 powf(ocean, 1.32f) * 40.0f;
        height -= surface.trench * SmoothRange(0.20f, 0.72f, ocean) * 32.0f;
        height += (hills - 0.5f) * 6.0f;
    } else {
        float land = Clamp((continents - coast) / fmaxf(1.0f - coast, 0.08f),
                           0.0f, 1.0f);
        float mountainMask = SmoothRange(0.08f, 0.62f, land) *
                             (1.0f - surface.erosion * 0.48f);
        height = 84.0f + land * 30.0f + (hills - 0.5f) * 13.0f;
        height += surface.ridge * mountainMask * 78.0f * roughness;
        height += surface.peak * mountainMask * 44.0f * roughness;
    }

    switch (surface.biome) {
    case PLANET_BIOME_LAVA_SEA:
        height = fminf(height, 74.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_VOLCANIC_RIDGE:
        height += 14.0f + surface.volcanicCone * 32.0f;
        break;
    case PLANET_BIOME_BASALT_PLAINS:
        height += sinf(fx * 0.15f) * cosf(fz * 0.13f) * 2.5f;
        break;
    case PLANET_BIOME_GLACIER:
        height = fminf(height, 76.0f + (hills - 0.5f) * 7.0f);
        {
            float latitude = Clamp(fz * (PI / PLANET_GLOBAL_POLE_TO_POLE_BLOCKS),
                                   -0.5f * PI, 0.5f * PI);
            // Ice flows downhill from each pole toward the equator. The
            // latitude gradient gives the cut plane a consistent flow axis.
            height += surface.glacierFlow * (fabsf(latitude) - 0.70f) * 18.0f;
        }
        height -= surface.glacierCracks * 1.4f;
        break;
    case PLANET_BIOME_ICE_SHEET:
        height += 8.0f + sinf((fx + fz) * 0.008f) * 6.0f;
        break;
    case PLANET_BIOME_DUNES:
        {
            float wind = profile->prevailingWindAngle;
            float longitude = fx * (2.0f * PI / PLANET_GLOBAL_CIRCUMFERENCE_BLOCKS);
            float latitude = Clamp(fz * (PI / PLANET_GLOBAL_POLE_TO_POLE_BLOCKS),
                                   -0.5f * PI, 0.5f * PI);
            float latitudeCos = cosf(latitude);
            Vector3 point = { latitudeCos * cosf(longitude), sinf(latitude),
                              latitudeCos * sinf(longitude) };
            Vector3 windAxis = { cosf(wind), 0.0f, sinf(wind) };
            Vector3 crossAxis = { -sinf(wind), 0.0f, cosf(wind) };
            float windCoord = Vector3DotProduct(point, windAxis);
            float crossWind = Vector3DotProduct(point, crossAxis);
            float duneShape = 0.5f + 0.5f * sinf(windCoord * 24.0f +
                                                    crossWind * 5.0f);
            height += duneShape * surface.duneBand * 12.0f;
        }
        break;
    case PLANET_BIOME_BADLANDS:
        height += 8.0f + fabsf(sinf(fx * 0.026f) * cosf(fz * 0.022f)) * 16.0f;
        break;
    case PLANET_BIOME_OASIS:
        height = fminf(height, 84.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_IMPACT_BASIN:
        height -= 14.0f + (1.0f - hills) * 10.0f;
        break;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        height += fabsf(sinf(fx * 0.021f) * cosf(fz * 0.019f)) * 10.0f;
        break;
    case PLANET_BIOME_OCEAN:
        height = fminf(height, 76.0f + (hills - 0.5f) * 5.0f);
        break;
    case PLANET_BIOME_COAST:
        height = fminf(height, 87.0f + (hills - 0.5f) * 6.0f);
        break;
    case PLANET_BIOME_ALPINE:
        height += 18.0f + (hills - 0.35f) * 20.0f * roughness;
        break;
    case PLANET_BIOME_STORM_BANDS:
        height = 112.0f + sinf(fz * 0.025f) * 14.0f +
                 sinf(fx * 0.009f) * 10.0f;
        break;
    case PLANET_BIOME_PLAINS:
    case PLANET_BIOME_FOREST:
    default:
        height += sinf(fx * 0.024f) * cosf(fz * 0.021f) * 5.0f;
        break;
    }

    // These fields are shared with the orbital map: the same crater walls,
    // ejecta blankets, volcanic cones and lava channels continue at landing.
    height -= surface.impactDepth * 26.0f;
    height += surface.impactRim * 18.0f + surface.ejecta * 7.0f;
    height += surface.volcanicCone * 30.0f;
    height -= surface.caldera * 18.0f;
    height += surface.lavaFlow * (profile->style == SOLAR_STYLE_LAVA ? 7.0f : 2.5f);

    height = roundf(height);
    if (height < 5.0f) height = 5.0f;
    if (height > 240.0f) height = 240.0f;
    return (int)height;
}

static bool SurfaceLandingCandidate(int x, int z, int footprintRadius,
                                    int *outGroundY)
{
    int seaLevel = PlanetWorldIsActive() ? PlanetTerrainSeaLevel()
                                         : TerrainSeaLevel(terrainMode);
    int minHeight = WORLD_HEIGHT;
    int maxHeight = 0;
    for (int dz = -footprintRadius; dz <= footprintRadius; dz++) {
        for (int dx = -footprintRadius; dx <= footprintRadius; dx++) {
            int height = PlanetWorldIsActive()
                             ? PlanetTerrainHeight(x + dx, z + dz)
                             : TerrainHeight(x + dx, z + dz, terrainMode);
            if (seaLevel >= 0 && height <= seaLevel + 1) return false;
            if (height < minHeight) minHeight = height;
            if (height > maxHeight) maxHeight = height;
        }
    }
    if (maxHeight - minHeight > 1 || maxHeight > WORLD_HEIGHT - 6) return false;
    if (outGroundY) *outGroundY = maxHeight;
    return true;
}

bool FindSafeSurfaceLanding(int preferredX, int preferredZ, int maxRadius,
                            int footprintRadius, int *outX, int *outZ,
                            int *outGroundY)
{
    if (maxRadius < 0 || footprintRadius < 0) return false;
    for (int radius = 0; radius <= maxRadius; radius++) {
        int step = radius > 24 ? 2 : 1;
        if (radius == 0) {
            int ground = 0;
            if (SurfaceLandingCandidate(preferredX, preferredZ,
                                        footprintRadius, &ground)) {
                if (outX) *outX = preferredX;
                if (outZ) *outZ = preferredZ;
                if (outGroundY) *outGroundY = ground;
                return true;
            }
            continue;
        }
        for (int offset = -radius; offset <= radius; offset += step) {
            const int candidates[4][2] = {
                { offset, -radius }, { radius, offset },
                { -offset, radius }, { -radius, -offset }
            };
            for (int i = 0; i < 4; i++) {
                int x = preferredX + candidates[i][0];
                int z = preferredZ + candidates[i][1];
                int ground = 0;
                if (!SurfaceLandingCandidate(x, z, footprintRadius,
                                             &ground)) continue;
                if (outX) *outX = x;
                if (outZ) *outZ = z;
                if (outGroundY) *outGroundY = ground;
                return true;
            }
        }
    }
    return false;
}

static BlockType PlanetSubsurfaceBlock(SolarBodyStyle style, PlanetBiome biome,
                                       int depth, unsigned int hash)
{
    if (depth == 0) {
        switch (biome) {
        case PLANET_BIOME_LAVA_SEA:
        case PLANET_BIOME_BASALT_PLAINS:
        case PLANET_BIOME_VOLCANIC_RIDGE:
            return hash % 19u == 0u ? BLOCK_GLOWSTONE : BLOCK_NETHERRACK;
        case PLANET_BIOME_GLACIER:
            return BLOCK_ICE;
        case PLANET_BIOME_ICE_SHEET:
        case PLANET_BIOME_ALPINE:
            return BLOCK_SNOW;
        case PLANET_BIOME_BADLANDS:
            return BLOCK_SANDSTONE;
        case PLANET_BIOME_DUNES:
        case PLANET_BIOME_COAST:
        case PLANET_BIOME_OCEAN:
            return BLOCK_SAND;
        case PLANET_BIOME_OASIS:
        case PLANET_BIOME_FOREST:
        case PLANET_BIOME_PLAINS:
            return BLOCK_GRASS;
        case PLANET_BIOME_IMPACT_BASIN:
            return hash % 9u == 0u ? BLOCK_METEORITE : BLOCK_MOON_SAND;
        case PLANET_BIOME_CRATER_HIGHLANDS:
            return BLOCK_MOON_ROCK;
        case PLANET_BIOME_STORM_BANDS:
        default:
            break;
        }
    }

    switch (style) {
    case SOLAR_STYLE_LAVA:
        if (depth <= 2) return hash % 11u == 0u ? BLOCK_GLOWSTONE : BLOCK_NETHERRACK;
        return hash % 17u == 0u ? BLOCK_METEORITE : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_ICE:
        if (depth == 0) return BLOCK_SNOW;
        if (depth <= 3) return BLOCK_ICE;
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_DESERT:
        return depth <= 3 ? BLOCK_SAND : BLOCK_SANDSTONE;
    case SOLAR_STYLE_GAS:
        if (depth <= 2) return hash % 7u == 0u ? BLOCK_GLOWSTONE : BLOCK_SOUL_SAND;
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_CRATER:
        if (depth == 0) return hash % 13u == 0u ? BLOCK_METEORITE : BLOCK_MOON_SAND;
        return BLOCK_MOON_ROCK;
    case SOLAR_STYLE_TEMPERATE:
        if (depth == 0) return BLOCK_GRASS;
        if (depth <= 3) return BLOCK_DIRT;
        return BLOCK_STONE;
    default:
        return depth == 0 ? BLOCK_GRASS : BLOCK_STONE;
    }
}

static SubsurfaceParams PlanetSubsurfaceParamsFor(
    SolarBodyStyle style, const PlanetProfile *profile)
{
    float roughness = profile ? profile->terrainRoughness : 1.0f;
    float oceanCoverage = profile ? profile->oceanCoverage : 0.0f;
    SubsurfaceParams params = {
        .seed = PlanetWorldSeed(),
        .activity = Clamp(0.72f + roughness * 0.38f, 0.55f, 1.35f),
        .minY = 2,
        .surfaceClearance = 4,
        .aquiferLevel = 36,
        .aquiferChance = Clamp(oceanCoverage * 0.95f, 0.0f, 0.88f)
    };
    switch (style) {
    case SOLAR_STYLE_LAVA:
        params.activity = fmaxf(params.activity, 1.18f);
        params.aquiferLevel = 42;
        params.aquiferChance = 0.72f;
        break;
    case SOLAR_STYLE_ICE:
        params.activity *= 0.88f;
        params.aquiferLevel = 32;
        params.aquiferChance = 0.54f;
        break;
    case SOLAR_STYLE_DESERT:
        params.activity *= 0.94f;
        params.aquiferChance *= 0.22f;
        break;
    case SOLAR_STYLE_GAS:
        params.activity = 0.78f;
        params.aquiferChance = 0.08f;
        break;
    case SOLAR_STYLE_CRATER:
        params.activity = fmaxf(params.activity, 1.05f);
        params.aquiferChance *= 0.12f;
        break;
    case SOLAR_STYLE_TEMPERATE:
    default:
        params.aquiferChance = Clamp(0.22f + oceanCoverage * 0.78f,
                                     0.0f, 0.90f);
        break;
    }
    return params;
}

static void PlacePlanetForest(Chunk *chunk, int treeX, int treeZ)
{
    if (PlanetBiomeAt(treeX, treeZ) != PLANET_BIOME_FOREST) return;
    if (PlanetHash2D(treeX, treeZ, 151u) % 59u != 0u) return;
    PlanetEcologySuitability suitability = PlanetEcologyStaticSuitabilityAt(
        treeX, treeZ);
    float ecologyRoll = (float)(PlanetHash2D(treeX, treeZ, 173u) & 0x00ffffffu) /
                        16777215.0f;
    if (ecologyRoll > suitability.floraCapacity) return;

    int base = PlanetTerrainHeight(treeX, treeZ) + 1;
    if (base <= PlanetSeaLevelForProfile(PlanetWorldStyle(), PlanetWorldProfile())) return;
    int trunkHeight = 4 + (int)(PlanetHash2D(treeX, treeZ, 163u) % 3u);
    for (int y = base; y < base + trunkHeight && InHeight(y); y++) {
        SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
    }
    for (int ox = -2; ox <= 2; ox++) {
        for (int oz = -2; oz <= 2; oz++) {
            for (int oy = trunkHeight - 2; oy <= trunkHeight; oy++) {
                if (abs(ox) + abs(oz) + (oy == trunkHeight ? 1 : 0) > 4) continue;
                SetChunkLocalBlock(chunk, treeX + ox, base + oy, treeZ + oz, BLOCK_LEAVES);
            }
        }
    }
}

static void GeneratePlanetChunkTerrain(Chunk *chunk, int cx, int cz)
{
    ChunkClearBlockStorage(chunk);
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    SolarBodyStyle style = PlanetWorldStyle();
    const PlanetProfile *profile = PlanetWorldProfile();
    SubsurfaceParams subsurface = PlanetSubsurfaceParamsFor(style, profile);

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            int localX = worldX;
            int localZ = worldZ;
            int height = PlanetTerrainHeight(worldX, worldZ);
            PlanetSurfaceSample surface = PlanetSampleLocalSurface(worldX, worldZ, NULL, NULL);
            PlanetBiome biome = surface.biome;

            for (int y = 0; y <= height; y++) {
                unsigned int h = PlanetHash2D(localX + y * 19, localZ - y * 23, 1u);
                int depth = height - y;
                BlockType type = y == 0 ? BLOCK_BEDROCK :
                                 PlanetSubsurfaceBlock(style, biome, depth, h);
                SubsurfaceSample cave = SubsurfaceSampleAt(
                    &subsurface, worldX, y, worldZ, height);
                if (cave.cave) {
                    type = cave.flooded && style == SOLAR_STYLE_LAVA
                               ? BLOCK_LAVA
                               : (cave.flooded ? BLOCK_WATER : BLOCK_AIR);
                }
                ChunkSetLocalBlock(chunk, lx, y, lz, type);
            }

            int seaLevel = PlanetSeaLevelForProfile(style, profile);
            if (seaLevel >= 0 && height < seaLevel) {
                BlockType liquid = style == SOLAR_STYLE_LAVA ? BLOCK_LAVA : BLOCK_WATER;
                if (surface.iceCoverage > 0.64f) liquid = BLOCK_ICE;
                for (int y = height + 1; y <= seaLevel && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, liquid);
                }
                if (style == SOLAR_STYLE_ICE && InHeight(seaLevel)) {
                    ChunkSetLocalBlock(chunk, lx, seaLevel, lz, BLOCK_ICE);
                }
            }

            unsigned int decor = PlanetHash2D(localX, localZ, 7u);
            if (!InHeight(height + 1)) continue;
            if (surface.glacierCracks > 0.84f && decor % 67u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_MOON_ROCK);
            } else if (surface.lavaFlow > 0.82f && decor % 53u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_GLOWSTONE);
            } else if (surface.ejecta > 0.76f && decor % 83u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_METEORITE);
            } else if ((biome == PLANET_BIOME_DUNES || biome == PLANET_BIOME_BADLANDS) &&
                decor % (biome == PLANET_BIOME_DUNES ? 181u : 293u) == 0u &&
                ((float)(PlanetHash2D(worldX, worldZ, 401u) & 0x00ffffffu) /
                 16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                     worldX, worldZ).floraCapacity) {
                for (int y = height + 1; y <= height + 3 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_CACTUS);
                }
            } else if (biome == PLANET_BIOME_GLACIER && decor % 137u == 0u) {
                for (int y = height + 1; y <= height + 4 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_ICE);
                }
            } else if (biome == PLANET_BIOME_ICE_SHEET && decor % 211u == 0u) {
                for (int y = height + 1; y <= height + 2 && InHeight(y); y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz, BLOCK_ICE);
                }
            } else if ((biome == PLANET_BIOME_BASALT_PLAINS ||
                        biome == PLANET_BIOME_VOLCANIC_RIDGE) && decor % 193u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_GLOWSTONE);
            } else if (biome == PLANET_BIOME_STORM_BANDS && decor % 157u == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 409u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_MUSHROOM);
            } else if ((biome == PLANET_BIOME_IMPACT_BASIN ||
                        biome == PLANET_BIOME_CRATER_HIGHLANDS) && decor % 149u == 0u) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_METEORITE);
            } else if ((biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_PLAINS ||
                        biome == PLANET_BIOME_OASIS) && height > seaLevel + 1 &&
                       decor % (biome == PLANET_BIOME_FOREST ? 61u : 97u) == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 419u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                ChunkSetLocalBlock(chunk, lx, height + 1, lz, BLOCK_FLOWER);
            }
        }
    }

    for (int treeX = startX - 2; treeX < startX + CHUNK_SIZE + 2; treeX++) {
        for (int treeZ = startZ - 2; treeZ < startZ + CHUNK_SIZE + 2; treeZ++) {
            PlacePlanetForest(chunk, treeX, treeZ);
        }
    }
    PlanetEcologyApplyToChunk(chunk, cx, cz);
    PlanetPoiApplyToChunk(chunk, cx, cz);
}

void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->floraStructureCount = 0;
    if (PlanetWorldIsActive()) {
        GeneratePlanetChunkTerrain(chunk, cx, cz);
        return;
    }

    ChunkClearBlockStorage(chunk);

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            int height = TerrainHeight(worldX, worldZ, mode);
            Biome biome = BiomeAt(worldX, worldZ);
            int seaLevel = TerrainSeaLevel(mode);
            bool submerged = seaLevel >= 0 && height < seaLevel;

            for (int y = 0; y <= height; y++) {
                BlockType type = BLOCK_STONE;
                if (mode == TERRAIN_FLAT) {
                    type = (y == height) ? BLOCK_GRASS : BLOCK_DIRT;
                } else if (y == 0) {
                    type = BLOCK_BEDROCK;
                } else if (y < height) {
                    if (biome == BIOME_DESERT) {
                        type = (y > height - 3) ? BLOCK_SAND : StoneOrCaveBlock(worldX, y, worldZ, height);
                    } else if (biome == BIOME_SNOW) {
                        type = (y > height - 3) ? BLOCK_DIRT : StoneOrCaveBlock(worldX, y, worldZ, height);
                    } else if (biome == BIOME_MOUNTAIN) {
                        type = (y > height - 4 && height < 24) ? BLOCK_DIRT : StoneOrCaveBlock(worldX, y, worldZ, height);
                    } else {
                        type = (y > height - 4) ? BLOCK_DIRT : StoneOrCaveBlock(worldX, y, worldZ, height);
                    }
                } else if (submerged && y == height) {
                    type = height > seaLevel - 8 ? BLOCK_SAND : BLOCK_STONE;
                } else if (biome == BIOME_DESERT) {
                    type = BLOCK_SAND;
                } else if (biome == BIOME_SNOW) {
                    type = BLOCK_SNOW;
                } else if (biome == BIOME_MOUNTAIN) {
                    type = (height >= 165) ? BLOCK_SNOW :
                           ((height >= 125) ? BLOCK_STONE : BLOCK_GRASS);
                } else {
                    type = BLOCK_GRASS;
                }
                ChunkSetLocalBlock(chunk, lx, y, lz, type);
            }

            if (submerged) {
                for (int y = height + 1; y <= seaLevel; y++) {
                    ChunkSetLocalBlock(chunk, lx, y, lz,
                                       biome == BIOME_SNOW && y == seaLevel
                                           ? BLOCK_ICE
                                           : BLOCK_WATER);
                }
            }

            if (mode != TERRAIN_FLAT && CaveWaterAt(worldX, height - 2, worldZ, height) &&
                CaveAt(worldX, height - 2, worldZ, height) &&
                !CaveAt(worldX, height - 3, worldZ, height)) {
                ChunkSetLocalBlock(chunk, lx, height - 2, lz, BLOCK_WATER);
            }

            if (mode != TERRAIN_FLAT && !submerged &&
                biome == BIOME_DESERT && height > 6 &&
                (WorldHash2D(worldX, worldZ) % 23u) == 0u) {
                int cactusHeight = 2 + (int)(WorldHash2D(worldX, worldZ + 13) % 2u);
                for (int y = height + 1; y < height + 1 + cactusHeight && InHeight(y); y++) {
                    SetChunkLocalBlock(chunk, worldX, y, worldZ, BLOCK_CACTUS);
                }
            }
        }
    }

    if (mode != TERRAIN_FLAT) {
        for (int lx = 0; lx < CHUNK_SIZE; lx++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int worldX = startX + lx;
                int worldZ = startZ + lz;
                int height = TerrainHeight(worldX, worldZ, mode);
                if (height < 4) continue;
                BlockType surface = ChunkGetLocalBlock(chunk, lx, height, lz);
                if (surface != BLOCK_GRASS) continue;
                unsigned int h = WorldHash2D(worldX, worldZ);
                if (h % 173u == 0u) {
                    SetChunkLocalBlock(chunk, worldX, height + 1, worldZ, BLOCK_FLOWER);
                } else if (h % 397u == 0u) {
                    SetChunkLocalBlock(chunk, worldX, height + 1, worldZ, BLOCK_MUSHROOM);
                }
            }
        }
    }

    for (int treeX = startX - 2; treeX < startX + CHUNK_SIZE + 2; treeX++) {
        for (int treeZ = startZ - 2; treeZ < startZ + CHUNK_SIZE + 2; treeZ++) {
            if (!ShouldPlaceTree(treeX, treeZ, mode)) continue;

            int base = TerrainHeight(treeX, treeZ, mode) + 1;
            Biome treeBiome = BiomeAt(treeX, treeZ);
            bool pine = treeBiome == BIOME_SNOW || treeBiome == BIOME_MOUNTAIN;

            if (pine) {
                int trunkHeight = 5 + (int)(WorldHash2D(treeX, treeZ + 31) % 3u);
                for (int y = base; y < base + trunkHeight; y++) {
                    SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
                }
                for (int layer = 0; layer < 4; layer++) {
                    int radius = (layer < 2) ? 1 : 2;
                    int ly = base + trunkHeight - 2 - layer;
                    if (ly < base || !InHeight(ly)) continue;
                    for (int dx = -radius; dx <= radius; dx++) {
                        for (int dz = -radius; dz <= radius; dz++) {
                            if (dx == 0 && dz == 0) continue;
                            SetChunkLocalBlock(chunk, treeX + dx, ly, treeZ + dz, BLOCK_LEAVES);
                        }
                    }
                }
            } else {
                for (int y = base; y < base + 4; y++) {
                    SetChunkLocalBlock(chunk, treeX, y, treeZ, BLOCK_WOOD);
                }

                for (int ox = -2; ox <= 2; ox++) {
                    for (int oz = -2; oz <= 2; oz++) {
                        for (int oy = 3; oy <= 4; oy++) {
                            if (abs(ox) + abs(oz) + (oy == 4 ? 1 : 0) <= 4) {
                                SetChunkLocalBlock(chunk, treeX + ox, base + oy, treeZ + oz, BLOCK_LEAVES);
                            }
                        }
                    }
                }
            }
        }
    }

    if (mode != TERRAIN_FLAT) {
        GenerateVillage(chunk, cx, cz, mode);
        GenerateMineshaft(chunk, cx, cz, mode);
        GenerateDungeon(chunk, cx, cz, mode);
        GenerateDesertTemple(chunk, cx, cz, mode);
    }
}

void ApplyEditsToChunk(Chunk *chunk)
{
    int editCount = WorldGetEditCount();
    for (int i = 0; i < editCount; i++) {
        BlockEdit edit;
        if (!WorldGetEditForCurrentDimension(i, &edit)) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit.x, edit.z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz && InHeight(edit.y)) {
            ChunkSetLocalBlock(chunk, editLx, edit.y, editLz, edit.type);
        }
    }
}
