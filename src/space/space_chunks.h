#ifndef VOXELCRAFT_SPACE_CHUNKS_H
#define VOXELCRAFT_SPACE_CHUNKS_H

#include "core/perf_metrics.h"
#include "world/world_types.h"

#include <stdbool.h>

#define MAX_SPACE_CHUNKS \
    ((SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1) * \
     (SPACE_RENDER_DISTANCE_CHUNKS * 2 + 1))
#define MAX_SPACE_EDITS 65536
#define MAX_SPACE_GEN_JOBS 16

typedef struct SpaceChunk {
    bool loaded;
    bool generating;
    bool dirty;
    bool hasModel;
    bool hasWaterModel;
    int cx;
    int cz;
    Model model;
    Model waterModel;
    unsigned short blocks[CHUNK_SIZE][SPACE_LAYER_HEIGHT][CHUNK_SIZE];
} SpaceChunk;

void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance,
                       int generationPerFrame);
void SpaceProcessFinishedGenJobs(void);
const SpaceChunk *SpaceChunksView(void);
void SpaceUpdateSolarGlow(Vector3 playerPosition);
BlockType SpaceBlockAt(int x, int y, int z);
bool SpaceBlockReadyAt(int x, int y, int z);
void SpaceSetBlock(int x, int y, int z, BlockType type);
void UnloadAllSpaceChunks(void);
int GetActiveSpaceChunkCount(void);
RenderResourceSnapshot SpaceGetRenderResourceSnapshot(void);
int GetSpaceEditCount(void);
void SpaceRebuildTorchList(void);

#endif
