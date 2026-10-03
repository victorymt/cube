#ifndef VOXELCRAFT_ASH_RUNTIME_H
#define VOXELCRAFT_ASH_RUNTIME_H
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define ASH_CAPACITY 1024u
#define ASH_MAX_CELLS 2048u
#define ASH_STEP_SECONDS 0.1

typedef struct AshCell {
    uint32_t bodyId;
    int x, y, z;
    unsigned volume;
    double washCredit;
} AshCell;
typedef enum AshBoundary { ASH_UNKNOWN, ASH_BLOCKED, ASH_OPEN } AshBoundary;
typedef struct AshContext {
    bool valid;
    uint32_t bodyId;
    AshBoundary (*boundary)(int x, int y, int z, void *user);
    /* Rain/water exposure [0,1]. Unknown exposure must return zero. */
    double (*wetness)(int x, int y, int z, void *user);
    void *user;
} AshContext;
typedef struct AshStats {
    unsigned cells;
    uint64_t supplied, volume, washed, rejected, ticks, collected;
} AshStats;

void AshRuntimeReset(void);
bool AshRuntimeAt(unsigned index, AshCell *out);
bool AshRuntimeCellAt(uint32_t body, int x, int y, int z, AshCell *out);
unsigned AshRuntimeDeposit(const AshContext *context, int x, int y, int z, unsigned units);
/* Compare and remove one whole overlay cell; caller owns inventory transaction. */
bool AshRuntimeCollect(uint32_t body, int x, int y, int z, unsigned expectedUnits);
void AshRuntimeStep(const AshContext *context);
AshStats AshRuntimeStats(void);
bool AshRuntimeSave(FILE *file);
bool AshRuntimeLoad(FILE *file);
#endif
