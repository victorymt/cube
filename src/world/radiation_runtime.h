#ifndef VOXELCRAFT_RADIATION_RUNTIME_H
#define VOXELCRAFT_RADIATION_RUNTIME_H

#include "space/planet_profile.h"
#include "world/radiation_balance.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>

/* Increment this when the in-memory runtime state layout changes. */
#define RADIATION_RUNTIME_STATE_VERSION 1u
#define RADIATION_RUNTIME_STATE_MAGIC 0x52544431u /* "RTD1" */

#define RADIATION_RUNTIME_DEFAULT_FIXED_STEP_SECONDS (1.0 / 60.0)
#define RADIATION_RUNTIME_DEFAULT_HEAT_CAPACITY_JM2K 10000.0
#define RADIATION_RUNTIME_DEFAULT_INITIAL_TEMPERATURE_K 288.0
#define RADIATION_RUNTIME_DEFAULT_LONGWAVE_EMISSIVITY 1.0
#define RADIATION_RUNTIME_DEFAULT_DAY_LENGTH_GAME_TIME 240.0
#define RADIATION_RUNTIME_DEFAULT_GAME_TIME_UNITS_PER_SECOND 1.0
#define RADIATION_RUNTIME_DEFAULT_MAX_CATCH_UP_STEPS 8u
#define RADIATION_RUNTIME_REGION_SIZE 128

typedef struct RadiationRuntimeConfig {
    /* Fixed physical integration step used by RadiationRuntimeStep. */
    double fixedStepSeconds;
    double heatCapacityJm2K;
    double initialTemperatureK;
    double longwaveEmissivity;
    /* A profile's rotationRate is preferred; this is the fallback day. */
    double dayLengthGameTime;
    /* Converts simulation seconds to the profile's game-time units. */
    double gameTimeUnitsPerSecond;
    unsigned maxCatchUpSteps;
} RadiationRuntimeConfig;

typedef struct RadiationRuntimeLocationTime {
    double latitudeRad;
    double longitudeRad;
    /* PlanetProfile seasons and rotation are expressed in game-time units. */
    double simulationTimeGameTime;
} RadiationRuntimeLocationTime;

typedef struct RadiationRuntimeSnapshot {
    uint32_t version;
    uint64_t stepCount;
    double simulationTimeGameTime;
    RadiationColumnState column;
    RadiationBalanceOutput balance;
    RadiationRuntimeLocationTime location;
    bool valid;
} RadiationRuntimeSnapshot;

typedef struct RadiationRuntime {
    uint32_t version;
    RadiationRuntimeConfig config;
    RadiationColumnState column;
    RadiationRuntimeSnapshot snapshot;
    double accumulatorSeconds;
    double simulationTimeGameTime;
    uint32_t boundSurfaceId;
    int boundRegionX;
    int boundRegionZ;
    double baselineTemperatureK;
    bool hasBinding;
    bool initialized;
} RadiationRuntime;

RadiationRuntimeConfig RadiationRuntimeDefaultConfig(void);
bool RadiationRuntimeConfigNormalize(RadiationRuntimeConfig *config);
bool RadiationRuntimeConfigIsValid(const RadiationRuntimeConfig *config);

bool RadiationRuntimeInit(RadiationRuntime *runtime,
                          const RadiationRuntimeConfig *config);
bool RadiationRuntimeStateIsValid(const RadiationRuntime *runtime);

/* Convert profile and spherical coordinates into radiation forcing. */
bool RadiationRuntimeBuildForcing(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    const RadiationRuntimeConfig *config,
    RadiationBalanceInput *out);

/* Variant used when evaluating a live column at its current temperature. */
bool RadiationRuntimeBuildForcingAtTemperature(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    const RadiationRuntimeConfig *config,
    double surfaceTemperatureK,
    RadiationBalanceInput *out);

/* Integrate exactly one configured fixed step. */
bool RadiationRuntimeStep(RadiationRuntime *runtime,
                          const PlanetProfile *profile,
                          const RadiationRuntimeLocationTime *location,
                          double deltaSeconds);

/* Accumulate elapsed seconds and consume up to maxCatchUpSteps fixed steps. */
bool RadiationRuntimeAdvance(RadiationRuntime *runtime,
                             const PlanetProfile *profile,
                             const RadiationRuntimeLocationTime *location,
                             double elapsedSeconds,
                             unsigned *outSteps);

bool RadiationRuntimeGetSnapshot(const RadiationRuntime *runtime,
                                 RadiationRuntimeSnapshot *out);

/* Optional process-level bridge for save/debug callers. The simulation model
 * remains instance-based; these wrappers own one explicitly reset instance. */
bool RadiationRuntimeGlobalReset(const RadiationRuntimeConfig *config);
bool RadiationRuntimeGlobalBind(uint32_t surfaceId, int mapX, int mapZ,
                                double initialTemperatureK);
bool RadiationRuntimeGlobalAdvance(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    double elapsedSeconds,
    unsigned *outSteps);
bool RadiationRuntimeGlobalSnapshot(RadiationRuntimeSnapshot *out);
bool RadiationRuntimeGlobalTemperatureOffsetAt(uint32_t surfaceId,
                                               int mapX, int mapZ,
                                               double *outOffsetK);
bool RadiationRuntimeGlobalSaveState(FILE *file);
bool RadiationRuntimeGlobalLoadState(FILE *file);

#endif
