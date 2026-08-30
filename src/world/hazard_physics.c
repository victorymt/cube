#include "world/hazard_physics.h"

#include <math.h>
#include <stddef.h>

static double Clamp01(double value)
{
    if (!isfinite(value)) return 0.0;
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

static double NonNegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

static double PositiveOr(double value, double fallback)
{
    return isfinite(value) && value > 0.0 ? value : fallback;
}

HazardFireParameters HazardFireDefaultParameters(void)
{
    return (HazardFireParameters){
        .ignitionTemperatureK = 500.0,
        .burnRatePerSecond = 0.03,
        .heatReleaseMJPerKg = 18.0,
        .moistureHeatMJPerKg = 2.26,
        .heatCapacityMJPerK = 2.0,
        .coolingPerSecond = 0.04,
        .smokeYield = 0.12,
        .smokeLossPerSecond = 0.01,
        .windSpreadGain = 0.08,
        .moistureExtinctionRatio = 0.35,
    };
}

HazardFireEnvironment HazardFireDefaultEnvironment(void)
{
    return (HazardFireEnvironment){
        .airTemperatureK = 293.15,
        .windSpeedMps = 0.0,
        .relativeHumidity = 0.5,
        .rainfallMmPerHour = 0.0,
    };
}

bool HazardFireStateValid(const HazardFireState *state)
{
    if (state == NULL) return false;
    const double *values = (const double *)state;
    for (size_t index = 0; index < sizeof(*state) / sizeof(double); index++) {
        if (!isfinite(values[index]) || values[index] < 0.0) return false;
    }
    return true;
}

static HazardFireParameters SanitizeFireParameters(
    const HazardFireParameters *input)
{
    HazardFireParameters result = HazardFireDefaultParameters();
    if (input == NULL) return result;
    result = *input;
    result.ignitionTemperatureK =
        PositiveOr(result.ignitionTemperatureK, 500.0);
    result.burnRatePerSecond = PositiveOr(result.burnRatePerSecond, 0.03);
    result.heatReleaseMJPerKg = PositiveOr(result.heatReleaseMJPerKg, 18.0);
    result.moistureHeatMJPerKg =
        PositiveOr(result.moistureHeatMJPerKg, 2.26);
    result.heatCapacityMJPerK = PositiveOr(result.heatCapacityMJPerK, 2.0);
    result.coolingPerSecond = NonNegative(result.coolingPerSecond);
    result.smokeYield = Clamp01(result.smokeYield);
    result.smokeLossPerSecond = NonNegative(result.smokeLossPerSecond);
    result.windSpreadGain = NonNegative(result.windSpreadGain);
    result.moistureExtinctionRatio =
        PositiveOr(result.moistureExtinctionRatio, 0.35);
    return result;
}

static HazardFireEnvironment SanitizeFireEnvironment(
    const HazardFireEnvironment *input)
{
    HazardFireEnvironment result = HazardFireDefaultEnvironment();
    if (input == NULL) return result;
    result = *input;
    result.airTemperatureK = PositiveOr(result.airTemperatureK, 293.15);
    result.windSpeedMps = NonNegative(result.windSpeedMps);
    result.relativeHumidity = Clamp01(result.relativeHumidity);
    result.rainfallMmPerHour = NonNegative(result.rainfallMmPerHour);
    return result;
}

double HazardFireSpreadPotential(const HazardFireState *state,
                                 const HazardFireParameters *parameters,
                                 const HazardFireEnvironment *environment,
                                 double distanceMeters)
{
    if (!HazardFireStateValid(state) || !isfinite(distanceMeters) ||
        distanceMeters < 0.0) return 0.0;
    HazardFireParameters p = SanitizeFireParameters(parameters);
    HazardFireEnvironment e = SanitizeFireEnvironment(environment);
    if (state->fuelKg <= 0.0 || state->temperatureK < p.ignitionTemperatureK)
        return 0.0;
    double dryness = 1.0 - fmin(1.0, state->moistureKg /
                                      fmax(1e-12, state->fuelKg *
                                           p.moistureExtinctionRatio));
    double wind = 1.0 + p.windSpreadGain * e.windSpeedMps;
    double humidity = 1.0 - 0.6 * e.relativeHumidity;
    double attenuation = exp(-distanceMeters / fmax(1.0, 4.0 +
                                                    2.0 * e.windSpeedMps));
    return Clamp01(dryness * wind * humidity * attenuation);
}

bool HazardFireStep(HazardFireState *state,
                    const HazardFireParameters *parameters,
                    const HazardFireEnvironment *environment,
                    double seconds,
                    HazardFireFlux *flux)
{
    if (state == NULL || !HazardFireStateValid(state) ||
        !isfinite(seconds) || seconds < 0.0) return false;
    HazardFireParameters p = SanitizeFireParameters(parameters);
    HazardFireEnvironment e = SanitizeFireEnvironment(environment);
    HazardFireFlux local = { 0 };
    if (seconds == 0.0) {
        if (flux) *flux = local;
        return true;
    }

    double rainAdded = e.rainfallMmPerHour * seconds / 3600.0 * 0.05;
    state->moistureKg += rainAdded;
    double hotness = fmax(0.0, state->temperatureK - 373.15);
    double evaporated = fmin(state->moistureKg, hotness * 0.0005 * seconds);
    state->moistureKg -= evaporated;
    local.evaporatedMoistureKg = evaporated;

    bool ignited = state->fuelKg > 0.0 &&
                   state->temperatureK >= p.ignitionTemperatureK &&
                   state->moistureKg <= state->fuelKg *
                                           p.moistureExtinctionRatio;
    double burned = 0.0;
    if (ignited) {
        double dryness = 1.0 - fmin(1.0, state->moistureKg /
                                          fmax(1e-12, state->fuelKg *
                                               p.moistureExtinctionRatio));
        double rate = p.burnRatePerSecond *
                      (1.0 + p.windSpreadGain * e.windSpeedMps) * dryness;
        burned = fmin(state->fuelKg, state->fuelKg * rate * seconds);
        state->fuelKg -= burned;
    }
    double released = burned * p.heatReleaseMJPerKg;
    double absorbed = evaporated * p.moistureHeatMJPerKg;
    double netHeat = released - absorbed;
    double cooling = (state->temperatureK - e.airTemperatureK) *
                     p.coolingPerSecond * seconds;
    state->temperatureK += netHeat / p.heatCapacityMJPerK - cooling;
    state->temperatureK = fmax(e.airTemperatureK, state->temperatureK);
    double producedSmoke = burned * p.smokeYield;
    double removedSmoke = fmin(state->smokeKg,
                               state->smokeKg * p.smokeLossPerSecond * seconds);
    state->smokeKg += producedSmoke - removedSmoke;
    local.burnedFuelKg = burned;
    local.heatReleasedMJ = released;
    local.smokeProducedKg = producedSmoke;
    local.smokeRemovedKg = removedSmoke;
    local.spreadPotential = HazardFireSpreadPotential(state, &p, &e, 1.0);
    return HazardFireStateValid(state) &&
           (!flux || ((*flux = local), true));
}

HazardSupercellParameters HazardSupercellDefaultParameters(void)
{
    return (HazardSupercellParameters){
        .airDensityKgPerM3 = 1.2,
        .formationEnergyJ = 1.0e6,
        .dissipationPerSecond = 0.015,
        .buoyancyEnergyPerSecond = 2.0e4,
        .minimumShearMps = 10.0,
        .steeringPerSecond = 0.15,
        .minimumRadiusMeters = 30.0,
        .maximumRadiusMeters = 600.0,
        .maximumPressureDeficitPa = 9000.0,
    };
}

HazardSupercellEnvironment HazardSupercellDefaultEnvironment(void)
{
    return (HazardSupercellEnvironment){
        .surfaceTemperatureK = 300.0,
        .dewPointTemperatureK = 290.0,
        .verticalShearMps = 20.0,
        .ambientWindX = 5.0,
        .ambientWindZ = 0.0,
    };
}

bool HazardSupercellStateValid(const HazardSupercellState *state)
{
    if (state == NULL) return false;
    const double *values = (const double *)state;
    for (size_t index = 0; index < sizeof(*state) / sizeof(double); index++) {
        if (!isfinite(values[index]) || values[index] < 0.0) {
            /* Position and velocity may be signed; the other scalar fields
             * are checked below to keep the state predicate explicit. */
            break;
        }
    }
    return isfinite(state->availableEnergyJ) && state->availableEnergyJ >= 0.0 &&
           isfinite(state->angularVelocityRadPerSecond) &&
           state->angularVelocityRadPerSecond >= 0.0 &&
           isfinite(state->radiusMeters) && state->radiusMeters > 0.0 &&
           isfinite(state->pressureDeficitPa) && state->pressureDeficitPa >= 0.0 &&
           isfinite(state->positionX) && isfinite(state->positionZ) &&
           isfinite(state->velocityX) && isfinite(state->velocityZ) &&
           isfinite(state->ageSeconds) && state->ageSeconds >= 0.0;
}

static HazardSupercellParameters SanitizeSupercellParameters(
    const HazardSupercellParameters *input)
{
    HazardSupercellParameters result = HazardSupercellDefaultParameters();
    if (input == NULL) return result;
    result = *input;
    result.airDensityKgPerM3 = PositiveOr(result.airDensityKgPerM3, 1.2);
    result.formationEnergyJ = PositiveOr(result.formationEnergyJ, 1.0e6);
    result.dissipationPerSecond = NonNegative(result.dissipationPerSecond);
    result.buoyancyEnergyPerSecond =
        PositiveOr(result.buoyancyEnergyPerSecond, 2.0e4);
    result.minimumShearMps = NonNegative(result.minimumShearMps);
    result.steeringPerSecond = NonNegative(result.steeringPerSecond);
    result.minimumRadiusMeters = PositiveOr(result.minimumRadiusMeters, 30.0);
    result.maximumRadiusMeters =
        fmax(result.minimumRadiusMeters,
             PositiveOr(result.maximumRadiusMeters, 600.0));
    result.maximumPressureDeficitPa =
        PositiveOr(result.maximumPressureDeficitPa, 9000.0);
    return result;
}

static HazardSupercellEnvironment SanitizeSupercellEnvironment(
    const HazardSupercellEnvironment *input)
{
    HazardSupercellEnvironment result = HazardSupercellDefaultEnvironment();
    if (!input) return result;
    result = *input;
    result.surfaceTemperatureK = PositiveOr(result.surfaceTemperatureK, 300.0);
    result.dewPointTemperatureK = PositiveOr(result.dewPointTemperatureK, 290.0);
    result.verticalShearMps = NonNegative(result.verticalShearMps);
    result.ambientWindX = isfinite(result.ambientWindX) ? result.ambientWindX : 0.0;
    result.ambientWindZ = isfinite(result.ambientWindZ) ? result.ambientWindZ : 0.0;
    return result;
}

bool HazardSupercellStep(HazardSupercellState *state,
                         const HazardSupercellParameters *parameters,
                         const HazardSupercellEnvironment *environment,
                         double seconds,
                         HazardSupercellFlux *flux)
{
    if (state == NULL || !HazardSupercellStateValid(state) ||
        !isfinite(seconds) || seconds < 0.0) return false;
    HazardSupercellParameters p = SanitizeSupercellParameters(parameters);
    HazardSupercellEnvironment e = SanitizeSupercellEnvironment(environment);
    HazardSupercellFlux local = { 0 };
    if (seconds == 0.0) {
        if (flux) *flux = local;
        return true;
    }
    double buoyancy = Clamp01((e.surfaceTemperatureK - e.dewPointTemperatureK) /
                              30.0);
    double shearFactor = Clamp01(e.verticalShearMps /
                                 fmax(1.0, p.minimumShearMps * 2.0));
    double buoyancyEnergy = p.buoyancyEnergyPerSecond * buoyancy * shearFactor *
                            seconds;
    double dissipated = fmin(state->availableEnergyJ,
                             state->availableEnergyJ *
                                 p.dissipationPerSecond * seconds);
    state->availableEnergyJ += buoyancyEnergy - dissipated;
    local.buoyancyEnergyJ = buoyancyEnergy;
    local.dissipatedEnergyJ = dissipated;
    if (!state->active && state->availableEnergyJ >= p.formationEnergyJ &&
        e.verticalShearMps >= p.minimumShearMps) {
        state->active = true;
        state->radiusMeters = p.minimumRadiusMeters;
        local.formed = true;
    }
    if (state->active && (state->availableEnergyJ < p.formationEnergyJ * 0.2 ||
                          e.verticalShearMps < p.minimumShearMps * 0.5)) {
        state->active = false;
        state->pressureDeficitPa = 0.0;
        local.dissipated = true;
    }
    if (state->active) {
        double energyScale = sqrt(state->availableEnergyJ /
                                  fmax(1.0, p.formationEnergyJ));
        state->radiusMeters = fmin(p.maximumRadiusMeters,
                                   fmax(p.minimumRadiusMeters,
                                        p.minimumRadiusMeters * energyScale));
        state->angularVelocityRadPerSecond =
            fmax(0.0, 2.0 * sqrt(state->availableEnergyJ /
                                 fmax(1.0, p.airDensityKgPerM3)) /
                          fmax(1.0, state->radiusMeters * state->radiusMeters));
        double tangential = state->angularVelocityRadPerSecond *
                            state->radiusMeters;
        state->pressureDeficitPa = fmin(p.maximumPressureDeficitPa,
                                        0.5 * p.airDensityKgPerM3 *
                                            tangential * tangential);
        state->velocityX += (e.ambientWindX - state->velocityX) *
                            Clamp01(p.steeringPerSecond * seconds);
        state->velocityZ += (e.ambientWindZ - state->velocityZ) *
                            Clamp01(p.steeringPerSecond * seconds);
        state->positionX += state->velocityX * seconds;
        state->positionZ += state->velocityZ * seconds;
        state->ageSeconds += seconds;
    } else {
        state->angularVelocityRadPerSecond = 0.0;
        state->pressureDeficitPa = 0.0;
    }
    local.pressureDeficitPa = state->pressureDeficitPa;
    return HazardSupercellStateValid(state) &&
           (!flux || ((*flux = local), true));
}
