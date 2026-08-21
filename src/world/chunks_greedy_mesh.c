#include "world/chunks_internal.h"

typedef struct GreedyMeshContext {
    const unsigned short (*blocks)[CHUNK_SIZE];
    int height;
    int layerY;
    int chunkX;
    int chunkZ;
    const SurfaceBoundarySnapshot *boundary;
    const int (*faces)[3];
    const int *nearbyTorchIndices;
    int nearbyTorchCount;
} GreedyMeshContext;

typedef struct GreedyFaceCell {
    BlockType type;
    BlockTexture texture;
    float ambientOcclusion;
    float localLight;
    bool present;
    bool mergeable;
} GreedyFaceCell;

static BlockType ChunkFaceNeighbor(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int lx, int y, int lz, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborY >= 0 && neighborY < height &&
        neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        return (BlockType)blocks[neighborLx * height + neighborY][neighborLz];
    }

    int worldY = layerY + neighborY;
    if (!InHeight(worldY)) return BLOCK_AIR;
    if (boundary) {
        BlockType neighbor = BLOCK_AIR;
        unsigned char ignoredVolume = 0u;
        return SurfaceBoundaryCellAt(
            boundary, neighborLx, neighborY, neighborLz,
            &neighbor, &ignoredVolume) ? neighbor : BLOCK_STONE;
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    return GetBlockAt(wx, worldY, wz);
}

bool ChunkFaceIsVisibleWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int lx, int y, int lz, int nx, int ny, int nz)
{
    BlockType neighbor = ChunkFaceNeighbor(
        blocks, height, layerY, chunkX, chunkZ, boundary,
        lx, y, lz, nx, ny, nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED ||
           IsTranslucentBlock(neighbor);
}

static bool BlockOccludesAmbient(BlockType block)
{
    return block != BLOCK_AIR && block != BLOCK_SPACESHIP_OCCUPIED &&
           !IsTranslucentBlock(block);
}

static BlockType SnapshotBlockAt(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int worldX, int worldY, int worldZ)
{
    int lx = worldX - chunkX * CHUNK_SIZE;
    int lz = worldZ - chunkZ * CHUNK_SIZE;
    int localY = worldY - layerY;
    if (blocks && lx >= 0 && lx < CHUNK_SIZE && lz >= 0 &&
        lz < CHUNK_SIZE && localY >= 0 && localY < height) {
        return (BlockType)blocks[lx * height + localY][lz];
    }
    if (boundary) {
        BlockType block = BLOCK_AIR;
        unsigned char ignoredVolume = 0u;
        return SurfaceBoundaryCellAt(
            boundary, lx, localY, lz, &block, &ignoredVolume)
            ? block : BLOCK_STONE;
    }
    return GetBlockAt(worldX, worldY, worldZ);
}

float BlockCornerAmbientOcclusion(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    int x, int y, int z, Vector3 normal, Vector3 corner)
{
    int nx = (int)normal.x;
    int ny = (int)normal.y;
    int nz = (int)normal.z;
    int t1x = 0, t1y = 0, t1z = 0;
    int t2x = 0, t2y = 0, t2z = 0;
    if (nx != 0) {
        t1y = corner.y > (float)y + 0.5f ? 1 : -1;
        t2z = corner.z > (float)z + 0.5f ? 1 : -1;
    } else if (ny != 0) {
        t1x = corner.x > (float)x + 0.5f ? 1 : -1;
        t2z = corner.z > (float)z + 0.5f ? 1 : -1;
    } else {
        t1x = corner.x > (float)x + 0.5f ? 1 : -1;
        t2y = corner.y > (float)y + 0.5f ? 1 : -1;
    }
    int outsideX = x + nx;
    int outsideY = layerY + y + ny;
    int outsideZ = z + nz;
    bool side1 = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ, boundary,
        outsideX + t1x, outsideY + t1y, outsideZ + t1z));
    bool side2 = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ, boundary,
        outsideX + t2x, outsideY + t2y, outsideZ + t2z));
    bool diagonal = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ, boundary,
        outsideX + t1x + t2x, outsideY + t1y + t2y,
        outsideZ + t1z + t2z));
    int occlusion = side1 && side2 ? 3 :
                    (int)side1 + (int)side2 + (int)diagonal;
    static const float factors[4] = { 1.0f, 0.84f, 0.66f, 0.48f };
    return factors[occlusion];
}

bool BlockUsesGreedyCubeMesh(BlockType type)
{
    if (type == BLOCK_AIR || type == BLOCK_SPACESHIP_OCCUPIED ||
        IsTranslucentBlock(type) || IsPlantBlock(type) ||
        BlockRenderShapeFor(type) != BLOCK_RENDER_CUBE) {
        return false;
    }
    if (type == BLOCK_TORCH || type == BLOCK_ALBUM ||
        type == BLOCK_SPACESHIP ||
        (type >= BLOCK_SPACESHIP_CORE_NORTH &&
         type <= BLOCK_SPACESHIP_CORE_WEST) ||
        type == BLOCK_SLAB || type == BLOCK_DOOR ||
        type == BLOCK_DOOR_OPEN || type == BLOCK_STONE_STAIRS ||
        type == BLOCK_WOOD_STAIRS || type == BLOCK_FENCE ||
        type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN ||
        type == BLOCK_GLASS_PANE) {
        return false;
    }
    return true;
}

static void GreedyFaceCoordinates(int face, int slice, int u, int v,
                                  int *lx, int *y, int *lz)
{
    if (face == 0 || face == 1) {
        *lx = slice;
        *y = v;
        *lz = u;
    } else if (face == 2 || face == 3) {
        *lx = u;
        *y = slice;
        *lz = v;
    } else {
        *lx = u;
        *y = v;
        *lz = slice;
    }
}

static void GreedyFaceGeometry(int face, int x, int y, int z,
                               int width, int height,
                               Vector3 corners[6], Vector3 *normal)
{
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float x1 = x0 + 1.0f;
    float y1 = y0 + 1.0f;
    float z1 = z0 + 1.0f;
    if (face == 0 || face == 1) {
        y1 = y0 + (float)height;
        z1 = z0 + (float)width;
    } else if (face == 2 || face == 3) {
        x1 = x0 + (float)width;
        z1 = z0 + (float)height;
    } else {
        x1 = x0 + (float)width;
        y1 = y0 + (float)height;
    }

    switch (face) {
    case 0:
        *normal = (Vector3){ 1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x1, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x1, y1, z1 };
        break;
    case 1:
        *normal = (Vector3){ -1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z1 };
        corners[2] = (Vector3){ x0, y1, z1 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 2:
        *normal = (Vector3){ 0.0f, 1.0f, 0.0f };
        corners[0] = (Vector3){ x0, y1, z1 };
        corners[1] = (Vector3){ x1, y1, z1 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 3:
        *normal = (Vector3){ 0.0f, -1.0f, 0.0f };
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y0, z1 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, y0, z1 };
        break;
    case 4:
        *normal = (Vector3){ 0.0f, 0.0f, 1.0f };
        corners[0] = (Vector3){ x0, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z1 };
        corners[2] = (Vector3){ x1, y1, z1 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x0, y1, z1 };
        break;
    default:
        *normal = (Vector3){ 0.0f, 0.0f, -1.0f };
        corners[0] = (Vector3){ x1, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z0 };
        corners[2] = (Vector3){ x0, y1, z0 };
        corners[3] = corners[0];
        corners[4] = corners[2];
        corners[5] = (Vector3){ x1, y1, z0 };
        break;
    }
}

static void GreedyAtlasUVs(BlockTexture texture, int width, int height,
                           Vector2 uvs[6])
{
    int tileIndex = (int)texture;
    int tileX = tileIndex % ATLAS_COLUMNS;
    int tileY = tileIndex / ATLAS_COLUMNS;
    float baseU = 1.0f + (float)(tileX * GREEDY_MESH_UV_STRIDE);
    float baseV = 1.0f + (float)(tileY * GREEDY_MESH_UV_STRIDE);
    float u0 = -baseU;
    float u1 = -(baseU + (float)width);
    float v0 = -baseV;
    float v1 = -(baseV + (float)height);
    uvs[0] = (Vector2){ u0, v1 };
    uvs[1] = (Vector2){ u1, v1 };
    uvs[2] = (Vector2){ u1, v0 };
    uvs[3] = uvs[0];
    uvs[4] = uvs[2];
    uvs[5] = (Vector2){ u0, v0 };
}

static void GreedyFaceAmbientOcclusion(
    const GreedyMeshContext *context, int x, int y, int z,
    Vector3 normal, const Vector3 corners[6], float ao[6])
{
    for (int index = 0; index < 3; index++) {
        ao[index] = BlockCornerAmbientOcclusion(
            context->blocks, context->height, context->layerY,
            context->chunkX, context->chunkZ, context->boundary,
            x, y, z, normal, corners[index]);
    }
    ao[3] = ao[0];
    ao[4] = ao[2];
    ao[5] = BlockCornerAmbientOcclusion(
        context->blocks, context->height, context->layerY,
        context->chunkX, context->chunkZ, context->boundary,
        x, y, z, normal, corners[5]);
}

static bool GreedyFaceCellEqual(const GreedyFaceCell *left,
                                const GreedyFaceCell *right)
{
    return left->present && right->present && left->mergeable &&
           right->mergeable && left->type == right->type &&
           left->texture == right->texture &&
           left->ambientOcclusion == right->ambientOcclusion &&
           left->localLight == right->localLight;
}

static GreedyFaceCell GreedyFaceCellAt(
    const GreedyMeshContext *context, int face, int lx, int y, int lz)
{
    GreedyFaceCell cell = { 0 };
    BlockType type = (BlockType)context->blocks[
        lx * context->height + y][lz];
    if (!BlockUsesGreedyCubeMesh(type) ||
        !ChunkFaceIsVisibleWithSnapshot(
            context->blocks, context->height, context->layerY,
            context->chunkX, context->chunkZ, context->boundary,
            lx, y, lz, context->faces[face][0], context->faces[face][1],
            context->faces[face][2])) {
        return cell;
    }

    int x = context->chunkX * CHUNK_SIZE + lx;
    int z = context->chunkZ * CHUNK_SIZE + lz;
    Vector3 corners[6] = { 0 };
    Vector3 normal = Vector3Zero();
    float ao[6] = { 0 };
    GreedyFaceGeometry(face, x, y, z, 1, 1, corners, &normal);
    GreedyFaceAmbientOcclusion(context, x, y, z, normal, corners, ao);
    cell.type = type;
    cell.texture = TextureForBlockFace(type, face);
    cell.ambientOcclusion = ao[0];
    cell.localLight = TorchLightAtBlockNearby(
        x, context->layerY + y, z, context->nearbyTorchIndices,
        context->nearbyTorchCount);
    cell.present = true;
    cell.mergeable = ao[0] == ao[1] && ao[0] == ao[2] && ao[0] == ao[5];
    return cell;
}

static void EmitGreedyFaceRectangle(
    const GreedyMeshContext *context, ChunkMeshEmitter *emitter,
    int face, int slice, int u, int v, int width, int height,
    const GreedyFaceCell *cell)
{
    int lx = 0, y = 0, lz = 0;
    GreedyFaceCoordinates(face, slice, u, v, &lx, &y, &lz);
    int x = context->chunkX * CHUNK_SIZE + lx;
    int z = context->chunkZ * CHUNK_SIZE + lz;
    Vector3 corners[6] = { 0 };
    Vector3 normal = Vector3Zero();
    Vector2 uvs[6] = { 0 };
    float ao[6] = { 0 };
    GreedyFaceGeometry(face, x, y, z, width, height, corners, &normal);
    if (width == 1 && height == 1) {
        AtlasUVs(cell->texture, uvs);
        GreedyFaceAmbientOcclusion(context, x, y, z, normal, corners, ao);
    } else {
        GreedyAtlasUVs(cell->texture, width, height, uvs);
        for (int index = 0; index < 6; index++) {
            ao[index] = cell->ambientOcclusion;
        }
    }
    AddMeshFaceLighting(emitter, corners, normal, uvs, WHITE, ao,
                        cell->localLight);
}

void EmitGreedyCubeFaces(
    ChunkMeshEmitter *emitter,
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const SurfaceBoundarySnapshot *boundary,
    const int faces[6][3], const int *nearbyTorchIndices,
    int nearbyTorchCount, int maximumSpan)
{
    GreedyMeshContext context = {
        .blocks = blocks,
        .height = height,
        .layerY = layerY,
        .chunkX = chunkX,
        .chunkZ = chunkZ,
        .boundary = boundary,
        .faces = faces,
        .nearbyTorchIndices = nearbyTorchIndices,
        .nearbyTorchCount = nearbyTorchCount
    };
    int maximumDimension = height > CHUNK_SIZE ? height : CHUNK_SIZE;
    GreedyFaceCell *mask = calloc(
        (size_t)CHUNK_SIZE * (size_t)maximumDimension, sizeof(*mask));
    if (!mask) {
        emitter->failed = true;
        return;
    }

    if (maximumSpan < 1) maximumSpan = 1;
    if (maximumSpan > CHUNK_SIZE) maximumSpan = CHUNK_SIZE;
    for (int face = 0; face < 6 && !emitter->failed; face++) {
        int sliceCount = (face == 2 || face == 3) ? height : CHUNK_SIZE;
        int uCount = CHUNK_SIZE;
        int vCount = (face == 2 || face == 3) ? CHUNK_SIZE : height;
        for (int slice = 0; slice < sliceCount; slice++) {
            memset(mask, 0, (size_t)uCount * (size_t)vCount * sizeof(*mask));
            for (int v = 0; v < vCount; v++) {
                for (int u = 0; u < uCount; u++) {
                    int lx = 0, y = 0, lz = 0;
                    GreedyFaceCoordinates(face, slice, u, v, &lx, &y, &lz);
                    mask[v * uCount + u] = GreedyFaceCellAt(
                        &context, face, lx, y, lz);
                }
            }

            for (int v = 0; v < vCount; v++) {
                for (int u = 0; u < uCount; u++) {
                    GreedyFaceCell *cell = &mask[v * uCount + u];
                    if (!cell->present) continue;
                    int width = 1;
                    while (width < maximumSpan && u + width < uCount &&
                           GreedyFaceCellEqual(
                               cell, &mask[v * uCount + u + width])) {
                        width++;
                    }
                    int rectangleHeight = 1;
                    while (rectangleHeight < maximumSpan &&
                           v + rectangleHeight < vCount) {
                        bool rowMatches = true;
                        for (int offset = 0; offset < width; offset++) {
                            if (!GreedyFaceCellEqual(
                                    cell, &mask[(v + rectangleHeight) *
                                                uCount + u + offset])) {
                                rowMatches = false;
                                break;
                            }
                        }
                        if (!rowMatches) break;
                        rectangleHeight++;
                    }
                    EmitGreedyFaceRectangle(
                        &context, emitter, face, slice, u, v,
                        width, rectangleHeight, cell);
                    for (int row = 0; row < rectangleHeight; row++) {
                        for (int column = 0; column < width; column++) {
                            mask[(v + row) * uCount + u + column].present =
                                false;
                        }
                    }
                }
            }
        }
    }
    free(mask);
}
