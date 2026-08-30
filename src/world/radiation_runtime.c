#include "world/radiation_runtime.h"

#include <float.h>
#include <math.h>
#include <string.h>
#include <stdint.h>

#define RADIATION_RUNTIME_PI 3.141592653589793238462643383279502884
#define RADIATION_RUNTIME_TWO_PI (2.0 * RADIATION_RUNTIME_PI)

static bool PositiveFinite(double value)
{
    return isfinite(value) && value > 0.0;
}

static bool NonNegativeFinite(double value)
{
    return isfinite(value) && value >= 0.0;
}

static double Clamp(double value, double minimum, double maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static double NormalizeAngle(double angle)
{
    double normalized = fmod(angle, RADIATION_RUNTIME_TWO_PI);
    if (!isfinite(normalized)) return 0.0;
    if (normalized > RADIATION_RUNTIME_PI) normalized -=
        RADIATION_RUNTIME_TWO_PI;
    if (normalized < -RADIATION_RUNTIME_PI) normalized +=
        RADIATION_RUNTIME_TWO_PI;
    return normalized;
}

RadiationRuntimeConfig RadiationRuntimeDefaultConfig(void)
{
    return (RadiationRuntimeConfig){
        .fixedStepSeconds = RADIATION_RUNTIME_DEFAULT_FIXED_STEP_SECONDS,
        .heatCapacityJm2K = RADIATION_RUNTIME_DEFAULT_HEAT_CAPACITY_JM2K,
        .initialTemperatureK =
            RADIATION_RUNTIME_DEFAULT_INITIAL_TEMPERATURE_K,
        .longwaveEmissivity =
            RADIATION_RUNTIME_DEFAULT_LONGWAVE_EMISSIVITY,
        .dayLengthGameTime = RADIATION_RUNTIME_DEFAULT_DAY_LENGTH_GAME_TIME,
        .gameTimeUnitsPerSecond =
            RADIATION_RUNTIME_DEFAULT_GAME_TIME_UNITS_PER_SECOND,
        .maxCatchUpSteps = RADIATION_RUNTIME_DEFAULT_MAX_CATCH_UP_STEPS
    };
}

bool RadiationRuntimeConfigNormalize(RadiationRuntimeConfig *config)
{
    if (!config) return false;
    RadiationRuntimeConfig defaults = RadiationRuntimeDefaultConfig();
    if (!PositiveFinite(config->fixedStepSeconds)) {
        config->fixedStepSeconds = defaults.fixedStepSeconds;
    }
    if (!PositiveFinite(config->heatCapacityJm2K)) {
        config->heatCapacityJm2K = defaults.heatCapacityJm2K;
    }
    if (!PositiveFinite(config->initialTemperatureK)) {
        config->initialTemperatureK = defaults.initialTemperatureK;
    }
    if (!(config->longwaveEmissivity > 0.0) ||
        !isfinite(config->longwaveEmissivity)) {
        config->longwaveEmissivity = defaults.longwaveEmissivity;
    }
    config->longwaveEmissivity =
        Clamp(config->longwaveEmissivity, DBL_MIN, 1.0);
    if (!PositiveFinite(config->dayLengthGameTime)) {
        config->dayLengthGameTime = defaults.dayLengthGameTime;
    }
    if (!PositiveFinite(config->gameTimeUnitsPerSecond)) {
        config->gameTimeUnitsPerSecond = defaults.gameTimeUnitsPerSecond;
    }
    if (config->maxCatchUpSteps == 0u) {
        config->maxCatchUpSteps = defaults.maxCatchUpSteps;
    }
    return RadiationRuntimeConfigIsValid(config);
}

bool RadiationRuntimeConfigIsValid(const RadiationRuntimeConfig *config)
{
    return config && PositiveFinite(config->fixedStepSeconds) &&
           PositiveFinite(config->heatCapacityJm2K) &&
           PositiveFinite(config->initialTemperatureK) &&
           PositiveFinite(config->longwaveEmissivity) &&
           config->longwaveEmissivity <= 1.0 &&
           PositiveFinite(config->dayLengthGameTime) &&
           PositiveFinite(config->gameTimeUnitsPerSecond) &&
           config->maxCatchUpSteps > 0u;
}

bool RadiationRuntimeInit(RadiationRuntime *runtime,
                          const RadiationRuntimeConfig *config)
{
    if (!runtime) return false;
    *runtime = (RadiationRuntime){ 0 };
    RadiationRuntimeConfig normalized = config
        ? *config : RadiationRuntimeDefaultConfig();
    if (!RadiationRuntimeConfigNormalize(&normalized)) return false;
    if (!RadiationColumnStateInit(&runtime->column,
                                  normalized.initialTemperatureK,
                                  normalized.heatCapacityJm2K)) {
        *runtime = (RadiationRuntime){ 0 };
        return false;
    }
    runtime->version = RADIATION_RUNTIME_STATE_VERSION;
    runtime->config = normalized;
    runtime->initialized = true;
    runtime->snapshot.version = RADIATION_RUNTIME_STATE_VERSION;
    runtime->snapshot.column = runtime->column;
    runtime->simulationTimeGameTime = 0.0;
    return true;
}

bool RadiationRuntimeStateIsValid(const RadiationRuntime *runtime)
{
    if (!runtime || !runtime->initialized ||
        runtime->version != RADIATION_RUNTIME_STATE_VERSION ||
        !RadiationRuntimeConfigIsValid(&runtime->config) ||
        !isfinite(runtime->accumulatorSeconds) ||
        runtime->accumulatorSeconds < 0.0 ||
        runtime->accumulatorSeconds >=
            runtime->config.fixedStepSeconds *
                (double)runtime->config.maxCatchUpSteps + 1e-9 ||
        !isfinite(runtime->simulationTimeGameTime) ||
        runtime->simulationTimeGameTime < 0.0 ||
        !isfinite(runtime->column.energyJm2) ||
        !isfinite(runtime->column.temperatureK) ||
        !isfinite(runtime->column.heatCapacityJm2K) ||
        !(runtime->column.energyJm2 > 0.0) ||
        !(runtime->column.temperatureK > 0.0) ||
        !(runtime->column.heatCapacityJm2K > 0.0) ||
        runtime->snapshot.version != RADIATION_RUNTIME_STATE_VERSION ||
        runtime->snapshot.stepCount == UINT64_MAX) {
        return false;
    }
    double representedEnergy = runtime->column.temperatureK *
                               runtime->column.heatCapacityJm2K;
    if (!isfinite(representedEnergy) ||
        fabs(representedEnergy - runtime->column.energyJm2) >
            fmax(1.0, fabs(runtime->column.energyJm2)) * 1e-12) {
        return false;
    }
    if (runtime->snapshot.valid) {
        const RadiationBalanceOutput *balance = &runtime->snapshot.balance;
        const RadiationRuntimeLocationTime *location =
            &runtime->snapshot.location;
        if (runtime->snapshot.stepCount == 0u ||
            !isfinite(runtime->snapshot.simulationTimeGameTime) ||
            runtime->snapshot.simulationTimeGameTime < 0.0 ||
            runtime->snapshot.simulationTimeGameTime !=
                location->simulationTimeGameTime ||
            !isfinite(location->latitudeRad) ||
            location->latitudeRad < -RADIATION_RUNTIME_PI * 0.5 ||
            location->latitudeRad > RADIATION_RUNTIME_PI * 0.5 ||
            !isfinite(location->longitudeRad) ||
            runtime->snapshot.column.energyJm2 != runtime->column.energyJm2 ||
            runtime->snapshot.column.temperatureK != runtime->column.temperatureK ||
            runtime->snapshot.column.heatCapacityJm2K !=
                runtime->column.heatCapacityJm2K ||
            !isfinite(balance->eccentricAnomalyRad) ||
            !isfinite(balance->trueAnomalyRad) ||
            !PositiveFinite(balance->orbitalDistanceAu) ||
            !isfinite(balance->solarDeclinationRad) ||
            !isfinite(balance->solarZenithCosine) ||
            balance->solarZenithCosine < -1.0 ||
            balance->solarZenithCosine > 1.0 ||
            !PositiveFinite(balance->solarIrradianceWm2) ||
            !NonNegativeFinite(balance->instantaneousInsolationWm2) ||
            !NonNegativeFinite(balance->dailyMeanInsolationWm2) ||
            !NonNegativeFinite(balance->absorbedShortwaveWm2) ||
            !PositiveFinite(balance->effectiveLongwaveEmissivity) ||
            balance->effectiveLongwaveEmissivity > 1.0 ||
            !NonNegativeFinite(balance->outgoingLongwaveWm2) ||
            !isfinite(balance->netRadiativeFluxWm2) ||
            !NonNegativeFinite(balance->equilibriumTemperatureK)) {
            return false;
        }
    } else if (runtime->snapshot.stepCount != 0u) {
        return false;
    }
    if (runtime->hasBinding) {
        if (!PositiveFinite(runtime->baselineTemperatureK)) return false;
    } else if (runtime->baselineTemperatureK != 0.0) {
        return false;
    }
    return true;
}

static bool PlanetProfileForcingIsValid(const PlanetProfile *profile)
{
    return profile && isfinite(profile->receivedIrradiance) &&
           profile->receivedIrradiance > 0.0 &&
           isfinite(profile->orbitalEccentricity) &&
           profile->orbitalEccentricity >= 0.0 &&
           profile->orbitalEccentricity < 1.0 &&
           isfinite(profile->orbitalMeanAnomalyAtEpoch) &&
           isfinite(profile->seasonPhase) && isfinite(profile->axialTilt) &&
           isfinite(profile->yearLength) && profile->yearLength > 0.0f &&
           isfinite(profile->albedo) && profile->albedo >= 0.0f &&
           profile->albedo <= 1.0f && isfinite(profile->greenhouseEffect) &&
           profile->greenhouseEffect >= 0.0f &&
           profile->greenhouseEffect <= 1.0e6f;
}

bool RadiationRuntimeBuildForcingAtTemperature(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    const RadiationRuntimeConfig *config,
    double surfaceTemperatureK,
    RadiationBalanceInput *out)
{
    if (!out) return false;
    *out = (RadiationBalanceInput){ 0 };
    RadiationRuntimeConfig defaults = RadiationRuntimeDefaultConfig();
    const RadiationRuntimeConfig *effectiveConfig = config;
    if (!effectiveConfig) effectiveConfig = &defaults;
    if (!RadiationRuntimeConfigIsValid(effectiveConfig) ||
        !PlanetProfileForcingIsValid(profile) || !location ||
        !isfinite(location->latitudeRad) ||
        location->latitudeRad < -RADIATION_RUNTIME_PI * 0.5 ||
        location->latitudeRad > RADIATION_RUNTIME_PI * 0.5 ||
        !isfinite(location->longitudeRad) ||
        !isfinite(location->simulationTimeGameTime) ||
        location->simulationTimeGameTime < 0.0 ||
        !PositiveFinite(surfaceTemperatureK)) {
        return false;
    }

    double year = (double)profile->yearLength;
    double meanAnomaly = (double)profile->orbitalMeanAnomalyAtEpoch +
                         fmod(location->simulationTimeGameTime, year) *
                             (RADIATION_RUNTIME_TWO_PI / year);
    double dayLength = effectiveConfig->dayLengthGameTime;
    if (isfinite(profile->rotationRate) && profile->rotationRate > 0.0f) {
        dayLength = 360.0 / (double)profile->rotationRate;
    }
    if (!PositiveFinite(dayLength)) return false;
    double rotationPhase = fmod(location->simulationTimeGameTime, dayLength) /
                           dayLength * RADIATION_RUNTIME_TWO_PI;
    double localHourAngle = NormalizeAngle(location->longitudeRad +
                                           rotationPhase);
    /* receivedIrradiance is the orbit-mean value. At a=1 AU, scaling the
     * proxy luminosity by sqrt(1-e^2) preserves that mean over the orbit. */
    double eccentricity = (double)profile->orbitalEccentricity;
    double proxyLuminosity = (double)profile->receivedIrradiance *
                             sqrt(1.0 - eccentricity * eccentricity);
    *out = (RadiationBalanceInput){
        .stellarLuminositySolar = proxyLuminosity,
        .semiMajorAxisAu = 1.0,
        .orbitalEccentricity = eccentricity,
        .orbitalMeanAnomalyRad = meanAnomaly,
        .seasonPhaseRad = profile->seasonPhase,
        .axialTiltRad = profile->axialTilt,
        .latitudeRad = location->latitudeRad,
        .localHourAngleRad = localHourAngle,
        .albedo = profile->albedo,
        .longwaveEmissivity = effectiveConfig->longwaveEmissivity,
        .greenhouseOpticalDepth = profile->greenhouseEffect,
        .surfaceTemperatureK = surfaceTemperatureK
    };
    return true;
}

bool RadiationRuntimeBuildForcing(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    const RadiationRuntimeConfig *config,
    RadiationBalanceInput *out)
{
    RadiationRuntimeConfig defaults = RadiationRuntimeDefaultConfig();
    const RadiationRuntimeConfig *effectiveConfig = config
        ? config : &defaults;
    if (!RadiationRuntimeConfigIsValid(effectiveConfig)) {
        if (out) *out = (RadiationBalanceInput){ 0 };
        return false;
    }
    return RadiationRuntimeBuildForcingAtTemperature(
        profile, location, effectiveConfig,
        effectiveConfig->initialTemperatureK, out);
}

static bool RuntimeStepAt(RadiationRuntime *runtime,
                          const PlanetProfile *profile,
                          const RadiationRuntimeLocationTime *location,
                          double deltaSeconds)
{
    RadiationBalanceInput forcing;
    RadiationBalanceOutput output;
    if (!RadiationRuntimeBuildForcingAtTemperature(
            profile, location, &runtime->config,
            runtime->column.temperatureK, &forcing) ||
        !RadiationColumnStateStep(&runtime->column, &forcing, deltaSeconds,
                                   &output)) {
        return false;
    }
    if (runtime->snapshot.stepCount == UINT64_MAX) return false;
    runtime->snapshot.version = RADIATION_RUNTIME_STATE_VERSION;
    runtime->snapshot.stepCount++;
    runtime->snapshot.simulationTimeGameTime =
        location->simulationTimeGameTime;
    runtime->snapshot.column = runtime->column;
    runtime->snapshot.balance = output;
    runtime->snapshot.location = *location;
    runtime->snapshot.valid = true;
    if (location->simulationTimeGameTime <=
        DBL_MAX - runtime->config.fixedStepSeconds *
            runtime->config.gameTimeUnitsPerSecond) {
        runtime->simulationTimeGameTime = location->simulationTimeGameTime +
            runtime->config.fixedStepSeconds *
                runtime->config.gameTimeUnitsPerSecond;
    }
    return true;
}

bool RadiationRuntimeStep(RadiationRuntime *runtime,
                          const PlanetProfile *profile,
                          const RadiationRuntimeLocationTime *location,
                          double deltaSeconds)
{
    if (!RadiationRuntimeStateIsValid(runtime) || !location ||
        !PositiveFinite(deltaSeconds)) return false;
    double fixed = runtime->config.fixedStepSeconds;
    if (fabs(deltaSeconds - fixed) > fmax(1e-12, fixed * 1e-9)) return false;
    return RuntimeStepAt(runtime, profile, location, fixed) &&
           RadiationRuntimeStateIsValid(runtime);
}

bool RadiationRuntimeAdvance(RadiationRuntime *runtime,
                             const PlanetProfile *profile,
                             const RadiationRuntimeLocationTime *location,
                             double elapsedSeconds,
                             unsigned *outSteps)
{
    if (outSteps) *outSteps = 0u;
    if (!RadiationRuntimeStateIsValid(runtime) || !location ||
        !NonNegativeFinite(elapsedSeconds) ||
        !isfinite(location->simulationTimeGameTime) ||
        location->simulationTimeGameTime < 0.0) return false;

    RadiationBalanceInput checkedForcing;
    if (!RadiationRuntimeBuildForcingAtTemperature(
            profile, location, &runtime->config,
            runtime->column.temperatureK, &checkedForcing)) {
        return false;
    }

    double fixed = runtime->config.fixedStepSeconds;
    double maxAccumulator = fixed * (double)runtime->config.maxCatchUpSteps;
    if (!PositiveFinite(maxAccumulator)) return false;
    if (elapsedSeconds > maxAccumulator ||
        runtime->accumulatorSeconds > maxAccumulator - elapsedSeconds) {
        runtime->accumulatorSeconds = maxAccumulator;
    } else {
        runtime->accumulatorSeconds += elapsedSeconds;
    }

    unsigned steps = 0u;
    double startTime = location->simulationTimeGameTime;
    while (steps < runtime->config.maxCatchUpSteps &&
           runtime->accumulatorSeconds + 1e-12 >= fixed) {
        runtime->accumulatorSeconds -= fixed;
        if (runtime->accumulatorSeconds < 0.0) runtime->accumulatorSeconds = 0.0;
        RadiationRuntimeLocationTime stepLocation = *location;
        stepLocation.simulationTimeGameTime = startTime +
            (double)steps * fixed * runtime->config.gameTimeUnitsPerSecond;
        if (!RuntimeStepAt(runtime, profile, &stepLocation, fixed)) {
            runtime->accumulatorSeconds += fixed;
            return false;
        }
        steps++;
    }
    if (outSteps) *outSteps = steps;
    return RadiationRuntimeStateIsValid(runtime);
}

bool RadiationRuntimeGetSnapshot(const RadiationRuntime *runtime,
                                 RadiationRuntimeSnapshot *out)
{
    if (!out) return false;
    *out = (RadiationRuntimeSnapshot){ 0 };
    if (!RadiationRuntimeStateIsValid(runtime) || !runtime->snapshot.valid) {
        return false;
    }
    *out = runtime->snapshot;
    return true;
}

static RadiationRuntime globalRuntime;

static int RegionCoordinate(int mapCoordinate)
{
    int64_t value = mapCoordinate;
    if (value >= 0) return (int)(value / RADIATION_RUNTIME_REGION_SIZE);
    return (int)(-((-value + RADIATION_RUNTIME_REGION_SIZE - 1) /
                  RADIATION_RUNTIME_REGION_SIZE));
}

bool RadiationRuntimeGlobalReset(const RadiationRuntimeConfig *config)
{
    return RadiationRuntimeInit(&globalRuntime, config);
}

bool RadiationRuntimeGlobalBind(uint32_t surfaceId, int mapX, int mapZ,
                                double initialTemperatureK)
{
    if (!PositiveFinite(initialTemperatureK)) return false;
    RadiationRuntimeConfig config = globalRuntime.initialized
        ? globalRuntime.config : RadiationRuntimeDefaultConfig();
    config.initialTemperatureK = initialTemperatureK;
    int regionX = RegionCoordinate(mapX);
    int regionZ = RegionCoordinate(mapZ);
    bool sameBinding = globalRuntime.initialized && globalRuntime.hasBinding &&
                       globalRuntime.boundSurfaceId == surfaceId &&
                       globalRuntime.boundRegionX == regionX &&
                       globalRuntime.boundRegionZ == regionZ;
    if (!sameBinding && !RadiationRuntimeInit(&globalRuntime, &config)) {
        return false;
    }
    if (!sameBinding) {
        globalRuntime.boundSurfaceId = surfaceId;
        globalRuntime.boundRegionX = regionX;
        globalRuntime.boundRegionZ = regionZ;
        globalRuntime.baselineTemperatureK = initialTemperatureK;
        globalRuntime.hasBinding = true;
    }
    return RadiationRuntimeStateIsValid(&globalRuntime);
}

bool RadiationRuntimeGlobalAdvance(
    const PlanetProfile *profile,
    const RadiationRuntimeLocationTime *location,
    double elapsedSeconds,
    unsigned *outSteps)
{
    return RadiationRuntimeAdvance(&globalRuntime, profile, location,
                                   elapsedSeconds, outSteps);
}

bool RadiationRuntimeGlobalSnapshot(RadiationRuntimeSnapshot *out)
{
    return RadiationRuntimeGetSnapshot(&globalRuntime, out);
}

bool RadiationRuntimeGlobalTemperatureOffsetAt(uint32_t surfaceId,
                                               int mapX, int mapZ,
                                               double *outOffsetK)
{
    if (outOffsetK) *outOffsetK = 0.0;
    if (!outOffsetK || !RadiationRuntimeStateIsValid(&globalRuntime) ||
        !globalRuntime.hasBinding || !globalRuntime.snapshot.valid ||
        globalRuntime.boundSurfaceId != surfaceId ||
        globalRuntime.boundRegionX != RegionCoordinate(mapX) ||
        globalRuntime.boundRegionZ != RegionCoordinate(mapZ)) {
        return false;
    }
    double offset = globalRuntime.column.temperatureK -
                    globalRuntime.baselineTemperatureK;
    if (!isfinite(offset)) return false;
    *outOffsetK = Clamp(offset, -30.0, 30.0);
    return true;
}

static bool WriteValue(FILE *file, const void *value, size_t size)
{
    return file && value && fwrite(value, size, 1u, file) == 1u;
}

static bool ReadValue(FILE *file, void *value, size_t size)
{
    return file && value && fread(value, size, 1u, file) == 1u;
}

bool RadiationRuntimeGlobalSaveState(FILE *file)
{
    if (!RadiationRuntimeStateIsValid(&globalRuntime) || !file) return false;
    uint32_t flags = (globalRuntime.snapshot.valid ? 1u : 0u) |
                     (globalRuntime.hasBinding ? 2u : 0u);
    uint32_t header[3] = {
        RADIATION_RUNTIME_STATE_MAGIC,
        RADIATION_RUNTIME_STATE_VERSION,
        flags
    };
    if (!WriteValue(file, header, sizeof(header)) ||
        !WriteValue(file, &globalRuntime.config,
                    sizeof(globalRuntime.config)) ||
        !WriteValue(file, &globalRuntime.column,
                    sizeof(globalRuntime.column)) ||
        !WriteValue(file, &globalRuntime.accumulatorSeconds,
                    sizeof(globalRuntime.accumulatorSeconds)) ||
        !WriteValue(file, &globalRuntime.simulationTimeGameTime,
                    sizeof(globalRuntime.simulationTimeGameTime)) ||
        !WriteValue(file, &globalRuntime.snapshot,
                    sizeof(globalRuntime.snapshot)) ||
        !WriteValue(file, &globalRuntime.boundSurfaceId,
                    sizeof(globalRuntime.boundSurfaceId)) ||
        !WriteValue(file, &globalRuntime.boundRegionX,
                    sizeof(globalRuntime.boundRegionX)) ||
        !WriteValue(file, &globalRuntime.boundRegionZ,
                    sizeof(globalRuntime.boundRegionZ)) ||
        !WriteValue(file, &globalRuntime.baselineTemperatureK,
                    sizeof(globalRuntime.baselineTemperatureK))) {
        return false;
    }
    return true;
}

bool RadiationRuntimeGlobalLoadState(FILE *file)
{
    if (!file) return false;
    uint32_t header[3] = { 0 };
    RadiationRuntime loaded = { 0 };
    if (!ReadValue(file, header, sizeof(header)) ||
        header[0] != RADIATION_RUNTIME_STATE_MAGIC ||
        header[1] != RADIATION_RUNTIME_STATE_VERSION ||
        header[2] > 3u ||
        !ReadValue(file, &loaded.config, sizeof(loaded.config)) ||
        !ReadValue(file, &loaded.column, sizeof(loaded.column)) ||
        !ReadValue(file, &loaded.accumulatorSeconds,
                   sizeof(loaded.accumulatorSeconds)) ||
        !ReadValue(file, &loaded.simulationTimeGameTime,
                   sizeof(loaded.simulationTimeGameTime)) ||
        !ReadValue(file, &loaded.snapshot, sizeof(loaded.snapshot))) {
        return false;
    }
    if (!ReadValue(file, &loaded.boundSurfaceId,
                   sizeof(loaded.boundSurfaceId)) ||
        !ReadValue(file, &loaded.boundRegionX, sizeof(loaded.boundRegionX)) ||
        !ReadValue(file, &loaded.boundRegionZ, sizeof(loaded.boundRegionZ)) ||
        !ReadValue(file, &loaded.baselineTemperatureK,
                   sizeof(loaded.baselineTemperatureK))) {
        return false;
    }
    loaded.version = header[1];
    loaded.initialized = true;
    loaded.snapshot.version = header[1];
    loaded.snapshot.valid = (header[2] & 1u) != 0u;
    loaded.hasBinding = (header[2] & 2u) != 0u;
    if (!RadiationRuntimeStateIsValid(&loaded)) return false;
    globalRuntime = loaded;
    return true;
}
