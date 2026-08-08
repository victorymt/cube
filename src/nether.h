#ifndef VOXELCRAFT_NETHER_H
#define VOXELCRAFT_NETHER_H

#include "types.h"

#include <stdio.h>

#define MAX_NETHER_CHUNKS ((NETHER_RENDER_DISTANCE_CHUNKS * 2 + 1) * (NETHER_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define MAX_NETHER_EDITS 65536

typedef struct NetherChunk {
    bool loaded;
    bool dirty;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
} NetherChunk;

extern NetherChunk netherChunks[MAX_NETHER_CHUNKS];

void NetherInit(void);
void UpdateNetherChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
BlockType NetherBlockAt(int x, int y, int z);
void NetherSetBlock(int x, int y, int z, BlockType type);
void NetherSaveEdits(FILE *file);
void NetherLoadEdits(FILE *file);
void UnloadAllNetherChunks(void);
int GetActiveNetherChunkCount(void);

#endif
