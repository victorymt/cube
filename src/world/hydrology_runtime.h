#ifndef VOXELCRAFT_HYDROLOGY_RUNTIME_H
#define VOXELCRAFT_HYDROLOGY_RUNTIME_H

#include "world/hydrology_model.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

/* One bounded coarse solver covers the currently active canonical surface. */
#define HYDROLOGY_RUNTIME_STATE_VERSION 1u
#define HYDROLOGY_RUNTIME_STATE_MAGIC 0x48594431u /* "HYD1" */
#define HYDROLOGY_RUNTIME_REGION_SIZE 128
#define HYDROLOGY_RUNTIME_GRID_SIZE 32
#define HYDROLOGY_RUNTIME_DEFAULT_FIXED_STEP_SECONDS (1.0 / 60.0)
#define HYDROLOGY_RUNTIME_DEFAULT_MAX_CATCH_UP_STEPS 8u
#define HYDROLOGY_RUNTIME_DEFAULT_CELL_SIZE_M 4.0

typedef struct HydrologyRuntimeConfig {
    double fixedStepSeconds;
    unsigned maxCatchUpSteps;
    HydrologyConfig model;
    float initialSurfaceWaterM;
    float initialGroundwaterM;
    float initialSoilDepthM;
} HydrologyRuntimeConfig;

typedef struct HydrologyRuntimeLocation {
    uint32_t surfaceId;
    int regionX;
    int regionZ;
} HydrologyRuntimeLocation;

typedef struct HydrologyRuntimeCellInput {
    float elevationM;
    float soilDepthM;
    /* Rainfall depth rate in metres per second. */
    float precipitationMPerS;
} HydrologyRuntimeCellInput;

typedef struct HydrologyRuntimeFeedback {
    bool active;
    float surfaceWaterM;
    float soilMoisture;
    float groundwaterM;
    float runoffM3PerS;
    float evaporationMPerS;
    float sedimentLoadKgM2;
} HydrologyRuntimeFeedback;

typedef struct HydrologyRuntimeSnapshot {
    uint32_t version;
    uint64_t stepCount;
    double simulationTimeSeconds;
    double accumulatorSeconds;
    HydrologyRuntimeLocation location;
    HydrologyStats stats;
    double totalWaterVolumeM3;
    double totalSedimentMassKg;
    bool active;
    bool valid;
} HydrologyRuntimeSnapshot;

typedef struct HydrologyRuntime {
    uint32_t version;
    HydrologyRuntimeConfig config;
    HydrologyGrid grid;
    float *precipitationRatesMPerS;
    HydrologyRuntimeLocation location;
    double accumulatorSeconds;
    double simulationTimeSeconds;
    uint64_t stepCount;
    bool active;
    bool initialized;
} HydrologyRuntime;

HydrologyRuntimeConfig HydrologyRuntimeDefaultConfig(void);
bool HydrologyRuntimeConfigNormalize(HydrologyRuntimeConfig *config);
bool HydrologyRuntimeConfigIsValid(const HydrologyRuntimeConfig *config);

bool HydrologyRuntimeInit(HydrologyRuntime *runtime,
                          const HydrologyRuntimeConfig *config);
void HydrologyRuntimeFree(HydrologyRuntime *runtime);
void HydrologyRuntimeReset(HydrologyRuntime *runtime);
bool HydrologyRuntimeStateIsValid(const HydrologyRuntime *runtime);

bool HydrologyRuntimeBind(HydrologyRuntime *runtime,
                          uint32_t surfaceId, int mapX, int mapZ);
bool HydrologyRuntimeBindRegion(HydrologyRuntime *runtime,
                                uint32_t surfaceId, int regionX, int regionZ);
bool HydrologyRuntimeSetCellInput(HydrologyRuntime *runtime,
                                  int x, int z,
                                  HydrologyRuntimeCellInput input);
bool HydrologyRuntimeSetTerrainCell(HydrologyRuntime *runtime,
                                    int x, int z,
                                    float elevationM, float soilDepthM);
bool HydrologyRuntimeSetPrecipitationRateCell(HydrologyRuntime *runtime,
                                              int x, int z,
                                              float rateMPerS);
bool HydrologyRuntimeAddPrecipitationCell(HydrologyRuntime *runtime,
                                          int x, int z, float depthM);
bool HydrologyRuntimeStep(HydrologyRuntime *runtime);
bool HydrologyRuntimeAdvance(HydrologyRuntime *runtime,
                             double elapsedSeconds, unsigned *outSteps);
bool HydrologyRuntimeQueryCell(const HydrologyRuntime *runtime,
                               int x, int z, HydrologyCell *outCell);
bool HydrologyRuntimeGetSnapshot(const HydrologyRuntime *runtime,
                                 HydrologyRuntimeSnapshot *out);

/* Query by canonical map cell; this is the preferred cross-system boundary. */
bool HydrologyRuntimeFeedbackAt(const HydrologyRuntime *runtime,
                                uint32_t surfaceId, int mapX, int mapZ,
                                HydrologyRuntimeFeedback *out);

bool HydrologyRuntimeGlobalReset(const HydrologyRuntimeConfig *config);
void HydrologyRuntimeGlobalFree(void);
bool HydrologyRuntimeGlobalBind(uint32_t surfaceId, int mapX, int mapZ);
/* Process-local epoch, changed by reset/free, region rebinding and successful
 * load (including loading the same region). Never serialized. */
uint64_t HydrologyRuntimeGlobalGeneration(void);
bool HydrologyRuntimeGlobalSetCellInput(int x, int z,
                                        HydrologyRuntimeCellInput input);
bool HydrologyRuntimeGlobalSetTerrainCell(int x, int z,
                                          float elevationM, float soilDepthM);
bool HydrologyRuntimeGlobalSetPrecipitationRateCell(int x, int z,
                                                    float rateMPerS);
bool HydrologyRuntimeGlobalAddPrecipitationCell(int x, int z, float depthM);
bool HydrologyRuntimeGlobalAdvance(double elapsedSeconds, unsigned *outSteps);
bool HydrologyRuntimeGlobalSnapshot(HydrologyRuntimeSnapshot *out);
bool HydrologyRuntimeGlobalFeedbackAt(uint32_t surfaceId,
                                      int mapX, int mapZ,
                                      HydrologyRuntimeFeedback *out);
bool HydrologyRuntimeGlobalSaveState(FILE *file);
bool HydrologyRuntimeGlobalLoadState(FILE *file);

#endif
