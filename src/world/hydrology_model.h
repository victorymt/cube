#ifndef VOXELCRAFT_HYDROLOGY_MODEL_H
#define VOXELCRAFT_HYDROLOGY_MODEL_H

#include <stdbool.h>
#include <stdint.h>

/*
 * A small, deterministic surface-water and aquifer model.  Depths are in
 * metres, sediment load is kilograms per square metre, and terrain elevation
 * is metres above the local datum.
 */
typedef struct HydrologyConfig {
    int width;
    int height;
    float cellSizeM;
    float maxGroundwaterDepthM;
    float infiltrationRateMPerS;
    float groundwaterDischargeRatePerS;
    float surfaceFlowRatePerS;
    float evaporationRateMPerS;
    float erosionRatePerS;
    float depositionRatePerS;
    float sedimentCapacityKgM2PerWaterM;
    float soilDensityKgPerM3;
    bool openBoundaries;
} HydrologyConfig;

typedef struct HydrologyCell {
    float elevationM;
    float surfaceWaterDepthM;
    float groundwaterDepthM;
    float soilDepthM;
    float sedimentLoadKgM2;
    float runoffM3PerS;
} HydrologyCell;

typedef struct HydrologyStats {
    uint64_t steps;
    double precipitationVolumeM3;
    double evaporationVolumeM3;
    double infiltrationVolumeM3;
    double groundwaterDischargeVolumeM3;
    double runoffOutVolumeM3;
    double erodedSedimentKg;
    double depositedSedimentKg;
} HydrologyStats;

typedef struct HydrologyGrid {
    HydrologyConfig config;
    HydrologyCell *cells;
    HydrologyCell *scratch;
    float *flow;
    float *flowSourceWater;
    float *flowSourceSediment;
    int cellCount;
    HydrologyStats stats;
} HydrologyGrid;

HydrologyConfig HydrologyDefaultConfig(void);
bool HydrologyConfigNormalize(HydrologyConfig *config);
bool HydrologyInit(HydrologyGrid *grid, HydrologyConfig config);
void HydrologyFree(HydrologyGrid *grid);
void HydrologyReset(HydrologyGrid *grid);

bool HydrologySetTerrain(HydrologyGrid *grid, int x, int z,
                         float elevationM, float soilDepthM);
bool HydrologySetSurfaceWater(HydrologyGrid *grid, int x, int z,
                              float depthM);
bool HydrologySetSedimentLoad(HydrologyGrid *grid, int x, int z,
                              float loadKgM2);
bool HydrologyAddPrecipitation(HydrologyGrid *grid, int x, int z,
                               float depthM);
bool HydrologyAddGroundwater(HydrologyGrid *grid, int x, int z,
                             float depthM);
const HydrologyCell *HydrologyCellAt(const HydrologyGrid *grid, int x, int z);

bool HydrologyStep(HydrologyGrid *grid, float dtSeconds);
double HydrologyTotalWaterVolumeM3(const HydrologyGrid *grid);
double HydrologyTotalSedimentMassKg(const HydrologyGrid *grid);
HydrologyStats HydrologyGetStats(const HydrologyGrid *grid);

#endif
