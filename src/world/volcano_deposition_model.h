#ifndef VOXELCRAFT_VOLCANO_DEPOSITION_MODEL_H
#define VOXELCRAFT_VOLCANO_DEPOSITION_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define VOLCANO_DEPOSITION_MAX_SIDE 256u
#define VOLCANO_DEPOSITION_MAX_STEPS 100000u
#define VOLCANO_DEPOSITION_STEP_SECONDS 0.1

/* Closed-boundary, constant-density column model. Distances are blocks,
   volume is blocks cubed, energy is volume times specific lava enthalpy.
   Caller owns each independent model; there is no world/runtime access. */
typedef struct VolcanoDepositionConfig {
    unsigned width, height, maxSteps;
    double cellSize, maxDepth, ambientK;
    double mobility;      /* Surface-gradient flow coefficient, blocks/second. */
    double cooling;       /* Enthalpy loss per exposed square block/second. */
} VolcanoDepositionConfig;
typedef struct VolcanoDepositionCell {
    double ground, volume, energy;
    double liquidVolume, solidDepth, surface, temperatureK;
    double transportedVolume; /* Cumulative accepted incoming liquid, excluding supply. */
} VolcanoDepositionCell;
typedef struct VolcanoDepositionLedger {
    unsigned steps;
    double suppliedVolume, suppliedEnergy, cooledEnergy;
    double retainedVolume, retainedEnergy, liquidVolume, boundaryVolume;
    double volumeError, energyError;
} VolcanoDepositionLedger;
typedef struct VolcanoDepositionModel VolcanoDepositionModel;

/* Copies width*height finite initial heights. Returns NULL on invalid input
   or allocation failure. No input is retained; destroy accepts NULL. */
VolcanoDepositionModel *VolcanoDepositionCreate(VolcanoDepositionConfig config,
                                               const double *ground);
void VolcanoDepositionDestroy(VolcanoDepositionModel *model);
/* Returns accepted volume. Capacity rejection stays with the caller. Invalid
   requests return zero without mutation; cannot supply after the step limit. */
double VolcanoDepositionSupply(VolcanoDepositionModel *model, unsigned x, unsigned y,
                                double volume, double temperatureK);
/* One fixed step: old-state fluxes, simultaneous transport, then surface cooling.
   At the limit returns false with all remaining mass/heat intact. */
bool VolcanoDepositionStep(VolcanoDepositionModel *model);
/* Explicit historical end-of-era cooling. Removes only heat, records it in
   cooledEnergy, and works after the step budget is exhausted. Returns loss. */
double VolcanoDepositionCoolToAmbient(VolcanoDepositionModel *model);
bool VolcanoDepositionRead(const VolcanoDepositionModel *model, unsigned x, unsigned y,
                            VolcanoDepositionCell *out);
VolcanoDepositionLedger VolcanoDepositionInspect(const VolcanoDepositionModel *model);
#ifdef VOLCANO_DEPOSITION_TESTING
bool VolcanoDepositionStepReverse(VolcanoDepositionModel *model);
bool VolcanoDepositionStepFullGrid(VolcanoDepositionModel *model);
#endif
#endif
