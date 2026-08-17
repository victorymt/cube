#include "world/chunks_internal.h"

BlockType GetBlock(int x, int y, int z)
{
    if (!InHeight(y)) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return ChunkGetLocalBlock(chunk, lx, y, lz);
}

bool FaceIsVisible(int x, int y, int z, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (!InHeight(neighborY)) return true;
    BlockType neighbor = GetBlock(x + nx, neighborY, z + nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED;
}

static BlockType ChunkFaceNeighbor(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (neighborY < 0 || neighborY >= height) {
        int worldY = layerY + neighborY;
        if (!InHeight(worldY)) return BLOCK_AIR;
        return GetBlockAt(chunkX * CHUNK_SIZE + lx + nx, worldY,
                          chunkZ * CHUNK_SIZE + lz + nz);
    }

    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        return (BlockType)blocks[neighborLx * height + neighborY][neighborLz];
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    return GetBlockAt(wx, layerY + neighborY, wz);
}

bool ChunkFaceIsVisible(const unsigned short (*blocks)[CHUNK_SIZE],
                        int height, int layerY, int chunkX, int chunkZ,
                        int lx, int y, int lz, int nx, int ny, int nz)
{
    BlockType neighbor = ChunkFaceNeighbor(
        blocks, height, layerY, chunkX, chunkZ, lx, y, lz, nx, ny, nz);
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED ||
           IsTranslucentBlock(neighbor);
}

static bool ChunkTransparentNeighbor(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz, int nx, int ny, int nz,
    BlockType *outNeighbor)
{
    if (!outNeighbor) return false;
    int neighborY = y + ny;
    int worldY = layerY + neighborY;
    if (!InHeight(worldY)) {
        *outNeighbor = BLOCK_AIR;
        return true;
    }

    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborY >= 0 && neighborY < height &&
        neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        *outNeighbor = (BlockType)blocks[
            neighborLx * height + neighborY][neighborLz];
        return true;
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    int neighborCx = 0;
    int neighborCz = 0;
    int localX = 0;
    int localZ = 0;
    WorldToChunkLocal(wx, wz, &neighborCx, &neighborCz, &localX, &localZ);
    Chunk *neighborChunk = FindChunk(neighborCx, neighborCz);
    if (!neighborChunk) return false;
    *outNeighbor = ChunkGetLocalBlock(neighborChunk, localX, worldY, localZ);
    return true;
}

static bool ChunkTransparentFaceIsVisible(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz, int nx, int ny, int nz,
    BlockType current)
{
    BlockType neighbor = BLOCK_AIR;
    if (!ChunkTransparentNeighbor(
            blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
            nx, ny, nz, &neighbor)) {
        // Missing streamed neighbors are unknown, not air. Hiding this face
        // avoids transient water walls until the neighbor loads and dirties
        // both chunk borders for a definitive rebuild.
        return false;
    }
    return neighbor == BLOCK_AIR || neighbor == BLOCK_SPACESHIP_OCCUPIED ||
           (IsTranslucentBlock(neighbor) && neighbor != current);
}

Color ShadeColor(Color color, float brightness)
{
    return (Color){
        (unsigned char)Clamp((float)color.r * brightness, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.g * brightness, 0.0f, 255.0f),
        (unsigned char)Clamp((float)color.b * brightness, 0.0f, 255.0f),
        color.a
    };
}

static bool GrowMeshVertexCapacity(Mesh *mesh, int populatedVertices,
                                   int requiredCapacity)
{
    int oldCapacity = mesh->vertexCount;
    int newCapacity = oldCapacity > 0 ? oldCapacity * 2 : 6;
    if (newCapacity < requiredCapacity) newCapacity = requiredCapacity;

    float *vertices = malloc((size_t)newCapacity * 3 * sizeof(float));
    float *normals = malloc((size_t)newCapacity * 3 * sizeof(float));
    float *texcoords = malloc((size_t)newCapacity * 2 * sizeof(float));
    float *texcoords2 = malloc((size_t)newCapacity * 2 * sizeof(float));
    unsigned char *colors = malloc((size_t)newCapacity * 4 * sizeof(unsigned char));
    if (!vertices || !normals || !texcoords || !texcoords2 || !colors) {
        free(vertices);
        free(normals);
        free(texcoords);
        free(texcoords2);
        free(colors);
        return false;
    }

    memcpy(vertices, mesh->vertices, (size_t)populatedVertices * 3 * sizeof(float));
    memcpy(normals, mesh->normals, (size_t)populatedVertices * 3 * sizeof(float));
    memcpy(texcoords, mesh->texcoords, (size_t)populatedVertices * 2 * sizeof(float));
    if (mesh->texcoords2) {
        memcpy(texcoords2, mesh->texcoords2,
               (size_t)populatedVertices * 2 * sizeof(float));
    } else {
        for (int vertex = 0; vertex < populatedVertices; vertex++) {
            texcoords2[vertex * 2] = 1.0f;
            texcoords2[vertex * 2 + 1] = 0.0f;
        }
    }
    memcpy(colors, mesh->colors, (size_t)populatedVertices * 4 * sizeof(unsigned char));
    free(mesh->vertices);
    free(mesh->normals);
    free(mesh->texcoords);
    free(mesh->texcoords2);
    free(mesh->colors);
    mesh->vertices = vertices;
    mesh->normals = normals;
    mesh->texcoords = texcoords;
    mesh->texcoords2 = texcoords2;
    mesh->colors = colors;
    mesh->vertexCount = newCapacity;
    mesh->triangleCount = newCapacity / 3;
    return true;
}

void AddMeshFaceLighting(ChunkMeshEmitter *emitter,
                         Vector3 corners[6], Vector3 normal,
                         Vector2 uvs[6], Color color,
                         const float ambientOcclusion[6],
                         float localLight)
{
    if (emitter->failed) return;
    if (emitter->vertexIndex > INT_MAX - 6) {
        emitter->failed = true;
        return;
    }
    int requiredCapacity = emitter->vertexIndex + 6;
    if (!emitter->mesh) {
        emitter->vertexIndex = requiredCapacity;
        return;
    }
    if (requiredCapacity > emitter->vertexCapacity) {
        if (!emitter->dynamicCapacity ||
            !GrowMeshVertexCapacity(emitter->mesh, emitter->vertexIndex,
                                    requiredCapacity)) {
            emitter->failed = true;
            return;
        }
        emitter->vertexCapacity = emitter->mesh->vertexCount;
    }

    for (int i = 0; i < 6; i++) {
        int vertex = emitter->vertexIndex + i;
        emitter->mesh->vertices[vertex * 3 + 0] = corners[i].x;
        emitter->mesh->vertices[vertex * 3 + 1] = corners[i].y;
        emitter->mesh->vertices[vertex * 3 + 2] = corners[i].z;
        emitter->mesh->normals[vertex * 3 + 0] = normal.x;
        emitter->mesh->normals[vertex * 3 + 1] = normal.y;
        emitter->mesh->normals[vertex * 3 + 2] = normal.z;
        emitter->mesh->texcoords[vertex * 2 + 0] = uvs[i].x;
        emitter->mesh->texcoords[vertex * 2 + 1] = uvs[i].y;
        if (emitter->mesh->texcoords2) {
            float ao = ambientOcclusion ? ambientOcclusion[i] : 1.0f;
            emitter->mesh->texcoords2[vertex * 2 + 0] = Clamp(ao, 0.0f, 1.0f);
            emitter->mesh->texcoords2[vertex * 2 + 1] =
                fmaxf(localLight, 0.0f);
        }
        emitter->mesh->colors[vertex * 4 + 0] = color.r;
        emitter->mesh->colors[vertex * 4 + 1] = color.g;
        emitter->mesh->colors[vertex * 4 + 2] = color.b;
        emitter->mesh->colors[vertex * 4 + 3] = color.a;
    }
    emitter->vertexIndex = requiredCapacity;
}

static void AddMeshFace(ChunkMeshEmitter *emitter, Vector3 corners[6],
                        Vector3 normal, Vector2 uvs[6], Color color)
{
    AddMeshFaceLighting(emitter, corners, normal, uvs, color, NULL, 0.0f);
}

void CountMeshFace(ChunkMeshEmitter *emitter)
{
    emitter->vertexIndex += 6;
}

void UnloadAllChunks(void)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        ChunkClearBlockStorage(&chunks[i]);
        chunks[i].loaded = false;
    }
}

static bool BlockOccludesAmbient(BlockType block)
{
    return block != BLOCK_AIR && block != BLOCK_SPACESHIP_OCCUPIED &&
           !IsTranslucentBlock(block);
}

static BlockType SnapshotBlockAt(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int worldX, int worldY, int worldZ)
{
    int lx = worldX - chunkX * CHUNK_SIZE;
    int lz = worldZ - chunkZ * CHUNK_SIZE;
    int localY = worldY - layerY;
    if (blocks && lx >= 0 && lx < CHUNK_SIZE && lz >= 0 &&
        lz < CHUNK_SIZE && localY >= 0 && localY < height) {
        return (BlockType)blocks[lx * height + localY][lz];
    }
    return GetBlockAt(worldX, worldY, worldZ);
}

static float BlockCornerAmbientOcclusion(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, int x, int y, int z, Vector3 normal,
    Vector3 corner)
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
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t1x, outsideY + t1y, outsideZ + t1z));
    bool side2 = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t2x, outsideY + t2y, outsideZ + t2z));
    bool diagonal = BlockOccludesAmbient(SnapshotBlockAt(
        blocks, height, layerY, chunkX, chunkZ,
        outsideX + t1x + t2x, outsideY + t1y + t2y,
        outsideZ + t1z + t2z));
    int occlusion = side1 && side2 ? 3 :
                    (int)side1 + (int)side2 + (int)diagonal;
    static const float factors[4] = { 1.0f, 0.84f, 0.66f, 0.48f };
    return factors[occlusion];
}

static void AddBlockFaceInternal(
    ChunkMeshEmitter *emitter, int x, int y, int z, int face,
    BlockType type, Color baseColor, float extraLight,
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool realtimeLighting)
{
    if (!emitter->mesh) {
        CountMeshFace(emitter);
        return;
    }
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float x1 = x0 + 1.0f;
    float y1 = y0 + 1.0f;
    float z1 = z0 + 1.0f;
    Vector3 normal = Vector3Zero();
    Vector3 corners[6] = { 0 };
    Vector2 uvs[6] = { 0 };
    float shade = 1.0f;

    switch (face) {
    case 0:
        normal = (Vector3){ 1.0f, 0.0f, 0.0f };
        shade = 0.82f;
        corners[0] = (Vector3){ x1, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = (Vector3){ x1, y0, z1 };
        corners[4] = (Vector3){ x1, y1, z0 };
        corners[5] = (Vector3){ x1, y1, z1 };
        break;
    case 1:
        normal = (Vector3){ -1.0f, 0.0f, 0.0f };
        shade = 0.72f;
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z1 };
        corners[2] = (Vector3){ x0, y1, z1 };
        corners[3] = (Vector3){ x0, y0, z0 };
        corners[4] = (Vector3){ x0, y1, z1 };
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 2:
        normal = (Vector3){ 0.0f, 1.0f, 0.0f };
        shade = 1.08f;
        corners[0] = (Vector3){ x0, y1, z1 };
        corners[1] = (Vector3){ x1, y1, z1 };
        corners[2] = (Vector3){ x1, y1, z0 };
        corners[3] = (Vector3){ x0, y1, z1 };
        corners[4] = (Vector3){ x1, y1, z0 };
        corners[5] = (Vector3){ x0, y1, z0 };
        break;
    case 3:
        normal = (Vector3){ 0.0f, -1.0f, 0.0f };
        shade = 0.56f;
        corners[0] = (Vector3){ x0, y0, z0 };
        corners[1] = (Vector3){ x1, y0, z0 };
        corners[2] = (Vector3){ x1, y0, z1 };
        corners[3] = (Vector3){ x0, y0, z0 };
        corners[4] = (Vector3){ x1, y0, z1 };
        corners[5] = (Vector3){ x0, y0, z1 };
        break;
    case 4:
        normal = (Vector3){ 0.0f, 0.0f, 1.0f };
        shade = 0.90f;
        corners[0] = (Vector3){ x0, y0, z1 };
        corners[1] = (Vector3){ x1, y0, z1 };
        corners[2] = (Vector3){ x1, y1, z1 };
        corners[3] = (Vector3){ x0, y0, z1 };
        corners[4] = (Vector3){ x1, y1, z1 };
        corners[5] = (Vector3){ x0, y1, z1 };
        break;
    default:
        normal = (Vector3){ 0.0f, 0.0f, -1.0f };
        shade = 0.66f;
        corners[0] = (Vector3){ x1, y0, z0 };
        corners[1] = (Vector3){ x0, y0, z0 };
        corners[2] = (Vector3){ x0, y1, z0 };
        corners[3] = (Vector3){ x1, y0, z0 };
        corners[4] = (Vector3){ x0, y1, z0 };
        corners[5] = (Vector3){ x1, y1, z0 };
        break;
    }

    AtlasUVs(TextureForBlockFace(type, face), uvs);
    if (realtimeLighting) {
        float ambientOcclusion[6];
        for (int i = 0; i < 3; i++) {
            ambientOcclusion[i] = BlockCornerAmbientOcclusion(
                blocks, height, layerY, chunkX, chunkZ,
                x, y, z, normal, corners[i]);
        }
        ambientOcclusion[3] = ambientOcclusion[0];
        ambientOcclusion[4] = ambientOcclusion[2];
        ambientOcclusion[5] = BlockCornerAmbientOcclusion(
            blocks, height, layerY, chunkX, chunkZ,
            x, y, z, normal, corners[5]);
        AddMeshFaceLighting(emitter, corners, normal, uvs, baseColor,
                            ambientOcclusion, extraLight);
    } else {
        float brightness = shade * (1.0f + extraLight);
        if (type == BLOCK_STAR_MATTER) brightness *= 2.1f;
        else if (type == BLOCK_LAVA || type == BLOCK_GLOWSTONE) brightness *= 1.8f;
        AddMeshFace(emitter, corners, normal, uvs,
                    ShadeColor(baseColor, brightness));
    }
}

void AddBlockFace(Mesh *mesh, int *vertexIndex, int x, int y, int z,
                  int face, BlockType type, Color baseColor, float extraLight)
{
    ChunkMeshEmitter emitter = {
        .mesh = mesh,
        .vertexIndex = *vertexIndex,
        .vertexCapacity = mesh->vertexCount,
        .dynamicCapacity = true
    };
    AddBlockFaceInternal(&emitter, x, y, z, face, type, baseColor,
                         extraLight, NULL, 0, 0, 0, 0, false);
    *vertexIndex = emitter.vertexIndex;
    if (emitter.failed) {
        mesh->vertexCount = -1;
        mesh->triangleCount = 0;
    }
}

static void AddTorchMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.62f;
    float w = 0.13f;
    float brightness = 1.0f + extraLight;

    Vector2 stickUvs[6];
    Rectangle stickRect = AtlasSourceRect(TEX_TORCH);
    float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    float u0 = (stickRect.x + 0.25f) / atlasWidth;
    float u1 = (stickRect.x + stickRect.width - 0.25f) / atlasWidth;
    float vTop = (stickRect.y + stickRect.height * 0.375f) / atlasHeight;
    float vBot = (stickRect.y + stickRect.height - 0.25f) / atlasHeight;
    stickUvs[0] = (Vector2){ u0, vBot };
    stickUvs[1] = (Vector2){ u1, vBot };
    stickUvs[2] = (Vector2){ u1, vTop };
    stickUvs[3] = (Vector2){ u0, vBot };
    stickUvs[4] = (Vector2){ u1, vTop };
    stickUvs[5] = (Vector2){ u0, vTop };

    Color stickColor = ShadeColor((Color){ 112, 74, 40, 255 }, brightness);
    Vector3 stickFaces[4][2] = {
        { { cx + w, y0, cz + w }, { cx - w, y0, cz + w } },
        { { cx + w, y0, cz - w }, { cx - w, y0, cz - w } },
        { { cx + w, y0, cz - w }, { cx + w, y0, cz + w } },
        { { cx - w, y0, cz + w }, { cx - w, y0, cz - w } }
    };
    Vector3 stickNormals[4] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }
    };
    for (int face = 0; face < 4; face++) {
        Vector3 a = stickFaces[face][0];
        Vector3 b = stickFaces[face][1];
        Vector3 corners[6] = {
            a, b, { b.x, y1, b.z },
            a, { b.x, y1, b.z }, { a.x, y1, a.z }
        };
        AddMeshFace(emitter, corners, stickNormals[face], stickUvs, stickColor);
    }

    float fy = (float)y + 0.80f;
    float hs = 0.15f;
    float flameU0 = u0;
    float flameU1 = u1;
    float flameV0 = (stickRect.y + 0.25f) / atlasHeight;
    float flameV1 = (stickRect.y + stickRect.height * 0.4375f) / atlasHeight;
    Vector2 flameUvs[6] = {
        { flameU0, flameV1 }, { flameU1, flameV1 }, { flameU1, flameV0 },
        { flameU0, flameV1 }, { flameU1, flameV0 }, { flameU0, flameV0 }
    };
    Color flameColor = ShadeColor((Color){ 255, 214, 128, 255 }, brightness);

    Vector3 flameCornersA[6] = {
        { cx - hs, fy - 0.12f, cz - hs }, { cx + hs, fy - 0.12f, cz + hs },
        { cx + hs, fy + 0.14f, cz + hs },
        { cx - hs, fy - 0.12f, cz - hs }, { cx + hs, fy + 0.14f, cz + hs },
        { cx - hs, fy + 0.14f, cz - hs }
    };
    Vector3 normalA = Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f });
    AddMeshFace(emitter, flameCornersA, normalA, flameUvs, flameColor);

    Vector3 flameCornersB[6] = {
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy - 0.12f, cz - hs },
        { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy + 0.14f, cz + hs }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(emitter, flameCornersB, normalB, flameUvs, flameColor);
}


static void AddAlbumMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.72f;
    float w = 0.22f;
    float t = 0.06f;
    float brightness = 1.0f + extraLight;

    Vector2 uvs[6];
    AtlasUVs(TEX_ALBUM, uvs);

    Vector3 faces[5][6] = {
        { { cx - w, y0, cz + t }, { cx + w, y0, cz + t }, { cx + w, y1, cz + t },
          { cx - w, y0, cz + t }, { cx + w, y1, cz + t }, { cx - w, y1, cz + t } },
        { { cx + w, y0, cz - t }, { cx - w, y0, cz - t }, { cx - w, y1, cz - t },
          { cx + w, y0, cz - t }, { cx - w, y1, cz - t }, { cx + w, y1, cz - t } },
        { { cx + w, y0, cz + t }, { cx + w, y0, cz - t }, { cx + w, y1, cz - t },
          { cx + w, y0, cz + t }, { cx + w, y1, cz - t }, { cx + w, y1, cz + t } },
        { { cx - w, y0, cz - t }, { cx - w, y0, cz + t }, { cx - w, y1, cz + t },
          { cx - w, y0, cz - t }, { cx - w, y1, cz + t }, { cx - w, y1, cz - t } },
        { { cx - w, y1, cz + t }, { cx + w, y1, cz + t }, { cx + w, y1, cz - t },
          { cx - w, y1, cz + t }, { cx + w, y1, cz - t }, { cx - w, y1, cz - t } }
    };
    Vector3 normals[5] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    float shades[5] = { 1.0f, 0.45f, 0.70f, 0.70f, 0.88f };

    for (int face = 0; face < 5; face++) {
        Color color = ShadeColor(WHITE, shades[face] * brightness);
        AddMeshFace(emitter, faces[face], normals[face], uvs, color);
    }
}

static void AddSpaceshipQuad(ChunkMeshEmitter *emitter,
                             Vector3 a, Vector3 b, Vector3 c, Vector3 d,
                             BlockTexture texture, Color color,
                             float brightness)
{
    if (!emitter->mesh) {
        CountMeshFace(emitter);
        return;
    }
    Vector3 normal = Vector3Normalize(Vector3CrossProduct(
        Vector3Subtract(b, a), Vector3Subtract(c, a)));
    float shade = 0.80f;
    if (normal.y > 0.5f) shade = 1.06f;
    else if (normal.y < -0.5f) shade = 0.58f;
    else if (normal.z > 0.5f) shade = 0.94f;
    else if (normal.z < -0.5f) shade = 0.70f;
    else shade = normal.x > 0.0f ? 0.86f : 0.76f;

    Vector3 corners[6] = { a, b, c, a, c, d };
    Vector2 uvs[6];
    AtlasUVs(texture, uvs);
    AddMeshFace(emitter, corners, normal, uvs,
                ShadeColor(color, shade * brightness));
}

static void AddSpaceshipBox(ChunkMeshEmitter *emitter,
                            Vector3 min, Vector3 max,
                            BlockTexture texture, Color color,
                            float brightness)
{
    AddSpaceshipQuad(
        emitter,
        (Vector3){ max.x, min.y, max.z }, (Vector3){ max.x, min.y, min.z },
        (Vector3){ max.x, max.y, min.z }, (Vector3){ max.x, max.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, min.z }, (Vector3){ min.x, min.y, max.z },
        (Vector3){ min.x, max.y, max.z }, (Vector3){ min.x, max.y, min.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, max.y, max.z }, (Vector3){ max.x, max.y, max.z },
        (Vector3){ max.x, max.y, min.z }, (Vector3){ min.x, max.y, min.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, min.z }, (Vector3){ max.x, min.y, min.z },
        (Vector3){ max.x, min.y, max.z }, (Vector3){ min.x, min.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ min.x, min.y, max.z }, (Vector3){ max.x, min.y, max.z },
        (Vector3){ max.x, max.y, max.z }, (Vector3){ min.x, max.y, max.z },
        texture, color, brightness);
    AddSpaceshipQuad(
        emitter,
        (Vector3){ max.x, min.y, min.z }, (Vector3){ min.x, min.y, min.z },
        (Vector3){ min.x, max.y, min.z }, (Vector3){ max.x, max.y, min.z },
        texture, color, brightness);
}

static void AddSpaceshipTaperedSection(
    ChunkMeshEmitter *emitter, float centerX,
    float rearZ, float rearY, float rearHalfWidth, float rearHalfHeight,
    float frontZ, float frontY, float frontHalfWidth, float frontHalfHeight,
    BlockTexture texture, Color color, float brightness)
{
    Vector3 rear[4] = {
        { centerX - rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { centerX + rearHalfWidth, rearY - rearHalfHeight, rearZ },
        { centerX + rearHalfWidth, rearY + rearHalfHeight, rearZ },
        { centerX - rearHalfWidth, rearY + rearHalfHeight, rearZ }
    };
    Vector3 front[4] = {
        { centerX - frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { centerX + frontHalfWidth, frontY - frontHalfHeight, frontZ },
        { centerX + frontHalfWidth, frontY + frontHalfHeight, frontZ },
        { centerX - frontHalfWidth, frontY + frontHalfHeight, frontZ }
    };
    AddSpaceshipQuad(emitter, front[0], front[1], front[2], front[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[1], rear[0], rear[3], rear[2],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, front[1], rear[1], rear[2], front[2],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[0], front[0], front[3], rear[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, front[3], front[2], rear[2], rear[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, rear[0], rear[1], front[1], front[0],
                     texture, color, brightness);
}

// Points wind clockwise from above so the top surface faces upward.
static void AddSpaceshipWing(ChunkMeshEmitter *emitter,
                             const Vector2 points[4], float bottomY, float topY,
                             BlockTexture texture, Color color,
                             float brightness)
{
    Vector3 bottom[4];
    Vector3 top[4];
    for (int point = 0; point < 4; point++) {
        bottom[point] = (Vector3){ points[point].x, bottomY, points[point].y };
        top[point] = (Vector3){ points[point].x, topY, points[point].y };
    }
    AddSpaceshipQuad(emitter, top[0], top[1], top[2], top[3],
                     texture, color, brightness);
    AddSpaceshipQuad(emitter, bottom[3], bottom[2], bottom[1], bottom[0],
                     texture, color, brightness);
    for (int point = 0; point < 4; point++) {
        int next = (point + 1) % 4;
        AddSpaceshipQuad(emitter,
                         bottom[point], bottom[next], top[next], top[point],
                         texture, color, brightness);
    }
}

static void AddSpaceshipMesh(ChunkMeshEmitter *emitter,
                             int x, int y, int z, BlockType type,
                             float extraLight)
{
    int firstVertex = emitter->vertexIndex;
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float cx = x0 + 0.5f;
    float brightness = 1.0f + extraLight;
    const Color hull = { 226, 232, 238, 255 };
    const Color hullDark = { 132, 145, 158, 255 };
    const Color canopy = { 132, 194, 232, 255 };
    const Vector2 leftWing[4] = {
        { x0 + 0.41f, z0 + 0.22f }, { x0 + 0.08f, z0 + 0.29f },
        { x0 + 0.04f, z0 + 0.43f }, { x0 + 0.41f, z0 + 0.60f }
    };
    const Vector2 rightWing[4] = {
        { x0 + 0.59f, z0 + 0.60f }, { x0 + 0.96f, z0 + 0.43f },
        { x0 + 0.92f, z0 + 0.29f }, { x0 + 0.59f, z0 + 0.22f }
    };

    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.18f, y0 + 0.36f, 0.15f, 0.16f,
        z0 + 0.68f, y0 + 0.37f, 0.12f, 0.14f,
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.68f, y0 + 0.37f, 0.12f, 0.14f,
        z0 + 0.93f, y0 + 0.34f, 0.02f, 0.03f,
        TEX_WHITE, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.42f, y0 + 0.15f, z0 + 0.22f },
        (Vector3){ x0 + 0.58f, y0 + 0.27f, z0 + 0.72f },
        TEX_GRAY, hullDark, brightness);
    AddSpaceshipTaperedSection(
        emitter, cx,
        z0 + 0.43f, y0 + 0.62f, 0.09f, 0.10f,
        z0 + 0.70f, y0 + 0.57f, 0.04f, 0.04f,
        TEX_GLASS, canopy, brightness);
    AddSpaceshipWing(emitter, leftWing, y0 + 0.32f, y0 + 0.39f,
                     TEX_SPACESHIP, hull, brightness);
    AddSpaceshipWing(emitter, rightWing, y0 + 0.32f, y0 + 0.39f,
                     TEX_SPACESHIP, hull, brightness);

    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.29f, y0 + 0.25f, z0 + 0.10f },
        (Vector3){ x0 + 0.39f, y0 + 0.49f, z0 + 0.32f },
        TEX_BLACK, (Color){ 155, 166, 178, 255 }, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.61f, y0 + 0.25f, z0 + 0.10f },
        (Vector3){ x0 + 0.71f, y0 + 0.49f, z0 + 0.32f },
        TEX_BLACK, (Color){ 155, 166, 178, 255 }, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.30f, y0 + 0.27f, z0 + 0.07f },
        (Vector3){ x0 + 0.38f, y0 + 0.46f, z0 + 0.11f },
        TEX_GLOWSTONE, (Color){ 255, 198, 112, 255 }, brightness * 1.55f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.27f, z0 + 0.07f },
        (Vector3){ x0 + 0.70f, y0 + 0.46f, z0 + 0.11f },
        TEX_GLOWSTONE, (Color){ 255, 198, 112, 255 }, brightness * 1.55f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.33f, y0 + 0.43f, z0 + 0.13f },
        (Vector3){ x0 + 0.38f, y0 + 0.78f, z0 + 0.25f },
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.43f, z0 + 0.13f },
        (Vector3){ x0 + 0.67f, y0 + 0.78f, z0 + 0.25f },
        TEX_SPACESHIP, hull, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.04f, y0 + 0.38f, z0 + 0.34f },
        (Vector3){ x0 + 0.09f, y0 + 0.47f, z0 + 0.40f },
        TEX_RED, WHITE, brightness * 1.25f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.91f, y0 + 0.38f, z0 + 0.34f },
        (Vector3){ x0 + 0.96f, y0 + 0.47f, z0 + 0.40f },
        TEX_GREEN, WHITE, brightness * 1.25f);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.36f, y0 + 0.37f, z0 + 0.38f },
        (Vector3){ x0 + 0.38f, y0 + 0.45f, z0 + 0.66f },
        TEX_ORANGE, WHITE, brightness);
    AddSpaceshipBox(
        emitter,
        (Vector3){ x0 + 0.62f, y0 + 0.37f, z0 + 0.38f },
        (Vector3){ x0 + 0.64f, y0 + 0.45f, z0 + 0.66f },
        TEX_ORANGE, WHITE, brightness);

    if (!emitter->mesh || type == BLOCK_SPACESHIP) return;

    int direction = (int)type - (int)BLOCK_SPACESHIP_CORE_NORTH;
    if (direction < 0 || direction > 3) direction = 0;
    float angle = (float)direction * PI * 0.5f;
    float sine = sinf(angle);
    float cosine = cosf(angle);
    float sourceCenterX = x0 + 0.5f;
    float sourceCenterZ = z0 + 0.5f;
    const float scaleX = 3.78f / 0.92f;
    const float scaleY = 1.10f / 0.63f;
    const float scaleZ = 3.90f / 0.86f;
    float targetCenterX = x0 + 1.0f;
    float targetCenterZ = z0 + 1.0f;
    for (int vertex = firstVertex; vertex < emitter->vertexIndex; vertex++) {
        Mesh *mesh = emitter->mesh;
        float localX = (mesh->vertices[vertex * 3] - sourceCenterX) * scaleX;
        float localZ = (mesh->vertices[vertex * 3 + 2] - sourceCenterZ) * scaleZ;
        mesh->vertices[vertex * 3] = targetCenterX + localX * cosine + localZ * sine;
        mesh->vertices[vertex * 3 + 1] =
            y0 + (mesh->vertices[vertex * 3 + 1] - (y0 + 0.15f)) * scaleY;
        mesh->vertices[vertex * 3 + 2] = targetCenterZ - localX * sine + localZ * cosine;

        Vector3 normal = {
            mesh->normals[vertex * 3] / scaleX,
            mesh->normals[vertex * 3 + 1] / scaleY,
            mesh->normals[vertex * 3 + 2] / scaleZ
        };
        normal = Vector3Normalize(normal);
        mesh->normals[vertex * 3] = normal.x * cosine + normal.z * sine;
        mesh->normals[vertex * 3 + 1] = normal.y;
        mesh->normals[vertex * 3 + 2] = -normal.x * sine + normal.z * cosine;
    }
}

static void AddSlabMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz,
                 const int faces[6][3], float extraLight)
{
    static const float shades[6] = { 0.82f, 0.72f, 1.08f, 0.56f, 0.90f, 0.66f };
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_STONE, uvs);

    for (int face = 0; face < 6; face++) {
        if (!ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) continue;

        int x = chunkX * CHUNK_SIZE + lx;
        int z = chunkZ * CHUNK_SIZE + lz;
        float x0 = (float)x;
        float y0 = (float)y;
        float z0 = (float)z;
        float x1 = x0 + 1.0f;
        float y1 = y0 + 0.5f;
        float z1 = z0 + 1.0f;
        Vector3 normal = Vector3Zero();
        Vector3 corners[6] = { 0 };
        Vector2 faceUvs[6] = { uvs[0], uvs[1], uvs[2], uvs[3], uvs[4], uvs[5] };

        switch (face) {
        case 0:
            normal = (Vector3){ 1.0f, 0.0f, 0.0f };
            corners[0] = (Vector3){ x1, y0, z1 }; corners[1] = (Vector3){ x1, y0, z0 };
            corners[2] = (Vector3){ x1, y1, z0 }; corners[3] = (Vector3){ x1, y0, z1 };
            corners[4] = (Vector3){ x1, y1, z0 }; corners[5] = (Vector3){ x1, y1, z1 };
            break;
        case 1:
            normal = (Vector3){ -1.0f, 0.0f, 0.0f };
            corners[0] = (Vector3){ x0, y0, z0 }; corners[1] = (Vector3){ x0, y0, z1 };
            corners[2] = (Vector3){ x0, y1, z1 }; corners[3] = (Vector3){ x0, y0, z0 };
            corners[4] = (Vector3){ x0, y1, z1 }; corners[5] = (Vector3){ x0, y1, z0 };
            break;
        case 2:
            normal = (Vector3){ 0.0f, 1.0f, 0.0f };
            corners[0] = (Vector3){ x0, y1, z1 }; corners[1] = (Vector3){ x1, y1, z1 };
            corners[2] = (Vector3){ x1, y1, z0 }; corners[3] = (Vector3){ x0, y1, z1 };
            corners[4] = (Vector3){ x1, y1, z0 }; corners[5] = (Vector3){ x0, y1, z0 };
            break;
        case 3:
            normal = (Vector3){ 0.0f, -1.0f, 0.0f };
            corners[0] = (Vector3){ x0, y0, z0 }; corners[1] = (Vector3){ x1, y0, z0 };
            corners[2] = (Vector3){ x1, y0, z1 }; corners[3] = (Vector3){ x0, y0, z0 };
            corners[4] = (Vector3){ x1, y0, z1 }; corners[5] = (Vector3){ x0, y0, z1 };
            break;
        case 4:
            normal = (Vector3){ 0.0f, 0.0f, 1.0f };
            corners[0] = (Vector3){ x0, y0, z1 }; corners[1] = (Vector3){ x1, y0, z1 };
            corners[2] = (Vector3){ x1, y1, z1 }; corners[3] = (Vector3){ x0, y0, z1 };
            corners[4] = (Vector3){ x1, y1, z1 }; corners[5] = (Vector3){ x0, y1, z1 };
            break;
        default:
            normal = (Vector3){ 0.0f, 0.0f, -1.0f };
            corners[0] = (Vector3){ x1, y0, z0 }; corners[1] = (Vector3){ x0, y0, z0 };
            corners[2] = (Vector3){ x0, y1, z0 }; corners[3] = (Vector3){ x1, y0, z0 };
            corners[4] = (Vector3){ x0, y1, z0 }; corners[5] = (Vector3){ x1, y1, z0 };
            break;
        }

        AddMeshFace(emitter, corners, normal, faceUvs,
                    ShadeColor(WHITE, shades[face] * brightness));
    }
}

static void AddDoorMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz,
                 const int faces[6][3], BlockType type, float extraLight)
{
    bool open = type == BLOCK_DOOR_OPEN;
    float brightness = 1.0f + extraLight;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 1.0f;
    float w = 0.44f;
    float t = 0.06f;

    Vector2 uvs[6];
    AtlasUVs(TEX_DOOR, uvs);

    static const int faceOrder[5] = { 4, 5, 0, 1, 2 };
    Vector3 normals[5] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f },
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f }
    };
    float shades[5] = { 1.0f, 0.55f, 0.75f, 0.75f, 0.90f };

    Vector3 faceCorners[5][6];
    if (!open) {
        faceCorners[0][0] = (Vector3){ cx - w, y0, cz + t }; faceCorners[0][1] = (Vector3){ cx + w, y0, cz + t };
        faceCorners[0][2] = (Vector3){ cx + w, y1, cz + t }; faceCorners[0][3] = (Vector3){ cx - w, y0, cz + t };
        faceCorners[0][4] = (Vector3){ cx + w, y1, cz + t }; faceCorners[0][5] = (Vector3){ cx - w, y1, cz + t };
        faceCorners[1][0] = (Vector3){ cx + w, y0, cz - t }; faceCorners[1][1] = (Vector3){ cx - w, y0, cz - t };
        faceCorners[1][2] = (Vector3){ cx - w, y1, cz - t }; faceCorners[1][3] = (Vector3){ cx + w, y0, cz - t };
        faceCorners[1][4] = (Vector3){ cx - w, y1, cz - t }; faceCorners[1][5] = (Vector3){ cx + w, y1, cz - t };
        faceCorners[2][0] = (Vector3){ cx + w, y0, cz + t }; faceCorners[2][1] = (Vector3){ cx + w, y0, cz - t };
        faceCorners[2][2] = (Vector3){ cx + w, y1, cz - t }; faceCorners[2][3] = (Vector3){ cx + w, y0, cz + t };
        faceCorners[2][4] = (Vector3){ cx + w, y1, cz - t }; faceCorners[2][5] = (Vector3){ cx + w, y1, cz + t };
        faceCorners[3][0] = (Vector3){ cx - w, y0, cz - t }; faceCorners[3][1] = (Vector3){ cx - w, y0, cz + t };
        faceCorners[3][2] = (Vector3){ cx - w, y1, cz + t }; faceCorners[3][3] = (Vector3){ cx - w, y0, cz - t };
        faceCorners[3][4] = (Vector3){ cx - w, y1, cz + t }; faceCorners[3][5] = (Vector3){ cx - w, y1, cz - t };
    } else {
        faceCorners[0][0] = (Vector3){ cx - t, y0, cz + w }; faceCorners[0][1] = (Vector3){ cx + t, y0, cz + w };
        faceCorners[0][2] = (Vector3){ cx + t, y1, cz + w }; faceCorners[0][3] = (Vector3){ cx - t, y0, cz + w };
        faceCorners[0][4] = (Vector3){ cx + t, y1, cz + w }; faceCorners[0][5] = (Vector3){ cx - t, y1, cz + w };
        faceCorners[1][0] = (Vector3){ cx + t, y0, cz - w }; faceCorners[1][1] = (Vector3){ cx - t, y0, cz - w };
        faceCorners[1][2] = (Vector3){ cx - t, y1, cz - w }; faceCorners[1][3] = (Vector3){ cx + t, y0, cz - w };
        faceCorners[1][4] = (Vector3){ cx - t, y1, cz - w }; faceCorners[1][5] = (Vector3){ cx + t, y1, cz - w };
        faceCorners[2][0] = (Vector3){ cx + t, y0, cz + w }; faceCorners[2][1] = (Vector3){ cx + t, y0, cz - w };
        faceCorners[2][2] = (Vector3){ cx + t, y1, cz - w }; faceCorners[2][3] = (Vector3){ cx + t, y0, cz + w };
        faceCorners[2][4] = (Vector3){ cx + t, y1, cz - w }; faceCorners[2][5] = (Vector3){ cx + t, y1, cz + w };
        faceCorners[3][0] = (Vector3){ cx - t, y0, cz - w }; faceCorners[3][1] = (Vector3){ cx - t, y0, cz + w };
        faceCorners[3][2] = (Vector3){ cx - t, y1, cz + w }; faceCorners[3][3] = (Vector3){ cx - t, y0, cz - w };
        faceCorners[3][4] = (Vector3){ cx - t, y1, cz + w }; faceCorners[3][5] = (Vector3){ cx - t, y1, cz - w };
    }
    faceCorners[4][0] = (Vector3){ cx - w, y1, cz + t }; faceCorners[4][1] = (Vector3){ cx + w, y1, cz + t };
    faceCorners[4][2] = (Vector3){ cx + w, y1, cz - t }; faceCorners[4][3] = (Vector3){ cx - w, y1, cz + t };
    faceCorners[4][4] = (Vector3){ cx + w, y1, cz - t }; faceCorners[4][5] = (Vector3){ cx - w, y1, cz - t };

    for (int f = 0; f < 5; f++) {
        int face = faceOrder[f];
        if (!ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) continue;
        Color color = ShadeColor(WHITE, shades[f] * brightness);
        AddMeshFace(emitter, faceCorners[f], normals[f], uvs, color);
    }
}

static void AddStairsMesh(ChunkMeshEmitter *emitter,
                   const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   int lx, int y, int lz, BlockType type, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    float x0 = (float)(chunkX * CHUNK_SIZE + lx);
    float z0 = (float)(chunkZ * CHUNK_SIZE + lz);
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs((type == BLOCK_WOOD_STAIRS) ? TEX_PLANK : TEX_STONE, uvs);

    for (int step = 0; step < 3; step++) {
        float zLow = z0 + (float)step / 3.0f;
        float zHigh = z0 + (float)(step + 1) / 3.0f;
        float yHigh = y0 + (float)(step + 1) / 3.0f;

        Vector3 top[6] = {
            { x0, yHigh, zHigh }, { x0 + 1.0f, yHigh, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0, yHigh, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0, yHigh, zLow }
        };
        AddMeshFace(emitter, top, (Vector3){ 0.0f, 1.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 1.08f * brightness));

        Vector3 front[6] = {
            { x0, y0, zHigh }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, yHigh, zHigh }
        };
        AddMeshFace(emitter, front, (Vector3){ 0.0f, 0.0f, 1.0f }, uvs,
                    ShadeColor(WHITE, 0.90f * brightness));

        Vector3 sideA[6] = {
            { x0, y0, zLow }, { x0, y0, zHigh },
            { x0, yHigh, zHigh }, { x0, y0, zLow },
            { x0, yHigh, zHigh }, { x0, yHigh, zLow }
        };
        AddMeshFace(emitter, sideA, (Vector3){ -1.0f, 0.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 0.72f * brightness));

        Vector3 sideB[6] = {
            { x0 + 1.0f, y0, zHigh }, { x0 + 1.0f, y0, zLow },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, yHigh, zHigh }
        };
        AddMeshFace(emitter, sideB, (Vector3){ 1.0f, 0.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 0.82f * brightness));
    }
}

static BlockType FenceNeighborBlock(const unsigned short (*blocks)[CHUNK_SIZE],
                                    int height, int layerY, int chunkX, int chunkZ,
                                    int lx, int y, int lz, int nx, int nz)
{
    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE && y >= 0 && y < height) {
        return (BlockType)blocks[neighborLx * height + y][neighborLz];
    }
    return GetBlockAt(chunkX * CHUNK_SIZE + lx + nx, layerY + y, chunkZ * CHUNK_SIZE + lz + nz);
}

static bool FenceShouldConnect(BlockType type)
{
    return type == BLOCK_FENCE || type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN;
}

static void AddFenceMesh(ChunkMeshEmitter *emitter,
                  const unsigned short (*blocks)[CHUNK_SIZE],
                  int height, int layerY, int chunkX, int chunkZ,
                  int lx, int y, int lz, float extraLight)
{
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_FENCE, uvs);

    static const int dirs[4][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
    for (int d = 0; d < 4; d++) {
        BlockType neighbor = FenceNeighborBlock(blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                                                dirs[d][0], dirs[d][2]);
        if (neighbor != BLOCK_AIR && !FenceShouldConnect(neighbor)) continue;

        float px = cx + (float)dirs[d][0] * 0.5f;
        float pz = cz + (float)dirs[d][2] * 0.5f;
        Vector3 corners[6] = {
            { px - 0.06f, y0 + 0.55f, pz - 0.06f }, { px + 0.06f, y0 + 0.55f, pz + 0.06f },
            { px + 0.06f, y0 + 0.95f, pz + 0.06f }, { px - 0.06f, y0 + 0.55f, pz - 0.06f },
            { px + 0.06f, y0 + 0.95f, pz + 0.06f }, { px - 0.06f, y0 + 0.95f, pz - 0.06f }
        };
        if (dirs[d][0] != 0) {
            for (int i = 0; i < 6; i++) {
                float tmp = corners[i].x;
                corners[i].x = corners[i].z;
                corners[i].z = tmp;
            }
        }
        Vector3 normal = { (float)dirs[d][0], 0.0f, (float)dirs[d][2] };
        AddMeshFace(emitter, corners, normal, uvs,
                    ShadeColor(WHITE, 0.85f * brightness));
    }

    Vector3 postCorners[6] = {
        { cx - 0.06f, y0, cz - 0.06f }, { cx + 0.06f, y0, cz + 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0, cz - 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0 + 1.0f, cz - 0.06f }
    };
    AddMeshFace(emitter, postCorners, (Vector3){ 0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 1.0f * brightness));
    AddMeshFace(emitter, postCorners, (Vector3){ -0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 0.85f * brightness));
}

static void AddGateMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz, bool open, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    (void)chunkZ;
    (void)lz;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.9f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_FENCE, uvs);

    float w = 0.36f;
    float t = 0.05f;
    Vector3 faces[3][6];
    if (!open) {
        faces[0][0] = (Vector3){ cx - w, y0, cz + t }; faces[0][1] = (Vector3){ cx + w, y0, cz + t };
        faces[0][2] = (Vector3){ cx + w, y1, cz + t }; faces[0][3] = (Vector3){ cx - w, y0, cz + t };
        faces[0][4] = (Vector3){ cx + w, y1, cz + t }; faces[0][5] = (Vector3){ cx - w, y1, cz + t };
        faces[1][0] = (Vector3){ cx + w, y0, cz - t }; faces[1][1] = (Vector3){ cx - w, y0, cz - t };
        faces[1][2] = (Vector3){ cx - w, y1, cz - t }; faces[1][3] = (Vector3){ cx + w, y0, cz - t };
        faces[1][4] = (Vector3){ cx - w, y1, cz - t }; faces[1][5] = (Vector3){ cx + w, y1, cz - t };
        faces[2][0] = (Vector3){ cx - w, y1, cz + t }; faces[2][1] = (Vector3){ cx + w, y1, cz + t };
        faces[2][2] = (Vector3){ cx + w, y1, cz - t }; faces[2][3] = (Vector3){ cx - w, y1, cz + t };
        faces[2][4] = (Vector3){ cx + w, y1, cz - t }; faces[2][5] = (Vector3){ cx - w, y1, cz - t };
    } else {
        faces[0][0] = (Vector3){ cx + t, y0, cz - w }; faces[0][1] = (Vector3){ cx + t, y0, cz + w };
        faces[0][2] = (Vector3){ cx + t, y1, cz + w }; faces[0][3] = (Vector3){ cx + t, y0, cz - w };
        faces[0][4] = (Vector3){ cx + t, y1, cz + w }; faces[0][5] = (Vector3){ cx + t, y1, cz - w };
        faces[1][0] = (Vector3){ cx - t, y0, cz + w }; faces[1][1] = (Vector3){ cx - t, y0, cz - w };
        faces[1][2] = (Vector3){ cx - t, y1, cz - w }; faces[1][3] = (Vector3){ cx - t, y0, cz + w };
        faces[1][4] = (Vector3){ cx - t, y1, cz - w }; faces[1][5] = (Vector3){ cx - t, y1, cz + w };
        faces[2][0] = (Vector3){ cx - t, y1, cz - w }; faces[2][1] = (Vector3){ cx + t, y1, cz - w };
        faces[2][2] = (Vector3){ cx + t, y1, cz + w }; faces[2][3] = (Vector3){ cx - t, y1, cz - w };
        faces[2][4] = (Vector3){ cx + t, y1, cz + w }; faces[2][5] = (Vector3){ cx - t, y1, cz + w };
    }
    Vector3 normals[3] = {
        { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }
    };
    float shades[3] = { 1.0f, 0.55f, 0.90f };
    for (int f = 0; f < 3; f++) {
        AddMeshFace(emitter, faces[f], normals[f], uvs,
                    ShadeColor(WHITE, shades[f] * brightness));
    }
}

static void AddPaneMesh(ChunkMeshEmitter *emitter,
                 const unsigned short (*blocks)[CHUNK_SIZE],
                 int height, int layerY, int chunkX, int chunkZ,
                 int lx, int y, int lz, float extraLight)
{
    (void)blocks;
    (void)height;
    (void)layerY;
    float cx = (float)(chunkX * CHUNK_SIZE + lx) + 0.5f;
    float cz = (float)(chunkZ * CHUNK_SIZE + lz) + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 1.0f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TEX_GLASS, uvs);

    float zNear = cz - 0.45f;
    float zFar = cz + 0.45f;
    Vector3 faces[5][6];
    faces[0][0] = (Vector3){ cx + 0.07f, y0, zFar }; faces[0][1] = (Vector3){ cx + 0.07f, y0, zNear };
    faces[0][2] = (Vector3){ cx + 0.07f, y1, zNear }; faces[0][3] = (Vector3){ cx + 0.07f, y0, zFar };
    faces[0][4] = (Vector3){ cx + 0.07f, y1, zNear }; faces[0][5] = (Vector3){ cx + 0.07f, y1, zFar };
    faces[1][0] = (Vector3){ cx - 0.07f, y0, zNear }; faces[1][1] = (Vector3){ cx - 0.07f, y0, zFar };
    faces[1][2] = (Vector3){ cx - 0.07f, y1, zFar }; faces[1][3] = (Vector3){ cx - 0.07f, y0, zNear };
    faces[1][4] = (Vector3){ cx - 0.07f, y1, zFar }; faces[1][5] = (Vector3){ cx - 0.07f, y1, zNear };
    faces[2][0] = (Vector3){ cx + 0.07f, y1, zFar }; faces[2][1] = (Vector3){ cx - 0.07f, y1, zFar };
    faces[2][2] = (Vector3){ cx - 0.07f, y1, zNear }; faces[2][3] = (Vector3){ cx + 0.07f, y1, zFar };
    faces[2][4] = (Vector3){ cx - 0.07f, y1, zNear }; faces[2][5] = (Vector3){ cx + 0.07f, y1, zNear };
    faces[3][0] = (Vector3){ cx + 0.07f, y0, zNear }; faces[3][1] = (Vector3){ cx - 0.07f, y0, zNear };
    faces[3][2] = (Vector3){ cx - 0.07f, y1, zNear }; faces[3][3] = (Vector3){ cx + 0.07f, y0, zNear };
    faces[3][4] = (Vector3){ cx - 0.07f, y1, zNear }; faces[3][5] = (Vector3){ cx + 0.07f, y1, zNear };
    faces[4][0] = (Vector3){ cx - 0.07f, y0, zFar }; faces[4][1] = (Vector3){ cx + 0.07f, y0, zFar };
    faces[4][2] = (Vector3){ cx + 0.07f, y1, zFar }; faces[4][3] = (Vector3){ cx - 0.07f, y0, zFar };
    faces[4][4] = (Vector3){ cx + 0.07f, y1, zFar }; faces[4][5] = (Vector3){ cx - 0.07f, y1, zFar };

    Vector3 normals[5] = {
        { 1.0f, 0.0f, 0.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f },
        { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }
    };
    float shades[5] = { 0.85f, 0.75f, 1.0f, 0.80f, 0.85f };
    for (int f = 0; f < 5; f++) {
        AddMeshFace(emitter, faces[f], normals[f], uvs,
                    ShadeColor(WHITE, shades[f] * brightness));
    }
}

static void AddPlantMesh(ChunkMeshEmitter *emitter, int x, int y, int z,
                         BlockType type, float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs(TextureForBlockFace(type, 0), uvs);

    if (BlockRenderShapeFor(type) == BLOCK_RENDER_CARPET) {
        float inset = 0.035f;
        float top = y0 + 0.035f;
        Vector3 topFace[6] = {
            { (float)x + inset, top, (float)z + inset },
            { (float)x + 1.0f - inset, top, (float)z + inset },
            { (float)x + 1.0f - inset, top, (float)z + 1.0f - inset },
            { (float)x + inset, top, (float)z + inset },
            { (float)x + 1.0f - inset, top, (float)z + 1.0f - inset },
            { (float)x + inset, top, (float)z + 1.0f - inset }
        };
        Vector3 bottomFace[6] = {
            topFace[5], topFace[4], topFace[3],
            topFace[2], topFace[1], topFace[0]
        };
        AddMeshFace(emitter, topFace, (Vector3){ 0.0f, 1.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, brightness));
        AddMeshFace(emitter, bottomFace, (Vector3){ 0.0f, -1.0f, 0.0f },
                    uvs, ShadeColor(WHITE, 0.72f * brightness));
        return;
    }

    float plantHeight = 0.4f;
    float halfWidth = 0.16f;
    if (type == BLOCK_TALL_GRASS) {
        plantHeight = 0.78f;
        halfWidth = 0.25f;
    } else if (type == BLOCK_FERN) {
        plantHeight = 0.62f;
        halfWidth = 0.28f;
    } else if (type == BLOCK_REED) {
        plantHeight = 0.96f;
        halfWidth = 0.20f;
    } else if (type == BLOCK_LICHEN) {
        plantHeight = 0.30f;
        halfWidth = 0.24f;
    }
    float y1 = y0 + plantHeight;

    Vector3 quadA[6] = {
        { cx - halfWidth, y0, cz - halfWidth },
        { cx + halfWidth, y0, cz + halfWidth },
        { cx + halfWidth, y1, cz + halfWidth },
        { cx - halfWidth, y0, cz - halfWidth },
        { cx + halfWidth, y1, cz + halfWidth },
        { cx - halfWidth, y1, cz - halfWidth }
    };
    Vector3 normalA = Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f });
    AddMeshFace(emitter, quadA, normalA, uvs,
                ShadeColor(WHITE, 0.95f * brightness));

    Vector3 quadB[6] = {
        { cx - halfWidth, y0, cz + halfWidth },
        { cx + halfWidth, y0, cz - halfWidth },
        { cx + halfWidth, y1, cz - halfWidth },
        { cx - halfWidth, y0, cz + halfWidth },
        { cx + halfWidth, y1, cz - halfWidth },
        { cx - halfWidth, y1, cz + halfWidth }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(emitter, quadB, normalB, uvs,
                ShadeColor(WHITE, 0.85f * brightness));
}
bool ChunkBlockHasTransparentMesh(BlockType type)
{
    return IsTranslucentBlock(type);
}

typedef struct ChunkMeshBuildContext {
    const unsigned short (*blocks)[CHUNK_SIZE];
    int height;
    int layerY;
    int chunkX;
    int chunkZ;
    bool transparent;
    bool includePlants;
    bool plantsOnly;
    bool excludeWater;
    const int (*faces)[3];
    const int *nearbyTorchIndices;
    int nearbyTorchCount;
} ChunkMeshBuildContext;

static void EmitChunkBlocksFiltered(const ChunkMeshBuildContext *context,
                                    ChunkMeshEmitter *emitter)
{
    const unsigned short (*blocks)[CHUNK_SIZE] = context->blocks;
    int height = context->height;
    int layerY = context->layerY;
    int chunkX = context->chunkX;
    int chunkZ = context->chunkZ;
    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;
    const int (*faces)[3] = context->faces;
    bool transparent = context->transparent;
    bool includePlants = context->includePlants;
    bool plantsOnly = context->plantsOnly;
    bool excludeWater = context->excludeWater;
    bool counting = emitter->mesh == NULL;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < height; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                if (emitter->failed) return;
                BlockType type = (BlockType)blocks[lx * height + y][lz];
                bool plant = IsPlantBlock(type);
                if (plantsOnly) {
                    if (!plant) continue;
                } else {
                    if (type == BLOCK_AIR ||
                        ChunkBlockHasTransparentMesh(type) !=
                            transparent) continue;
                    if (excludeWater && type == BLOCK_WATER) continue;
                    if (plant && !includePlants) continue;
                }

                int x = lx;
                int z = lz;
                float blockLight = 0.0f;
                if (!counting) {
                    x += startX;
                    z += startZ;
                    blockLight = TorchLightAtBlockNearby(
                        x, layerY + y, z, context->nearbyTorchIndices,
                        context->nearbyTorchCount);
                }
                if (type == BLOCK_TORCH) {
                    AddTorchMesh(emitter, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_ALBUM) {
                    AddAlbumMesh(emitter, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_SPACESHIP ||
                    (type >= BLOCK_SPACESHIP_CORE_NORTH &&
                     type <= BLOCK_SPACESHIP_CORE_WEST)) {
                    AddSpaceshipMesh(emitter, x, y, z, type, blockLight);
                    continue;
                }
                if (type == BLOCK_SPACESHIP_OCCUPIED) continue;
                if (type == BLOCK_SLAB) {
                    AddSlabMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, faces, blockLight);
                    continue;
                }
                if (type == BLOCK_DOOR || type == BLOCK_DOOR_OPEN) {
                    AddDoorMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, faces, type, blockLight);
                    continue;
                }
                if (type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) {
                    AddStairsMesh(emitter, blocks, height, layerY, chunkX,
                                  chunkZ, lx, y, lz, type, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE) {
                    AddFenceMesh(emitter, blocks, height, layerY, chunkX,
                                 chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN) {
                    AddGateMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz,
                                type == BLOCK_FENCE_GATE_OPEN, blockLight);
                    continue;
                }
                if (type == BLOCK_GLASS_PANE) {
                    AddPaneMesh(emitter, blocks, height, layerY, chunkX,
                                chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (plant) {
                    AddPlantMesh(emitter, x, y, z, type, blockLight);
                    continue;
                }
                for (int face = 0; face < 6; face++) {
                    bool visible = transparent ?
                        ChunkTransparentFaceIsVisible(
                            blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                            faces[face][0], faces[face][1], faces[face][2],
                            type) :
                        ChunkFaceIsVisible(
                            blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                            faces[face][0], faces[face][1], faces[face][2]);
                    if (visible) {
                        if (!counting) {
                            AddBlockFaceInternal(
                                emitter, x, y, z, face, type, WHITE,
                                blockLight, blocks, height, layerY, chunkX,
                                chunkZ, true);
                        } else {
                            CountMeshFace(emitter);
                        }
                    }
                }
            }
        }
    }
}

bool BuildMeshDataFiltered(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, bool excludeWater, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    ChunkMeshBuildContext context = {
        .blocks = blocks,
        .height = height,
        .layerY = layerY,
        .chunkX = chunkX,
        .chunkZ = chunkZ,
        .transparent = transparent,
        .includePlants = includePlants,
        .plantsOnly = plantsOnly,
        .excludeWater = excludeWater,
        .faces = faces,
        .nearbyTorchIndices = nearbyTorchIndices,
        .nearbyTorchCount = nearbyTorchCount
    };
    ChunkMeshEmitter counter = { 0 };
    EmitChunkBlocksFiltered(&context, &counter);
    if (counter.failed || counter.vertexIndex == 0) return false;

    Mesh mesh = { 0 };
    mesh.vertexCount = counter.vertexIndex;
    mesh.triangleCount = counter.vertexIndex / 3;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.texcoords2 = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));

    if (!mesh.vertices || !mesh.texcoords || !mesh.texcoords2 ||
        !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.texcoords2);
        free(mesh.normals);
        free(mesh.colors);
        return false;
    }

    ChunkMeshEmitter writer = {
        .mesh = &mesh,
        .vertexCapacity = mesh.vertexCount
    };
    EmitChunkBlocksFiltered(&context, &writer);
    if (writer.failed || writer.vertexIndex != counter.vertexIndex) {
        FreeMeshData(&mesh);
        return false;
    }
    *outMesh = mesh;
    return true;
}

bool BuildMeshData(const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   bool transparent, const int faces[6][3],
                   const int *nearbyTorchIndices, int nearbyTorchCount,
                   Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, transparent, true, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildSurfaceSolidMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}
