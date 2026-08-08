#ifndef VOXELCRAFT_SPACE_H
#define VOXELCRAFT_SPACE_H

#include "types.h"

#include <stdio.h>

#define MAX_SPACE_CHUNKS ((SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1) * (SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define MAX_SPACE_EDITS 65536

typedef struct SpaceChunk {
    bool loaded;
    bool dirty;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    bool hasStar;
    int starX;
    int starY;
    int starZ;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][SPACE_LAYER_HEIGHT][CHUNK_SIZE];
} SpaceChunk;

extern SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];

void SpaceInit(void);
void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame);
void SpaceUpdateStarGlow(Vector3 playerPosition);
void SpaceUpdateSolarGlow(Vector3 playerPosition);
void SolarSystemBodies(Vector3 *positions, int maxCount);
BlockType SpaceBlockAt(int x, int y, int z);
void SpaceSetBlock(int x, int y, int z, BlockType type);
void SpaceSaveEdits(FILE *file);
void SpaceLoadEdits(FILE *file);
void UnloadAllSpaceChunks(void);
int GetActiveSpaceChunkCount(void);
int GetSpaceEditCount(void);
void SpaceRebuildTorchList(void);

#endif
