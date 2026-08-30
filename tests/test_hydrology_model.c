#include "world/hydrology_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestConfigAndBounds(void)
{
    HydrologyConfig config = HydrologyDefaultConfig();
    config.width = 0;
    assert(!HydrologyConfigNormalize(&config));

    config = HydrologyDefaultConfig();
    config.width = 2;
    config.height = 1;
    config.maxGroundwaterDepthM = -1.0f;
    config.infiltrationRateMPerS = NAN;
    assert(HydrologyConfigNormalize(&config));
    assert(config.maxGroundwaterDepthM == 0.0f);
    assert(config.infiltrationRateMPerS == 0.0f);

    HydrologyGrid grid = { 0 };
    assert(HydrologyInit(&grid, config));
    assert(HydrologyCellAt(&grid, -1, 0) == NULL);
    assert(!HydrologySetTerrain(&grid, 2, 0, 0.0f, 1.0f));
    HydrologyFree(&grid);
}

static void TestRainfallInfiltrationConservesWater(void)
{
    HydrologyConfig config = HydrologyDefaultConfig();
    config.width = 1;
    config.height = 1;
    config.cellSizeM = 10.0f;
    config.infiltrationRateMPerS = 0.25f;
    config.maxGroundwaterDepthM = 1.0f;
    config.groundwaterDischargeRatePerS = 0.0f;
    config.surfaceFlowRatePerS = 0.0f;
    config.evaporationRateMPerS = 0.0f;
    config.erosionRatePerS = 0.0f;
    config.depositionRatePerS = 0.0f;
    HydrologyGrid grid = { 0 };
    assert(HydrologyInit(&grid, config));
    assert(HydrologySetTerrain(&grid, 0, 0, 0.0f, 2.0f));
    assert(HydrologyAddPrecipitation(&grid, 0, 0, 2.0f));
    double before = HydrologyTotalWaterVolumeM3(&grid);
    assert(HydrologyStep(&grid, 1.0f));
    double after = HydrologyTotalWaterVolumeM3(&grid);
    assert(fabs(before - after) < 0.0001);
    const HydrologyCell *cell = HydrologyCellAt(&grid, 0, 0);
    assert(cell != NULL);
    assert(fabsf(cell->groundwaterDepthM - 0.25f) < 0.0001f);
    HydrologyStats stats = HydrologyGetStats(&grid);
    assert(fabs(stats.precipitationVolumeM3 - 200.0) < 0.0001);
    assert(fabs(stats.infiltrationVolumeM3 - 25.0) < 0.0001);
    HydrologyFree(&grid);
}

static void TestDownhillRoutingIsDeterministic(void)
{
    HydrologyConfig config = HydrologyDefaultConfig();
    config.width = 3;
    config.height = 1;
    config.maxGroundwaterDepthM = 0.0f;
    config.infiltrationRateMPerS = 0.0f;
    config.groundwaterDischargeRatePerS = 0.0f;
    config.surfaceFlowRatePerS = 8.0f;
    config.evaporationRateMPerS = 0.0f;
    config.erosionRatePerS = 0.0f;
    config.depositionRatePerS = 0.0f;
    HydrologyGrid first = { 0 };
    HydrologyGrid second = { 0 };
    assert(HydrologyInit(&first, config));
    assert(HydrologyInit(&second, config));
    for (int x = 0; x < 3; x++) {
        assert(HydrologySetTerrain(&first, x, 0, (float)(3 - x), 1.0f));
        assert(HydrologySetTerrain(&second, x, 0, (float)(3 - x), 1.0f));
    }
    assert(HydrologySetSurfaceWater(&first, 0, 0, 1.0f));
    assert(HydrologySetSurfaceWater(&second, 0, 0, 1.0f));
    assert(HydrologyStep(&first, 1.0f));
    assert(HydrologyStep(&second, 1.0f));
    const HydrologyCell *middle = HydrologyCellAt(&first, 1, 0);
    const HydrologyCell *right = HydrologyCellAt(&first, 2, 0);
    assert(middle->surfaceWaterDepthM > 0.90f);
    assert(right->surfaceWaterDepthM == 0.0f);
    assert(fabs(HydrologyTotalWaterVolumeM3(&first) -
               HydrologyTotalWaterVolumeM3(&second)) < 0.0001);
    for (int x = 0; x < 3; x++) {
        const HydrologyCell *a = HydrologyCellAt(&first, x, 0);
        const HydrologyCell *b = HydrologyCellAt(&second, x, 0);
        assert(memcmp(a, b, sizeof(*a)) == 0);
    }
    assert(HydrologyStep(&first, 1.0f));
    assert(HydrologyCellAt(&first, 2, 0)->surfaceWaterDepthM > 0.90f);
    HydrologyFree(&first);
    HydrologyFree(&second);
}

static void TestErosionAndDepositionConserveSediment(void)
{
    HydrologyConfig config = HydrologyDefaultConfig();
    config.width = 2;
    config.height = 1;
    config.maxGroundwaterDepthM = 0.0f;
    config.infiltrationRateMPerS = 0.0f;
    config.groundwaterDischargeRatePerS = 0.0f;
    config.surfaceFlowRatePerS = 4.0f;
    config.evaporationRateMPerS = 0.0f;
    config.erosionRatePerS = 0.8f;
    config.depositionRatePerS = 0.0f;
    config.sedimentCapacityKgM2PerWaterM = 100000.0f;
    HydrologyGrid grid = { 0 };
    assert(HydrologyInit(&grid, config));
    assert(HydrologySetTerrain(&grid, 0, 0, 10.0f, 2.0f));
    assert(HydrologySetTerrain(&grid, 1, 0, 0.0f, 2.0f));
    assert(HydrologySetSurfaceWater(&grid, 0, 0, 1.0f));
    double sedimentBefore = HydrologyTotalSedimentMassKg(&grid);
    assert(HydrologyStep(&grid, 1.0f));
    const HydrologyCell *source = HydrologyCellAt(&grid, 0, 0);
    assert(source->elevationM < 10.0f);
    assert(source->sedimentLoadKgM2 > 0.0f);
    assert(HydrologyStep(&grid, 1.0f));
    const HydrologyCell *sink = HydrologyCellAt(&grid, 1, 0);
    assert(sink->sedimentLoadKgM2 > 0.0f);
    assert(fabs(HydrologyTotalSedimentMassKg(&grid) - sedimentBefore) <
           0.01);

    assert(HydrologySetSedimentLoad(&grid, 1, 0, 500.0f));
    config = grid.config;
    config.depositionRatePerS = 1.0f;
    config.sedimentCapacityKgM2PerWaterM = 0.0f;
    grid.config = config;
    float soilBefore = HydrologyCellAt(&grid, 1, 0)->soilDepthM;
    assert(HydrologyStep(&grid, 1.0f));
    assert(HydrologyCellAt(&grid, 1, 0)->soilDepthM > soilBefore);
    HydrologyFree(&grid);
}

static void TestOpenBoundaryReportsRunoff(void)
{
    HydrologyConfig config = HydrologyDefaultConfig();
    config.width = 1;
    config.height = 1;
    config.maxGroundwaterDepthM = 0.0f;
    config.infiltrationRateMPerS = 0.0f;
    config.groundwaterDischargeRatePerS = 0.0f;
    config.surfaceFlowRatePerS = 2.0f;
    config.evaporationRateMPerS = 0.0f;
    config.erosionRatePerS = 0.0f;
    config.depositionRatePerS = 0.0f;
    config.openBoundaries = true;
    HydrologyGrid grid = { 0 };
    assert(HydrologyInit(&grid, config));
    assert(HydrologySetTerrain(&grid, 0, 0, 10.0f, 1.0f));
    assert(HydrologySetSurfaceWater(&grid, 0, 0, 1.0f));
    assert(HydrologyStep(&grid, 1.0f));
    HydrologyStats stats = HydrologyGetStats(&grid);
    assert(stats.runoffOutVolumeM3 > 0.0);
    assert(HydrologyCellAt(&grid, 0, 0)->surfaceWaterDepthM < 0.2f);
    assert(!HydrologyStep(&grid, 0.0f));
    HydrologyFree(&grid);
}

int main(void)
{
    TestConfigAndBounds();
    TestRainfallInfiltrationConservesWater();
    TestDownhillRoutingIsDeterministic();
    TestErosionAndDepositionConserveSediment();
    TestOpenBoundaryReportsRunoff();
    puts("hydrology model tests passed");
    return 0;
}
