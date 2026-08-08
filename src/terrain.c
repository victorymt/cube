#include "terrain.h"

#include "chunks.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <stdlib.h>

#include "chunks.h"
#include "world.h"
unsigned int Hash2D(int x, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ (unsigned int)(z * 374761393)) * 16777619u;
    h ^= h >> 13;
    h *= 1274126177u;
    return h ^ (h >> 16);
}

float TerrainNoise(float x, float z)
{
    float h = 0.0f;
    h += sinf(x * 0.085f) * 4.0f;
    h += cosf(z * 0.073f) * 3.5f;
    h += sinf((x + z) * 0.041f) * 5.0f;
    h += cosf((x - z) * 0.113f) * 1.8f;
    return h;
}

float BiomeNoise(int x, int z)
{
    return sinf((float)x * 0.017f) * 0.45f +
           cosf((float)z * 0.021f) * 0.40f +
           sinf((float)(x + z) * 0.009f) * 0.35f;
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
    float dx = sinf((float)x * 0.021f) * 1.4f + cosf((float)(x + z) * 0.013f) * 1.2f;
    float dz = sinf((float)z * 0.019f) * 1.4f + cosf((float)(x - z) * 0.011f) * 1.1f;
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
    unsigned int hash = Hash2D(x, z);
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

    float n = sinf((float)x * 0.17f) * sinf((float)z * 0.13f) * sinf((float)y * 0.31f);
    n += sinf((float)(x + z) * 0.09f + (float)y * 0.23f) * 0.7f;

    float tunnel = sinf((float)x * 0.045f + (float)y * 0.09f) * sinf((float)z * 0.045f - (float)y * 0.07f);
    float branch = sinf((float)(x + y) * 0.06f) * sinf((float)(z - y) * 0.055f);
    n += (tunnel + branch) * 0.55f;

    if (n > 0.95f) return true;

    float chamber = sinf((float)x * 0.028f) * sinf((float)z * 0.026f) * sinf((float)y * 0.035f);
    if (chamber > 0.90f && y < height - 6) return true;

    return n > 0.60f;
}

bool CaveWaterAt(int x, int y, int z, int height)
{
    if (y < 4 || y >= height - 4) return false;
    float n = sinf((float)x * 0.11f + 3.7f) * sinf((float)z * 0.13f + 1.3f) * sinf((float)y * 0.19f);
    return n > 0.90f;
}

BlockType OreAt(int x, int y, int z)
{
    unsigned int h = Hash3D(x, y, z);
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
    return Hash2D(x, z) % 97u == 0u;
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
            if (Hash2D(anchorX + 17, anchorZ + 29) % 100u >= 30u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 8 + (int)(Hash2D(anchorX + 3, anchorZ + 5) % 5u);
            int dx = (Hash2D(anchorX + 7, anchorZ + 11) % 2u) ? 1 : -1;
            int dz = (Hash2D(anchorX + 13, anchorZ + 19) % 2u) ? 1 : -1;
            int length = 12 + (int)(Hash2D(anchorX + 23, anchorZ + 31) % 9u);

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
            if (Hash2D(anchorX + 41, anchorZ + 53) % 100u >= 25u) continue;

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 10 + (int)(Hash2D(anchorX + 2, anchorZ + 4) % 4u);

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
            if (Hash2D(anchorX + 67, anchorZ + 79) % 100u >= 20u) continue;

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
            if (Hash2D(anchorX, anchorZ) % 100u >= VILLAGE_PROBABILITY) continue;
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
void GenerateChunkTerrain(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
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
                (Hash2D(worldX, worldZ) % 23u) == 0u) {
                int cactusHeight = 2 + (int)(Hash2D(worldX, worldZ + 13) % 2u);
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
                unsigned int h = Hash2D(worldX, worldZ);
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
                int trunkHeight = 5 + (int)(Hash2D(treeX, treeZ + 31) % 3u);
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
        const BlockEdit *edit = WorldGetEditAt(i);
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit->x, edit->z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz && InHeight(edit->y)) {
            chunk->blocks[editLx][edit->y][editLz] = (unsigned short)edit->type;
        }
    }
}

