#include "world/hydrology_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestFixedStepAndBinding(void)
{
    HydrologyRuntime runtime = { 0 };
    HydrologyRuntimeConfig config = HydrologyRuntimeDefaultConfig();
    config.fixedStepSeconds = 1.0;
    config.maxCatchUpSteps = 4u;
    assert(HydrologyRuntimeInit(&runtime, &config));
    assert(HydrologyRuntimeBind(&runtime, 9u, -1, -1));
    assert(runtime.location.regionX == -1 && runtime.location.regionZ == -1);
    assert(HydrologyRuntimeSetCellInput(&runtime, 0, 0,
        (HydrologyRuntimeCellInput){ .elevationM = 10.0f,
            .soilDepthM = 2.0f, .precipitationMPerS = 0.001f }));
    unsigned steps = 0u;
    assert(HydrologyRuntimeAdvance(&runtime, 0.5, &steps));
    assert(steps == 0u && runtime.stepCount == 0u);
    assert(HydrologyRuntimeAdvance(&runtime, 0.5, &steps));
    assert(steps == 1u && runtime.stepCount == 1u);
    HydrologyCell cell = { 0 };
    assert(HydrologyRuntimeQueryCell(&runtime, 0, 0, &cell));
    assert(cell.surfaceWaterDepthM >= 0.0f);
    assert(HydrologyRuntimeBind(&runtime, 9u, 128, 128));
    assert(runtime.location.regionX == 1 && runtime.location.regionZ == 1);
    assert(runtime.stepCount == 0u);
    HydrologyRuntimeFree(&runtime);
}

static void TestCatchUpAndFeedback(void)
{
    HydrologyRuntime runtime = { 0 };
    HydrologyRuntimeConfig config = HydrologyRuntimeDefaultConfig();
    config.fixedStepSeconds = 0.25;
    config.maxCatchUpSteps = 3u;
    assert(HydrologyRuntimeInit(&runtime, &config));
    assert(HydrologyRuntimeBindRegion(&runtime, 0u, 0, 0));
    assert(HydrologyRuntimeSetCellInput(&runtime, 3, 2,
        (HydrologyRuntimeCellInput){ .elevationM = 2.0f,
            .soilDepthM = 1.5f, .precipitationMPerS = 0.02f }));
    unsigned steps = 0u;
    assert(HydrologyRuntimeAdvance(&runtime, 5.0, &steps));
    assert(steps == 3u);
    assert(runtime.accumulatorSeconds < config.fixedStepSeconds *
           (double)config.maxCatchUpSteps + 1e-9);
    HydrologyRuntimeFeedback feedback = { 0 };
    assert(HydrologyRuntimeFeedbackAt(&runtime, 0u, 14, 10, &feedback));
    assert(feedback.active && feedback.soilMoisture >= 0.0f &&
           feedback.soilMoisture <= 1.0f);
    assert(!HydrologyRuntimeFeedbackAt(&runtime, 0u, 128, 0, &feedback));
    HydrologyRuntimeFree(&runtime);
}

static void TestRoundTripAndCorruption(void)
{
    assert(HydrologyRuntimeGlobalReset(NULL));
    assert(HydrologyRuntimeGlobalBind(4u, 17, 33));
    unsigned steps = 0u;
    assert(HydrologyRuntimeGlobalAdvance(1.0 / 60.0, &steps));
    FILE *file = tmpfile();
    assert(file != NULL);
    assert(HydrologyRuntimeGlobalSaveState(file));
    rewind(file);
    assert(HydrologyRuntimeGlobalReset(NULL));
    assert(HydrologyRuntimeGlobalLoadState(file));
    HydrologyRuntimeSnapshot snapshot = { 0 };
    assert(HydrologyRuntimeGlobalSnapshot(&snapshot));
    assert(snapshot.active && snapshot.location.surfaceId == 4u);
    fclose(file);

    file = tmpfile();
    assert(file != NULL);
    unsigned char bad = 0xffu;
    assert(fwrite(&bad, sizeof(bad), 1, file) == 1);
    rewind(file);
    assert(!HydrologyRuntimeGlobalLoadState(file));
    fclose(file);
}

int main(void)
{
    TestFixedStepAndBinding();
    TestCatchUpAndFeedback();
    TestRoundTripAndCorruption();
    puts("hydrology runtime tests passed");
    return 0;
}
