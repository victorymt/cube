#ifndef VOXELCRAFT_LAVA_RUNTIME_H
#define VOXELCRAFT_LAVA_RUNTIME_H
#include "world/lava_model.h"
#include <stdio.h>

#define LAVA_MAX_HOT_CELLS 2048u
#define LAVA_MAX_RECORDS 8192u

typedef struct LavaCell {
    uint32_t bodyId;
    int x, y, z;
    LavaThermalState thermal;
    bool cooled;
} LavaCell;

typedef enum LavaBoundary { LAVA_UNKNOWN, LAVA_BLOCKED, LAVA_OPEN } LavaBoundary;
typedef struct LavaContext {
    bool valid;
    uint32_t bodyId;
    LavaBoundary (*boundary)(int x, int y, int z, void *user);
    double (*ambient)(int x, int z, void *user);
    bool (*solidify)(int x, int y, int z, void *user);
    void *user;
} LavaContext;

typedef struct LavaStats {
    unsigned records, hotCells;
    uint64_t supplied, solidified, removed, blockedTransfers, rejectedCells, ticks;
    uint64_t volume, liquidVolume, solidVolume;
    double energy, suppliedEnergy, cooledEnergy, exportedEnergy, accumulator;
} LavaStats;

void LavaRuntimeReset(void);
bool LavaRuntimeAt(unsigned index, LavaCell *out);
bool LavaRuntimeCellAt(uint32_t body, int x, int y, int z, LavaCell *out);
unsigned LavaRuntimeDeposit(const LavaContext *context, int x, int y, int z,
                            unsigned volume, double temperatureK);
void LavaRuntimeStep(const LavaContext *context);
/* Partition-independent 10 Hz driver; callback supplies and steps the world. */
void LavaRuntimeAdvance(double seconds, bool active,
                         void (*tick)(void *user), void *user);
LavaStats LavaRuntimeStats(void);
bool LavaRuntimeHasHotNear(uint32_t body, int x, int y, int z, float radius);
bool LavaRuntimeSave(FILE *file);
bool LavaRuntimeLoad(FILE *file);
#endif
