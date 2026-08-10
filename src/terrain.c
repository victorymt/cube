#include "terrain.h"

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
unsigned int Hash2D(int x, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ ((unsigned int)z * 374761393u)) * 16777619u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
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
    return MixWorldSeed(Hash2D(x, z));
}

unsigned int WorldHash3D(int x, int y, int z)
{
    return MixWorldSeed(Hash3D(x, y, z));
}

float TerrainNoise(float x, float z)
{
    x += WorldSeedCoordinateOffset(1u);
    z += WorldSeedCoordinateOffset(2u);
    float h = 0.0f;
    h += sinf(x * 0.085f) * 4.0f;
    h += cosf(z * 0.073f) * 3.5f;
    h += sinf((x + z) * 0.041f) * 5.0f;
    h += cosf((x - z) * 0.113f) * 1.8f;
    return h;
}

float BiomeNoise(int x, int z)
{
    float fx = (float)x + WorldSeedCoordinateOffset(3u);
    float fz = (float)z + WorldSeedCoordinateOffset(4u);
    return sinf(fx * 0.017f) * 0.45f +
           cosf(fz * 0.021f) * 0.40f +
           sinf((fx + fz) * 0.009f) * 0.35f;
}

Biome BiomeAt(int x, int z)
{
    float n = BiomeNoise(x, z);
    if (n > 0.72f) return BIOME_MOUNTAIN;
    if (n > 0.28f) return BIOME_FOREST;
    if (n < -0.55f) return BIOME_SNOW;
    if (n < -0.12f) return BIOME_DESERT;
    return BIOME_PLAINS;
}

float CanyonNoise(int x, int z)
{
    float fx = (float)x + WorldSeedCoordinateOffset(5u);
    float fz = (float)z + WorldSeedCoordinateOffset(6u);
    float dx = sinf(fx * 0.021f) * 1.4f + cosf((fx + fz) * 0.013f) * 1.2f;
    float dz = sinf(fz * 0.019f) * 1.4f + cosf((fx - fz) * 0.011f) * 1.1f;
    return dx + dz;
}

int TerrainHeight(int x, int z, TerrainMode mode)
{
    if (mode == TERRAIN_FLAT) {
        (void)x;
        (void)z;
        return 8;
    }

    float noise = TerrainNoise((float)x, (float)z);
    int height;
    switch (BiomeAt(x, z)) {
    case BIOME_MOUNTAIN: height = 15 + (int)roundf(noise * 1.7f); break;
    case BIOME_SNOW:     height = 9 + (int)roundf(noise * 0.9f);  break;
    case BIOME_DESERT:   height = 9 + (int)roundf(noise * 0.7f);  break;
    default:             height = 10 + (int)roundf(noise);        break;
    }

    float canyon = CanyonNoise(x, z);
    if (canyon > 2.05f) {
        int depth = (int)((canyon - 2.05f) * 9.0f);
        if (depth > 9) depth = 9;
        height -= depth;
    }

    if (height < 3) height = 3;
    if (height > WORLD_HEIGHT - 4) height = WORLD_HEIGHT - 4;
    return height;
}

bool ShouldPlaceTree(int x, int z, TerrainMode mode)
{
    if (mode == TERRAIN_FLAT) return false;

    int height = TerrainHeight(x, z, mode);
    if (height <= 6) return false;
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
    if (y < 2 || y >= height - 3) return false;

    float fx = (float)x + WorldSeedCoordinateOffset(7u);
    float fy = (float)y + WorldSeedCoordinateOffset(8u);
    float fz = (float)z + WorldSeedCoordinateOffset(9u);
    float n = sinf(fx * 0.17f) * sinf(fz * 0.13f) * sinf(fy * 0.31f);
    n += sinf((fx + fz) * 0.09f + fy * 0.23f) * 0.7f;

    float tunnel = sinf(fx * 0.045f + fy * 0.09f) * sinf(fz * 0.045f - fy * 0.07f);
    float branch = sinf((fx + fy) * 0.06f) * sinf((fz - fy) * 0.055f);
    n += (tunnel + branch) * 0.55f;

    if (n > 0.95f) return true;

    float chamber = sinf(fx * 0.028f) * sinf(fz * 0.026f) * sinf(fy * 0.035f);
    if (chamber > 0.90f && y < height - 6) return true;

    return n > 0.60f;
}

bool CaveWaterAt(int x, int y, int z, int height)
{
    if (y < 4 || y >= height - 4) return false;
    float fx = (float)x + WorldSeedCoordinateOffset(10u);
    float fy = (float)y + WorldSeedCoordinateOffset(11u);
    float fz = (float)z + WorldSeedCoordinateOffset(12u);
    float n = sinf(fx * 0.11f + 3.7f) * sinf(fz * 0.13f + 1.3f) * sinf(fy * 0.19f);
    return n > 0.90f;
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
    if (CaveAt(x, y, z, height)) return BLOCK_AIR;
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
        chunk->blocks[lx][y][lz] = (unsigned short)type;
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

static int PlanetSeaLevel(SolarBodyStyle style, const PlanetProfile *profile)
{
    if (profile->oceanCoverage <= 0.05f) return -1;
    if (style == SOLAR_STYLE_LAVA) return 11;
    if (style == SOLAR_STYLE_ICE || style == SOLAR_STYLE_TEMPERATE) return 12;
    return -1;
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
    float coast = 0.34f + profile->oceanCoverage * 0.46f;
    float height = 12.0f + (continents - coast) * 16.0f;
    height += (hills - 0.5f) * 9.0f * roughness;
    height += sinf((fx + fz) * 0.010f) * 1.8f * roughness;

    switch (surface.biome) {
    case PLANET_BIOME_LAVA_SEA:
        height = fminf(height, 9.5f + (hills - 0.5f) * 2.0f);
        break;
    case PLANET_BIOME_VOLCANIC_RIDGE:
        height += 5.0f + fabsf(sinf(fx * 0.070f) * cosf(fz * 0.060f)) * 4.0f;
        break;
    case PLANET_BIOME_BASALT_PLAINS:
        height += sinf(fx * 0.15f) * cosf(fz * 0.13f) * 2.5f;
        break;
    case PLANET_BIOME_GLACIER:
        height = fminf(height, 10.5f + (hills - 0.5f) * 3.0f);
        {
            float latitude = Clamp(fz * (PI / PLANET_GLOBAL_POLE_TO_POLE_BLOCKS),
                                   -0.5f * PI, 0.5f * PI);
            // Ice flows downhill from each pole toward the equator. The
            // latitude gradient gives the cut plane a consistent flow axis.
            height += surface.glacierFlow * (fabsf(latitude) - 0.70f) * 7.0f;
        }
        height -= surface.glacierCracks * 1.4f;
        break;
    case PLANET_BIOME_ICE_SHEET:
        height += 2.0f + sinf((fx + fz) * 0.008f) * 3.0f;
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
            height += duneShape * surface.duneBand * 6.0f;
        }
        break;
    case PLANET_BIOME_BADLANDS:
        height += 3.0f + fabsf(sinf(fx * 0.026f) * cosf(fz * 0.022f)) * 6.0f;
        break;
    case PLANET_BIOME_OASIS:
        height = fminf(height, 11.0f + (hills - 0.5f) * 2.0f);
        break;
    case PLANET_BIOME_IMPACT_BASIN:
        height -= 5.0f + (1.0f - hills) * 3.0f;
        break;
    case PLANET_BIOME_CRATER_HIGHLANDS:
        height += fabsf(sinf(fx * 0.021f) * cosf(fz * 0.019f)) * 3.0f;
        break;
    case PLANET_BIOME_OCEAN:
        height = fminf(height, 10.0f + (hills - 0.5f) * 3.0f);
        break;
    case PLANET_BIOME_COAST:
        height = fminf(height, 13.0f + (hills - 0.5f) * 4.0f);
        break;
    case PLANET_BIOME_ALPINE:
        height += 5.0f + (hills - 0.35f) * 8.0f * roughness;
        break;
    case PLANET_BIOME_STORM_BANDS:
        height = 14.0f + sinf(fz * 0.025f) * 4.0f + sinf(fx * 0.009f) * 3.0f;
        break;
    case PLANET_BIOME_PLAINS:
    case PLANET_BIOME_FOREST:
    default:
        height += sinf(fx * 0.024f) * cosf(fz * 0.021f) * 2.2f;
        break;
    }

    // These fields are shared with the orbital map: the same crater walls,
    // ejecta blankets, volcanic cones and lava channels continue at landing.
    height -= surface.impactDepth * 9.0f;
    height += surface.impactRim * 5.5f + surface.ejecta * 2.0f;
    height += surface.volcanicCone * 8.0f;
    height -= surface.caldera * 5.0f;
    height += surface.lavaFlow * (profile->style == SOLAR_STYLE_LAVA ? 2.2f : 0.8f);

    height = roundf(height);
    if (height < 5.0f) height = 5.0f;
    if (height > 30.0f) height = 30.0f;
    return (int)height;
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
    if (base <= PlanetSeaLevel(PlanetWorldStyle(), PlanetWorldProfile())) return;
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
    memset(chunk->blocks, 0, sizeof(chunk->blocks));
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    SolarBodyStyle style = PlanetWorldStyle();
    const PlanetProfile *profile = PlanetWorldProfile();

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
                bool cave = y > 2 && depth > 3 && h % 101u < 3u;
                chunk->blocks[lx][y][lz] = (unsigned short)(cave ? BLOCK_AIR : type);
            }

            int seaLevel = PlanetSeaLevel(style, profile);
            if (seaLevel >= 0 && height < seaLevel) {
                BlockType liquid = style == SOLAR_STYLE_LAVA ? BLOCK_LAVA : BLOCK_WATER;
                if (surface.iceCoverage > 0.64f) liquid = BLOCK_ICE;
                for (int y = height + 1; y <= seaLevel && InHeight(y); y++) {
                    chunk->blocks[lx][y][lz] = (unsigned short)liquid;
                }
                if (style == SOLAR_STYLE_ICE && InHeight(seaLevel)) {
                    chunk->blocks[lx][seaLevel][lz] = (unsigned short)BLOCK_ICE;
                }
            }

            unsigned int decor = PlanetHash2D(localX, localZ, 7u);
            if (!InHeight(height + 1)) continue;
            if (surface.glacierCracks > 0.84f && decor % 67u == 0u) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_MOON_ROCK;
            } else if (surface.lavaFlow > 0.82f && decor % 53u == 0u) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_GLOWSTONE;
            } else if (surface.ejecta > 0.76f && decor % 83u == 0u) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_METEORITE;
            } else if ((biome == PLANET_BIOME_DUNES || biome == PLANET_BIOME_BADLANDS) &&
                decor % (biome == PLANET_BIOME_DUNES ? 181u : 293u) == 0u &&
                ((float)(PlanetHash2D(worldX, worldZ, 401u) & 0x00ffffffu) /
                 16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                     worldX, worldZ).floraCapacity) {
                for (int y = height + 1; y <= height + 3 && InHeight(y); y++) {
                    chunk->blocks[lx][y][lz] = (unsigned short)BLOCK_CACTUS;
                }
            } else if (biome == PLANET_BIOME_GLACIER && decor % 137u == 0u) {
                for (int y = height + 1; y <= height + 4 && InHeight(y); y++) {
                    chunk->blocks[lx][y][lz] = (unsigned short)BLOCK_ICE;
                }
            } else if (biome == PLANET_BIOME_ICE_SHEET && decor % 211u == 0u) {
                for (int y = height + 1; y <= height + 2 && InHeight(y); y++) {
                    chunk->blocks[lx][y][lz] = (unsigned short)BLOCK_ICE;
                }
            } else if ((biome == PLANET_BIOME_BASALT_PLAINS ||
                        biome == PLANET_BIOME_VOLCANIC_RIDGE) && decor % 193u == 0u) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_GLOWSTONE;
            } else if (biome == PLANET_BIOME_STORM_BANDS && decor % 157u == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 409u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_MUSHROOM;
            } else if ((biome == PLANET_BIOME_IMPACT_BASIN ||
                        biome == PLANET_BIOME_CRATER_HIGHLANDS) && decor % 149u == 0u) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_METEORITE;
            } else if ((biome == PLANET_BIOME_FOREST || biome == PLANET_BIOME_PLAINS ||
                        biome == PLANET_BIOME_OASIS) && height > seaLevel + 1 &&
                       decor % (biome == PLANET_BIOME_FOREST ? 61u : 97u) == 0u &&
                       ((float)(PlanetHash2D(worldX, worldZ, 419u) & 0x00ffffffu) /
                        16777215.0f) <= PlanetEcologyStaticSuitabilityAt(
                            worldX, worldZ).floraCapacity) {
                chunk->blocks[lx][height + 1][lz] = (unsigned short)BLOCK_FLOWER;
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

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < WORLD_HEIGHT; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][y][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            int height = TerrainHeight(worldX, worldZ, mode);
            Biome biome = BiomeAt(worldX, worldZ);
            bool pond = mode != TERRAIN_FLAT && ShouldPlacePond(worldX, worldZ, height);

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
                } else if (pond) {
                    type = (biome == BIOME_SNOW) ? BLOCK_ICE : BLOCK_WATER;
                } else if (biome == BIOME_DESERT) {
                    type = BLOCK_SAND;
                } else if (biome == BIOME_SNOW) {
                    type = BLOCK_SNOW;
                } else if (biome == BIOME_MOUNTAIN) {
                    type = (height >= 22) ? BLOCK_SNOW : ((height >= 17) ? BLOCK_STONE : BLOCK_GRASS);
                } else {
                    type = BLOCK_GRASS;
                }
                chunk->blocks[lx][y][lz] = (unsigned short)type;
            }

            if (mode != TERRAIN_FLAT && CaveWaterAt(worldX, height - 2, worldZ, height) &&
                CaveAt(worldX, height - 2, worldZ, height) &&
                !CaveAt(worldX, height - 3, worldZ, height)) {
                chunk->blocks[lx][height - 2][lz] = (unsigned short)BLOCK_WATER;
            }

            if (mode != TERRAIN_FLAT && biome == BIOME_DESERT && height > 6 &&
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
                BlockType surface = (BlockType)chunk->blocks[lx][height][lz];
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
            chunk->blocks[editLx][edit.y][editLz] = (unsigned short)edit.type;
        }
    }
}
