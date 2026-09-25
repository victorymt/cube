#include "world/hydrology_runtime.h"

#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS \
    (HYDROLOGY_RUNTIME_GRID_SIZE * HYDROLOGY_RUNTIME_GRID_SIZE)

static bool PositiveFinite(double value)
{
    return isfinite(value) && value > 0.0;
}

static bool NonNegativeFinite(double value)
{
    return isfinite(value) && value >= 0.0;
}

static float Clamp01(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static int RegionCoordinate(int mapCoordinate)
{
    int64_t value = mapCoordinate;
    if (value >= 0) return (int)(value / HYDROLOGY_RUNTIME_REGION_SIZE);
    return (int)(-((-value + HYDROLOGY_RUNTIME_REGION_SIZE - 1) /
                  HYDROLOGY_RUNTIME_REGION_SIZE));
}

static int GridCoordinate(int mapCoordinate, int region)
{
    int regionOrigin = region * HYDROLOGY_RUNTIME_REGION_SIZE;
    int local = mapCoordinate - regionOrigin;
    if (local < 0 || local >= HYDROLOGY_RUNTIME_REGION_SIZE) return -1;
    return local / (HYDROLOGY_RUNTIME_REGION_SIZE /
                    HYDROLOGY_RUNTIME_GRID_SIZE);
}

static bool CellCoordinate(int x, int z)
{
    return x >= 0 && z >= 0 && x < HYDROLOGY_RUNTIME_GRID_SIZE &&
           z < HYDROLOGY_RUNTIME_GRID_SIZE;
}

HydrologyRuntimeConfig HydrologyRuntimeDefaultConfig(void)
{
    HydrologyConfig model = HydrologyDefaultConfig();
    model.width = HYDROLOGY_RUNTIME_GRID_SIZE;
    model.height = HYDROLOGY_RUNTIME_GRID_SIZE;
    model.cellSizeM = (float)HYDROLOGY_RUNTIME_DEFAULT_CELL_SIZE_M;
    model.openBoundaries = true;
    return (HydrologyRuntimeConfig){
        .fixedStepSeconds = HYDROLOGY_RUNTIME_DEFAULT_FIXED_STEP_SECONDS,
        .maxCatchUpSteps = HYDROLOGY_RUNTIME_DEFAULT_MAX_CATCH_UP_STEPS,
        .model = model,
        .initialSurfaceWaterM = 0.0f,
        .initialGroundwaterM = 0.0f,
        .initialSoilDepthM = 1.0f
    };
}

bool HydrologyRuntimeConfigNormalize(HydrologyRuntimeConfig *config)
{
    if (!config) return false;
    HydrologyRuntimeConfig defaults = HydrologyRuntimeDefaultConfig();
    if (!PositiveFinite(config->fixedStepSeconds)) {
        config->fixedStepSeconds = defaults.fixedStepSeconds;
    }
    if (config->maxCatchUpSteps == 0u) {
        config->maxCatchUpSteps = defaults.maxCatchUpSteps;
    }
    if (config->model.width != HYDROLOGY_RUNTIME_GRID_SIZE ||
        config->model.height != HYDROLOGY_RUNTIME_GRID_SIZE) {
        config->model.width = defaults.model.width;
        config->model.height = defaults.model.height;
    }
    if (!isfinite(config->model.cellSizeM) || config->model.cellSizeM < 0.01f) {
        config->model.cellSizeM = defaults.model.cellSizeM;
    }
    /* Keep the coarse cells aligned exactly with the canonical region. */
    config->model.cellSizeM = (float)HYDROLOGY_RUNTIME_DEFAULT_CELL_SIZE_M;
    if (!isfinite(config->initialSurfaceWaterM) ||
        config->initialSurfaceWaterM < 0.0f) config->initialSurfaceWaterM = 0.0f;
    if (!isfinite(config->initialGroundwaterM) ||
        config->initialGroundwaterM < 0.0f) config->initialGroundwaterM = 0.0f;
    if (!isfinite(config->initialSoilDepthM) ||
        config->initialSoilDepthM < 0.0f) {
        config->initialSoilDepthM = defaults.initialSoilDepthM;
    }
    if (config->initialGroundwaterM > config->model.maxGroundwaterDepthM) {
        config->initialGroundwaterM = config->model.maxGroundwaterDepthM;
    }
    return HydrologyConfigNormalize(&config->model) &&
           HydrologyRuntimeConfigIsValid(config);
}

bool HydrologyRuntimeConfigIsValid(const HydrologyRuntimeConfig *config)
{
    if (!config || !PositiveFinite(config->fixedStepSeconds) ||
        config->maxCatchUpSteps == 0u ||
        !NonNegativeFinite(config->initialSurfaceWaterM) ||
        !NonNegativeFinite(config->initialGroundwaterM) ||
        !NonNegativeFinite(config->initialSoilDepthM)) return false;
    HydrologyConfig model = config->model;
    if (!HydrologyConfigNormalize(&model) ||
        model.width != HYDROLOGY_RUNTIME_GRID_SIZE ||
        model.height != HYDROLOGY_RUNTIME_GRID_SIZE ||
        !isfinite(model.cellSizeM) || model.cellSizeM < 0.01f ||
        !isfinite(model.maxGroundwaterDepthM) ||
        model.maxGroundwaterDepthM < 0.0f ||
        config->initialGroundwaterM > model.maxGroundwaterDepthM) return false;
    return true;
}

bool HydrologyRuntimeInit(HydrologyRuntime *runtime,
                          const HydrologyRuntimeConfig *config)
{
    if (!runtime) return false;
    memset(runtime, 0, sizeof(*runtime));
    HydrologyRuntimeConfig normalized = config
        ? *config : HydrologyRuntimeDefaultConfig();
    if (!HydrologyRuntimeConfigNormalize(&normalized) ||
        !HydrologyInit(&runtime->grid, normalized.model)) return false;
    runtime->precipitationRatesMPerS = calloc(
        HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS,
        sizeof(*runtime->precipitationRatesMPerS));
    if (!runtime->precipitationRatesMPerS) {
        HydrologyFree(&runtime->grid);
        memset(runtime, 0, sizeof(*runtime));
        return false;
    }
    runtime->version = HYDROLOGY_RUNTIME_STATE_VERSION;
    runtime->config = normalized;
    runtime->initialized = true;
    return HydrologyRuntimeStateIsValid(runtime);
}

void HydrologyRuntimeFree(HydrologyRuntime *runtime)
{
    if (!runtime) return;
    HydrologyFree(&runtime->grid);
    free(runtime->precipitationRatesMPerS);
    memset(runtime, 0, sizeof(*runtime));
}

void HydrologyRuntimeReset(HydrologyRuntime *runtime)
{
    if (!runtime || !runtime->initialized) return;
    HydrologyReset(&runtime->grid);
    memset(runtime->precipitationRatesMPerS, 0,
           HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS *
               sizeof(*runtime->precipitationRatesMPerS));
    runtime->location = (HydrologyRuntimeLocation){ 0 };
    runtime->accumulatorSeconds = 0.0;
    runtime->simulationTimeSeconds = 0.0;
    runtime->stepCount = 0u;
    runtime->active = false;
}

static bool RuntimeCellValid(const HydrologyRuntime *runtime, int index)
{
    const HydrologyCell *cell = &runtime->grid.cells[index];
    return isfinite(cell->elevationM) &&
        NonNegativeFinite(cell->surfaceWaterDepthM) &&
        NonNegativeFinite(cell->groundwaterDepthM) &&
        NonNegativeFinite(cell->soilDepthM) &&
        NonNegativeFinite(cell->sedimentLoadKgM2) &&
        NonNegativeFinite(cell->runoffM3PerS) &&
        NonNegativeFinite(runtime->precipitationRatesMPerS[index]);
}

static bool RuntimeCellsValid(const HydrologyRuntime *runtime)
{
    for (int i = 0; i < runtime->grid.cellCount; i++) {
        if (!RuntimeCellValid(runtime, i)) return false;
    }
    return true;
}

/* Cheap invariant check used while refreshing all 1024 input cells. The
 * complete cell scan remains at step/snapshot boundaries, but doing it from
 * every setter turns one frame's O(n) input upload into O(n^2) work. */
static bool RuntimeReady(const HydrologyRuntime *runtime)
{
    return runtime && runtime->initialized &&
           runtime->version == HYDROLOGY_RUNTIME_STATE_VERSION &&
           HydrologyRuntimeConfigIsValid(&runtime->config) &&
           runtime->grid.cells && runtime->grid.scratch &&
           runtime->grid.flow && runtime->grid.flowSourceWater &&
           runtime->grid.flowSourceSediment &&
           runtime->grid.cellCount == HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS &&
           runtime->precipitationRatesMPerS &&
           isfinite(runtime->accumulatorSeconds) &&
           runtime->accumulatorSeconds >= 0.0 &&
           runtime->accumulatorSeconds < runtime->config.fixedStepSeconds *
               (double)runtime->config.maxCatchUpSteps + 1e-9 &&
           isfinite(runtime->simulationTimeSeconds) &&
           runtime->simulationTimeSeconds >= 0.0 &&
           runtime->stepCount != UINT64_MAX;
}

bool HydrologyRuntimeStateIsValid(const HydrologyRuntime *runtime)
{
    return RuntimeReady(runtime) && RuntimeCellsValid(runtime);
}

static void InitializeCells(HydrologyRuntime *runtime)
{
    HydrologyReset(&runtime->grid);
    for (int i = 0; i < runtime->grid.cellCount; i++) {
        runtime->grid.cells[i].surfaceWaterDepthM =
            runtime->config.initialSurfaceWaterM;
        runtime->grid.cells[i].groundwaterDepthM =
            runtime->config.initialGroundwaterM;
        runtime->grid.cells[i].soilDepthM = runtime->config.initialSoilDepthM;
        runtime->grid.scratch[i] = runtime->grid.cells[i];
    }
    memset(runtime->precipitationRatesMPerS, 0,
           HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS *
               sizeof(*runtime->precipitationRatesMPerS));
    runtime->accumulatorSeconds = 0.0;
    runtime->simulationTimeSeconds = 0.0;
    runtime->stepCount = 0u;
}

bool HydrologyRuntimeBindRegion(HydrologyRuntime *runtime,
                                uint32_t surfaceId, int regionX, int regionZ)
{
    if (!RuntimeReady(runtime)) return false;
    if (runtime->active && runtime->location.surfaceId == surfaceId &&
        runtime->location.regionX == regionX && runtime->location.regionZ == regionZ) {
        return true;
    }
    if (!HydrologyRuntimeStateIsValid(runtime)) return false;
    InitializeCells(runtime);
    runtime->location = (HydrologyRuntimeLocation){ surfaceId, regionX, regionZ };
    runtime->active = true;
    return HydrologyRuntimeStateIsValid(runtime);
}

bool HydrologyRuntimeBind(HydrologyRuntime *runtime,
                          uint32_t surfaceId, int mapX, int mapZ)
{
    return HydrologyRuntimeBindRegion(runtime, surfaceId,
                                      RegionCoordinate(mapX),
                                      RegionCoordinate(mapZ));
}

bool HydrologyRuntimeSetCellInput(HydrologyRuntime *runtime,
                                  int x, int z,
                                  HydrologyRuntimeCellInput input)
{
    if (!RuntimeReady(runtime) || !runtime->active ||
        !CellCoordinate(x, z) || !isfinite(input.elevationM) ||
        !NonNegativeFinite(input.soilDepthM) ||
        !NonNegativeFinite(input.precipitationMPerS)) return false;
    int index = z * HYDROLOGY_RUNTIME_GRID_SIZE + x;
    if (!HydrologySetTerrain(&runtime->grid, x, z,
                             input.elevationM, input.soilDepthM)) return false;
    runtime->precipitationRatesMPerS[index] = input.precipitationMPerS;
    return true;
}

bool HydrologyRuntimeSetTerrainCell(HydrologyRuntime *runtime,
                                    int x, int z,
                                    float elevationM, float soilDepthM)
{
    if (!RuntimeReady(runtime) || !runtime->active ||
        !CellCoordinate(x, z)) return false;
    return HydrologySetTerrain(&runtime->grid, x, z, elevationM, soilDepthM);
}

bool HydrologyRuntimeSetPrecipitationRateCell(HydrologyRuntime *runtime,
                                              int x, int z, float rateMPerS)
{
    if (!RuntimeReady(runtime) || !runtime->active ||
        !CellCoordinate(x, z) || !NonNegativeFinite(rateMPerS)) return false;
    runtime->precipitationRatesMPerS[z * HYDROLOGY_RUNTIME_GRID_SIZE + x] =
        rateMPerS;
    return true;
}

bool HydrologyRuntimeAddPrecipitationCell(HydrologyRuntime *runtime,
                                          int x, int z, float depthM)
{
    if (!RuntimeReady(runtime) || !runtime->active ||
        !CellCoordinate(x, z) || !NonNegativeFinite(depthM)) return false;
    return HydrologyAddPrecipitation(&runtime->grid, x, z, depthM);
}

bool HydrologyRuntimeStep(HydrologyRuntime *runtime)
{
    if (!HydrologyRuntimeStateIsValid(runtime) || !runtime->active ||
        runtime->stepCount == UINT64_MAX) return false;
    float dt = (float)runtime->config.fixedStepSeconds;
    for (int z = 0; z < HYDROLOGY_RUNTIME_GRID_SIZE; z++) {
        for (int x = 0; x < HYDROLOGY_RUNTIME_GRID_SIZE; x++) {
            int index = z * HYDROLOGY_RUNTIME_GRID_SIZE + x;
            float depth = runtime->precipitationRatesMPerS[index] * dt;
            if (!isfinite(depth) || depth < 0.0f ||
                !HydrologyAddPrecipitation(&runtime->grid, x, z, depth)) return false;
        }
    }
    if (!HydrologyStep(&runtime->grid, dt)) return false;
    runtime->stepCount++;
    runtime->simulationTimeSeconds += runtime->config.fixedStepSeconds;
    return HydrologyRuntimeStateIsValid(runtime);
}

bool HydrologyRuntimeAdvance(HydrologyRuntime *runtime,
                             double elapsedSeconds, unsigned *outSteps)
{
    if (outSteps) *outSteps = 0u;
    if (!HydrologyRuntimeStateIsValid(runtime) || !runtime->active ||
        !NonNegativeFinite(elapsedSeconds)) return false;
    double fixed = runtime->config.fixedStepSeconds;
    double maxAccumulator = fixed * (double)runtime->config.maxCatchUpSteps;
    if (elapsedSeconds >= maxAccumulator ||
        runtime->accumulatorSeconds > maxAccumulator - elapsedSeconds) {
        runtime->accumulatorSeconds = maxAccumulator;
    } else {
        runtime->accumulatorSeconds += elapsedSeconds;
    }
    unsigned steps = 0u;
    while (steps < runtime->config.maxCatchUpSteps &&
           runtime->accumulatorSeconds + 1e-12 >= fixed) {
        runtime->accumulatorSeconds -= fixed;
        if (runtime->accumulatorSeconds < 0.0) runtime->accumulatorSeconds = 0.0;
        if (!HydrologyRuntimeStep(runtime)) {
            runtime->accumulatorSeconds += fixed;
            return false;
        }
        steps++;
    }
    if (outSteps) *outSteps = steps;
    return HydrologyRuntimeStateIsValid(runtime);
}

bool HydrologyRuntimeQueryCell(const HydrologyRuntime *runtime,
                               int x, int z, HydrologyCell *outCell)
{
    if (!outCell) return false;
    *outCell = (HydrologyCell){ 0 };
    if (!HydrologyRuntimeStateIsValid(runtime) || !runtime->active ||
        !CellCoordinate(x, z)) return false;
    const HydrologyCell *cell = HydrologyCellAt(&runtime->grid, x, z);
    if (!cell) return false;
    *outCell = *cell;
    return true;
}

bool HydrologyRuntimeGetSnapshot(const HydrologyRuntime *runtime,
                                 HydrologyRuntimeSnapshot *out)
{
    if (!out) return false;
    *out = (HydrologyRuntimeSnapshot){ 0 };
    if (!HydrologyRuntimeStateIsValid(runtime)) return false;
    out->version = runtime->version;
    out->stepCount = runtime->stepCount;
    out->simulationTimeSeconds = runtime->simulationTimeSeconds;
    out->accumulatorSeconds = runtime->accumulatorSeconds;
    out->location = runtime->location;
    out->stats = HydrologyGetStats(&runtime->grid);
    out->totalWaterVolumeM3 = HydrologyTotalWaterVolumeM3(&runtime->grid);
    out->totalSedimentMassKg = HydrologyTotalSedimentMassKg(&runtime->grid);
    out->active = runtime->active;
    out->valid = runtime->active;
    return true;
}

bool HydrologyRuntimeFeedbackAt(const HydrologyRuntime *runtime,
                                uint32_t surfaceId, int mapX, int mapZ,
                                HydrologyRuntimeFeedback *out)
{
    if (!out) return false;
    *out = (HydrologyRuntimeFeedback){ 0 };
    if (!RuntimeReady(runtime) || !runtime->active ||
        runtime->location.surfaceId != surfaceId ||
        runtime->location.regionX != RegionCoordinate(mapX) ||
        runtime->location.regionZ != RegionCoordinate(mapZ)) return false;
    int x = GridCoordinate(mapX, runtime->location.regionX);
    int z = GridCoordinate(mapZ, runtime->location.regionZ);
    if (!CellCoordinate(x, z)) return false;
    int index = z * HYDROLOGY_RUNTIME_GRID_SIZE + x;
    if (!RuntimeCellValid(runtime, index)) return false;
    const HydrologyCell *cell = HydrologyCellAt(&runtime->grid, x, z);
    if (!cell) return false;
    float maxSoil = fmaxf(runtime->config.initialSoilDepthM, 1.0e-4f);
    float soilMoisture = Clamp01(cell->groundwaterDepthM /
                                 fmaxf(runtime->config.model.maxGroundwaterDepthM, 1.0f));
    soilMoisture = Clamp01(soilMoisture +
                           cell->surfaceWaterDepthM / maxSoil * 0.25f);
    out->active = true;
    out->surfaceWaterM = cell->surfaceWaterDepthM;
    out->soilMoisture = soilMoisture;
    out->groundwaterM = cell->groundwaterDepthM;
    out->runoffM3PerS = cell->runoffM3PerS;
    out->evaporationMPerS = runtime->config.model.evaporationRateMPerS;
    out->sedimentLoadKgM2 = cell->sedimentLoadKgM2;
    return true;
}

static HydrologyRuntime globalRuntime;
static uint64_t globalGeneration = 1u;

static void GlobalInputsChanged(void)
{
    if (++globalGeneration == 0u) globalGeneration = 1u;
}

uint64_t HydrologyRuntimeGlobalGeneration(void)
{
    return globalGeneration;
}

bool HydrologyRuntimeGlobalReset(const HydrologyRuntimeConfig *config)
{
    GlobalInputsChanged();
    if (globalRuntime.initialized) HydrologyRuntimeFree(&globalRuntime);
    return HydrologyRuntimeInit(&globalRuntime, config);
}

void HydrologyRuntimeGlobalFree(void)
{
    GlobalInputsChanged();
    HydrologyRuntimeFree(&globalRuntime);
}

bool HydrologyRuntimeGlobalBind(uint32_t surfaceId, int mapX, int mapZ)
{
    if (!globalRuntime.initialized && !HydrologyRuntimeGlobalReset(NULL)) return false;
    bool changed = !globalRuntime.active ||
        globalRuntime.location.surfaceId != surfaceId ||
        globalRuntime.location.regionX != RegionCoordinate(mapX) ||
        globalRuntime.location.regionZ != RegionCoordinate(mapZ);
    if (!HydrologyRuntimeBind(&globalRuntime, surfaceId, mapX, mapZ)) return false;
    if (changed) GlobalInputsChanged();
    return true;
}

bool HydrologyRuntimeGlobalSetCellInput(int x, int z,
                                        HydrologyRuntimeCellInput input)
{
    return HydrologyRuntimeSetCellInput(&globalRuntime, x, z, input);
}

bool HydrologyRuntimeGlobalSetTerrainCell(int x, int z,
                                          float elevationM, float soilDepthM)
{
    return HydrologyRuntimeSetTerrainCell(&globalRuntime, x, z,
                                          elevationM, soilDepthM);
}

bool HydrologyRuntimeGlobalSetPrecipitationRateCell(int x, int z,
                                                    float rateMPerS)
{
    return HydrologyRuntimeSetPrecipitationRateCell(&globalRuntime, x, z,
                                                    rateMPerS);
}

bool HydrologyRuntimeGlobalSetUniformPrecipitationRate(float rateMPerS)
{
    if (!RuntimeReady(&globalRuntime) || !globalRuntime.active ||
        !NonNegativeFinite(rateMPerS)) return false;
    for (int i = 0; i < globalRuntime.grid.cellCount; i++) {
        globalRuntime.precipitationRatesMPerS[i] = rateMPerS;
    }
    return true;
}

bool HydrologyRuntimeGlobalAddPrecipitationCell(int x, int z, float depthM)
{
    return HydrologyRuntimeAddPrecipitationCell(&globalRuntime, x, z, depthM);
}

bool HydrologyRuntimeGlobalAdvance(double elapsedSeconds, unsigned *outSteps)
{
    return HydrologyRuntimeAdvance(&globalRuntime, elapsedSeconds, outSteps);
}

bool HydrologyRuntimeGlobalSnapshot(HydrologyRuntimeSnapshot *out)
{
    return HydrologyRuntimeGetSnapshot(&globalRuntime, out);
}

bool HydrologyRuntimeGlobalFeedbackAt(uint32_t surfaceId, int mapX, int mapZ,
                                      HydrologyRuntimeFeedback *out)
{
    return HydrologyRuntimeFeedbackAt(&globalRuntime, surfaceId, mapX, mapZ, out);
}

static bool WriteValue(FILE *file, const void *value, size_t size)
{
    return file && value && fwrite(value, size, 1u, file) == 1u;
}

static bool ReadValue(FILE *file, void *value, size_t size)
{
    return file && value && fread(value, size, 1u, file) == 1u;
}

bool HydrologyRuntimeGlobalSaveState(FILE *file)
{
    if (!file || !HydrologyRuntimeStateIsValid(&globalRuntime)) return false;
    uint32_t header[3] = {
        HYDROLOGY_RUNTIME_STATE_MAGIC,
        HYDROLOGY_RUNTIME_STATE_VERSION,
        globalRuntime.active ? 1u : 0u
    };
    uint32_t count = (uint32_t)globalRuntime.grid.cellCount;
    return WriteValue(file, header, sizeof(header)) &&
           WriteValue(file, &globalRuntime.config, sizeof(globalRuntime.config)) &&
           WriteValue(file, &globalRuntime.location, sizeof(globalRuntime.location)) &&
           WriteValue(file, &globalRuntime.accumulatorSeconds,
                      sizeof(globalRuntime.accumulatorSeconds)) &&
           WriteValue(file, &globalRuntime.simulationTimeSeconds,
                      sizeof(globalRuntime.simulationTimeSeconds)) &&
           WriteValue(file, &globalRuntime.stepCount, sizeof(globalRuntime.stepCount)) &&
           WriteValue(file, &count, sizeof(count)) &&
           WriteValue(file, globalRuntime.grid.cells,
                      (size_t)count * sizeof(*globalRuntime.grid.cells)) &&
           WriteValue(file, globalRuntime.precipitationRatesMPerS,
                      (size_t)count * sizeof(*globalRuntime.precipitationRatesMPerS)) &&
           WriteValue(file, &globalRuntime.grid.stats,
                      sizeof(globalRuntime.grid.stats));
}

bool HydrologyRuntimeGlobalLoadState(FILE *file)
{
    if (!file) return false;
    uint32_t header[3] = { 0 };
    HydrologyRuntime loaded = { 0 };
    uint32_t count = 0u;
    HydrologyRuntimeConfig savedConfig = { 0 };
    if (!ReadValue(file, header, sizeof(header)) ||
        header[0] != HYDROLOGY_RUNTIME_STATE_MAGIC ||
        header[1] != HYDROLOGY_RUNTIME_STATE_VERSION || header[2] > 1u ||
        !ReadValue(file, &savedConfig, sizeof(savedConfig)) ||
        !HydrologyRuntimeInit(&loaded, &savedConfig) ||
        !ReadValue(file, &loaded.location, sizeof(loaded.location)) ||
        !ReadValue(file, &loaded.accumulatorSeconds,
                   sizeof(loaded.accumulatorSeconds)) ||
        !ReadValue(file, &loaded.simulationTimeSeconds,
                   sizeof(loaded.simulationTimeSeconds)) ||
        !ReadValue(file, &loaded.stepCount, sizeof(loaded.stepCount)) ||
        !ReadValue(file, &count, sizeof(count)) ||
        count != HYDROLOGY_RUNTIME_MAX_SERIALIZED_CELLS ||
        !ReadValue(file, loaded.grid.cells,
                   (size_t)count * sizeof(*loaded.grid.cells)) ||
        !ReadValue(file, loaded.precipitationRatesMPerS,
                   (size_t)count * sizeof(*loaded.precipitationRatesMPerS)) ||
        !ReadValue(file, &loaded.grid.stats, sizeof(loaded.grid.stats))) {
        HydrologyRuntimeFree(&loaded);
        return false;
    }
    memcpy(loaded.grid.scratch, loaded.grid.cells,
           (size_t)count * sizeof(*loaded.grid.scratch));
    loaded.active = header[2] != 0u;
    loaded.version = header[1];
    loaded.grid.config = loaded.config.model;
    if (!HydrologyRuntimeStateIsValid(&loaded)) {
        HydrologyRuntimeFree(&loaded);
        return false;
    }
    if (globalRuntime.initialized) HydrologyRuntimeFree(&globalRuntime);
    globalRuntime = loaded;
    GlobalInputsChanged();
    return true;
}
