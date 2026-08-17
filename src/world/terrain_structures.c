#include "world/terrain_structures_internal.h"

#include "world/chunks.h"
#include "world/terrain.h"

#include <stdlib.h>

#define VILLAGE_SPACING 48
#define VILLAGE_PROBABILITY 35u
#define VILLAGE_HALF_WIDTH 3
#define VILLAGE_HALF_DEPTH 2

static void MaterializeHomeTerrainRange(Chunk *chunk, int cx, int cz,
                                        int minY, int maxY,
                                        TerrainMode mode)
{
    int firstSectionY = SurfaceSectionYFromBlockY(minY);
    int lastSectionY = SurfaceSectionYFromBlockY(maxY);
    for (int sectionY = firstSectionY;
         sectionY <= lastSectionY; sectionY++) {
        GenerateChunkTerrainSectionBase(chunk, cx, cz, sectionY, mode);
    }
}

static void GenerateMineshaft(Chunk *chunk, int cx, int cz,
                              TerrainMode mode)
{
    const int spacing = 40;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 16, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 16, spacing);
    int minAnchorZ = FloorDivInt(startZ - 16, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 16, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 17, anchorZ + 29) % 100u >= 30u) {
                continue;
            }

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 8 + (int)(WorldHash2D(anchorX + 3, anchorZ + 5) % 5u);
            int dx = (WorldHash2D(anchorX + 7, anchorZ + 11) % 2u) ? 1 : -1;
            int dz = (WorldHash2D(anchorX + 13, anchorZ + 19) % 2u) ? 1 : -1;
            int length = 12 +
                         (int)(WorldHash2D(anchorX + 23,
                                          anchorZ + 31) % 9u);
            MaterializeHomeTerrainRange(chunk, cx, cz, wy, wy + 3, mode);

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
                        SetChunkLocalBlock(
                            chunk, bx, wy + ly, bz, BLOCK_WOOD);
                    }
                    SetChunkLocalBlock(chunk, bx, wy + 3, bz, BLOCK_WOOD);
                }
            }
        }
    }
}

static void GenerateDungeon(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    const int spacing = 80;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 6, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 6, spacing);
    int minAnchorZ = FloorDivInt(startZ - 6, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 6, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX + 41, anchorZ + 53) % 100u >= 25u) {
                continue;
            }

            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            int wy = 10 + (int)(WorldHash2D(anchorX + 2,
                                            anchorZ + 4) % 4u);
            MaterializeHomeTerrainRange(chunk, cx, cz, wy, wy + 3, mode);

            for (int ox = -3; ox <= 3; ox++) {
                for (int oz = -3; oz <= 3; oz++) {
                    bool wall = abs(ox) == 3 || abs(oz) == 3;
                    bool door = ox >= -1 && ox <= 0 && oz == -3;
                    int bx = wx + ox;
                    int bz = wz + oz;
                    if (wall && !door) {
                        for (int oy = 0; oy <= 3; oy++) {
                            SetChunkLocalBlock(
                                chunk, bx, wy + oy, bz,
                                BLOCK_STONE_BRICKS);
                        }
                    }
                }
            }
            for (int oy = 0; oy <= 3; oy++) {
                SetChunkLocalBlock(
                    chunk, wx - 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(
                    chunk, wx + 3, wy + oy, wz, BLOCK_STONE_BRICKS);
                SetChunkLocalBlock(
                    chunk, wx, wy + oy, wz + 3, BLOCK_STONE_BRICKS);
            }
        }
    }
}

static void GenerateDesertTemple(Chunk *chunk, int cx, int cz,
                                 TerrainMode mode)
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
            if (WorldHash2D(anchorX + 67, anchorZ + 79) % 100u >= 20u) {
                continue;
            }

            int base = TerrainHeight(wx, wz, mode);

            for (int ox = -2; ox <= 2; ox++) {
                for (int oz = -2; oz <= 2; oz++) {
                    for (int oy = 0; oy <= 2; oy++) {
                        SetChunkLocalBlock(
                            chunk, wx + ox, base + 1 + oy, wz + oz,
                            BLOCK_SANDSTONE);
                    }
                }
            }
            SetChunkLocalBlock(chunk, wx, base + 4, wz, BLOCK_SANDSTONE);

            for (int oy = 1; oy <= 4; oy++) {
                SetChunkLocalBlock(
                    chunk, wx - 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(
                    chunk, wx + 4, base + oy, wz, BLOCK_SANDSTONE);
                SetChunkLocalBlock(
                    chunk, wx, base + oy, wz - 4, BLOCK_SANDSTONE);
                SetChunkLocalBlock(
                    chunk, wx, base + oy, wz + 4, BLOCK_SANDSTONE);
            }
            for (int ox = -1; ox <= 1; ox++) {
                for (int oz = -1; oz <= 1; oz++) {
                    for (int oy = 0; oy <= 3; oy++) {
                        SetChunkLocalBlock(
                            chunk, wx + ox, base - 1 - oy, wz + oz,
                            BLOCK_SANDSTONE);
                    }
                }
            }
        }
    }
}

static void SetVillageBlock(Chunk *chunk, int x, int y, int z,
                            BlockType type)
{
    SetChunkLocalBlock(chunk, x, y, z, type);
}

static void GenerateVillage(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(
        startX - VILLAGE_HALF_WIDTH - 1, VILLAGE_SPACING);
    int maxAnchorX = FloorDivInt(
        startX + CHUNK_SIZE + VILLAGE_HALF_WIDTH, VILLAGE_SPACING);
    int minAnchorZ = FloorDivInt(
        startZ - VILLAGE_HALF_DEPTH - 1, VILLAGE_SPACING);
    int maxAnchorZ = FloorDivInt(
        startZ + CHUNK_SIZE + VILLAGE_HALF_DEPTH, VILLAGE_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * VILLAGE_SPACING;
            int wz = anchorZ * VILLAGE_SPACING;
            if (WorldHash2D(anchorX, anchorZ) % 100u >=
                VILLAGE_PROBABILITY) {
                continue;
            }
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
                        z < startZ || z >= startZ + CHUNK_SIZE) {
                        continue;
                    }

                    for (int y = 0; y <= 5; y++) {
                        BlockType type = BLOCK_AIR;
                        bool edge = x == minX || x == maxX ||
                                    z == minZ || z == maxZ;
                        bool corner = (x == minX || x == maxX) &&
                                      (z == minZ || z == maxZ);
                        bool front = z == maxZ;

                        if (y == 0) {
                            type = BLOCK_PLANK;
                        } else if (y >= 1 && y <= 2 && edge) {
                            if (front && (x == wx - 1 || x == wx)) {
                                type = BLOCK_AIR;
                            } else if (y == 2 && edge &&
                                       (x == wx || z == wz) &&
                                       !(z == minZ || z == maxZ) &&
                                       x != minX && x != maxX) {
                                type = BLOCK_GLASS;
                            } else {
                                type = corner ? BLOCK_WOOD : BLOCK_PLANK;
                            }
                        } else if (y == 3) {
                            type = edge ? BLOCK_PLANK : BLOCK_AIR;
                        } else if (y == 4) {
                            type = BLOCK_AIR;
                            if (x > minX && x < maxX &&
                                z > minZ && z < maxZ) {
                                type = BLOCK_PLANK;
                            }
                        } else if (y == 5) {
                            type = BLOCK_AIR;
                            if (x >= minX + 2 && x <= maxX - 2 && z == wz) {
                                type = BLOCK_PLANK;
                            }
                        }
                        if (type != BLOCK_AIR) {
                            SetVillageBlock(chunk, x, base + y, z, type);
                        }
                    }
                }
            }
        }
    }
}

void TerrainStructuresGenerate(Chunk *chunk, int cx, int cz,
                               TerrainMode mode)
{
    GenerateVillage(chunk, cx, cz, mode);
    GenerateMineshaft(chunk, cx, cz, mode);
    GenerateDungeon(chunk, cx, cz, mode);
    GenerateDesertTemple(chunk, cx, cz, mode);
}

#ifdef TERRAIN_TESTING
void TerrainTestGenerateMineshaft(Chunk *chunk, int cx, int cz,
                                  TerrainMode mode)
{
    GenerateMineshaft(chunk, cx, cz, mode);
}

void TerrainTestGenerateStructures(Chunk *chunk, int cx, int cz,
                                   TerrainMode mode)
{
    TerrainStructuresGenerate(chunk, cx, cz, mode);
}
#endif
