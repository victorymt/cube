#include "nether.h"

#include "raymath.h"
#include "chunks.h"
#include "terrain.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define NETHER_CHUNK_GENERATION_PER_FRAME 4
#define NETHER_MESH_REBUILDS_PER_FRAME 4

NetherChunk netherChunks[MAX_NETHER_CHUNKS];
static BlockEdit netherEdits[MAX_NETHER_EDITS];
static int netherEditCount = 0;

void NetherInit(void)
{
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        netherChunks[i].loaded = false;
        netherChunks[i].dirty = false;
    }
    netherEditCount = 0;
}

static NetherChunk *FindNetherChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        if (netherChunks[i].loaded && netherChunks[i].cx == cx && netherChunks[i].cz == cz) return &netherChunks[i];
    }
    return NULL;
}

static void ApplyNetherEditsToChunk(NetherChunk *chunk);

static NetherChunk *AllocateNetherChunkSlot(int cx, int cz)
{
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        if (!netherChunks[i].loaded) {
            memset(&netherChunks[i], 0, sizeof(netherChunks[i]));
            netherChunks[i].cx = cx;
            netherChunks[i].cz = cz;
            return &netherChunks[i];
        }
    }
    return NULL;
}

static void UnloadNetherChunkModel(NetherChunk *chunk)
{
    if (chunk->hasModel) {
        UnloadModel(chunk->model);
        chunk->hasModel = false;
    }
    if (chunk->hasWaterModel) {
        UnloadModel(chunk->waterModel);
        chunk->hasWaterModel = false;
    }
}

static int NetherSurface(int x, int z)
{
    float noise = TerrainNoise((float)x * 0.8f, (float)z * 0.8f);
    return 20 + (int)roundf(noise * 2.0f);
}

static void GenerateNetherChunk(NetherChunk *chunk, int cx, int cz)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < WORLD_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int lz = 0; lz < CHUNK_SIZE; lz++) {
            int worldX = startX + lx;
            int worldZ = startZ + lz;
            int surface = NetherSurface(worldX, worldZ);
            unsigned int h = WorldHash2D(worldX, worldZ);
            bool lavaPit = (h % 17u) == 0u;
            int lavaLevel = surface - 3;

            for (int ly = 0; ly <= surface && ly < WORLD_HEIGHT; ly++) {
                int by = NETHER_LAYER_Y + ly;
                BlockType type = BLOCK_NETHERRACK;
                if (lavaPit && ly <= lavaLevel) type = BLOCK_LAVA;
                else if (ly >= surface - 1) type = (WorldHash2D(worldX + 3, worldZ + 7) % 3u == 0u) ? BLOCK_SOUL_SAND : BLOCK_NETHERRACK;
                else if (WorldHash3D(worldX, by, worldZ) % 53u == 0u) type = BLOCK_GLOWSTONE;
                else if (CaveAt(worldX, by, worldZ, 30)) type = BLOCK_AIR;
                chunk->blocks[lx][ly][lz] = (unsigned short)type;
            }
        }
    }

    ApplyNetherEditsToChunk(chunk);
    chunk->loaded = true;
    chunk->dirty = true;
}

static void ApplyNetherEditsToChunk(NetherChunk *chunk)
{
    for (int i = 0; i < netherEditCount; i++) {
        const BlockEdit *edit = &netherEdits[i];
        if (edit->y < NETHER_LAYER_Y || edit->y >= NETHER_LAYER_TOP) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit->x, edit->z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz) {
            chunk->blocks[editLx][edit->y - NETHER_LAYER_Y][editLz] = (unsigned short)edit->type;
        }
    }
}

static void NetherRememberEdit(int x, int y, int z, BlockType type)
{
    for (int i = 0; i < netherEditCount; i++) {
        if (netherEdits[i].x == x && netherEdits[i].y == y && netherEdits[i].z == z) {
            netherEdits[i].type = type;
            return;
        }
    }
    if (netherEditCount < MAX_NETHER_EDITS) {
        netherEdits[netherEditCount++] = (BlockEdit){ x, y, z, type };
    }
}

static void RebuildNetherChunkMesh(NetherChunk *chunk)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int nearbyTorchIndices[MAX_TORCH_LIGHTS];
    int nearbyTorchCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        nearbyTorchIndices);

    UnloadNetherChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    bool hasSolid = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  WORLD_HEIGHT, NETHER_LAYER_Y,
                                  chunk->cx, chunk->cz, false, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  WORLD_HEIGHT, NETHER_LAYER_Y,
                                  chunk->cx, chunk->cz, true, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &waterMesh);

    if (hasSolid) {
        UploadMesh(&solidMesh, false);
        chunk->model = LoadModelFromMesh(solidMesh);
        SetMaterialTexture(&chunk->model.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasModel = true;
    }
    if (hasWater) {
        UploadMesh(&waterMesh, false);
        chunk->waterModel = LoadModelFromMesh(waterMesh);
        SetMaterialTexture(&chunk->waterModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasWaterModel = true;
    }
    chunk->dirty = false;
}

void UpdateNetherChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame)
{
    if (playerPosition.y > 20.0f) return;

    int renderDist = NETHER_RENDER_DISTANCE_CHUNKS;
    if (groundRenderDistance < renderDist) renderDist = groundRenderDistance;

    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        if (!netherChunks[i].loaded) continue;
        if (abs(netherChunks[i].cx - playerCx) > renderDist ||
            abs(netherChunks[i].cz - playerCz) > renderDist) {
            UnloadNetherChunkModel(&netherChunks[i]);
            netherChunks[i].loaded = false;
            netherChunks[i].dirty = false;
        }
    }

    int generated = 0;
    for (int dz = -renderDist; dz <= renderDist && generated < generationPerFrame; dz++) {
        for (int dx = -renderDist; dx <= renderDist && generated < generationPerFrame; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindNetherChunk(cx, cz)) continue;
            NetherChunk *chunk = AllocateNetherChunkSlot(cx, cz);
            if (!chunk) break;
            GenerateNetherChunk(chunk, cx, cz);
            generated++;
        }
    }

    int rebuilt = 0;
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        if (!netherChunks[i].loaded || !netherChunks[i].dirty) continue;
        RebuildNetherChunkMesh(&netherChunks[i]);
        if (++rebuilt >= NETHER_MESH_REBUILDS_PER_FRAME) break;
    }
}

BlockType NetherBlockAt(int x, int y, int z)
{
    if (y < NETHER_LAYER_Y || y >= NETHER_LAYER_TOP) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    NetherChunk *chunk = FindNetherChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return (BlockType)chunk->blocks[lx][y - NETHER_LAYER_Y][lz];
}

void NetherSetBlock(int x, int y, int z, BlockType type)
{
    if (y < NETHER_LAYER_Y || y >= NETHER_LAYER_TOP) return;

    NetherRememberEdit(x, y, z, type);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    NetherChunk *chunk = FindNetherChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][y - NETHER_LAYER_Y][lz] = (unsigned short)type;
    chunk->dirty = true;
}

void NetherSaveEdits(FILE *file)
{
    uint32_t count = (uint32_t)netherEditCount;
    fwrite(&count, sizeof(count), 1, file);
    if (netherEditCount > 0) {
        fwrite(netherEdits, sizeof(BlockEdit), (size_t)netherEditCount, file);
    }
}

void NetherLoadEdits(FILE *file)
{
    netherEditCount = 0;

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return;

    if (count > MAX_NETHER_EDITS) return;

    for (uint32_t i = 0; i < count; i++) {
        BlockEdit edit;
        if (fread(&edit, sizeof(edit), 1, file) != 1) break;
        if (edit.y < NETHER_LAYER_Y || edit.y >= NETHER_LAYER_TOP) continue;
        if (!IsValidBlockType(edit.type)) continue;
        if (netherEditCount < MAX_NETHER_EDITS) netherEdits[netherEditCount++] = edit;
    }
}

void UnloadAllNetherChunks(void)
{
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        UnloadNetherChunkModel(&netherChunks[i]);
        netherChunks[i].loaded = false;
        netherChunks[i].dirty = false;
    }
}

void NetherReset(void)
{
    UnloadAllNetherChunks();
    netherEditCount = 0;
}

int GetActiveNetherChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_NETHER_CHUNKS; i++) {
        if (netherChunks[i].loaded) count++;
    }
    return count;
}
