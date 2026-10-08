#include "world/terrain_cube.h"
#include "world/chunks.h"

#include <math.h>
#include <string.h>

bool TerrainCubeGenerateChunkSection(const TerrainCubeContext *context,
                                     Chunk *staging, int sectionY)
{
    if (!staging || staging->loaded || !staging->cubeGrid || !staging->spherical ||
        ChunkTerrainSectionIsResolved(staging, sectionY) ||
        ChunkGetSectionConst(staging, sectionY)) return false;
    unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE];
    if (!TerrainCubeGenerateSection(context, staging->cubeKey, sectionY, blocks)) return false;
    bool any = false;
    for (int u = 0; u < CHUNK_SIZE && !any; u++)
        for (int y = 0; y < SURFACE_SECTION_HEIGHT && !any; y++)
            for (int v = 0; v < CHUNK_SIZE; v++)
                if (blocks[u][y][v] != BLOCK_AIR) { any = true; break; }
    if (any) {
        ChunkSection *section = ChunkGetSection(staging, sectionY, true);
        if (!section) return false;
        memcpy(section->blocks, blocks, sizeof(blocks));
        section->dirty = true;
        section->dirtyStamp = 1;
    }
    return ChunkMarkTerrainSectionResolved(staging, sectionY);
}

bool TerrainCubeGenerateChunk(const TerrainCubeContext *context,
                              SurfaceCubeChunkKey key, Chunk *staging)
{
    const WorldSurfaceSnapshot *snapshot = TerrainCubeContextSnapshot(context);
    if (!snapshot || !staging || staging->loaded || staging->sectionCount ||
        staging->resolvedTerrainSectionCount || !SurfaceCubeChunkKeyIsValid(key) ||
        key.bodyId != snapshot->context.bodyId) return false;
    staging->spherical = true;
    staging->cubeGrid = true;
    staging->cubeKey = key;
    staging->surfaceAddress = (SurfaceAddress){key.bodyId, key.face,
        key.u*CHUNK_SIZE, key.v*CHUNK_SIZE, 0};
    enum {FIRST = TERRAIN_CUBE_MIN_RADIAL / SURFACE_SECTION_HEIGHT,
          LAST = SURFACE_GENERATION_MAX_Y_EXCLUSIVE / SURFACE_SECTION_HEIGHT};
    bool required[LAST - FIRST] = {false};
    for (int u = 0; u < CHUNK_SIZE; u++) for (int v = 0; v < CHUNK_SIZE; v++) {
        TerrainCubeColumn column;
        SurfaceAddress cell = {key.bodyId, key.face, key.u*CHUNK_SIZE + u, key.v*CHUNK_SIZE + v, 0};
        if (!TerrainCubeSampleColumn(context, cell, &column)) return false;
        float surfaces[] = {column.terrain.elevation, column.terrain.seaLevel,
            column.terrain.landform.hasWater ? column.terrain.landform.waterLevel : column.terrain.elevation};
        for (unsigned i = 0; i < sizeof(surfaces)/sizeof(surfaces[0]); i++) {
            if (i == 1 && surfaces[i] < 0) continue;
            int center = (int)floorf(surfaces[i]/SURFACE_SECTION_HEIGHT);
            for (int sectionY = center - 1; sectionY <= center + 1; sectionY++)
                if (sectionY >= FIRST && sectionY < LAST) required[sectionY - FIRST] = true;
        }
    }
    for (int sectionY = FIRST; sectionY < LAST; sectionY++)
        if (required[sectionY - FIRST] && !TerrainCubeGenerateChunkSection(context, staging, sectionY))
            return false;
    return true;
}
