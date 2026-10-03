#ifndef VOXELCRAFT_VOLCANO_RUNTIME_H
#define VOXELCRAFT_VOLCANO_RUNTIME_H

#include "world/volcano_model.h"
#include <stdio.h>

#define VOLCANO_MAX_RECORDS 64u
#define VOLCANO_MAX_ACTIVE 2u
#define VOLCANO_ENTER_RADIUS 160.0f
#define VOLCANO_LEAVE_RADIUS 224.0f

typedef struct VolcanoRecord {
    VolcanoDefinition definition;
    VolcanoState state;
    double accumulator;
    bool active;
} VolcanoRecord;

typedef bool (*VolcanoSiteReady)(const VolcanoDefinition *definition, void *context);
typedef struct VolcanoContext {
    bool valid;
    bool discover;
    uint32_t bodyId, seed;
    Vector3 observerMap;
    VolcanoSiteSample sample;
    VolcanoSiteReady ready;
    unsigned (*emitLava)(const VolcanoDefinition *definition, unsigned units, void *userData);
    bool (*hasHotLava)(const VolcanoDefinition *definition, void *userData);
    void *userData;
} VolcanoContext;

typedef struct VolcanoStats {
    unsigned count, active;
    uint64_t rejectedAdmissions;
} VolcanoStats;

void VolcanoRuntimeReset(void);
void VolcanoRuntimeUpdate(double seconds, const VolcanoContext *context);
bool VolcanoRuntimeForce(VolcanoDefinition definition, float intensity, unsigned ticks);
bool VolcanoRuntimeStop(uint64_t id);
bool VolcanoRuntimeAt(unsigned index, VolcanoRecord *out);
bool VolcanoRuntimeNearest(uint32_t bodyId, Vector3 observerMap, VolcanoRecord *out);
VolcanoStats VolcanoRuntimeStats(void);
bool VolcanoRuntimeSave(FILE *file);
bool VolcanoRuntimeLoad(FILE *file);

#endif
