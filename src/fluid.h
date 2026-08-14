#ifndef VOXELCRAFT_FLUID_H
#define VOXELCRAFT_FLUID_H

#include "types.h"

#include <stdio.h>
#include <stdint.h>

#define FLUID_CAPACITY 255u
#define FLUID_TICK_RATE 10.0f
#define FLUID_MAX_CELLS_PER_TICK 4096u
#define FLUID_BLOCK_DISPLACEMENT_MAX_CELLS 7

typedef struct FluidSample {
    uint8_t volume;
    float surfaceY;
    Vector3 velocity;
} FluidSample;

typedef struct FluidStats {
    uint64_t ticks;
    uint64_t processedCells;
    uint64_t transferredVolume;
    uint32_t activeCells;
    uint32_t editCount;
    uint32_t maxActiveCells;
    uint32_t queueOverflows;
    uint32_t lastProcessedCells;
} FluidStats;

typedef struct FluidVolumeChange {
    int x;
    int y;
    int z;
    uint8_t before;
    uint8_t after;
    uint8_t baseline;
    bool baselineKnown;
} FluidVolumeChange;

typedef struct FluidBlockDisplacement {
    uint8_t count;
    FluidVolumeChange cells[FLUID_BLOCK_DISPLACEMENT_MAX_CELLS];
} FluidBlockDisplacement;

void FluidReset(void);
void FluidCleanup(void);
void FluidUpdate(float dt);
void FluidStepTicks(unsigned ticks);
void FluidWakeCell(int x, int y, int z);
void FluidOnBlockChanged(int x, int y, int z, BlockType previous,
                         BlockType next);
void FluidApplyEditsToChunk(Chunk *chunk);
void FluidOnChunkLoaded(Chunk *chunk);

uint8_t FluidGetVolumeAt(int x, int y, int z);
bool FluidSetVolumeAt(int x, int y, int z, uint8_t volume);
FluidSample FluidSampleAt(Vector3 position);
bool FluidTryDepositUnit(int x, int y, int z);
bool FluidTryCollectUnit(int x, int y, int z);
bool FluidTryDisplaceForBlock(int x, int y, int z);
bool FluidTryDisplaceForBlockTracked(int x, int y, int z,
                                     FluidBlockDisplacement *outDisplacement);
bool FluidReplayBlockDisplacement(
    const FluidBlockDisplacement *displacement, bool after);
uint64_t FluidLoadedVolume(void);
FluidStats FluidGetStats(void);

bool FluidSaveState(FILE *file);
bool FluidLoadState(FILE *file);

#endif
