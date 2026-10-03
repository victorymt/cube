#ifndef VOXELCRAFT_VOLCANO_MODEL_H
#define VOXELCRAFT_VOLCANO_MODEL_H

#include "world/surface_topology.h"

#define VOLCANO_GENERATOR_VERSION 1u
#define VOLCANO_STEP_SECONDS 0.5
#define VOLCANO_CANDIDATE_SPACING 128

typedef enum VolcanoPhase {
    VOLCANO_DORMANT = 0, VOLCANO_UNREST, VOLCANO_ERUPTING,
    VOLCANO_WANING, VOLCANO_COOLING, VOLCANO_RECOVERY
} VolcanoPhase;

typedef struct VolcanoDefinition {
    uint64_t id;
    uint32_t seed;
    SurfaceAddress address;
    int mapX, mapZ;
    float potential;
    bool forced;
} VolcanoDefinition;

typedef struct VolcanoState {
    VolcanoPhase phase;
    uint32_t phaseTicks;
    uint32_t eruptionTicks;
    uint32_t event;
    uint64_t activeTicks;
    float intensity;
    float pressure;
    /* Cubic blocks; runtime debits only accepted 1/1024-block discharge. */
    float reservoir;
} VolcanoState;

typedef bool (*VolcanoSiteSample)(SurfaceAddress address, float *potential,
                                  int *height, void *context);

uint64_t VolcanoId(SurfaceAddress address);
SurfaceMapCell VolcanoMapCell(SurfaceAddress address);
VolcanoDefinition VolcanoDefine(uint32_t bodyId, uint32_t seed,
                                int mapX, int height, int mapZ,
                                float potential, bool forced);
bool VolcanoCandidateAt(SurfaceAddress address, uint32_t seed,
                         VolcanoSiteSample sample, void *context,
                         VolcanoDefinition *out);
bool VolcanoDefinitionValid(const VolcanoDefinition *definition);
bool VolcanoStateValid(const VolcanoState *state);
VolcanoState VolcanoModelCreate(const VolcanoDefinition *definition);
bool VolcanoModelForce(VolcanoState *state, float intensity, unsigned ticks);
void VolcanoModelStop(VolcanoState *state);
void VolcanoModelStep(VolcanoState *state, const VolcanoDefinition *definition);
const char *VolcanoPhaseName(VolcanoPhase phase);

#endif
