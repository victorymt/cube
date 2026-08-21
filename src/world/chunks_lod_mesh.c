#include "world/chunks_internal.h"

typedef struct ChunkLodColumn {
    float height;
    BlockType type;
    bool valid;
} ChunkLodColumn;

static int ChunkLodStep(ChunkLodLevel lod)
{
    if (lod == CHUNK_LOD_HALF) return 2;
    if (lod == CHUNK_LOD_QUARTER) return 4;
    return 0;
}

static bool ChunkLodBlockAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int lx, int y, int lz,
    BlockType *outBlock)
{
    if (!outBlock) return false;
    if (lx >= 0 && lx < CHUNK_SIZE && y >= 0 &&
        y < SURFACE_SECTION_HEIGHT && lz >= 0 && lz < CHUNK_SIZE) {
        *outBlock = (BlockType)blocks[lx][y][lz];
        return true;
    }
    unsigned char ignoredVolume = 0u;
    return boundary && SurfaceBoundaryCellAt(
        boundary, lx, y, lz, outBlock, &ignoredVolume);
}

static ChunkLodColumn ChunkLodColumnAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int lx, int lz)
{
    BlockType above = BLOCK_AIR;
    if (ChunkLodBlockAt(blocks, boundary, lx, SURFACE_SECTION_HEIGHT,
                        lz, &above) && BlockUsesGreedyCubeMesh(above)) {
        return (ChunkLodColumn){ 0 };
    }
    for (int y = SURFACE_SECTION_HEIGHT - 1; y >= 0; y--) {
        BlockType type = BLOCK_AIR;
        if (ChunkLodBlockAt(blocks, boundary, lx, y, lz, &type) &&
            BlockUsesGreedyCubeMesh(type)) {
            return (ChunkLodColumn){
                .height = (float)y + 1.0f,
                .type = type,
                .valid = true
            };
        }
    }
    return (ChunkLodColumn){ 0 };
}

static ChunkLodColumn ChunkLodVertexAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int vx, int vz)
{
    ChunkLodColumn result = { 0 };
    float heightSum = 0.0f;
    int count = 0;
    for (int dz = -1; dz <= 0; dz++) {
        for (int dx = -1; dx <= 0; dx++) {
            ChunkLodColumn sample = ChunkLodColumnAt(
                blocks, boundary, vx + dx, vz + dz);
            if (!sample.valid) continue;
            heightSum += sample.height;
            if (!result.valid || sample.height > result.height) {
                result = sample;
            }
            count++;
        }
    }
    if (count > 0) result.height = heightSum / (float)count;
    return result;
}

static ChunkLodColumn ChunkLodCellMaterial(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int x, int z, int step)
{
    ChunkLodColumn result = { 0 };
    for (int dz = 0; dz < step; dz++) {
        for (int dx = 0; dx < step; dx++) {
            ChunkLodColumn sample = ChunkLodColumnAt(
                blocks, boundary, x + dx, z + dz);
            if (sample.valid &&
                (!result.valid || sample.height > result.height)) {
                result = sample;
            }
        }
    }
    return result;
}

static bool ChunkLodWaterAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int lx, int y, int lz,
    BlockType *outBlock, unsigned char *outVolume)
{
    if (!outBlock || !outVolume) return false;
    if (lx >= 0 && lx < CHUNK_SIZE && y >= 0 &&
        y < SURFACE_SECTION_HEIGHT && lz >= 0 && lz < CHUNK_SIZE) {
        *outBlock = (BlockType)blocks[lx][y][lz];
        *outVolume = *outBlock == BLOCK_WATER
            ? (waterVolumes ? waterVolumes[lx][y][lz]
                            : (unsigned char)WATER_VOLUME_CAPACITY)
            : 0u;
        return true;
    }
    return boundary && SurfaceBoundaryCellAt(
        boundary, lx, y, lz, outBlock, outVolume);
}

static ChunkLodColumn ChunkLodWaterColumnAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int lx, int lz)
{
    BlockType above = BLOCK_AIR;
    unsigned char aboveVolume = 0u;
    if (ChunkLodWaterAt(blocks, waterVolumes, boundary, lx,
                        SURFACE_SECTION_HEIGHT, lz,
                        &above, &aboveVolume) &&
        above == BLOCK_WATER && aboveVolume > 0u) {
        return (ChunkLodColumn){ 0 };
    }
    for (int y = SURFACE_SECTION_HEIGHT - 1; y >= 0; y--) {
        BlockType type = BLOCK_AIR;
        unsigned char volume = 0u;
        if (ChunkLodWaterAt(blocks, waterVolumes, boundary, lx, y, lz,
                            &type, &volume) &&
            type == BLOCK_WATER && volume > 0u) {
            return (ChunkLodColumn){
                .height = (float)y +
                    (float)volume / (float)WATER_VOLUME_CAPACITY,
                .type = BLOCK_WATER,
                .valid = true
            };
        }
    }
    return (ChunkLodColumn){ 0 };
}

static ChunkLodColumn ChunkLodWaterVertexAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int vx, int vz)
{
    ChunkLodColumn result = { 0 };
    float heightSum = 0.0f;
    int count = 0;
    for (int dz = -1; dz <= 0; dz++) {
        for (int dx = -1; dx <= 0; dx++) {
            ChunkLodColumn sample = ChunkLodWaterColumnAt(
                blocks, waterVolumes, boundary, vx + dx, vz + dz);
            if (!sample.valid) continue;
            heightSum += sample.height;
            result = sample;
            count++;
        }
    }
    if (count > 0) result.height = heightSum / (float)count;
    return result;
}

static ChunkLodColumn ChunkLodWaterCellAt(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const SurfaceBoundarySnapshot *boundary, int x, int z, int step)
{
    ChunkLodColumn result = { 0 };
    for (int dz = 0; dz < step; dz++) {
        for (int dx = 0; dx < step; dx++) {
            ChunkLodColumn sample = ChunkLodWaterColumnAt(
                blocks, waterVolumes, boundary, x + dx, z + dz);
            if (sample.valid &&
                (!result.valid || sample.height > result.height)) {
                result = sample;
            }
        }
    }
    return result;
}

static Vector3 ChunkLodTopNormal(const Vector3 corners[6])
{
    Vector3 first = Vector3CrossProduct(
        Vector3Subtract(corners[1], corners[0]),
        Vector3Subtract(corners[2], corners[0]));
    Vector3 second = Vector3CrossProduct(
        Vector3Subtract(corners[4], corners[3]),
        Vector3Subtract(corners[5], corners[3]));
    Vector3 normal = Vector3Add(first, second);
    if (Vector3LengthSqr(normal) < 0.000001f) {
        return (Vector3){ 0.0f, 1.0f, 0.0f };
    }
    return Vector3Normalize(normal);
}

static void ChunkLodEmitFace(ChunkMeshEmitter *emitter, Vector3 corners[6],
                             Vector3 normal, BlockType type, int face,
                             float textureWidth, float textureHeight)
{
    Vector2 uvs[6] = { 0 };
    float ao[6] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };
    AtlasRepeatedUVs(TextureForBlockFace(type, face), textureWidth,
                     textureHeight, uvs);
    AddMeshFaceLighting(emitter, corners, normal, uvs, WHITE, ao, 0.0f);
}

static void ChunkLodEmitTop(ChunkMeshEmitter *emitter, int worldX, int worldZ,
                            int step, const ChunkLodColumn vertices[4],
                            BlockType type)
{
    float x0 = (float)worldX;
    float z0 = (float)worldZ;
    float x1 = x0 + (float)step;
    float z1 = z0 + (float)step;
    Vector3 corners[6] = {
        { x0, vertices[2].height, z1 },
        { x1, vertices[3].height, z1 },
        { x1, vertices[1].height, z0 },
        { x0, vertices[2].height, z1 },
        { x1, vertices[1].height, z0 },
        { x0, vertices[0].height, z0 }
    };
    ChunkLodEmitFace(emitter, corners, ChunkLodTopNormal(corners), type, 2,
                     (float)step, (float)step);
}

static void ChunkLodEmitSkirt(ChunkMeshEmitter *emitter, int face,
                              int worldX, int worldZ, int step,
                              float firstHeight, float secondHeight,
                              BlockType type)
{
    float x0 = (float)worldX;
    float z0 = (float)worldZ;
    float x1 = x0 + (float)step;
    float z1 = z0 + (float)step;
    Vector3 corners[6] = { 0 };
    Vector3 normal = { 0 };
    if (face == 1) {
        normal = (Vector3){ -1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x0, 0.0f, z0 };
        corners[1] = (Vector3){ x0, 0.0f, z1 };
        corners[2] = (Vector3){ x0, secondHeight, z1 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, firstHeight, z0 };
    } else if (face == 0) {
        normal = (Vector3){ 1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x1, 0.0f, z1 };
        corners[1] = (Vector3){ x1, 0.0f, z0 };
        corners[2] = (Vector3){ x1, firstHeight, z0 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x1, secondHeight, z1 };
    } else if (face == 5) {
        normal = (Vector3){ 0.0f, 0.0f, -1.0f };
        corners[0] = (Vector3){ x1, 0.0f, z0 };
        corners[1] = (Vector3){ x0, 0.0f, z0 };
        corners[2] = (Vector3){ x0, firstHeight, z0 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x1, secondHeight, z0 };
    } else {
        normal = (Vector3){ 0.0f, 0.0f, 1.0f };
        corners[0] = (Vector3){ x0, 0.0f, z1 };
        corners[1] = (Vector3){ x1, 0.0f, z1 };
        corners[2] = (Vector3){ x1, secondHeight, z1 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, firstHeight, z1 };
    }
    ChunkLodEmitFace(emitter, corners, normal, type, face, (float)step,
                     fmaxf(firstHeight, secondHeight));
}

bool BuildChunkLodHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh)
{
    if (!blocks || !outMesh) return false;
    int step = ChunkLodStep(lod);
    if (step == 0 || CHUNK_SIZE % step != 0) return false;

    int gridSize = CHUNK_SIZE / step;
    ChunkLodColumn vertices[CHUNK_SIZE / 2 + 1][CHUNK_SIZE / 2 + 1] = { 0 };
    for (int gz = 0; gz <= gridSize; gz++) {
        for (int gx = 0; gx <= gridSize; gx++) {
            vertices[gx][gz] = ChunkLodVertexAt(
                blocks, boundary, gx * step, gz * step);
        }
    }

    ChunkMeshEmitter emitter = {
        .mesh = outMesh,
        .dynamicCapacity = true
    };
    for (int gz = 0; gz < gridSize && !emitter.failed; gz++) {
        for (int gx = 0; gx < gridSize && !emitter.failed; gx++) {
            int x = gx * step;
            int z = gz * step;
            ChunkLodColumn material = ChunkLodCellMaterial(
                blocks, boundary, x, z, step);
            ChunkLodColumn cellVertices[4] = {
                vertices[gx][gz], vertices[gx + 1][gz],
                vertices[gx][gz + 1], vertices[gx + 1][gz + 1]
            };
            if (!material.valid || !cellVertices[0].valid ||
                !cellVertices[1].valid || !cellVertices[2].valid ||
                !cellVertices[3].valid) {
                continue;
            }
            int worldX = chunkX * CHUNK_SIZE + x;
            int worldZ = chunkZ * CHUNK_SIZE + z;
            ChunkLodEmitTop(&emitter, worldX, worldZ, step,
                            cellVertices, material.type);
            if (gx == 0) {
                ChunkLodEmitSkirt(&emitter, 1, worldX, worldZ, step,
                                  cellVertices[0].height,
                                  cellVertices[2].height, material.type);
            }
            if (gx == gridSize - 1) {
                ChunkLodEmitSkirt(&emitter, 0, worldX, worldZ, step,
                                  cellVertices[1].height,
                                  cellVertices[3].height, material.type);
            }
            if (gz == 0) {
                ChunkLodEmitSkirt(&emitter, 5, worldX, worldZ, step,
                                  cellVertices[0].height,
                                  cellVertices[1].height, material.type);
            }
            if (gz == gridSize - 1) {
                ChunkLodEmitSkirt(&emitter, 4, worldX, worldZ, step,
                                  cellVertices[2].height,
                                  cellVertices[3].height, material.type);
            }
        }
    }
    if (emitter.failed || emitter.vertexIndex == 0) {
        FreeMeshData(outMesh);
        return false;
    }
    outMesh->vertexCount = emitter.vertexIndex;
    outMesh->triangleCount = emitter.vertexIndex / 3;
    return true;
}

bool BuildChunkLodWaterHeightfieldMeshData(
    const unsigned short blocks[CHUNK_SIZE][SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    const unsigned char waterVolumes[CHUNK_SIZE]
                                    [SURFACE_SECTION_HEIGHT][CHUNK_SIZE],
    int chunkX, int chunkZ, ChunkLodLevel lod,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh)
{
    if (!blocks || !outMesh) return false;
    int step = ChunkLodStep(lod);
    if (step == 0 || CHUNK_SIZE % step != 0) return false;

    int gridSize = CHUNK_SIZE / step;
    ChunkLodColumn vertices[CHUNK_SIZE / 2 + 1]
                           [CHUNK_SIZE / 2 + 1] = { 0 };
    for (int gz = 0; gz <= gridSize; gz++) {
        for (int gx = 0; gx <= gridSize; gx++) {
            vertices[gx][gz] = ChunkLodWaterVertexAt(
                blocks, waterVolumes, boundary, gx * step, gz * step);
        }
    }

    ChunkMeshEmitter emitter = {
        .mesh = outMesh,
        .dynamicCapacity = true
    };
    for (int gz = 0; gz < gridSize && !emitter.failed; gz++) {
        for (int gx = 0; gx < gridSize && !emitter.failed; gx++) {
            int x = gx * step;
            int z = gz * step;
            ChunkLodColumn surface = ChunkLodWaterCellAt(
                blocks, waterVolumes, boundary, x, z, step);
            if (!surface.valid) continue;
            ChunkLodColumn cellVertices[4] = {
                vertices[gx][gz], vertices[gx + 1][gz],
                vertices[gx][gz + 1], vertices[gx + 1][gz + 1]
            };
            for (int corner = 0; corner < 4; corner++) {
                if (!cellVertices[corner].valid) {
                    cellVertices[corner] = surface;
                }
            }
            ChunkLodEmitTop(
                &emitter, chunkX * CHUNK_SIZE + x,
                chunkZ * CHUNK_SIZE + z, step,
                cellVertices, BLOCK_WATER);
        }
    }
    if (emitter.failed || emitter.vertexIndex == 0) {
        FreeMeshData(outMesh);
        return false;
    }
    outMesh->vertexCount = emitter.vertexIndex;
    outMesh->triangleCount = emitter.vertexIndex / 3;
    return true;
}
