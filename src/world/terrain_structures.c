#include "world/terrain_structures_internal.h"

#include "world/cga_voxel.h"
#include "world/chunks.h"
#include "world/terrain.h"

#include <stdlib.h>

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
    const int spacing = 64;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 16, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 16, spacing);
    int minAnchorZ = FloorDivInt(startZ - 16, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 16, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            if (HomeSurfaceHashAt(wx, wz, 1409u) % 100u >= 75u) {
                continue;
            }

            int wy = 8 + (int)(HomeSurfaceHashAt(wx, wz, 1423u) % 5u);
            int dx = (HomeSurfaceHashAt(wx, wz, 1427u) % 2u) ? 1 : -1;
            int dz = (HomeSurfaceHashAt(wx, wz, 1429u) % 2u) ? 1 : -1;
            int length = 12 +
                         (int)(HomeSurfaceHashAt(wx, wz, 1433u) % 9u);
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
    const int spacing = 128;
    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 6, spacing);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 6, spacing);
    int minAnchorZ = FloorDivInt(startZ - 6, spacing);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 6, spacing);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            int wx = anchorX * spacing;
            int wz = anchorZ * spacing;
            if (HomeSurfaceHashAt(wx, wz, 1451u) % 100u >= 64u) {
                continue;
            }

            int wy = 10 + (int)(HomeSurfaceHashAt(wx, wz, 1453u) % 4u);
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
    const int spacing = 128;
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
            if (HomeSurfaceHashAt(wx, wz, 1471u) % 100u >= 40u) {
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

void TerrainStructuresGenerate(Chunk *chunk, int cx, int cz,
                               TerrainMode mode)
{
    CgaGenerateHouseFeatures(chunk, cx, cz, mode);
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
