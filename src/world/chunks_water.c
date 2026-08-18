#include "world/chunks_internal.h"

static unsigned char SurfaceWaterSnapshotVolume(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int lx, int y, int lz)
{
    BlockType block = (BlockType)blocks[lx * height + y][lz];
    if (block != BLOCK_WATER) return 0u;
    if (!waterVolumes) return (unsigned char)WATER_VOLUME_CAPACITY;
    return waterVolumes[(lx * height + y) * CHUNK_SIZE + lz];
}

static bool TrySampleSurfaceWaterCell(
    int worldX, int worldY, int worldZ, BlockType *outBlock,
    unsigned char *outVolume)
{
    if (!outBlock || !outVolume) return false;
    if (!InHeight(worldY)) {
        *outBlock = BLOCK_AIR;
        *outVolume = 0u;
        return true;
    }

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(worldX, worldZ, &cx, &cz, &lx, &lz);
    const Chunk *chunk = FindChunk(cx, cz);
    if (!chunk) return false;

    int sectionY = SurfaceSectionYFromBlockY(worldY);
    BlockType block = BLOCK_AIR;
    if (!ChunkTryGetLocalBlock(chunk, lx, worldY, lz, &block)) {
        if (!ChunkTerrainSectionIsResolved(chunk, sectionY)) return false;
    }

    *outBlock = block;
    *outVolume = 0u;
    if (block != BLOCK_WATER) return true;

    const ChunkSection *section = ChunkGetSectionConst(chunk, sectionY);
    int index = (lx * SURFACE_SECTION_HEIGHT +
                 SurfaceSectionLocalYFromBlockY(worldY)) * CHUNK_SIZE + lz;
    *outVolume = section && section->waterVolumes
        ? section->waterVolumes[index] : (unsigned char)WATER_VOLUME_CAPACITY;
    return true;
}

static void CaptureSurfaceBoundaryCell(
    int worldX, int worldY, int worldZ, unsigned short *outBlock,
    unsigned char *outVolume, unsigned char *outKnown)
{
    BlockType block = BLOCK_AIR;
    unsigned char volume = 0u;
    if (!TrySampleSurfaceWaterCell(
            worldX, worldY, worldZ, &block, &volume)) {
        *outBlock = (unsigned short)GetBlockAt(worldX, worldY, worldZ);
        *outVolume = 0u;
        *outKnown = 0u;
        return;
    }
    *outBlock = (unsigned short)block;
    *outVolume = volume;
    *outKnown = 1u;
}

void CaptureSurfaceBoundary(SurfaceBoundarySnapshot *snapshot,
                            int chunkX, int chunkZ, int sectionY)
{
    memset(snapshot, 0, sizeof(*snapshot));
    int baseX = chunkX * CHUNK_SIZE;
    int baseY = sectionY * SURFACE_SECTION_HEIGHT;
    int baseZ = chunkZ * CHUNK_SIZE;

    for (int px = 0; px < CHUNK_SIZE + 2; px++) {
        for (int py = 0; py < SURFACE_SECTION_HEIGHT + 2; py++) {
            for (int pz = 0; pz < CHUNK_SIZE + 2; pz++) {
                bool boundary = px == 0 || px == CHUNK_SIZE + 1 ||
                                py == 0 ||
                                py == SURFACE_SECTION_HEIGHT + 1 ||
                                pz == 0 || pz == CHUNK_SIZE + 1;
                if (!boundary) continue;
                CaptureSurfaceBoundaryCell(
                    baseX + px - 1, baseY + py - 1, baseZ + pz - 1,
                    &snapshot->blocks[px][py][pz],
                    &snapshot->volumes[px][py][pz],
                    &snapshot->known[px][py][pz]);
            }
        }
    }
}

bool SurfaceBoundaryBlockAt(const SurfaceBoundarySnapshot *snapshot,
                            int lx, int y, int lz, BlockType *outBlock)
{
    if (!snapshot || !outBlock || lx < -1 || lx > CHUNK_SIZE ||
        y < -1 || y > SURFACE_SECTION_HEIGHT ||
        lz < -1 || lz > CHUNK_SIZE) return false;
    bool outside = lx < 0 || lx >= CHUNK_SIZE || y < 0 ||
                   y >= SURFACE_SECTION_HEIGHT || lz < 0 ||
                   lz >= CHUNK_SIZE;
    if (!outside) return false;
    *outBlock = (BlockType)snapshot->blocks[lx + 1][y + 1][lz + 1];
    return true;
}

bool SurfaceBoundaryCellAt(const SurfaceBoundarySnapshot *snapshot,
                           int lx, int y, int lz, BlockType *outBlock,
                           unsigned char *outVolume)
{
    if (!outVolume ||
        !SurfaceBoundaryBlockAt(snapshot, lx, y, lz, outBlock)) return false;
    if (!snapshot->known[lx + 1][y + 1][lz + 1]) return false;
    *outVolume = snapshot->volumes[lx + 1][y + 1][lz + 1];
    return true;
}

static bool SurfaceWaterNeighbor(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes,
    const SurfaceBoundarySnapshot *boundary, int height, int layerY,
    int chunkX, int chunkZ, int lx, int y, int lz,
    int nx, int ny, int nz, BlockType *outBlock,
    unsigned char *outVolume)
{
    if (!outBlock || !outVolume) return false;
    int neighborY = y + ny;
    int worldY = layerY + neighborY;
    if (!InHeight(worldY)) {
        *outBlock = BLOCK_AIR;
        *outVolume = 0u;
        return true;
    }

    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborY >= 0 && neighborY < height &&
        neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        *outBlock = (BlockType)blocks[
            neighborLx * height + neighborY][neighborLz];
        *outVolume = SurfaceWaterSnapshotVolume(
            blocks, waterVolumes, height, neighborLx, neighborY, neighborLz);
        return true;
    }

    if (boundary) {
        return SurfaceBoundaryCellAt(boundary, neighborLx, neighborY,
                                     neighborLz, outBlock, outVolume);
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    return TrySampleSurfaceWaterCell(
        wx, worldY, wz, outBlock, outVolume);
}

static void AddSurfaceWaterFace(ChunkMeshEmitter *emitter, int x, int y,
                                int z, int face, float low, float high,
                                float blockLight)
{
    float x0 = (float)x;
    float y0 = (float)y;
    float z0 = (float)z;
    float x1 = x0 + 1.0f;
    float z1 = z0 + 1.0f;
    float lowY = y0 + low;
    float highY = y0 + high;
    Vector3 normal = Vector3Zero();
    Vector3 corners[6] = { 0 };
    static const float shades[6] = {
        0.82f, 0.72f, 1.08f, 0.56f, 0.90f, 0.66f
    };
    switch (face) {
    case 0:
        normal = (Vector3){ 1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x1, lowY, z1 };
        corners[1] = (Vector3){ x1, lowY, z0 };
        corners[2] = (Vector3){ x1, highY, z0 };
        corners[3] = (Vector3){ x1, lowY, z1 };
        corners[4] = (Vector3){ x1, highY, z0 };
        corners[5] = (Vector3){ x1, highY, z1 };
        break;
    case 1:
        normal = (Vector3){ -1.0f, 0.0f, 0.0f };
        corners[0] = (Vector3){ x0, lowY, z0 };
        corners[1] = (Vector3){ x0, lowY, z1 };
        corners[2] = (Vector3){ x0, highY, z1 };
        corners[3] = (Vector3){ x0, lowY, z0 };
        corners[4] = (Vector3){ x0, highY, z1 };
        corners[5] = (Vector3){ x0, highY, z0 };
        break;
    case 2:
        normal = (Vector3){ 0.0f, 1.0f, 0.0f };
        corners[0] = (Vector3){ x0, highY, z1 };
        corners[1] = (Vector3){ x1, highY, z1 };
        corners[2] = (Vector3){ x1, highY, z0 };
        corners[3] = (Vector3){ x0, highY, z1 };
        corners[4] = (Vector3){ x1, highY, z0 };
        corners[5] = (Vector3){ x0, highY, z0 };
        break;
    case 3:
        normal = (Vector3){ 0.0f, -1.0f, 0.0f };
        corners[0] = (Vector3){ x0, lowY, z0 };
        corners[1] = (Vector3){ x1, lowY, z0 };
        corners[2] = (Vector3){ x1, lowY, z1 };
        corners[3] = (Vector3){ x0, lowY, z0 };
        corners[4] = (Vector3){ x1, lowY, z1 };
        corners[5] = (Vector3){ x0, lowY, z1 };
        break;
    case 4:
        normal = (Vector3){ 0.0f, 0.0f, 1.0f };
        corners[0] = (Vector3){ x0, lowY, z1 };
        corners[1] = (Vector3){ x1, lowY, z1 };
        corners[2] = (Vector3){ x1, highY, z1 };
        corners[3] = (Vector3){ x0, lowY, z1 };
        corners[4] = (Vector3){ x1, highY, z1 };
        corners[5] = (Vector3){ x0, highY, z1 };
        break;
    default:
        normal = (Vector3){ 0.0f, 0.0f, -1.0f };
        corners[0] = (Vector3){ x1, lowY, z0 };
        corners[1] = (Vector3){ x0, lowY, z0 };
        corners[2] = (Vector3){ x0, highY, z0 };
        corners[3] = (Vector3){ x1, lowY, z0 };
        corners[4] = (Vector3){ x0, highY, z0 };
        corners[5] = (Vector3){ x1, highY, z0 };
        break;
    }
    Vector2 uvs[6];
    AtlasUVs(TextureForBlockFace(BLOCK_WATER, face), uvs);
    AddMeshFaceLighting(emitter, corners, normal, uvs,
                        ShadeColor(WHITE, shades[face]), NULL, blockLight);
}

static void EmitSurfaceWater(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes,
    const SurfaceBoundarySnapshot *boundary, int height, int layerY,
    int chunkX, int chunkZ, const int *nearbyTorchIndices,
    int nearbyTorchCount, ChunkMeshEmitter *emitter)
{
    static const int directions[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    bool counting = emitter->mesh == NULL;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < height; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                unsigned char volume = SurfaceWaterSnapshotVolume(
                    blocks, waterVolumes, height, lx, y, lz);
                if (volume == 0u) continue;
                float heightFraction =
                    (float)volume / (float)WATER_VOLUME_CAPACITY;
                int worldX = chunkX * CHUNK_SIZE + lx;
                int worldZ = chunkZ * CHUNK_SIZE + lz;
                float blockLight = counting ? 0.0f : TorchLightAtBlockNearby(
                    worldX, layerY + y, worldZ, nearbyTorchIndices,
                    nearbyTorchCount);
                for (int face = 0; face < 6; face++) {
                    BlockType neighbor = BLOCK_AIR;
                    unsigned char neighborVolume = 0u;
                    if (!SurfaceWaterNeighbor(
                            blocks, waterVolumes, boundary, height, layerY,
                            chunkX, chunkZ, lx, y, lz, directions[face][0],
                            directions[face][1], directions[face][2],
                            &neighbor, &neighborVolume)) {
                        continue;
                    }

                    float low = 0.0f;
                    bool visible = false;
                    if (face == 2) {
                        visible = neighborVolume == 0u &&
                            (neighbor == BLOCK_AIR ||
                             neighbor == BLOCK_SPACESHIP_OCCUPIED ||
                             IsTranslucentBlock(neighbor));
                        low = heightFraction;
                    } else if (face == 3) {
                        visible = neighborVolume == 0u &&
                            (neighbor == BLOCK_AIR ||
                             neighbor == BLOCK_SPACESHIP_OCCUPIED ||
                             IsTranslucentBlock(neighbor));
                    } else if (neighborVolume > 0u) {
                        low = (float)neighborVolume /
                              (float)WATER_VOLUME_CAPACITY;
                        visible = low + 0.0001f < heightFraction;
                    } else {
                        visible = neighbor == BLOCK_AIR ||
                                  neighbor == BLOCK_SPACESHIP_OCCUPIED ||
                                  IsTranslucentBlock(neighbor);
                    }
                    if (!visible) continue;
                    if (counting) CountMeshFace(emitter);
                    else AddSurfaceWaterFace(emitter, worldX, y, worldZ,
                                             face, low, heightFraction,
                                             blockLight);
                }
            }
        }
    }
}

static bool BuildSurfaceWaterVolumeMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes,
    const SurfaceBoundarySnapshot *boundary, int height, int layerY,
    int chunkX, int chunkZ, const int *nearbyTorchIndices,
    int nearbyTorchCount, Mesh *outMesh)
{
    ChunkMeshEmitter counter = { 0 };
    EmitSurfaceWater(blocks, waterVolumes, boundary, height, layerY,
                     chunkX, chunkZ, nearbyTorchIndices, nearbyTorchCount,
                     &counter);
    if (counter.failed || counter.vertexIndex == 0) return false;

    Mesh mesh = { 0 };
    mesh.vertexCount = counter.vertexIndex;
    mesh.triangleCount = counter.vertexIndex / 3;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3u * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2u * sizeof(float));
    mesh.texcoords2 = malloc((size_t)mesh.vertexCount * 2u * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3u * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4u);
    if (!mesh.vertices || !mesh.texcoords || !mesh.texcoords2 ||
        !mesh.normals || !mesh.colors) {
        FreeMeshData(&mesh);
        return false;
    }
    ChunkMeshEmitter writer = {
        .mesh = &mesh,
        .vertexCapacity = mesh.vertexCount
    };
    EmitSurfaceWater(blocks, waterVolumes, boundary, height, layerY,
                     chunkX, chunkZ, nearbyTorchIndices, nearbyTorchCount,
                     &writer);
    if (writer.failed || writer.vertexIndex != counter.vertexIndex) {
        FreeMeshData(&mesh);
        return false;
    }
    *outMesh = mesh;
    return true;
}

bool BuildSurfaceWaterMeshDataWithSnapshot(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount,
    const SurfaceBoundarySnapshot *boundary, Mesh *outMesh)
{
    if (!blocks || height <= 0 || !faces || !outMesh) return false;
    Mesh transparent = { 0 };
    Mesh water = { 0 };
    bool hasTransparent = BuildMeshDataFilteredWithSnapshot(
        blocks, height, layerY, chunkX, chunkZ, true, false, false, true,
        faces, nearbyTorchIndices, nearbyTorchCount, boundary, &transparent);
    bool hasWater = BuildSurfaceWaterVolumeMeshData(
        blocks, waterVolumes, boundary, height, layerY, chunkX, chunkZ,
        nearbyTorchIndices, nearbyTorchCount, &water);
    if (!hasTransparent && !hasWater) return false;
    Mesh combined = { 0 };
    if ((hasTransparent && !MergeMeshData(&combined, &transparent)) ||
        (hasWater && !MergeMeshData(&combined, &water))) {
        FreeMeshData(&combined);
        return false;
    }
    *outMesh = combined;
    return true;
}

bool BuildSurfaceWaterMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE],
    const unsigned char *waterVolumes, int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildSurfaceWaterMeshDataWithSnapshot(
        blocks, waterVolumes, height, layerY, chunkX, chunkZ, faces,
        nearbyTorchIndices, nearbyTorchCount, NULL, outMesh);
}
