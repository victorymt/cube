#include "space.h"

#include "raymath.h"
#include "chunks.h"
#include "terrain.h"
#include "particles.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ASTEROID_SPACING 26
#define ASTEROID_PROBABILITY 55u
#define SPACE_MESH_REBUILDS_PER_FRAME 2

#define SOLAR_SYSTEM_X 0
#define SOLAR_SYSTEM_Z 0
#define SOLAR_SYSTEM_Y (SPACE_LAYER_Y + 48)
#define SOLAR_SUN_RADIUS 13

typedef enum SolarBodyStyle {
    SOLAR_STYLE_SUN = 0,
    SOLAR_STYLE_LAVA,
    SOLAR_STYLE_ICE,
    SOLAR_STYLE_DESERT,
    SOLAR_STYLE_GAS,
    SOLAR_STYLE_CRATER
} SolarBodyStyle;

typedef struct SolarBodyDef {
    int orbit;
    int size;
    int yOffset;
    SolarBodyStyle style;
} SolarBodyDef;

static const SolarBodyDef solarBodyDefs[] = {
    { 180, 6, -22, SOLAR_STYLE_LAVA },
    { 260, 5,  20, SOLAR_STYLE_ICE },
    { 340, 7,  -8, SOLAR_STYLE_DESERT },
    { 430, 4,  30, SOLAR_STYLE_GAS },
    { 520, 5, -28, SOLAR_STYLE_CRATER },
    { 650, 3,  14, SOLAR_STYLE_LAVA }
};
#define SOLAR_PLANET_COUNT ((int)(sizeof(solarBodyDefs) / sizeof(solarBodyDefs[0])))

static Vector3 SolarBodyCenter(int index)
{
    if (index == 0) {
        return (Vector3){ (float)SOLAR_SYSTEM_X, (float)SOLAR_SYSTEM_Y, (float)SOLAR_SYSTEM_Z };
    }
    const SolarBodyDef *def = &solarBodyDefs[index - 1];
    float angle = (float)(Hash2D(index * 7 + 3, 19) % 6283u) / 1000.0f;
    return (Vector3){
        (float)SOLAR_SYSTEM_X + cosf(angle) * (float)def->orbit,
        (float)SOLAR_SYSTEM_Y + (float)def->yOffset,
        (float)SOLAR_SYSTEM_Z + sinf(angle) * (float)def->orbit
    };
}

static BlockType SolarBodyBlock(int bx, int by, int bz, float distSqr, float shellSqr, SolarBodyStyle style)
{
    unsigned int h = Hash3D(bx, by, bz);
    bool surface = distSqr >= shellSqr;

    switch (style) {
    case SOLAR_STYLE_SUN:
        if (!surface) return (h % 5u == 0u) ? BLOCK_STAR_MATTER : BLOCK_GLOWSTONE;
        if (h % 9u == 0u) return BLOCK_LAVA;
        if (h % 4u == 0u) return BLOCK_STAR_MATTER;
        return BLOCK_GLOWSTONE;
    case SOLAR_STYLE_LAVA:
        if (surface) return (h % 7u == 0u) ? BLOCK_LAVA : BLOCK_MOON_ROCK;
        return (h % 11u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_ICE:
        if (surface) return (h % 6u == 0u) ? BLOCK_SNOW : BLOCK_ICE;
        return (h % 13u == 0u) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_DESERT:
        if (surface) return (h % 8u == 0u) ? BLOCK_SANDSTONE : BLOCK_SAND;
        return (h % 9u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_GAS:
        if ((by % 6u) < 2u) return (h % 5u == 0u) ? BLOCK_GLOWSTONE : BLOCK_SOUL_SAND;
        return (h % 7u == 0u) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
    case SOLAR_STYLE_CRATER:
        if (surface) return (h % 9u == 0u) ? BLOCK_METEORITE : BLOCK_MOON_SAND;
        return BLOCK_MOON_ROCK;
    default:
        return BLOCK_MOON_ROCK;
    }
}

static void FillSolarBody(SpaceChunk *chunk, int startX, int startZ,
                          int cx, int cy, int cz, int radius, SolarBodyStyle style)
{
    int chunkMinX = startX;
    int chunkMaxX = startX + CHUNK_SIZE - 1;
    int chunkMinZ = startZ;
    int chunkMaxZ = startZ + CHUNK_SIZE - 1;
    if (cx + radius < chunkMinX || cx - radius > chunkMaxX) return;
    if (cz + radius < chunkMinZ || cz - radius > chunkMaxZ) return;

    float radiusSqr = (float)(radius * radius);
    float shellSqr = (float)((radius - 1) * (radius - 1));

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int bx = startX + lx;
                int by = SPACE_LAYER_Y + ly;
                int bz = startZ + lz;
                float dx = (float)(bx - cx);
                float dy = (float)(by - cy);
                float dz = (float)(bz - cz);
                float distSqr = dx * dx + dy * dy + dz * dz;
                if (distSqr >= radiusSqr) continue;
                chunk->blocks[lx][ly][lz] = (unsigned short)SolarBodyBlock(bx, by, bz, distSqr, shellSqr, style);
            }
        }
    }
}

static void FillSolarSystem(SpaceChunk *chunk, int startX, int startZ)
{
    Vector3 sun = SolarBodyCenter(0);
    FillSolarBody(chunk, startX, startZ,
                  (int)sun.x, (int)sun.y, (int)sun.z, SOLAR_SUN_RADIUS, SOLAR_STYLE_SUN);

    for (int i = 0; i < SOLAR_PLANET_COUNT; i++) {
        Vector3 center = SolarBodyCenter(i + 1);
        const SolarBodyDef *def = &solarBodyDefs[i];
        FillSolarBody(chunk, startX, startZ,
                      (int)center.x, (int)center.y, (int)center.z, def->size, def->style);
    }
}

void SolarSystemBodies(Vector3 *positions, int maxCount)
{
    int count = SOLAR_PLANET_COUNT + 1;
    if (maxCount < count) count = maxCount;
    for (int i = 0; i < count; i++) {
        positions[i] = SolarBodyCenter(i);
    }
}

SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];
static BlockEdit spaceEdits[MAX_SPACE_EDITS];
static int spaceEditCount = 0;

void SpaceInit(void)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        spaceChunks[i].loaded = false;
        spaceChunks[i].dirty = false;
    }
    spaceEditCount = 0;
}

static SpaceChunk *FindSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return &spaceChunks[i];
    }
    return NULL;
}

static SpaceChunk *AllocateSpaceChunkSlot(int cx, int cz)
{
    SpaceChunk *empty = NULL;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded) {
            empty = &spaceChunks[i];
            break;
        }
    }
    if (!empty) return NULL;
    memset(empty, 0, sizeof(*empty));
    empty->cx = cx;
    empty->cz = cz;
    return empty;
}

static void UnloadSpaceChunkModel(SpaceChunk *chunk)
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

static void ApplySpaceEditsToChunk(SpaceChunk *chunk)
{
    for (int i = 0; i < spaceEditCount; i++) {
        const BlockEdit *edit = &spaceEdits[i];
        if (edit->y < SPACE_LAYER_Y || edit->y >= SPACE_LAYER_TOP) continue;
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit->x, edit->z, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz) {
            chunk->blocks[editLx][edit->y - SPACE_LAYER_Y][editLz] = (unsigned short)edit->type;
        }
    }
}


static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(startX - 8, ASTEROID_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 8, ASTEROID_SPACING);
    int minAnchorZ = FloorDivInt(startZ - 8, ASTEROID_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 8, ASTEROID_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (Hash2D(anchorX, anchorZ) % 100u >= ASTEROID_PROBABILITY) continue;

            int wx = anchorX * ASTEROID_SPACING;
            int wz = anchorZ * ASTEROID_SPACING;
            int wy = SPACE_LAYER_Y + 8 + (int)(Hash2D(anchorX + 3, anchorZ) % (unsigned int)(WORLD_HEIGHT - 16));
            int radius = 3 + (int)(Hash2D(anchorX, anchorZ + 7) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 1) * (radius - 1));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        if (chunk->blocks[lx][ly][lz] != 0) continue;

                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (Hash3D(bx, by, bz) % 89u == 0u) type = BLOCK_METEORITE;
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    const int planetSpacing = 160;
    int minPlanetAnchorX = FloorDivInt(startX - 13, planetSpacing);
    int maxPlanetAnchorX = FloorDivInt(startX + CHUNK_SIZE + 13, planetSpacing);
    int minPlanetAnchorZ = FloorDivInt(startZ - 13, planetSpacing);
    int maxPlanetAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 13, planetSpacing);

    for (int anchorX = minPlanetAnchorX; anchorX <= maxPlanetAnchorX; anchorX++) {
        for (int anchorZ = minPlanetAnchorZ; anchorZ <= maxPlanetAnchorZ; anchorZ++) {
            if (Hash2D(anchorX + 71, anchorZ + 71) % 100u >= 20u) continue;

            int wx = anchorX * planetSpacing;
            int wz = anchorZ * planetSpacing;
            int wy = SPACE_LAYER_Y + 12 + (int)(Hash2D(anchorX + 31, anchorZ + 41) % (unsigned int)(WORLD_HEIGHT - 24));
            int radius = 8 + (int)(Hash2D(anchorX + 51, anchorZ + 61) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 2) * (radius - 2));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (distSqr >= shellSqr && Hash3D(bx, by, bz) % 19u == 0u) type = BLOCK_MOON_ROCK;
                        if (Hash3D(bx, by, bz) % 41u == 0u) type = BLOCK_METEORITE;
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    chunk->hasStar = false;
    const int starSpacing = 52;
    int minStarAnchorX = FloorDivInt(startX - 5, starSpacing);
    int maxStarAnchorX = FloorDivInt(startX + CHUNK_SIZE + 5, starSpacing);
    int minStarAnchorZ = FloorDivInt(startZ - 5, starSpacing);
    int maxStarAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 5, starSpacing);

    for (int anchorX = minStarAnchorX; anchorX <= maxStarAnchorX; anchorX++) {
        for (int anchorZ = minStarAnchorZ; anchorZ <= maxStarAnchorZ; anchorZ++) {
            if (Hash2D(anchorX + 101, anchorZ + 101) % 100u >= 35u) continue;

            int wx = anchorX * starSpacing;
            int wz = anchorZ * starSpacing;
            int wy = SPACE_LAYER_Y + 10 + (int)(Hash2D(anchorX + 5, anchorZ + 9) % (unsigned int)(WORLD_HEIGHT - 20));
            int radius = 2 + (int)(Hash2D(anchorX + 11, anchorZ + 13) % 3u);
            float radiusSqr = (float)(radius * radius);

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        if (dx * dx + dy * dy + dz * dz < radiusSqr) {
                            chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_STAR_MATTER;
                        }
                    }
                }
            }

            if (wx >= startX && wx < startX + CHUNK_SIZE &&
                wz >= startZ && wz < startZ + CHUNK_SIZE) {
                chunk->hasStar = true;
                chunk->starX = wx;
                chunk->starY = wy;
                chunk->starZ = wz;
            }
        }
    }

    FillSolarSystem(chunk, startX, startZ);

    ApplySpaceEditsToChunk(chunk);
    chunk->loaded = true;
    chunk->dirty = true;
}

static void SpaceRememberEdit(int x, int y, int z, BlockType type)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].x == x && spaceEdits[i].y == y && spaceEdits[i].z == z) {
            spaceEdits[i].type = type;
            return;
        }
    }
    if (spaceEditCount < MAX_SPACE_EDITS) {
        spaceEdits[spaceEditCount++] = (BlockEdit){ x, y, z, type };
    }
}

static void RebuildSpaceChunkMesh(SpaceChunk *chunk)
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

    UnloadSpaceChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    bool hasSolid = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, false, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
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

void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame)
{
    int renderDist = SPACE_RENDER_DISTANCE_CHUNKS;
    if (groundRenderDistance < renderDist) renderDist = groundRenderDistance;

    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded) continue;
        if (abs(spaceChunks[i].cx - playerCx) > renderDist ||
            abs(spaceChunks[i].cz - playerCz) > renderDist) {
            UnloadSpaceChunkModel(&spaceChunks[i]);
            spaceChunks[i].loaded = false;
            spaceChunks[i].dirty = false;
        }
    }

    if (playerPosition.y < 50.0f) return;

    int generated = 0;
    for (int dz = -renderDist; dz <= renderDist && generated < generationPerFrame; dz++) {
        for (int dx = -renderDist; dx <= renderDist && generated < generationPerFrame; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindSpaceChunk(cx, cz)) continue;
            SpaceChunk *chunk = AllocateSpaceChunkSlot(cx, cz);
            if (!chunk) break;
            GenerateSpaceChunk(chunk, cx, cz);
            generated++;
        }
    }

    int rebuilt = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded || !spaceChunks[i].dirty) continue;
        RebuildSpaceChunkMesh(&spaceChunks[i]);
        if (++rebuilt >= SPACE_MESH_REBUILDS_PER_FRAME) break;
    }
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return (BlockType)chunk->blocks[lx][y - SPACE_LAYER_Y][lz];
}

void SpaceSetBlock(int x, int y, int z, BlockType type)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return;

    SpaceRememberEdit(x, y, z, type);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][y - SPACE_LAYER_Y][lz] = (unsigned short)type;
    chunk->dirty = true;
}

void SpaceSaveEdits(FILE *file)
{
    uint32_t count = (uint32_t)spaceEditCount;
    fwrite(&count, sizeof(count), 1, file);
    if (spaceEditCount > 0) {
        fwrite(spaceEdits, sizeof(BlockEdit), (size_t)spaceEditCount, file);
    }
}

void SpaceLoadEdits(FILE *file)
{
    spaceEditCount = 0;

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return;

    if (count > MAX_SPACE_EDITS) return;

    for (uint32_t i = 0; i < count; i++) {
        BlockEdit edit;
        if (fread(&edit, sizeof(edit), 1, file) != 1) break;
        if (edit.y < SPACE_LAYER_Y || edit.y >= SPACE_LAYER_TOP) continue;
        if (!IsValidBlockType(edit.type)) continue;
        if (spaceEditCount < MAX_SPACE_EDITS) spaceEdits[spaceEditCount++] = edit;
    }
}

void UnloadAllSpaceChunks(void)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        UnloadSpaceChunkModel(&spaceChunks[i]);
        spaceChunks[i].loaded = false;
        spaceChunks[i].dirty = false;
    }
}

int GetActiveSpaceChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) count++;
    }
    return count;
}

void SpaceRebuildTorchList(void)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].type == BLOCK_TORCH) {
            TorchLightAdd(spaceEdits[i].x, spaceEdits[i].y, spaceEdits[i].z);
        }
    }
}

void SpaceUpdateStarGlow(Vector3 playerPosition)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded || !chunk->hasStar) continue;

        if (SpaceBlockAt(chunk->starX, chunk->starY, chunk->starZ) != BLOCK_STAR_MATTER) continue;

        Vector3 star = { (float)chunk->starX + 0.5f, (float)chunk->starY + 0.5f, (float)chunk->starZ + 0.5f };
        float dist = Vector3Distance(star, playerPosition);
        if (dist > 18.0f) continue;

        int count = (dist < 9.0f) ? 2 : 1;
        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f
            };
            ParticlesEmitOne(Vector3Add(star, offset),
                             (Vector3){ 0.1f, 0.35f, 0.1f },
                             (Color){ 255, 244, 190, 220 },
                             (Vector3){ 0.09f, 0.09f, 0.09f },
                             1.4f, 0.0f);
        }
    }
}

int GetSpaceEditCount(void)
{
    return spaceEditCount;
}

void SpaceUpdateSolarGlow(Vector3 playerPosition)
{
    Vector3 sun = SolarBodyCenter(0);
    float dist = Vector3Distance(sun, playerPosition);
    if (dist > 26.0f) return;

    int count = (dist < 12.0f) ? 3 : 1;
    for (int k = 0; k < count; k++) {
        Vector3 offset = {
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f
        };
        ParticlesEmitOne(Vector3Add(sun, offset),
                         (Vector3){ ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f,
                                    0.2f + (float)rand() / (float)RAND_MAX * 0.5f,
                                    ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f },
                         (Color){ 255, 190, 80, 220 },
                         (Vector3){ 0.14f, 0.14f, 0.14f },
                         1.8f, 0.0f);
    }
}
