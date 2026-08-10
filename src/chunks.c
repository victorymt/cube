#include "chunks.h"

#include "raymath.h"
#include "rlgl.h"
#include "ecology.h"
#include "space.h"
#include "terrain.h"
#include "world.h"
#include "weather.h"

#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "raymath.h"
Chunk chunks[MAX_ACTIVE_CHUNKS];
Texture2D blockAtlas = { 0 };
int renderDistanceChunks = DEFAULT_RENDER_DISTANCE_CHUNKS;
static ChunkGenJob chunkGenJobs[MAX_CHUNK_GEN_JOBS];
static pthread_mutex_t genMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t genCond = PTHREAD_COND_INITIALIZER;
static pthread_t genThread = 0;
static bool genShutdown = false;

struct MeshJob {
    bool inUse;
    bool done;
    int slotIndex;
    int cx;
    int cz;
    bool transparent;
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
    int nearbyIndices[MAX_TORCH_LIGHTS];
    int nearbyCount;
    Mesh mesh;
    Mesh floraMesh;
    bool hasMesh;
    bool hasFloraMesh;
};
typedef struct MeshJob MeshJob;
static bool HasPendingMeshJob(void);
static MeshJob *NextPendingMeshJob(void);
bool BuildMeshData(const unsigned short (*blocks)[CHUNK_SIZE],
                   int height, int layerY, int chunkX, int chunkZ,
                   bool transparent, const int faces[6][3],
                   const int *nearbyTorchIndices, int nearbyTorchCount,
                   Mesh *outMesh);

bool InHeight(int y)
{
    return y >= 0 && y < WORLD_HEIGHT;
}

int FloorDivInt(int value, int divisor)
{
    if (value >= 0) return value / divisor;
    return -((-value + divisor - 1) / divisor);
}

int PositiveMod(int value, int divisor)
{
    int result = value % divisor;
    return result < 0 ? result + divisor : result;
}

void WorldToChunkLocal(int x, int z, int *cx, int *cz, int *lx, int *lz)
{
    *cx = FloorDivInt(x, CHUNK_SIZE);
    *cz = FloorDivInt(z, CHUNK_SIZE);
    *lx = PositiveMod(x, CHUNK_SIZE);
    *lz = PositiveMod(z, CHUNK_SIZE);
}

Chunk *FindChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded && chunks[i].cx == cx && chunks[i].cz == cz) return &chunks[i];
    }
    return NULL;
}

static void ClearChunkFloraRuntime(Chunk *chunk)
{
    free(chunk->floraTargetScales);
    chunk->floraTargetScales = NULL;
    free(chunk->floraTargetWind);
    chunk->floraTargetWind = NULL;
    free(chunk->floraTargetPresence);
    chunk->floraTargetPresence = NULL;
    free(chunk->floraBaseVertices);
    chunk->floraBaseVertices = NULL;
    free(chunk->floraBaseColors);
    chunk->floraBaseColors = NULL;
    free(chunk->floraVisualInstances);
    chunk->floraVisualInstances = NULL;
    chunk->floraTargetScaleCount = 0;
}

void UnloadChunkModel(Chunk *chunk)
{
    if (chunk->hasModel) {
        UnloadModel(chunk->model);
        chunk->model = (Model){ 0 };
        chunk->hasModel = false;
    }
    if (chunk->hasWaterModel) {
        UnloadModel(chunk->waterModel);
        chunk->waterModel = (Model){ 0 };
        chunk->hasWaterModel = false;
    }
    if (chunk->hasFloraModel) {
        UnloadModel(chunk->floraModel);
        chunk->floraModel = (Model){ 0 };
        chunk->hasFloraModel = false;
    }
    ClearChunkFloraRuntime(chunk);
}

void MarkChunkDirty(int cx, int cz)
{
    Chunk *chunk = FindChunk(cx, cz);
    if (chunk) chunk->dirty = true;
}

void MarkChunkDirtyAtBlock(int x, int z)
{
    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);

    MarkChunkDirty(cx, cz);
    if (lx == 0) MarkChunkDirty(cx - 1, cz);
    if (lx == CHUNK_SIZE - 1) MarkChunkDirty(cx + 1, cz);
    if (lz == 0) MarkChunkDirty(cx, cz - 1);
    if (lz == CHUNK_SIZE - 1) MarkChunkDirty(cx, cz + 1);
}

void MarkChunkAndHorizontalNeighborsDirty(int cx, int cz)
{
    MarkChunkDirty(cx, cz);
    MarkChunkDirty(cx - 1, cz);
    MarkChunkDirty(cx + 1, cz);
    MarkChunkDirty(cx, cz - 1);
    MarkChunkDirty(cx, cz + 1);
}

unsigned int Hash3D(int x, int y, int z)
{
    unsigned int h = 2166136261u;
    h = (h ^ (unsigned int)x) * 16777619u;
    h = (h ^ (unsigned int)y) * 16777619u;
    h = (h ^ (unsigned int)z) * 16777619u;
    h ^= h >> 15;
    h *= 2246822519u;
    return h ^ (h >> 13);
}

Color ColorWithNoise(Color base, int amount, unsigned int hash)
{
    int delta = (int)(hash % (unsigned int)(amount * 2 + 1)) - amount;
    return (Color){
        (unsigned char)Clamp((float)((int)base.r + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.g + delta), 0.0f, 255.0f),
        (unsigned char)Clamp((float)((int)base.b + delta), 0.0f, 255.0f),
        base.a
    };
}

void DrawAtlasTile(Image *image, BlockTexture texture)
{
    int tileIndex = (int)texture;
    int cellX = (tileIndex % ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
    int cellY = (tileIndex / ATLAS_COLUMNS) * ATLAS_CELL_SIZE;
    int originX = cellX + ATLAS_TILE_PADDING;
    int originY = cellY + ATLAS_TILE_PADDING;
    bool dynamicColor = texture >= TEX_COLOR_START && texture < TEX_COUNT;
    Color dynamicBase = dynamicColor ? ColorPalette256((int)texture - TEX_COLOR_START) : WHITE;

    for (int y = 0; y < ATLAS_TILE_SIZE; y++) {
        for (int x = 0; x < ATLAS_TILE_SIZE; x++) {
            unsigned int hash = Hash3D((int)texture, x, y);
            Color color = WHITE;

            if (dynamicColor) {
                color = ColorWithNoise(dynamicBase, 2, hash);
                if ((x + y) % 8 == 0) color = ColorWithNoise(dynamicBase, 1, hash);
            } else switch (texture) {
            case TEX_GRASS_TOP:
                color = ColorWithNoise((Color){ 84, 170, 67, 255 }, 18, hash);
                if ((hash % 11u) == 0u) color = (Color){ 119, 199, 82, 255 };
                break;
            case TEX_GRASS_SIDE:
                if (y < 5 + (int)(hash % 3u)) color = ColorWithNoise((Color){ 88, 169, 70, 255 }, 15, hash);
                else color = ColorWithNoise((Color){ 121, 79, 45, 255 }, 17, hash);
                break;
            case TEX_DIRT:
                color = ColorWithNoise((Color){ 121, 77, 43, 255 }, 22, hash);
                if ((hash % 17u) == 0u) color = (Color){ 89, 55, 34, 255 };
                break;
            case TEX_STONE:
                color = ColorWithNoise((Color){ 118, 122, 124, 255 }, 20, hash);
                if ((x + y + (int)(hash % 5u)) % 13 == 0) color = (Color){ 84, 88, 91, 255 };
                break;
            case TEX_WOOD_SIDE:
                color = ColorWithNoise((x % 5 == 0) ? (Color){ 104, 67, 32, 255 } : (Color){ 142, 91, 42, 255 }, 13, hash);
                break;
            case TEX_WOOD_TOP: {
                int dx = x - ATLAS_TILE_SIZE / 2;
                int dy = y - ATLAS_TILE_SIZE / 2;
                int ring = (dx * dx + dy * dy) / 11;
                color = ColorWithNoise((ring % 2 == 0) ? (Color){ 154, 105, 55, 255 } : (Color){ 118, 75, 37, 255 }, 10, hash);
            } break;
            case TEX_SAND:
                color = ColorWithNoise((Color){ 214, 197, 132, 255 }, 16, hash);
                if ((hash % 19u) == 0u) color = (Color){ 183, 165, 101, 255 };
                break;
            case TEX_LEAVES:
                color = ColorWithNoise((Color){ 46, 128, 55, 255 }, 24, hash);
                if ((hash % 7u) == 0u) color = (Color){ 31, 95, 43, 255 };
                if ((x + y + (int)(hash % 4u)) % 9 == 0) color = (Color){ 82, 158, 68, 255 };
                break;
            case TEX_RED:
                color = ColorWithNoise((Color){ 207, 55, 54, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 238, 83, 75, 255 }, 10, hash);
                break;
            case TEX_ORANGE:
                color = ColorWithNoise((Color){ 229, 126, 38, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 247, 156, 55, 255 }, 10, hash);
                break;
            case TEX_YELLOW:
                color = ColorWithNoise((Color){ 238, 207, 64, 255 }, 16, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 255, 228, 86, 255 }, 8, hash);
                break;
            case TEX_BLUE:
                color = ColorWithNoise((Color){ 51, 116, 220, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 74, 150, 244, 255 }, 10, hash);
                break;
            case TEX_PURPLE:
                color = ColorWithNoise((Color){ 143, 72, 202, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 171, 98, 231, 255 }, 10, hash);
                break;
            case TEX_GREEN:
                color = ColorWithNoise((Color){ 64, 185, 85, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 91, 218, 108, 255 }, 10, hash);
                break;
            case TEX_CYAN:
                color = ColorWithNoise((Color){ 47, 188, 207, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 76, 219, 235, 255 }, 10, hash);
                break;
            case TEX_PINK:
                color = ColorWithNoise((Color){ 226, 96, 161, 255 }, 18, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 247, 128, 188, 255 }, 10, hash);
                break;
            case TEX_WHITE:
                color = ColorWithNoise((Color){ 232, 235, 224, 255 }, 10, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 250, 250, 241, 255 }, 6, hash);
                break;
            case TEX_GRAY:
                color = ColorWithNoise((Color){ 112, 119, 126, 255 }, 14, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 141, 148, 154, 255 }, 8, hash);
                break;
            case TEX_BLACK:
                color = ColorWithNoise((Color){ 28, 31, 35, 255 }, 10, hash);
                if ((x + y) % 6 == 0) color = ColorWithNoise((Color){ 52, 56, 62, 255 }, 6, hash);
                break;
            case TEX_PLANK:
                color = ColorWithNoise((y % 4 == 0) ? (Color){ 118, 72, 36, 255 } : (Color){ 156, 100, 48, 255 }, 12, hash);
                if ((x + (y / 4) % 2 * 4) % 8 == 0) color = ColorWithNoise((Color){ 108, 66, 32, 255 }, 8, hash);
                break;
            case TEX_BRICK: {
                int row = y / 4;
                int mortar = (y % 4 == 0) || ((x + (row % 2) * 4) % 8 == 0);
                if (mortar) color = ColorWithNoise((Color){ 205, 200, 190, 255 }, 8, hash);
                else color = ColorWithNoise((Color){ 148, 62, 48, 255 }, 16, hash);
                if (!mortar && (hash % 13u) == 0u) color = ColorWithNoise((Color){ 168, 80, 62, 255 }, 8, hash);
            } break;
            case TEX_GLASS:
                color = ColorWithNoise((Color){ 205, 230, 235, 230 }, 10, hash);
                if ((x + y) % 7 == 0) color = ColorWithNoise((Color){ 240, 250, 250, 235 }, 5, hash);
                if (x == 0 || y == 0 || x == ATLAS_TILE_SIZE - 1 || y == ATLAS_TILE_SIZE - 1) {
                    color = ColorWithNoise((Color){ 165, 205, 215, 225 }, 8, hash);
                }
                break;
            case TEX_WATER:
                color = ColorWithNoise((Color){ 52, 118, 205, 195 }, 12, hash);
                if (((y + (int)(hash % 3u)) % 6) == 0) color = ColorWithNoise((Color){ 86, 158, 228, 210 }, 10, hash);
                if (((x + (int)(hash % 2u)) % 9) == 0) color = ColorWithNoise((Color){ 40, 96, 178, 190 }, 8, hash);
                break;
            case TEX_SNOW:
                color = ColorWithNoise((Color){ 238, 244, 246, 255 }, 8, hash);
                if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 218, 228, 234, 255 }, 6, hash);
                break;
            case TEX_ICE:
                color = ColorWithNoise((Color){ 148, 205, 226, 235 }, 12, hash);
                if ((hash % 11u) == 0u) color = (Color){ 210, 240, 248, 235 };
                break;
            case TEX_CACTUS:
                color = ColorWithNoise((x % 3 == 0) ? (Color){ 52, 122, 54, 255 } : (Color){ 78, 152, 62, 255 }, 14, hash);
                if ((y % 6) == 0) color = ColorWithNoise((Color){ 148, 196, 92, 255 }, 10, hash);
                break;
            case TEX_BEDROCK:
                color = ColorWithNoise((Color){ 58, 58, 64, 255 }, 24, hash);
                if ((hash % 7u) == 0u) color = (Color){ 32, 32, 36, 255 };
                if ((hash % 11u) == 0u) color = (Color){ 86, 84, 88, 255 };
                break;
            case TEX_COAL_ORE:
                color = ColorWithNoise((Color){ 116, 120, 122, 255 }, 18, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 38, 40, 44, 255 }, 8, hash);
                if ((hash % 31u) == 0u) color = (Color){ 62, 64, 68, 255 };
                break;
            case TEX_IRON_ORE:
                color = ColorWithNoise((Color){ 116, 120, 122, 255 }, 18, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 190, 152, 108, 255 }, 10, hash);
                if ((hash % 31u) == 0u) color = (Color){ 226, 192, 150, 255 };
                break;
            case TEX_GOLD_ORE:
                color = ColorWithNoise((Color){ 116, 120, 122, 255 }, 18, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 232, 196, 64, 255 }, 12, hash);
                if ((hash % 29u) == 0u) color = (Color){ 250, 226, 110, 255 };
                break;
            case TEX_DIAMOND_ORE:
                color = ColorWithNoise((Color){ 116, 120, 122, 255 }, 18, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 92, 214, 232, 255 }, 12, hash);
                if ((hash % 29u) == 0u) color = (Color){ 140, 240, 250, 255 };
                break;
            case TEX_TORCH:
                if (y < 3) {
                    color = ColorWithNoise((Color){ 255, 186, 62, 255 }, 22, hash);
                    if ((hash % 5u) == 0u) color = (Color){ 255, 236, 130, 255 };
                } else if (y < 6) {
                    color = ColorWithNoise((Color){ 226, 110, 36, 255 }, 20, hash);
                } else {
                    color = ColorWithNoise((x % 4 == 0 || y % 5 == 0) ? (Color){ 92, 60, 32, 255 } : (Color){ 128, 82, 42, 255 }, 10, hash);
                }
                break;
            case TEX_DOOR:
                if (x % 4 == 0 || x == ATLAS_TILE_SIZE - 1) {
                    color = ColorWithNoise((Color){ 104, 66, 32, 255 }, 10, hash);
                } else if (y == 5 || y == 6 || y == 10 || y == 11) {
                    color = ColorWithNoise((Color){ 122, 80, 40, 255 }, 12, hash);
                } else {
                    color = ColorWithNoise((Color){ 156, 104, 52, 255 }, 12, hash);
                }
                if (x == 12 && y == 8) color = ColorWithNoise((Color){ 216, 190, 96, 255 }, 8, hash);
                if (x == 12 && (y == 7 || y == 9)) color = ColorWithNoise((Color){ 96, 62, 30, 255 }, 6, hash);
                break;
            case TEX_MOON_ROCK:
                color = ColorWithNoise((Color){ 138, 142, 148, 255 }, 14, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 164, 168, 174, 255 }, 8, hash);
                if ((hash % 23u) == 0u) color = (Color){ 104, 108, 114, 255 };
                break;
            case TEX_METEORITE:
                color = ColorWithNoise((Color){ 92, 78, 70, 255 }, 16, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 150, 130, 90, 255 }, 12, hash);
                if ((hash % 17u) == 0u) color = ColorWithNoise((Color){ 60, 52, 48, 255 }, 10, hash);
                break;
            case TEX_MOON_SAND:
                color = ColorWithNoise((Color){ 190, 186, 176, 255 }, 12, hash);
                if ((hash % 15u) == 0u) color = ColorWithNoise((Color){ 164, 158, 146, 255 }, 8, hash);
                if ((hash % 29u) == 0u) color = ColorWithNoise((Color){ 210, 208, 200, 255 }, 6, hash);
                break;
            case TEX_FENCE:
                if (x == 7 || x == 8) {
                    color = ColorWithNoise((Color){ 128, 82, 42, 255 }, 10, hash);
                    if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 156, 104, 54, 255 }, 6, hash);
                } else if (y == 7 || y == 8) {
                    color = ColorWithNoise((Color){ 138, 90, 46, 255 }, 10, hash);
                    if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 166, 112, 58, 255 }, 6, hash);
                } else {
                    color = ColorWithNoise((Color){ 150, 98, 50, 255 }, 10, hash);
                    if ((hash % 17u) == 0u) color = (Color){ 110, 70, 36, 255 };
                }
                break;
            case TEX_LAVA:
                color = ColorWithNoise((Color){ 224, 96, 24, 255 }, 22, hash);
                if ((hash % 7u) == 0u) color = ColorWithNoise((Color){ 255, 196, 48, 255 }, 14, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 168, 44, 12, 255 }, 12, hash);
                if ((hash % 19u) == 0u) color = ColorWithNoise((Color){ 255, 140, 40, 255 }, 10, hash);
                break;
            case TEX_FLOWER:
                color = (Color){ 0, 0, 0, 0 };
                if (x >= 7 && x <= 8 && y >= 10 && y <= 14) {
                    color = ColorWithNoise((Color){ 62, 148, 54, 255 }, 10, hash);
                }
                if (x >= 5 && x <= 10 && y >= 6 && y <= 9) {
                    color = ColorWithNoise((Color){ 208, 62, 54, 255 }, 14, hash);
                }
                if (x >= 7 && x <= 8 && y >= 7 && y <= 8) {
                    color = ColorWithNoise((Color){ 250, 224, 96, 255 }, 10, hash);
                }
                break;
            case TEX_MUSHROOM:
                color = (Color){ 0, 0, 0, 0 };
                if (x >= 7 && x <= 8 && y >= 12 && y <= 14) {
                    color = ColorWithNoise((Color){ 226, 224, 216, 255 }, 8, hash);
                }
                if (x >= 4 && x <= 11 && y >= 5 && y <= 11) {
                    color = ColorWithNoise((Color){ 196, 52, 46, 255 }, 14, hash);
                    if (((x + y) % 5) == 0) color = ColorWithNoise((Color){ 240, 238, 230, 255 }, 8, hash);
                }
                break;
            case TEX_BOOKSHELF:
                if (y == 0 || y == 15 || x == 0 || x == 15) {
                    color = ColorWithNoise((Color){ 148, 96, 48, 255 }, 10, hash);
                } else if (x % 3 == 1) {
                    color = ColorWithNoise((Color){ 118, 76, 40, 255 }, 10, hash);
                } else {
                    color = ColorWithNoise((Color){ 84, 54, 30, 255 }, 10, hash);
                    if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 158, 90, 60, 255 }, 10, hash);
                    if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 64, 110, 150, 255 }, 10, hash);
                    if ((hash % 17u) == 0u) color = ColorWithNoise((Color){ 140, 150, 60, 255 }, 10, hash);
                }
                break;
            case TEX_HAY:
                color = ColorWithNoise((y % 4 == 0) ? (Color){ 176, 132, 44, 255 } : (Color){ 218, 172, 66, 255 }, 14, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 236, 196, 92, 255 }, 8, hash);
                break;
            case TEX_PUMPKIN:
                if (x >= 7 && x <= 8 && y <= 2) {
                    color = ColorWithNoise((Color){ 96, 128, 52, 255 }, 10, hash);
                } else {
                    color = ColorWithNoise((Color){ 224, 138, 42, 255 }, 16, hash);
                    if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 246, 168, 62, 255 }, 10, hash);
                    if ((hash % 15u) == 0u) color = ColorWithNoise((Color){ 182, 98, 26, 255 }, 10, hash);
                }
                break;
            case TEX_NETHERRACK:
                color = ColorWithNoise((Color){ 116, 48, 42, 255 }, 22, hash);
                if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 168, 72, 56, 255 }, 14, hash);
                if ((hash % 17u) == 0u) color = ColorWithNoise((Color){ 76, 28, 26, 255 }, 10, hash);
                break;
            case TEX_SOUL_SAND:
                color = ColorWithNoise((Color){ 124, 106, 88, 255 }, 16, hash);
                if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 164, 148, 120, 255 }, 10, hash);
                if ((hash % 19u) == 0u) color = ColorWithNoise((Color){ 92, 76, 66, 255 }, 8, hash);
                break;
            case TEX_GLOWSTONE:
                color = ColorWithNoise((Color){ 178, 138, 62, 255 }, 18, hash);
                if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 250, 220, 110, 255 }, 14, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 240, 250, 190, 255 }, 10, hash);
                if ((hash % 23u) == 0u) color = ColorWithNoise((Color){ 120, 88, 40, 255 }, 8, hash);
                break;
            case TEX_STONE_BRICKS:
                if (y % 4 == 0 || x % 8 == 0) {
                    color = ColorWithNoise((Color){ 118, 118, 118, 255 }, 8, hash);
                } else {
                    color = ColorWithNoise((Color){ 138, 140, 142, 255 }, 10, hash);
                    if ((hash % 15u) == 0u) color = ColorWithNoise((Color){ 154, 156, 158, 255 }, 6, hash);
                }
                break;
            case TEX_SANDSTONE:
                color = ColorWithNoise((Color){ 216, 200, 150, 255 }, 10, hash);
                if (y % 4 == 0) color = ColorWithNoise((Color){ 196, 178, 128, 255 }, 8, hash);
                if ((hash % 19u) == 0u) color = ColorWithNoise((Color){ 230, 216, 168, 255 }, 6, hash);
                break;
            case TEX_OBSIDIAN:
                color = ColorWithNoise((Color){ 22, 16, 30, 255 }, 16, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 74, 48, 104, 255 }, 12, hash);
                if ((hash % 23u) == 0u) color = ColorWithNoise((Color){ 44, 30, 60, 255 }, 10, hash);
                break;
            case TEX_NETHER_PORTAL:
                if ((x + y) % 6 < 2) {
                    color = ColorWithNoise((Color){ 96, 28, 110, 255 }, 20, hash);
                } else {
                    color = ColorWithNoise((Color){ 158, 52, 190, 255 }, 20, hash);
                    if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 210, 120, 240, 255 }, 14, hash);
                }
                break;
            case TEX_STAR_MATTER:
                color = ColorWithNoise((Color){ 238, 236, 222, 255 }, 8, hash);
                if ((hash % 7u) == 0u) color = ColorWithNoise((Color){ 255, 240, 150, 255 }, 10, hash);
                if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 190, 210, 245, 255 }, 8, hash);
                if ((hash % 31u) == 0u) color = (Color){ 255, 255, 235, 255 };
                break;
            case TEX_SPACESHIP:
                if (y >= 12) {
                    color = ColorWithNoise((Color){ 150, 96, 42, 255 }, 14, hash);
                    if ((y == 13 || y == 14) && (hash % 5u) == 0u) color = (Color){ 255, 178, 66, 255 };
                } else if (y >= 9 && y <= 11 && x >= 5 && x <= 10) {
                    color = ColorWithNoise((Color){ 84, 132, 188, 255 }, 10, hash);
                    if ((hash % 9u) == 0u) color = ColorWithNoise((Color){ 140, 186, 228, 255 }, 6, hash);
                } else if (y >= 3 && y <= 7 && x >= 2 && x <= 13) {
                    color = ColorWithNoise((Color){ 196, 202, 210, 255 }, 10, hash);
                    if (x == 7 || x == 8) color = ColorWithNoise((Color){ 164, 170, 180, 255 }, 6, hash);
                    if ((hash % 11u) == 0u) color = ColorWithNoise((Color){ 222, 226, 232, 255 }, 5, hash);
                } else {
                    color = ColorWithNoise((Color){ 140, 146, 156, 255 }, 12, hash);
                    if ((hash % 17u) == 0u) color = (Color){ 90, 96, 106, 255 };
                }
                break;
            case TEX_ALBUM:
                if (x == 0 || x == ATLAS_TILE_SIZE - 1 || y == 0 || y == ATLAS_TILE_SIZE - 1 ||
                    x == 1 || x == ATLAS_TILE_SIZE - 2) {
                    color = ColorWithNoise((Color){ 150, 112, 52, 255 }, 12, hash);
                    if ((hash % 9u) == 0u) color = (Color){ 196, 156, 70, 255 };
                } else if (x >= 4 && x <= 11 && y >= 4 && y <= 11) {
                    if (y == 7 || y == 8) color = ColorWithNoise((Color){ 74, 52, 30, 255 }, 8, hash);
                    else color = ColorWithNoise((Color){ 206, 196, 176, 255 }, 12, hash);
                } else {
                    color = ColorWithNoise((Color){ 118, 76, 42, 255 }, 14, hash);
                    if ((hash % 13u) == 0u) color = ColorWithNoise((Color){ 150, 100, 56, 255 }, 8, hash);
                }
                break;
            default:
                color = MAGENTA;
                break;
            }

            ImageDrawPixel(image, originX + x, originY + y, color);
        }
    }

    // Mip generation must never average neighboring atlas tiles. Repeat each
    // tile's edge through a power-of-two gutter so its lower mip levels remain
    // isolated while the visible 16x16 artwork stays unchanged.
    for (int y = 0; y < ATLAS_CELL_SIZE; y++) {
        int sourceY = y - ATLAS_TILE_PADDING;
        if (sourceY < 0) sourceY = 0;
        if (sourceY >= ATLAS_TILE_SIZE) sourceY = ATLAS_TILE_SIZE - 1;
        for (int x = 0; x < ATLAS_CELL_SIZE; x++) {
            bool insideTile = x >= ATLAS_TILE_PADDING &&
                              x < ATLAS_TILE_PADDING + ATLAS_TILE_SIZE &&
                              y >= ATLAS_TILE_PADDING &&
                              y < ATLAS_TILE_PADDING + ATLAS_TILE_SIZE;
            if (insideTile) continue;
            int sourceX = x - ATLAS_TILE_PADDING;
            if (sourceX < 0) sourceX = 0;
            if (sourceX >= ATLAS_TILE_SIZE) sourceX = ATLAS_TILE_SIZE - 1;
            Color edge = GetImageColor(*image, originX + sourceX,
                                       originY + sourceY);
            ImageDrawPixel(image, cellX + x, cellY + y, edge);
        }
    }
}

Texture2D LoadBlockAtlas(void)
{
    Image image = GenImageColor(ATLAS_CELL_SIZE * ATLAS_COLUMNS,
                                ATLAS_CELL_SIZE * ATLAS_ROWS, BLANK);
    for (int i = 0; i < TEX_COUNT; i++) DrawAtlasTile(&image, (BlockTexture)i);

    Texture2D texture = LoadTextureFromImage(image);
    if (texture.id != 0) {
        GenTextureMipmaps(&texture);
        SetTextureFilter(texture, TEXTURE_FILTER_TRILINEAR);
        SetTextureFilter(texture, TEXTURE_FILTER_ANISOTROPIC_8X);
        rlTextureParameters(texture.id, RL_TEXTURE_MAG_FILTER,
                            RL_TEXTURE_FILTER_NEAREST);
        SetTextureWrap(texture, TEXTURE_WRAP_CLAMP);
    }
    UnloadImage(image);
    return texture;
}

bool HasPendingGenJob(void)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) return true;
    }
    return false;
}

ChunkGenJob *NextPendingGenJob(void)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) return &chunkGenJobs[i];
    }
    return NULL;
}

void *ChunkGenWorker(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&genMutex);
        while (!genShutdown && !HasPendingGenJob() && !HasPendingMeshJob()) {
            pthread_cond_wait(&genCond, &genMutex);
        }
        if (genShutdown) {
            pthread_mutex_unlock(&genMutex);
            break;
        }

        ChunkGenJob *job = NextPendingGenJob();
        if (job) {
            pthread_mutex_unlock(&genMutex);

            GenerateChunkTerrain(&chunks[job->slotIndex], job->cx, job->cz, job->terrainMode);

            pthread_mutex_lock(&genMutex);
            job->done = true;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        MeshJob *meshJob = NextPendingMeshJob();
        if (meshJob) {
            pthread_mutex_unlock(&genMutex);

            static const int faces[6][3] = {
                { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
                { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
            };
            if (meshJob->transparent) {
                meshJob->hasMesh = BuildSurfaceWaterMeshData(
                    (const unsigned short (*)[CHUNK_SIZE])meshJob->blocks,
                    WORLD_HEIGHT, 0, meshJob->cx, meshJob->cz, faces,
                    meshJob->nearbyIndices, meshJob->nearbyCount, &meshJob->mesh);
                meshJob->hasFloraMesh = false;
            } else {
                meshJob->hasMesh = BuildSurfaceSolidMeshData(
                    (const unsigned short (*)[CHUNK_SIZE])meshJob->blocks,
                    WORLD_HEIGHT, 0, meshJob->cx, meshJob->cz, faces,
                    meshJob->nearbyIndices, meshJob->nearbyCount, &meshJob->mesh);
                meshJob->hasFloraMesh = BuildFloraMeshData(
                    (const unsigned short (*)[CHUNK_SIZE])meshJob->blocks,
                    WORLD_HEIGHT, 0, meshJob->cx, meshJob->cz, faces,
                    meshJob->nearbyIndices, meshJob->nearbyCount, &meshJob->floraMesh);
            }

            pthread_mutex_lock(&genMutex);
            meshJob->done = true;
            pthread_cond_signal(&genCond);
            pthread_mutex_unlock(&genMutex);
            continue;
        }

        pthread_mutex_unlock(&genMutex);
    }

    return NULL;
}

bool SubmitChunkGenJob(Chunk *chunk, int cx, int cz, TerrainMode mode)
{
    if (genThread == 0) return false;

    pthread_mutex_lock(&genMutex);
    ChunkGenJob *job = NULL;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (!chunkGenJobs[i].inUse) {
            job = &chunkGenJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    *job = (ChunkGenJob){
        .inUse = true,
        .done = false,
        .cx = cx,
        .cz = cz,
        .slotIndex = (int)(chunk - chunks),
        .terrainMode = mode
    };
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

bool FindPendingGenJob(int cx, int cz)
{
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && chunkGenJobs[i].cx == cx && chunkGenJobs[i].cz == cz) return true;
    }
    return false;
}

void CompleteChunkGenJob(ChunkGenJob *job)
{
    Chunk *chunk = &chunks[job->slotIndex];
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    chunk->dirty = true;
    MarkChunkAndHorizontalNeighborsDirty(chunk->cx, chunk->cz);
}

void ProcessFinishedChunkJobs(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        ChunkGenJob *job = NULL;
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (chunkGenJobs[i].inUse && chunkGenJobs[i].done) {
                job = &chunkGenJobs[i];
                break;
            }
        }
        if (job) job->inUse = false;
        pthread_mutex_unlock(&genMutex);

        if (!job) return;
        CompleteChunkGenJob(job);
    }
}

void DrainChunkGen(void)
{
    for (;;) {
        pthread_mutex_lock(&genMutex);
        for (;;) {
            ChunkGenJob *job = NULL;
            for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
                if (chunkGenJobs[i].inUse && chunkGenJobs[i].done) {
                    job = &chunkGenJobs[i];
                    break;
                }
            }
            if (!job) break;
            job->inUse = false;
            pthread_mutex_unlock(&genMutex);
            CompleteChunkGenJob(job);
            pthread_mutex_lock(&genMutex);
        }

        bool busy = false;
        for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
            if (chunkGenJobs[i].inUse) {
                busy = true;
                break;
            }
        }
        if (!busy) {
            pthread_mutex_unlock(&genMutex);
            return;
        }
        pthread_cond_wait(&genCond, &genMutex);
        pthread_mutex_unlock(&genMutex);
    }
}

Chunk *AllocateChunkSlot(int nearCx, int nearCz)
{
    int bestIndex = -1;
    int bestDistance = -1;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded && !chunks[i].generating) return &chunks[i];

        int dx = abs(chunks[i].cx - nearCx);
        int dz = abs(chunks[i].cz - nearCz);
        int distance = dx > dz ? dx : dz;
        if (!chunks[i].generating && distance > bestDistance) {
            bestDistance = distance;
            bestIndex = i;
        }
    }

    if (bestIndex >= 0) {
        MarkChunkAndHorizontalNeighborsDirty(chunks[bestIndex].cx, chunks[bestIndex].cz);
    }
    return &chunks[bestIndex < 0 ? 0 : bestIndex];
}

bool EnsureChunk(int cx, int cz)
{
    if (FindChunk(cx, cz) || FindPendingGenJob(cx, cz)) return false;

    Chunk *chunk = AllocateChunkSlot(cx, cz);
    chunk->cx = cx;
    chunk->cz = cz;
    chunk->generating = true;
    chunk->loaded = false;
    UnloadChunkModel(chunk);
    chunk->floraActivity = 1.0f;
    chunk->floraCapacity = 1.0f;
    chunk->floraSampleTimer = 0.0f;
    chunk->floraVisualScale = 1.0f;

    if (SubmitChunkGenJob(chunk, cx, cz, terrainMode)) return true;

    GenerateChunkTerrain(chunk, cx, cz, terrainMode);
    ApplyEditsToChunk(chunk);
    chunk->generating = false;
    chunk->loaded = true;
    chunk->dirty = true;
    MarkChunkAndHorizontalNeighborsDirty(cx, cz);
    return true;
}

void UpdateChunks(Vector3 playerPosition, int effectiveRenderDistance)
{
    if (effectiveRenderDistance < MIN_RENDER_DISTANCE_CHUNKS) effectiveRenderDistance = MIN_RENDER_DISTANCE_CHUNKS;
    if (effectiveRenderDistance > MAX_RENDER_DISTANCE_CHUNKS) effectiveRenderDistance = MAX_RENDER_DISTANCE_CHUNKS;

    int playerX = (int)floorf(playerPosition.x);
    int playerZ = (int)floorf(playerPosition.z);
    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal(playerX, playerZ, &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded) continue;
        if (abs(chunks[i].cx - playerCx) > effectiveRenderDistance ||
            abs(chunks[i].cz - playerCz) > effectiveRenderDistance) {
            MarkChunkAndHorizontalNeighborsDirty(chunks[i].cx, chunks[i].cz);
            UnloadChunkModel(&chunks[i]);
            chunks[i].loaded = false;
            chunks[i].dirty = false;
        }
    }

    int missingChunks[MAX_ACTIVE_CHUNKS][2];
    int missingCount = 0;
    for (int dz = -effectiveRenderDistance; dz <= effectiveRenderDistance; dz++) {
        for (int dx = -effectiveRenderDistance; dx <= effectiveRenderDistance; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindChunk(cx, cz) || FindPendingGenJob(cx, cz)) continue;

            int insert = missingCount;
            int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            while (insert > 0) {
                int prevDx = missingChunks[insert - 1][0] - playerCx;
                int prevDz = missingChunks[insert - 1][1] - playerCz;
                int prevDistance = abs(prevDx) > abs(prevDz) ? abs(prevDx) : abs(prevDz);
                if (prevDistance <= distance) break;
                missingChunks[insert][0] = missingChunks[insert - 1][0];
                missingChunks[insert][1] = missingChunks[insert - 1][1];
                insert--;
            }
            missingChunks[insert][0] = cx;
            missingChunks[insert][1] = cz;
            missingCount++;
        }
    }

    int submissions = 0;
    for (int i = 0; i < missingCount && submissions < CHUNK_GEN_SUBMISSIONS_PER_FRAME; i++) {
        if (EnsureChunk(missingChunks[i][0], missingChunks[i][1])) submissions++;
    }
}

static void UpdateChunkFloraScale(Chunk *chunk, float elapsed,
                                  float daylight, bool refreshTargets)
{
    if (!chunk->hasFloraModel || chunk->floraModel.meshCount <= 0) return;

    Mesh *mesh = &chunk->floraModel.meshes[0];
    if (!mesh->vertices || mesh->vertexCount <= 0 ||
        !chunk->floraTargetScales || !chunk->floraTargetWind ||
        !chunk->floraTargetPresence || !chunk->floraBaseVertices ||
        !chunk->floraBaseColors || !chunk->floraVisualInstances ||
        !mesh->colors ||
        chunk->floraTargetScaleCount <= 0) return;

    float blend = fminf(elapsed * 1.8f, 1.0f);
    float colorBlend = fminf(elapsed * 2.2f, 1.0f);
    float scaleSum = 0.0f;
    int scaleCount = 0;
    bool changed = false;
    for (int group = 0; group < chunk->floraTargetScaleCount; group++) {
        const FloraVisualInstance *instance =
            &chunk->floraVisualInstances[group];
        int firstVertex = instance->firstVertex;
        int lastVertex = firstVertex + instance->vertexCount;
        if (firstVertex < 0 || firstVertex >= mesh->vertexCount ||
            instance->vertexCount <= 0) continue;
        if (lastVertex > mesh->vertexCount) lastVertex = mesh->vertexCount;
        int cellX = (int)floorf(instance->anchor.x);
        int cellZ = (int)floorf(instance->anchor.z);

        if (refreshTargets && PlanetWorldIsActive()) {
            PlanetLocalEcology local = PlanetEcologyLocalAt(cellX, cellZ, daylight);
            PlanetFloraRuntimeState runtime = PlanetEcologyFloraRuntime(
                local.suitability.floraActivity,
                local.suitability.floraCapacity);
            chunk->floraTargetScales[group] = runtime.growthScale;
            chunk->floraTargetPresence[group] = runtime.visualPresence;
            chunk->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
        } else if (refreshTargets) {
            chunk->floraTargetScales[group] = 1.0f;
            chunk->floraTargetPresence[group] = 1.0f;
            chunk->floraTargetWind[group] = WeatherFieldSampleAtWorld(
                cellX, cellZ).wind;
        }

        float baseGroundY = INFINITY;
        float baseTopY = -INFINITY;
        float currentGroundY = INFINITY;
        float currentTopY = -INFINITY;
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            float baseY = chunk->floraBaseVertices[vertex * 3 + 1];
            float currentY = mesh->vertices[vertex * 3 + 1];
            baseGroundY = fminf(baseGroundY, baseY);
            baseTopY = fmaxf(baseTopY, baseY);
            currentGroundY = fminf(currentGroundY, currentY);
            currentTopY = fmaxf(currentTopY, currentY);
        }
        float baseHeight = baseTopY - baseGroundY;
        if (!(baseHeight > 0.001f) || !isfinite(baseHeight)) continue;
        float oldScale = (currentTopY - currentGroundY) / baseHeight;
        if (!(oldScale > 0.01f) || !isfinite(oldScale)) continue;
        float targetScale = fmaxf(chunk->floraTargetScales[group], 0.01f);
        float newScale = oldScale + (targetScale - oldScale) * blend;
        float phase = (float)(Hash3D(cellX, 0, cellZ) & 4095u) * 0.0015339808f;
        float sway = sinf((float)SpaceSimulationTime() * 1.7f + phase) *
                     fmaxf(chunk->floraTargetWind[group], 0.0f) * 0.07f *
                     fmaxf(instance->windResponse, 0.0f);
        scaleSum += newScale;
        scaleCount++;
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            float *current = &mesh->vertices[vertex * 3];
            const float *base = &chunk->floraBaseVertices[vertex * 3];
            float heightFraction = (base[1] - baseGroundY) / baseHeight;
            float targetX = base[0] +
                cosf(chunk->floraWindAngle) * sway * heightFraction;
            float targetY = baseGroundY +
                (base[1] - baseGroundY) * newScale;
            float targetZ = base[2] +
                sinf(chunk->floraWindAngle) * sway * heightFraction;
            if (fabsf(current[0] - targetX) >= 0.0001f ||
                fabsf(current[1] - targetY) >= 0.0001f ||
                fabsf(current[2] - targetZ) >= 0.0001f) {
                changed = true;
            }
            current[0] = targetX;
            current[1] = targetY;
            current[2] = targetZ;
        }
    }
    if (changed) {
        UpdateMeshBuffer(*mesh, 0, mesh->vertices,
                         mesh->vertexCount * 3 * (int)sizeof(float), 0);
    }
    if (ApplyFloraMeshInstancePresenceColors(
            mesh->colors, chunk->floraBaseColors, mesh->vertexCount,
            chunk->floraTargetPresence, chunk->floraVisualInstances,
            chunk->floraTargetScaleCount, colorBlend)) {
        UpdateMeshBuffer(*mesh, 3, mesh->colors,
                         mesh->vertexCount * 4 * (int)sizeof(unsigned char), 0);
    }
    if (scaleCount > 0) chunk->floraVisualScale = scaleSum / (float)scaleCount;
}

static bool ApplyFloraMeshColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int targetCount, float blend)
{
    static const float dormantFactors[3] = { 0.55f, 0.42f, 0.32f };
    if (!colors || !baseColors || !targetPresence || vertexCount <= 0 ||
        targetCount <= 0) {
        return false;
    }

    float amount = fminf(fmaxf(blend, 0.0f), 1.0f);
    bool changed = false;
    for (int group = 0; group < targetCount; group++) {
        int firstVertex = instances ? instances[group].firstVertex : group * 12;
        int count = instances ? instances[group].vertexCount : 12;
        if (firstVertex < 0 || firstVertex >= vertexCount || count <= 0) {
            continue;
        }
        int lastVertex = firstVertex + count;
        if (lastVertex > vertexCount) lastVertex = vertexCount;
        float presence = fminf(fmaxf(targetPresence[group], 0.0f), 1.0f);
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            int colorIndex = vertex * 4;
            for (int channel = 0; channel < 3; channel++) {
                float base = (float)baseColors[colorIndex + channel];
                float dormant = base * dormantFactors[channel];
                float target = dormant + (base - dormant) * presence;
                float current = (float)colors[colorIndex + channel];
                unsigned char next = (unsigned char)lroundf(
                    current + (target - current) * amount);
                if (next != colors[colorIndex + channel]) {
                    colors[colorIndex + channel] = next;
                    changed = true;
                }
            }
            if (colors[colorIndex + 3] != baseColors[colorIndex + 3]) {
                colors[colorIndex + 3] = baseColors[colorIndex + 3];
                changed = true;
            }
        }
    }
    return changed;
}

bool ApplyFloraMeshPresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, int targetCount, float blend)
{
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, NULL, targetCount, blend);
}

bool ApplyFloraMeshInstancePresenceColors(
    unsigned char *colors, const unsigned char *baseColors, int vertexCount,
    const float *targetPresence, const FloraVisualInstance *instances,
    int instanceCount, float blend)
{
    if (!instances) return false;
    return ApplyFloraMeshColors(colors, baseColors, vertexCount,
                                targetPresence, instances, instanceCount,
                                blend);
}

void ChunksUpdateEcologyVisuals(float dt, float daylight)
{
    bool planetWorld = PlanetWorldIsActive();
    float elapsed = fmaxf(dt, 0.0f);
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;

        chunk->floraSampleTimer -= elapsed;
        bool refreshTargets = chunk->floraSampleTimer <= 0.0f;
        if (refreshTargets) {
            int centerX = chunk->cx * CHUNK_SIZE + CHUNK_SIZE / 2;
            int centerZ = chunk->cz * CHUNK_SIZE + CHUNK_SIZE / 2;
            chunk->floraWindAngle = WeatherWindAngleAtWorld(centerX, centerZ);
            if (planetWorld) {
                PlanetLocalEcology local = PlanetEcologyLocalAt(
                    centerX, centerZ, daylight);
                chunk->floraActivity = local.suitability.floraActivity;
                chunk->floraCapacity = local.suitability.floraCapacity;
            } else {
                chunk->floraActivity = 1.0f;
                chunk->floraCapacity = 1.0f;
            }

            unsigned int stagger = Hash3D(chunk->cx, 0, chunk->cz) & 255u;
            chunk->floraSampleTimer = 0.75f + (float)stagger / 510.0f;
        }

        UpdateChunkFloraScale(chunk, elapsed, daylight, refreshTargets);
    }
}

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
    return (BlockType)chunk->blocks[lx][y][lz];
}

bool FaceIsVisible(int x, int y, int z, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (!InHeight(neighborY)) return true;
    return GetBlock(x + nx, neighborY, z + nz) == BLOCK_AIR;
}

bool ChunkFaceIsVisible(const unsigned short (*blocks)[CHUNK_SIZE],
                        int height, int layerY, int chunkX, int chunkZ,
                        int lx, int y, int lz, int nx, int ny, int nz)
{
    int neighborY = y + ny;
    if (neighborY < 0 || neighborY >= height) return true;

    int neighborLx = lx + nx;
    int neighborLz = lz + nz;
    if (neighborLx >= 0 && neighborLx < CHUNK_SIZE &&
        neighborLz >= 0 && neighborLz < CHUNK_SIZE) {
        return (BlockType)blocks[neighborLx * height + neighborY][neighborLz] == BLOCK_AIR;
    }

    int wx = chunkX * CHUNK_SIZE + lx + nx;
    int wz = chunkZ * CHUNK_SIZE + lz + nz;
    return GetBlockAt(wx, layerY + neighborY, wz) == BLOCK_AIR;
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

BlockTexture TextureForBlockFace(BlockType type, int face)
{
    if (IsColorBlock(type)) return (BlockTexture)(TEX_COLOR_START + ColorBlockIndex(type));

    switch (type) {
    case BLOCK_GRASS:
        if (face == 2) return TEX_GRASS_TOP;
        if (face == 3) return TEX_DIRT;
        return TEX_GRASS_SIDE;
    case BLOCK_DIRT: return TEX_DIRT;
    case BLOCK_STONE: return TEX_STONE;
    case BLOCK_WOOD:
        if (face == 2 || face == 3) return TEX_WOOD_TOP;
        return TEX_WOOD_SIDE;
    case BLOCK_SAND: return TEX_SAND;
    case BLOCK_LEAVES: return TEX_LEAVES;
    case BLOCK_RED: return TEX_RED;
    case BLOCK_ORANGE: return TEX_ORANGE;
    case BLOCK_YELLOW: return TEX_YELLOW;
    case BLOCK_BLUE: return TEX_BLUE;
    case BLOCK_PURPLE: return TEX_PURPLE;
    case BLOCK_GREEN: return TEX_GREEN;
    case BLOCK_CYAN: return TEX_CYAN;
    case BLOCK_PINK: return TEX_PINK;
    case BLOCK_WHITE: return TEX_WHITE;
    case BLOCK_GRAY: return TEX_GRAY;
    case BLOCK_BLACK: return TEX_BLACK;
    case BLOCK_PLANK: return TEX_PLANK;
    case BLOCK_BRICK: return TEX_BRICK;
    case BLOCK_GLASS: return TEX_GLASS;
    case BLOCK_WATER: return TEX_WATER;
    case BLOCK_SNOW: return TEX_SNOW;
    case BLOCK_ICE: return TEX_ICE;
    case BLOCK_CACTUS: return TEX_CACTUS;
    case BLOCK_BEDROCK: return TEX_BEDROCK;
    case BLOCK_COAL_ORE: return TEX_COAL_ORE;
    case BLOCK_IRON_ORE: return TEX_IRON_ORE;
    case BLOCK_GOLD_ORE: return TEX_GOLD_ORE;
    case BLOCK_DIAMOND_ORE: return TEX_DIAMOND_ORE;
    case BLOCK_TORCH: return TEX_TORCH;
    case BLOCK_ALBUM: return TEX_ALBUM;
    case BLOCK_SLAB: return TEX_STONE;
    case BLOCK_DOOR: return TEX_DOOR;
    case BLOCK_DOOR_OPEN: return TEX_DOOR;
    case BLOCK_MOON_ROCK: return TEX_MOON_ROCK;
    case BLOCK_METEORITE: return TEX_METEORITE;
    case BLOCK_MOON_SAND: return TEX_MOON_SAND;
    case BLOCK_STAR_MATTER: return TEX_STAR_MATTER;
    case BLOCK_SPACESHIP: return TEX_SPACESHIP;
    case BLOCK_STONE_STAIRS: return TEX_STONE;
    case BLOCK_WOOD_STAIRS: return TEX_PLANK;
    case BLOCK_FENCE: return TEX_FENCE;
    case BLOCK_FENCE_GATE: return TEX_FENCE;
    case BLOCK_FENCE_GATE_OPEN: return TEX_FENCE;
    case BLOCK_GLASS_PANE: return TEX_GLASS;
    case BLOCK_LAVA: return TEX_LAVA;
    case BLOCK_FLOWER: return TEX_FLOWER;
    case BLOCK_MUSHROOM: return TEX_MUSHROOM;
    case BLOCK_BOOKSHELF: return TEX_BOOKSHELF;
    case BLOCK_HAY_BALE: return TEX_HAY;
    case BLOCK_PUMPKIN: return TEX_PUMPKIN;
    case BLOCK_NETHERRACK: return TEX_NETHERRACK;
    case BLOCK_SOUL_SAND: return TEX_SOUL_SAND;
    case BLOCK_GLOWSTONE: return TEX_GLOWSTONE;
    case BLOCK_STONE_BRICKS: return TEX_STONE_BRICKS;
    case BLOCK_SANDSTONE: return TEX_SANDSTONE;
    case BLOCK_OBSIDIAN: return TEX_OBSIDIAN;
    case BLOCK_NETHER_PORTAL: return TEX_NETHER_PORTAL;
    default: return TEX_DIRT;
    }
}

void AtlasUVs(BlockTexture texture, Vector2 uvs[6])
{
    int tileIndex = (int)texture;
    int tileX = tileIndex % ATLAS_COLUMNS;
    int tileY = tileIndex / ATLAS_COLUMNS;
    float atlasWidth = (float)(ATLAS_CELL_SIZE * ATLAS_COLUMNS);
    float atlasHeight = (float)(ATLAS_CELL_SIZE * ATLAS_ROWS);
    float tileSize = (float)ATLAS_TILE_SIZE;
    float cellSize = (float)ATLAS_CELL_SIZE;
    float padding = (float)ATLAS_TILE_PADDING;
    float inset = 0.25f;
    float u0 = ((float)tileX * cellSize + padding + inset) / atlasWidth;
    float u1 = ((float)tileX * cellSize + padding + tileSize - inset) /
               atlasWidth;
    float v0 = ((float)tileY * cellSize + padding + inset) / atlasHeight;
    float v1 = ((float)tileY * cellSize + padding + tileSize - inset) /
               atlasHeight;

    uvs[0] = (Vector2){ u0, v1 };
    uvs[1] = (Vector2){ u1, v1 };
    uvs[2] = (Vector2){ u1, v0 };
    uvs[3] = (Vector2){ u0, v1 };
    uvs[4] = (Vector2){ u1, v0 };
    uvs[5] = (Vector2){ u0, v0 };
}

Rectangle AtlasSourceRect(BlockTexture texture)
{
    int tileIndex = (int)texture;
    return (Rectangle){
        (float)((tileIndex % ATLAS_COLUMNS) * ATLAS_CELL_SIZE +
                ATLAS_TILE_PADDING),
        (float)((tileIndex / ATLAS_COLUMNS) * ATLAS_CELL_SIZE +
                ATLAS_TILE_PADDING),
        (float)ATLAS_TILE_SIZE,
        (float)ATLAS_TILE_SIZE
    };
}

void AddMeshVertex(Mesh *mesh, int *vertexIndex, Vector3 position, Vector3 normal, Vector2 uv, Color color)
{
    int v = *vertexIndex;
    mesh->vertices[v * 3 + 0] = position.x;
    mesh->vertices[v * 3 + 1] = position.y;
    mesh->vertices[v * 3 + 2] = position.z;

    mesh->normals[v * 3 + 0] = normal.x;
    mesh->normals[v * 3 + 1] = normal.y;
    mesh->normals[v * 3 + 2] = normal.z;

    mesh->texcoords[v * 2 + 0] = uv.x;
    mesh->texcoords[v * 2 + 1] = uv.y;

    mesh->colors[v * 4 + 0] = color.r;
    mesh->colors[v * 4 + 1] = color.g;
    mesh->colors[v * 4 + 2] = color.b;
    mesh->colors[v * 4 + 3] = color.a;
    *vertexIndex = v + 1;
}

void AddMeshFace(Mesh *mesh, int *vertexIndex, Vector3 corners[6], Vector3 normal, Vector2 uvs[6], Color color)
{
    for (int i = 0; i < 6; i++) {
        AddMeshVertex(mesh, vertexIndex, corners[i], normal, uvs[i], color);
    }
}

void UnloadAllChunks(void)
{
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        UnloadChunkModel(&chunks[i]);
        chunks[i].loaded = false;
        chunks[i].dirty = false;
    }
}

void AddBlockFace(Mesh *mesh, int *vertexIndex, int x, int y, int z, int face, BlockType type, Color baseColor, float extraLight)
{
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

    float brightness = shade * (1.0f + extraLight);
    if (type == BLOCK_STAR_MATTER) brightness *= 2.1f;
    else if (type == BLOCK_LAVA || type == BLOCK_GLOWSTONE) brightness *= 1.8f;
    AtlasUVs(TextureForBlockFace(type, face), uvs);
    AddMeshFace(mesh, vertexIndex, corners, normal, uvs, ShadeColor(baseColor, brightness));
}

void AddTorchMesh(Mesh *mesh, int *vertexIndex, int x, int y, int z, float extraLight)
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
        AddMeshFace(mesh, vertexIndex, corners, stickNormals[face], stickUvs, stickColor);
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
    AddMeshFace(mesh, vertexIndex, flameCornersA, normalA, flameUvs, flameColor);

    Vector3 flameCornersB[6] = {
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy - 0.12f, cz - hs },
        { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy - 0.12f, cz + hs }, { cx + hs, fy + 0.14f, cz - hs },
        { cx - hs, fy + 0.14f, cz + hs }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(mesh, vertexIndex, flameCornersB, normalB, flameUvs, flameColor);
}


void AddAlbumMesh(Mesh *mesh, int *vertexIndex, int x, int y, int z, float extraLight)
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
        AddMeshFace(mesh, vertexIndex, faces[face], normals[face], uvs, color);
    }
}

void AddSlabMesh(Mesh *mesh, int *vertexIndex,
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

        AddMeshFace(mesh, vertexIndex, corners, normal, faceUvs, ShadeColor(WHITE, shades[face] * brightness));
    }
}

void AddDoorMesh(Mesh *mesh, int *vertexIndex,
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
        AddMeshFace(mesh, vertexIndex, faceCorners[f], normals[f], uvs, color);
    }
}

void AddStairsMesh(Mesh *mesh, int *vertexIndex,
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
        AddMeshFace(mesh, vertexIndex, top, (Vector3){ 0.0f, 1.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 1.08f * brightness));

        Vector3 front[6] = {
            { x0, y0, zHigh }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, y0, zHigh },
            { x0 + 1.0f, yHigh, zHigh }, { x0, yHigh, zHigh }
        };
        AddMeshFace(mesh, vertexIndex, front, (Vector3){ 0.0f, 0.0f, 1.0f }, uvs,
                    ShadeColor(WHITE, 0.90f * brightness));

        Vector3 sideA[6] = {
            { x0, y0, zLow }, { x0, y0, zHigh },
            { x0, yHigh, zHigh }, { x0, y0, zLow },
            { x0, yHigh, zHigh }, { x0, yHigh, zLow }
        };
        AddMeshFace(mesh, vertexIndex, sideA, (Vector3){ -1.0f, 0.0f, 0.0f }, uvs,
                    ShadeColor(WHITE, 0.72f * brightness));

        Vector3 sideB[6] = {
            { x0 + 1.0f, y0, zHigh }, { x0 + 1.0f, y0, zLow },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, y0, zHigh },
            { x0 + 1.0f, yHigh, zLow }, { x0 + 1.0f, yHigh, zHigh }
        };
        AddMeshFace(mesh, vertexIndex, sideB, (Vector3){ 1.0f, 0.0f, 0.0f }, uvs,
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

void AddFenceMesh(Mesh *mesh, int *vertexIndex,
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
        AddMeshFace(mesh, vertexIndex, corners, normal, uvs, ShadeColor(WHITE, 0.85f * brightness));
    }

    Vector3 postCorners[6] = {
        { cx - 0.06f, y0, cz - 0.06f }, { cx + 0.06f, y0, cz + 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0, cz - 0.06f },
        { cx + 0.06f, y0 + 1.0f, cz + 0.06f }, { cx - 0.06f, y0 + 1.0f, cz - 0.06f }
    };
    AddMeshFace(mesh, vertexIndex, postCorners, (Vector3){ 0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 1.0f * brightness));
    AddMeshFace(mesh, vertexIndex, postCorners, (Vector3){ -0.707f, 0.0f, 0.707f }, uvs,
                ShadeColor(WHITE, 0.85f * brightness));
}

void AddGateMesh(Mesh *mesh, int *vertexIndex,
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
        AddMeshFace(mesh, vertexIndex, faces[f], normals[f], uvs, ShadeColor(WHITE, shades[f] * brightness));
    }
}

void AddPaneMesh(Mesh *mesh, int *vertexIndex,
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
        AddMeshFace(mesh, vertexIndex, faces[f], normals[f], uvs, ShadeColor(WHITE, shades[f] * brightness));
    }
}

void AddPlantMesh(Mesh *mesh, int *vertexIndex, int x, int y, int z, BlockType type, float extraLight)
{
    float cx = (float)x + 0.5f;
    float cz = (float)z + 0.5f;
    float y0 = (float)y;
    float y1 = y0 + 0.4f;
    float brightness = 1.0f + extraLight;
    Vector2 uvs[6];
    AtlasUVs((type == BLOCK_FLOWER) ? TEX_FLOWER : TEX_MUSHROOM, uvs);

    Vector3 quadA[6] = {
        { cx - 0.16f, y0, cz - 0.16f }, { cx + 0.16f, y0, cz + 0.16f },
        { cx + 0.16f, y1, cz + 0.16f }, { cx - 0.16f, y0, cz - 0.16f },
        { cx + 0.16f, y1, cz + 0.16f }, { cx - 0.16f, y1, cz - 0.16f }
    };
    Vector3 normalA = Vector3Normalize((Vector3){ 1.0f, 0.0f, 1.0f });
    AddMeshFace(mesh, vertexIndex, quadA, normalA, uvs, ShadeColor(WHITE, 0.95f * brightness));

    Vector3 quadB[6] = {
        { cx - 0.16f, y0, cz + 0.16f }, { cx + 0.16f, y0, cz - 0.16f },
        { cx + 0.16f, y1, cz - 0.16f }, { cx - 0.16f, y0, cz + 0.16f },
        { cx + 0.16f, y1, cz - 0.16f }, { cx - 0.16f, y1, cz + 0.16f }
    };
    Vector3 normalB = Vector3Normalize((Vector3){ 1.0f, 0.0f, -1.0f });
    AddMeshFace(mesh, vertexIndex, quadB, normalB, uvs, ShadeColor(WHITE, 0.85f * brightness));
}
bool ChunkBlockHasTransparentMesh(BlockType type)
{
    return IsTranslucentBlock(type);
}

static int CountChunkFacesFiltered(const unsigned short (*blocks)[CHUNK_SIZE],
                                   int height, int layerY,
                                   int chunkX, int chunkZ,
                                   bool transparent, bool includePlants,
                                   bool plantsOnly, const int faces[6][3])
{
    int faceCount = 0;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < height; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                BlockType type = (BlockType)blocks[lx * height + y][lz];
                bool plant = type == BLOCK_FLOWER || type == BLOCK_MUSHROOM;
                if (plantsOnly) {
                    if (plant) faceCount += 2;
                    continue;
                }
                if (type == BLOCK_AIR || ChunkBlockHasTransparentMesh(type) != transparent) continue;
                if (plant) {
                    if (includePlants) faceCount += 2;
                    continue;
                }
                if (type == BLOCK_TORCH) {
                    faceCount += 6;
                    continue;
                }
                if (type == BLOCK_ALBUM) {
                    faceCount += 5;
                    continue;
                }
                if (type == BLOCK_SLAB || type == BLOCK_DOOR || type == BLOCK_DOOR_OPEN) {
                    int customFaces = (type == BLOCK_SLAB) ? 6 : 5;
                    for (int face = 0; face < customFaces; face++) {
                        if (ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) faceCount++;
                    }
                    continue;
                }
                if (type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) {
                    faceCount += 9;
                    continue;
                }
                if (type == BLOCK_FENCE) {
                    faceCount += 2;
                    for (int d = 0; d < 4; d++) {
                        BlockType neighbor = FenceNeighborBlock(blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                                                                faces[d][0], faces[d][2]);
                        if (neighbor == BLOCK_AIR || FenceShouldConnect(neighbor)) faceCount++;
                    }
                    continue;
                }
                if (type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN) {
                    faceCount += 3;
                    continue;
                }
                if (type == BLOCK_GLASS_PANE) {
                    faceCount += 5;
                    continue;
                }
                for (int face = 0; face < 6; face++) {
                    if (ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) faceCount++;
                }
            }
        }
    }
    return faceCount;
}

static bool BuildMeshDataFiltered(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, bool transparent, bool includePlants,
    bool plantsOnly, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    int faceCount = CountChunkFacesFiltered(
        blocks, height, layerY, chunkX, chunkZ, transparent,
        includePlants, plantsOnly, faces);
    if (faceCount == 0) return false;

    int startX = chunkX * CHUNK_SIZE;
    int startZ = chunkZ * CHUNK_SIZE;

    Mesh mesh = { 0 };
    mesh.vertexCount = faceCount * 6;
    mesh.triangleCount = faceCount * 2;
    mesh.vertices = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.texcoords = malloc((size_t)mesh.vertexCount * 2 * sizeof(float));
    mesh.normals = malloc((size_t)mesh.vertexCount * 3 * sizeof(float));
    mesh.colors = malloc((size_t)mesh.vertexCount * 4 * sizeof(unsigned char));

    if (!mesh.vertices || !mesh.texcoords || !mesh.normals || !mesh.colors) {
        free(mesh.vertices);
        free(mesh.texcoords);
        free(mesh.normals);
        free(mesh.colors);
        return false;
    }

    int vertexIndex = 0;
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int y = 0; y < height; y++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                BlockType type = (BlockType)blocks[lx * height + y][lz];
                bool plant = type == BLOCK_FLOWER || type == BLOCK_MUSHROOM;
                if (plantsOnly) {
                    if (!plant) continue;
                } else {
                    if (type == BLOCK_AIR ||
                        ChunkBlockHasTransparentMesh(type) != transparent) continue;
                    if (plant && !includePlants) continue;
                }

                int x = startX + lx;
                int z = startZ + lz;
                float blockLight = TorchLightAtBlockNearby(x, y, z, nearbyTorchIndices, nearbyTorchCount);
                if (type == BLOCK_TORCH) {
                    AddTorchMesh(&mesh, &vertexIndex, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_ALBUM) {
                    AddAlbumMesh(&mesh, &vertexIndex, x, y, z, blockLight);
                    continue;
                }
                if (type == BLOCK_SLAB) {
                    AddSlabMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces, blockLight);
                    continue;
                }
                if (type == BLOCK_DOOR || type == BLOCK_DOOR_OPEN) {
                    AddDoorMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces, type, blockLight);
                    continue;
                }
                if (type == BLOCK_STONE_STAIRS || type == BLOCK_WOOD_STAIRS) {
                    AddStairsMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz, type, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE) {
                    AddFenceMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (type == BLOCK_FENCE_GATE || type == BLOCK_FENCE_GATE_OPEN) {
                    AddGateMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz,
                                type == BLOCK_FENCE_GATE_OPEN, blockLight);
                    continue;
                }
                if (type == BLOCK_GLASS_PANE) {
                    AddPaneMesh(&mesh, &vertexIndex, blocks, height, layerY, chunkX, chunkZ, lx, y, lz, blockLight);
                    continue;
                }
                if (type == BLOCK_FLOWER || type == BLOCK_MUSHROOM) {
                    AddPlantMesh(&mesh, &vertexIndex, x, y, z, type, blockLight);
                    continue;
                }
                for (int face = 0; face < 6; face++) {
                    if (ChunkFaceIsVisible(blocks, height, layerY, chunkX, chunkZ, lx, y, lz, faces[face][0], faces[face][1], faces[face][2])) {
                        AddBlockFace(&mesh, &vertexIndex, x, y, z, face, type, WHITE, blockLight);
                    }
                }
            }
        }
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
        blocks, height, layerY, chunkX, chunkZ, transparent, true, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildSurfaceSolidMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildSurfaceWaterMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, true, false, false,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

bool BuildFloraMeshData(
    const unsigned short (*blocks)[CHUNK_SIZE], int height, int layerY,
    int chunkX, int chunkZ, const int faces[6][3],
    const int *nearbyTorchIndices, int nearbyTorchCount, Mesh *outMesh)
{
    return BuildMeshDataFiltered(
        blocks, height, layerY, chunkX, chunkZ, false, false, true,
        faces, nearbyTorchIndices, nearbyTorchCount, outMesh);
}

#define MAX_MESH_JOBS 64
#define MAX_MESH_SUBMITS_PER_FRAME 4

static MeshJob meshJobs[MAX_MESH_JOBS];

static bool HasPendingMeshJob(void)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) return true;
    }
    return false;
}

static MeshJob *NextPendingMeshJob(void)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) return &meshJobs[i];
    }
    return NULL;
}

static bool FindPendingMeshJob(int slotIndex)
{
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && meshJobs[i].slotIndex == slotIndex) return true;
    }
    return false;
}

static void FreeMeshData(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static void ReplaceChunkModel(Model *model, bool *hasModel,
                              Mesh *mesh, bool hasMesh, bool dynamic)
{
    if (*hasModel) {
        UnloadModel(*model);
        *model = (Model){ 0 };
        *hasModel = false;
    }
    if (!hasMesh) {
        FreeMeshData(mesh);
        return;
    }

    UploadMesh(mesh, dynamic);
    *model = LoadModelFromMesh(*mesh);
    SetMaterialTexture(&model->materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
    *hasModel = true;
}

static void InitializeFloraTargets(Chunk *chunk)
{
    ClearChunkFloraRuntime(chunk);
    if (!chunk->hasFloraModel || chunk->floraModel.meshCount <= 0) return;

    Mesh *mesh = &chunk->floraModel.meshes[0];
    if (mesh->vertexCount <= 0 || !mesh->vertices || !mesh->colors) return;
    int count = (mesh->vertexCount + 11) / 12;
    chunk->floraTargetScales = malloc((size_t)count * sizeof(float));
    chunk->floraTargetWind = malloc((size_t)count * sizeof(float));
    chunk->floraTargetPresence = malloc((size_t)count * sizeof(float));
    chunk->floraBaseVertices = malloc(
        (size_t)mesh->vertexCount * 3u * sizeof(float));
    chunk->floraBaseColors = malloc((size_t)mesh->vertexCount * 4u);
    chunk->floraVisualInstances = malloc(
        (size_t)count * sizeof(FloraVisualInstance));
    if (!chunk->floraTargetScales || !chunk->floraTargetWind ||
        !chunk->floraTargetPresence || !chunk->floraBaseVertices ||
        !chunk->floraBaseColors || !chunk->floraVisualInstances) {
        ClearChunkFloraRuntime(chunk);
        return;
    }
    memcpy(chunk->floraBaseVertices, mesh->vertices,
           (size_t)mesh->vertexCount * 3u * sizeof(float));
    memcpy(chunk->floraBaseColors, mesh->colors,
           (size_t)mesh->vertexCount * 4u);
    for (int index = 0; index < count; index++) {
        int firstVertex = index * 12;
        int vertexCount = mesh->vertexCount - firstVertex;
        if (vertexCount > 12) vertexCount = 12;
        float groundY = INFINITY;
        for (int vertex = firstVertex;
             vertex < firstVertex + vertexCount; vertex++) {
            groundY = fminf(groundY, mesh->vertices[vertex * 3 + 1]);
        }
        float firstX = mesh->vertices[firstVertex * 3];
        float firstZ = mesh->vertices[firstVertex * 3 + 2];
        chunk->floraVisualInstances[index] = (FloraVisualInstance){
            .firstVertex = firstVertex,
            .vertexCount = vertexCount,
            .anchor = {
                floorf(firstX) + 0.5f,
                groundY,
                floorf(firstZ) + 0.5f
            },
            .windResponse = 1.0f
        };
        chunk->floraTargetScales[index] = 1.0f;
        chunk->floraTargetWind[index] = 0.0f;
        chunk->floraTargetPresence[index] = 1.0f;
    }
    chunk->floraTargetScaleCount = count;
}

static void UploadMeshJob(MeshJob *job)
{
    Chunk *chunk = &chunks[job->slotIndex];
    bool valid = chunk->loaded && chunk->cx == job->cx && chunk->cz == job->cz;

    if (valid) {
        if (job->transparent) {
            ReplaceChunkModel(&chunk->waterModel, &chunk->hasWaterModel,
                              &job->mesh, job->hasMesh, false);
            FreeMeshData(&job->floraMesh);
        } else {
            ReplaceChunkModel(&chunk->model, &chunk->hasModel,
                              &job->mesh, job->hasMesh, false);
            ReplaceChunkModel(&chunk->floraModel, &chunk->hasFloraModel,
                              &job->floraMesh, job->hasFloraMesh, true);
            InitializeFloraTargets(chunk);
            chunk->floraVisualScale = 1.0f;
        }
    } else {
        FreeMeshData(&job->mesh);
        FreeMeshData(&job->floraMesh);
    }

    job->inUse = false;
    if (valid && !FindPendingMeshJob(job->slotIndex)) chunk->dirty = false;
}

bool SubmitMeshJob(Chunk *chunk, bool transparent)
{
    if (genThread == 0) return false;

    pthread_mutex_lock(&genMutex);
    MeshJob *job = NULL;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (!meshJobs[i].inUse) {
            job = &meshJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&genMutex);
        return false;
    }

    memcpy(job->blocks, chunk->blocks, sizeof(job->blocks));
    job->nearbyCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        job->nearbyIndices);
    job->inUse = true;
    job->done = false;
    job->slotIndex = (int)(chunk - chunks);
    job->cx = chunk->cx;
    job->cz = chunk->cz;
    job->transparent = transparent;
    job->mesh = (Mesh){ 0 };
    job->floraMesh = (Mesh){ 0 };
    job->hasMesh = false;
    job->hasFloraMesh = false;
    pthread_cond_signal(&genCond);
    pthread_mutex_unlock(&genMutex);
    return true;
}

void ProcessFinishedMeshJobs(void)
{
    int uploaded = 0;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        MeshJob *job = &meshJobs[i];
        if (!job->inUse || !job->done) continue;
        UploadMeshJob(job);
        if (++uploaded >= MAX_MESH_REBUILDS_PER_FRAME) break;
    }
}

static void RebuildChunkMeshSync(Chunk *chunk)
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

    UnloadChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    Mesh floraMesh = { 0 };
    bool hasSolid = BuildSurfaceSolidMeshData(
        (const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
        WORLD_HEIGHT, 0, chunk->cx, chunk->cz, faces,
        nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildSurfaceWaterMeshData(
        (const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
        WORLD_HEIGHT, 0, chunk->cx, chunk->cz, faces,
        nearbyTorchIndices, nearbyTorchCount, &waterMesh);
    bool hasFlora = BuildFloraMeshData(
        (const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
        WORLD_HEIGHT, 0, chunk->cx, chunk->cz, faces,
        nearbyTorchIndices, nearbyTorchCount, &floraMesh);

    ReplaceChunkModel(&chunk->model, &chunk->hasModel,
                      &solidMesh, hasSolid, false);
    ReplaceChunkModel(&chunk->waterModel, &chunk->hasWaterModel,
                      &waterMesh, hasWater, false);
    ReplaceChunkModel(&chunk->floraModel, &chunk->hasFloraModel,
                      &floraMesh, hasFlora, true);
    InitializeFloraTargets(chunk);
    chunk->floraVisualScale = 1.0f;
    chunk->dirty = false;
}

void RebuildDirtyChunkMeshes(void)
{
    int submitted = 0;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (!chunks[i].loaded || !chunks[i].dirty) continue;
        if (FindPendingMeshJob(i)) continue;

        if (genThread == 0) {
            RebuildChunkMeshSync(&chunks[i]);
            continue;
        }

        if (!SubmitMeshJob(&chunks[i], false) || !SubmitMeshJob(&chunks[i], true)) {
            RebuildChunkMeshSync(&chunks[i]);
            continue;
        }
        if (++submitted >= MAX_MESH_SUBMITS_PER_FRAME) break;
    }
}

bool ChunkWithinDrawDistance(const Chunk *chunk, Vector3 cameraPosition, int effectiveRenderDistance)
{
    int cameraX = (int)floorf(cameraPosition.x);
    int cameraZ = (int)floorf(cameraPosition.z);
    int cameraCx = 0;
    int cameraCz = 0;
    int cameraLx = 0;
    int cameraLz = 0;
    WorldToChunkLocal(cameraX, cameraZ, &cameraCx, &cameraCz, &cameraLx, &cameraLz);

    return abs(chunk->cx - cameraCx) <= effectiveRenderDistance &&
           abs(chunk->cz - cameraCz) <= effectiveRenderDistance;
}

bool SphereInFrustum(const Camera3D *camera, Vector3 center, float radius)
{
    Vector3 forward = Vector3Normalize(Vector3Subtract(camera->target, camera->position));
    Vector3 right = Vector3Normalize(Vector3CrossProduct(forward, camera->up));
    Vector3 up = Vector3Normalize(Vector3CrossProduct(right, forward));
    Vector3 toCenter = Vector3Subtract(center, camera->position);

    float depth = Vector3DotProduct(toCenter, forward);
    if (depth + radius < CAMERA_NEAR_CULL_DISTANCE) return false;

    float visibleDepth = fmaxf(depth, CAMERA_NEAR_CULL_DISTANCE);
    float verticalTan = tanf(camera->fovy * DEG2RAD * 0.5f);
    int screenHeight = GetScreenHeight();
    float aspect = screenHeight > 0 ? (float)GetScreenWidth() / (float)screenHeight : 1.0f;
    float horizontalTan = verticalTan * aspect;
    float horizontalOffset = fabsf(Vector3DotProduct(toCenter, right));
    float verticalOffset = fabsf(Vector3DotProduct(toCenter, up));

    return horizontalOffset <= visibleDepth * horizontalTan + radius &&
           verticalOffset <= visibleDepth * verticalTan + radius;
}

bool ChunkIntersectsCameraView(const Chunk *chunk, const Camera3D *camera)
{
    Vector3 center = {
        (float)(chunk->cx * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f,
        (float)WORLD_HEIGHT * 0.5f,
        (float)(chunk->cz * CHUNK_SIZE) + (float)CHUNK_SIZE * 0.5f
    };
    float halfChunk = (float)CHUNK_SIZE * 0.5f;
    float halfHeight = (float)WORLD_HEIGHT * 0.5f;
    float radius = sqrtf(halfChunk * halfChunk * 2.0f + halfHeight * halfHeight);

    return SphereInFrustum(camera, center, radius);
}


bool ChunksStartGenThread(void)
{
    return pthread_create(&genThread, NULL, ChunkGenWorker, NULL) == 0;
}

void ChunksShutdownGenThread(void)
{
    DrainChunkGen();
    if (genThread != 0) {
        pthread_mutex_lock(&genMutex);
        genShutdown = true;
        pthread_cond_broadcast(&genCond);
        pthread_mutex_unlock(&genMutex);
        pthread_join(genThread, NULL);
        genThread = 0;
    }
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse) {
            if (meshJobs[i].hasMesh) FreeMeshData(&meshJobs[i].mesh);
            meshJobs[i].inUse = false;
        }
    }
}

int GetActiveChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_ACTIVE_CHUNKS; i++) {
        if (chunks[i].loaded) count++;
    }
    return count;
}

int GetPendingGenJobCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_CHUNK_GEN_JOBS; i++) {
        if (chunkGenJobs[i].inUse && !chunkGenJobs[i].done) count++;
    }
    return count;
}

int GetPendingMeshJobCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_MESH_JOBS; i++) {
        if (meshJobs[i].inUse && !meshJobs[i].done) count++;
    }
    return count;
}
