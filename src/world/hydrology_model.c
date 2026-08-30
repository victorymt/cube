#include "world/hydrology_model.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HYDROLOGY_MAX_CELLS 1048576
#define HYDROLOGY_DIRECTIONS 4
#define HYDROLOGY_EPSILON 0.000001f

static float HydrologyClamp(float value, float low, float high)
{
    if (!isfinite(value)) return low;
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static float HydrologyNonNegative(float value)
{
    return isfinite(value) && value > 0.0f ? value : 0.0f;
}

static int HydrologyIndex(const HydrologyGrid *grid, int x, int z)
{
    if (!grid || x < 0 || z < 0 || x >= grid->config.width ||
        z >= grid->config.height) {
        return -1;
    }
    return z * grid->config.width + x;
}

static int HydrologyNeighbor(int x, int z, int direction,
                             int width, int height)
{
    static const int offsetX[HYDROLOGY_DIRECTIONS] = { 1, -1, 0, 0 };
    static const int offsetZ[HYDROLOGY_DIRECTIONS] = { 0, 0, 1, -1 };
    int nx = x + offsetX[direction];
    int nz = z + offsetZ[direction];
    if (nx < 0 || nz < 0 || nx >= width || nz >= height) return -1;
    return nz * width + nx;
}

static float HydrologyBoundaryHead(const HydrologyCell *cell,
                                   float cellSizeM)
{
    /* An open edge is one cell below the source bed, so it behaves as an
     * outlet without introducing an infinite gradient or NaN arithmetic. */
    return cell->elevationM - fmaxf(cellSizeM, 1.0f);
}

HydrologyConfig HydrologyDefaultConfig(void)
{
    return (HydrologyConfig){
        .width = 32,
        .height = 32,
        .cellSizeM = 16.0f,
        .maxGroundwaterDepthM = 8.0f,
        .infiltrationRateMPerS = 0.00008f,
        .groundwaterDischargeRatePerS = 0.00002f,
        .surfaceFlowRatePerS = 0.80f,
        .evaporationRateMPerS = 0.000001f,
        .erosionRatePerS = 0.025f,
        .depositionRatePerS = 0.40f,
        .sedimentCapacityKgM2PerWaterM = 24.0f,
        .soilDensityKgPerM3 = 1600.0f,
        .openBoundaries = false
    };
}

bool HydrologyConfigNormalize(HydrologyConfig *config)
{
    if (!config || config->width <= 0 || config->height <= 0 ||
        config->width > HYDROLOGY_MAX_CELLS ||
        config->height > HYDROLOGY_MAX_CELLS) {
        return false;
    }
    size_t count = (size_t)config->width * (size_t)config->height;
    if (count == 0u || count > HYDROLOGY_MAX_CELLS) return false;

    if (!isfinite(config->cellSizeM) || config->cellSizeM < 0.01f) {
        config->cellSizeM = 1.0f;
    }
    if (!isfinite(config->maxGroundwaterDepthM) ||
        config->maxGroundwaterDepthM < 0.0f) {
        config->maxGroundwaterDepthM = 0.0f;
    }
    if (!isfinite(config->infiltrationRateMPerS) ||
        config->infiltrationRateMPerS < 0.0f) {
        config->infiltrationRateMPerS = 0.0f;
    }
    if (!isfinite(config->groundwaterDischargeRatePerS) ||
        config->groundwaterDischargeRatePerS < 0.0f) {
        config->groundwaterDischargeRatePerS = 0.0f;
    }
    if (!isfinite(config->surfaceFlowRatePerS) ||
        config->surfaceFlowRatePerS < 0.0f) {
        config->surfaceFlowRatePerS = 0.0f;
    }
    if (!isfinite(config->evaporationRateMPerS) ||
        config->evaporationRateMPerS < 0.0f) {
        config->evaporationRateMPerS = 0.0f;
    }
    if (!isfinite(config->erosionRatePerS) || config->erosionRatePerS < 0.0f) {
        config->erosionRatePerS = 0.0f;
    }
    if (!isfinite(config->depositionRatePerS) ||
        config->depositionRatePerS < 0.0f) {
        config->depositionRatePerS = 0.0f;
    }
    if (!isfinite(config->sedimentCapacityKgM2PerWaterM) ||
        config->sedimentCapacityKgM2PerWaterM < 0.0f) {
        config->sedimentCapacityKgM2PerWaterM = 0.0f;
    }
    if (!isfinite(config->soilDensityKgPerM3) ||
        config->soilDensityKgPerM3 < 1.0f) {
        config->soilDensityKgPerM3 = 1.0f;
    }
    return true;
}

bool HydrologyInit(HydrologyGrid *grid, HydrologyConfig config)
{
    if (!grid || !HydrologyConfigNormalize(&config)) return false;
    memset(grid, 0, sizeof(*grid));
    size_t count = (size_t)config.width * (size_t)config.height;
    HydrologyCell *cells = calloc(count, sizeof(*cells));
    HydrologyCell *scratch = calloc(count, sizeof(*scratch));
    float *flow = calloc(count * HYDROLOGY_DIRECTIONS, sizeof(*flow));
    float *flowSourceWater = calloc(count, sizeof(*flowSourceWater));
    float *flowSourceSediment = calloc(count, sizeof(*flowSourceSediment));
    if (!cells || !scratch || !flow || !flowSourceWater ||
        !flowSourceSediment) {
        free(cells);
        free(scratch);
        free(flow);
        free(flowSourceWater);
        free(flowSourceSediment);
        return false;
    }
    grid->config = config;
    grid->cells = cells;
    grid->scratch = scratch;
    grid->flow = flow;
    grid->flowSourceWater = flowSourceWater;
    grid->flowSourceSediment = flowSourceSediment;
    grid->cellCount = (int)count;
    for (int i = 0; i < grid->cellCount; i++) {
        grid->cells[i].soilDepthM = 1.0f;
        grid->scratch[i] = grid->cells[i];
    }
    return true;
}

void HydrologyFree(HydrologyGrid *grid)
{
    if (!grid) return;
    free(grid->cells);
    free(grid->scratch);
    free(grid->flow);
    free(grid->flowSourceWater);
    free(grid->flowSourceSediment);
    memset(grid, 0, sizeof(*grid));
}

void HydrologyReset(HydrologyGrid *grid)
{
    if (!grid || !grid->cells) return;
    memset(grid->cells, 0, (size_t)grid->cellCount * sizeof(*grid->cells));
    memset(grid->scratch, 0, (size_t)grid->cellCount * sizeof(*grid->scratch));
    memset(&grid->stats, 0, sizeof(grid->stats));
    for (int i = 0; i < grid->cellCount; i++) {
        grid->cells[i].soilDepthM = 1.0f;
        grid->scratch[i] = grid->cells[i];
    }
}

bool HydrologySetTerrain(HydrologyGrid *grid, int x, int z,
                         float elevationM, float soilDepthM)
{
    int index = HydrologyIndex(grid, x, z);
    if (index < 0 || !isfinite(elevationM) || !isfinite(soilDepthM)) {
        return false;
    }
    grid->cells[index].elevationM = elevationM;
    grid->cells[index].soilDepthM = HydrologyNonNegative(soilDepthM);
    return true;
}

bool HydrologySetSurfaceWater(HydrologyGrid *grid, int x, int z, float depthM)
{
    int index = HydrologyIndex(grid, x, z);
    if (index < 0 || !isfinite(depthM)) return false;
    grid->cells[index].surfaceWaterDepthM = HydrologyNonNegative(depthM);
    return true;
}

bool HydrologySetSedimentLoad(HydrologyGrid *grid, int x, int z,
                              float loadKgM2)
{
    int index = HydrologyIndex(grid, x, z);
    if (index < 0 || !isfinite(loadKgM2)) return false;
    grid->cells[index].sedimentLoadKgM2 = HydrologyNonNegative(loadKgM2);
    return true;
}

bool HydrologyAddPrecipitation(HydrologyGrid *grid, int x, int z, float depthM)
{
    int index = HydrologyIndex(grid, x, z);
    if (index < 0 || !isfinite(depthM) || depthM < 0.0f) return false;
    grid->cells[index].surfaceWaterDepthM += depthM;
    grid->stats.precipitationVolumeM3 +=
        (double)depthM * (double)grid->config.cellSizeM *
        (double)grid->config.cellSizeM;
    return true;
}

bool HydrologyAddGroundwater(HydrologyGrid *grid, int x, int z, float depthM)
{
    int index = HydrologyIndex(grid, x, z);
    if (index < 0 || !isfinite(depthM) || depthM < 0.0f) return false;
    float available = fmaxf(grid->config.maxGroundwaterDepthM -
                            grid->cells[index].groundwaterDepthM, 0.0f);
    grid->cells[index].groundwaterDepthM += fminf(depthM, available);
    return true;
}

const HydrologyCell *HydrologyCellAt(const HydrologyGrid *grid, int x, int z)
{
    int index = HydrologyIndex(grid, x, z);
    return index >= 0 ? &grid->cells[index] : NULL;
}

static void HydrologyPrepareCell(HydrologyGrid *grid, int index,
                                 float dtSeconds)
{
    const HydrologyConfig *config = &grid->config;
    const HydrologyCell *cell = &grid->cells[index];
    HydrologyCell *next = &grid->scratch[index];
    float surface = HydrologyNonNegative(cell->surfaceWaterDepthM);
    float groundwater = HydrologyClamp(cell->groundwaterDepthM, 0.0f,
                                       config->maxGroundwaterDepthM);
    float soil = HydrologyNonNegative(cell->soilDepthM);
    float sediment = HydrologyNonNegative(cell->sedimentLoadKgM2);
    float area = config->cellSizeM * config->cellSizeM;

    float evaporation = fminf(surface,
                              config->evaporationRateMPerS * dtSeconds);
    surface -= evaporation;
    grid->stats.evaporationVolumeM3 += (double)evaporation * (double)area;

    float groundwaterDischarge = fminf(
        groundwater, groundwater * config->groundwaterDischargeRatePerS *
        dtSeconds);
    groundwater -= groundwaterDischarge;
    surface += groundwaterDischarge;
    grid->stats.groundwaterDischargeVolumeM3 +=
        (double)groundwaterDischarge * (double)area;

    float saturation = config->maxGroundwaterDepthM > HYDROLOGY_EPSILON
                           ? groundwater / config->maxGroundwaterDepthM
                           : 1.0f;
    float infiltrationCapacity = config->infiltrationRateMPerS * dtSeconds *
                                 HydrologyClamp(1.0f - saturation, 0.0f, 1.0f);
    float infiltration = fminf(surface, infiltrationCapacity);
    surface -= infiltration;
    groundwater += infiltration;
    grid->stats.infiltrationVolumeM3 += (double)infiltration * (double)area;

    if (groundwater > config->maxGroundwaterDepthM) {
        surface += groundwater - config->maxGroundwaterDepthM;
        groundwater = config->maxGroundwaterDepthM;
    }

    float maxDrop = 0.0f;
    int x = index % config->width;
    int z = index / config->width;
    for (int direction = 0; direction < HYDROLOGY_DIRECTIONS; direction++) {
        int neighbor = HydrologyNeighbor(x, z, direction, config->width,
                                         config->height);
        float elevation = 0.0f;
        if (neighbor >= 0) {
            elevation = grid->cells[neighbor].elevationM;
        } else if (config->openBoundaries) {
            elevation = HydrologyBoundaryHead(cell, config->cellSizeM);
        } else {
            continue;
        }
        float drop = cell->elevationM - elevation;
        if (drop > maxDrop) maxDrop = drop;
    }

    float slope = maxDrop / fmaxf(config->cellSizeM, HYDROLOGY_EPSILON);
    float erosionDepth = fminf(soil, config->erosionRatePerS * dtSeconds *
                               slope * fminf(surface, 1.0f));
    soil -= erosionDepth;
    sediment += erosionDepth * config->soilDensityKgPerM3;
    next->elevationM = cell->elevationM - erosionDepth;
    next->soilDepthM = soil;
    next->sedimentLoadKgM2 = sediment;
    next->surfaceWaterDepthM = surface;
    next->groundwaterDepthM = groundwater;
    next->runoffM3PerS = 0.0f;
    grid->stats.erodedSedimentKg += (double)erosionDepth * (double)area *
                                    (double)config->soilDensityKgPerM3;

    float capacity = config->sedimentCapacityKgM2PerWaterM * surface;
    float excess = fmaxf(sediment - capacity, 0.0f);
    float depositionMass = excess * HydrologyClamp(
        config->depositionRatePerS * dtSeconds, 0.0f, 1.0f);
    float depositionDepth = depositionMass / config->soilDensityKgPerM3;
    next->sedimentLoadKgM2 = fmaxf(sediment - depositionMass, 0.0f);
    next->soilDepthM += depositionDepth;
    next->elevationM += depositionDepth;
    grid->stats.depositedSedimentKg += (double)depositionMass * (double)area;
}

static void HydrologyPrepareFlows(HydrologyGrid *grid, float dtSeconds)
{
    const HydrologyConfig *config = &grid->config;
    float flowFraction = 1.0f - expf(-config->surfaceFlowRatePerS * dtSeconds);
    flowFraction = HydrologyClamp(flowFraction, 0.0f, 1.0f);
    memset(grid->flow, 0, (size_t)grid->cellCount * HYDROLOGY_DIRECTIONS *
           sizeof(*grid->flow));
    for (int index = 0; index < grid->cellCount; index++) {
        int x = index % config->width;
        int z = index / config->width;
        HydrologyCell *next = &grid->scratch[index];
        float sourceWater = next->surfaceWaterDepthM;
        float sourceSediment = next->sedimentLoadKgM2;
        grid->flowSourceWater[index] = sourceWater;
        grid->flowSourceSediment[index] = sourceSediment;
        float sourceHead = next->elevationM + sourceWater;
        float dropSum = 0.0f;
        float drops[HYDROLOGY_DIRECTIONS] = { 0 };
        for (int direction = 0; direction < HYDROLOGY_DIRECTIONS; direction++) {
            int neighbor = HydrologyNeighbor(x, z, direction, config->width,
                                             config->height);
            float neighborHead;
            if (neighbor >= 0) {
                const HydrologyCell *target = &grid->scratch[neighbor];
                neighborHead = target->elevationM + target->surfaceWaterDepthM;
            } else if (config->openBoundaries) {
                neighborHead = HydrologyBoundaryHead(next, config->cellSizeM);
            } else {
                continue;
            }
            float drop = sourceHead - neighborHead;
            if (drop > 0.0f) {
                drops[direction] = drop;
                dropSum += drop;
            }
        }
        float totalOut = sourceWater * flowFraction;
        if (dropSum <= HYDROLOGY_EPSILON) totalOut = 0.0f;
        float actualOut = 0.0f;
        for (int direction = 0; direction < HYDROLOGY_DIRECTIONS;
             direction++) {
            float amount = dropSum > HYDROLOGY_EPSILON
                               ? totalOut * drops[direction] / dropSum
                               : 0.0f;
            grid->flow[index * HYDROLOGY_DIRECTIONS + direction] = amount;
            actualOut += amount;
        }
        next->runoffM3PerS = dtSeconds > 0.0f
                                 ? actualOut * config->cellSizeM *
                                       config->cellSizeM / dtSeconds
                                 : 0.0f;
    }
}

static void HydrologyApplyFlows(HydrologyGrid *grid)
{
    const HydrologyConfig *config = &grid->config;
    for (int index = 0; index < grid->cellCount; index++) {
        float sourceWater = grid->flowSourceWater[index];
        float sourceSediment = grid->flowSourceSediment[index];
        float totalOut = 0.0f;
        for (int direction = 0; direction < HYDROLOGY_DIRECTIONS;
             direction++) {
            totalOut += grid->flow[index * HYDROLOGY_DIRECTIONS + direction];
        }
        float sedimentOut = sourceWater > HYDROLOGY_EPSILON
                                ? sourceSediment * totalOut / sourceWater
                                : 0.0f;
        grid->scratch[index].surfaceWaterDepthM = fmaxf(
            grid->scratch[index].surfaceWaterDepthM - totalOut, 0.0f);
        grid->scratch[index].sedimentLoadKgM2 = fmaxf(
            grid->scratch[index].sedimentLoadKgM2 - sedimentOut, 0.0f);
        for (int direction = 0; direction < HYDROLOGY_DIRECTIONS;
             direction++) {
            float amount = grid->flow[index * HYDROLOGY_DIRECTIONS + direction];
            if (amount <= 0.0f) continue;
            int x = index % config->width;
            int z = index / config->width;
            int neighbor = HydrologyNeighbor(x, z, direction, config->width,
                                             config->height);
            if (neighbor < 0) {
                grid->stats.runoffOutVolumeM3 += (double)amount *
                    (double)config->cellSizeM * (double)config->cellSizeM;
                continue;
            }
            float sedimentAmount = sourceWater > HYDROLOGY_EPSILON
                                       ? sourceSediment * amount / sourceWater
                                       : 0.0f;
            grid->scratch[neighbor].surfaceWaterDepthM += amount;
            grid->scratch[neighbor].sedimentLoadKgM2 += sedimentAmount;
        }
    }
}

bool HydrologyStep(HydrologyGrid *grid, float dtSeconds)
{
    if (!grid || !grid->cells || !grid->scratch || !isfinite(dtSeconds) ||
        dtSeconds <= 0.0f) {
        return false;
    }
    if (dtSeconds > 86400.0f) dtSeconds = 86400.0f;
    for (int index = 0; index < grid->cellCount; index++) {
        HydrologyPrepareCell(grid, index, dtSeconds);
    }
    HydrologyPrepareFlows(grid, dtSeconds);
    HydrologyApplyFlows(grid);
    memcpy(grid->cells, grid->scratch,
           (size_t)grid->cellCount * sizeof(*grid->cells));
    grid->stats.steps++;
    return true;
}

double HydrologyTotalWaterVolumeM3(const HydrologyGrid *grid)
{
    if (!grid || !grid->cells) return 0.0;
    double area = (double)grid->config.cellSizeM *
                  (double)grid->config.cellSizeM;
    double totalDepth = 0.0;
    for (int index = 0; index < grid->cellCount; index++) {
        const HydrologyCell *cell = &grid->cells[index];
        totalDepth += (double)HydrologyNonNegative(cell->surfaceWaterDepthM);
        totalDepth += (double)HydrologyNonNegative(cell->groundwaterDepthM);
    }
    return totalDepth * area;
}

double HydrologyTotalSedimentMassKg(const HydrologyGrid *grid)
{
    if (!grid || !grid->cells) return 0.0;
    double area = (double)grid->config.cellSizeM *
                  (double)grid->config.cellSizeM;
    double total = 0.0;
    for (int index = 0; index < grid->cellCount; index++) {
        const HydrologyCell *cell = &grid->cells[index];
        total += (double)HydrologyNonNegative(cell->sedimentLoadKgM2) * area;
        total += (double)HydrologyNonNegative(cell->soilDepthM) * area *
                 (double)grid->config.soilDensityKgPerM3;
    }
    return total;
}

HydrologyStats HydrologyGetStats(const HydrologyGrid *grid)
{
    return grid ? grid->stats : (HydrologyStats){ 0 };
}
