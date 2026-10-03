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
    /* Fractional contact-time credits, not stored heat. Energy remains in
       thermal until an entire water parcel can be atomically evaporated. */
    double contactCredit[6];
    double steam; /* transient recent committed evaporation; never saved */
    LavaMaterial material;
} LavaCell;

typedef enum LavaBoundary { LAVA_UNKNOWN, LAVA_BLOCKED, LAVA_OPEN } LavaBoundary;
typedef struct LavaContext {
    bool valid;
    uint32_t bodyId;
    LavaBoundary (*boundary)(int x, int y, int z, void *user);
    double (*ambient)(int x, int z, void *user);
    bool (*solidify)(int x, int y, int z, void *user);
    void *user;
    /* Unknown/unloaded water returns false and parks the face credit. */
    bool (*water)(int x, int y, int z, unsigned *volume, void *user);
    /* Must compare expected volume and commit all units or none. Reservoir
       units are externally replenished and accounted separately. */
    bool (*evaporate)(int x, int y, int z, unsigned expected, unsigned units,
                      bool *reservoir, void *user);
} LavaContext;

typedef struct LavaStats {
    unsigned records, hotCells;
    uint64_t supplied, solidified, removed, blockedTransfers, rejectedCells, ticks;
    uint64_t volume, liquidVolume, solidVolume;
    double energy, suppliedEnergy, cooledEnergy, exportedEnergy, accumulator;
    uint64_t evaporatedWater, reservoirWater;
    double waterEnergy;
} LavaStats;

void LavaRuntimeReset(void);
bool LavaRuntimeAt(unsigned index, LavaCell *out);
bool LavaRuntimeCellAt(uint32_t body, int x, int y, int z, LavaCell *out);
unsigned LavaRuntimeDeposit(const LavaContext *context, int x, int y, int z,
                            unsigned volume, double temperatureK);
unsigned LavaRuntimeDepositMaterial(const LavaContext *context, int x, int y, int z,
                                     unsigned volume, double temperatureK,
                                     double silicaFraction, double vesicleFraction);
void LavaRuntimeStep(const LavaContext *context);
/* Partition-independent 10 Hz driver; callback supplies and steps the world. */
void LavaRuntimeAdvance(double seconds, bool active,
                         void (*tick)(void *user), void *user);
LavaStats LavaRuntimeStats(void);
/* Only cells that can advance in this context may delay vent recovery. */
bool LavaRuntimeHasActiveHotNear(const LavaContext *context,
                                 int x, int y, int z, float radius);
bool LavaRuntimeSave(FILE *file);
bool LavaRuntimeLoad(FILE *file);
#endif
