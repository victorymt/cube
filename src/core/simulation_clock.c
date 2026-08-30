#include "core/simulation_clock.h"

#include <float.h>
#include <math.h>
#include <stddef.h>

static bool PositiveFinite(double value)
{
    return isfinite(value) && value > 0.0;
}

static bool NonNegativeFinite(double value)
{
    return isfinite(value) && value >= 0.0;
}

static double SanitizePositive(double value, double fallback)
{
    return PositiveFinite(value) ? value : fallback;
}

static unsigned SanitizeCatchUp(unsigned value)
{
    return value == 0 ? SIMULATION_CLOCK_DEFAULT_MAX_CATCH_UP_STEPS : value;
}

static SimulationClockConfig SanitizeConfig(const SimulationClockConfig *input)
{
    SimulationClockConfig defaults = SimulationClockDefaultConfig();
    if (input == NULL) return defaults;

    SimulationClockConfig result = *input;
    result.simulationSecondsPerRealSecond =
        NonNegativeFinite(result.simulationSecondsPerRealSecond)
            ? result.simulationSecondsPerRealSecond
            : defaults.simulationSecondsPerRealSecond;
    result.fixedStepSeconds =
        SanitizePositive(result.fixedStepSeconds, defaults.fixedStepSeconds);
    result.maxCatchUpSteps = SanitizeCatchUp(result.maxCatchUpSteps);
    result.planetaryDaySeconds =
        SanitizePositive(result.planetaryDaySeconds,
                         defaults.planetaryDaySeconds);
    result.planetaryYearSeconds =
        SanitizePositive(result.planetaryYearSeconds,
                         defaults.planetaryYearSeconds);
    return result;
}

static double FiniteNonNegative(double value)
{
    return isfinite(value) && value >= 0.0 ? value : 0.0;
}

static double SafeScale(double duration, double rate, double limit)
{
    duration = FiniteNonNegative(duration);
    if (duration == 0.0 || !NonNegativeFinite(rate) || rate == 0.0) {
        return 0.0;
    }
    if (duration > DBL_MAX / rate) return limit;

    double scaled = duration * rate;
    if (!isfinite(scaled) || scaled >= limit) return limit;
    return scaled;
}

static double SafePhase(double seconds, double period)
{
    if (!isfinite(seconds) || seconds < 0.0 || !PositiveFinite(period)) {
        return 0.0;
    }

    double remainder = fmod(seconds, period);
    if (!isfinite(remainder) || remainder < 0.0) return 0.0;
    double phase = remainder / period;
    return isfinite(phase) && phase >= 0.0 && phase < 1.0 ? phase : 0.0;
}

SimulationClockConfig SimulationClockDefaultConfig(void)
{
    SimulationClockConfig config = {
        .simulationSecondsPerRealSecond = SIMULATION_CLOCK_DEFAULT_RATE,
        .fixedStepSeconds = SIMULATION_CLOCK_DEFAULT_FIXED_STEP,
        .maxCatchUpSteps = SIMULATION_CLOCK_DEFAULT_MAX_CATCH_UP_STEPS,
        .planetaryDaySeconds = SIMULATION_CLOCK_DEFAULT_PLANETARY_DAY,
        .planetaryYearSeconds = SIMULATION_CLOCK_DEFAULT_PLANETARY_YEAR,
    };
    return config;
}

bool SimulationClockInit(SimulationClock *clock,
                         const SimulationClockConfig *config)
{
    if (clock == NULL) return false;

    clock->config = SanitizeConfig(config);
    clock->simulationSeconds = 0.0;
    clock->accumulatorSeconds = 0.0;
    return true;
}

void SimulationClockReset(SimulationClock *clock, double simulationSeconds)
{
    if (clock == NULL) return;
    clock->simulationSeconds = FiniteNonNegative(simulationSeconds);
    clock->accumulatorSeconds = 0.0;
}

double SimulationClockGetSimulationSeconds(const SimulationClock *clock)
{
    if (clock == NULL || !isfinite(clock->simulationSeconds) ||
        clock->simulationSeconds < 0.0) {
        return 0.0;
    }
    return clock->simulationSeconds;
}

double SimulationClockGetAccumulatorSeconds(const SimulationClock *clock)
{
    if (clock == NULL || !isfinite(clock->accumulatorSeconds) ||
        clock->accumulatorSeconds < 0.0) {
        return 0.0;
    }
    return clock->accumulatorSeconds;
}

double SimulationClockInterpolationAlpha(const SimulationClock *clock)
{
    if (clock == NULL || !PositiveFinite(clock->config.fixedStepSeconds)) {
        return 0.0;
    }

    double accumulator = SimulationClockGetAccumulatorSeconds(clock);
    double alpha = accumulator / clock->config.fixedStepSeconds;
    if (!isfinite(alpha) || alpha < 0.0) return 0.0;
    return alpha >= 1.0 ? 1.0 : alpha;
}

double SimulationClockRealSecondsToSimulationSeconds(
    const SimulationClock *clock,
    double realSeconds)
{
    if (clock == NULL) return 0.0;
    double rate = clock->config.simulationSecondsPerRealSecond;
    if (!NonNegativeFinite(rate) || rate == 0.0) return 0.0;
    if (!isfinite(realSeconds) || realSeconds < 0.0) return 0.0;
    if (realSeconds > DBL_MAX / rate) return DBL_MAX;

    double result = realSeconds * rate;
    return isfinite(result) ? result : DBL_MAX;
}

double SimulationClockRealSecondsToSimulationDays(const SimulationClock *clock,
                                                  double realSeconds)
{
    return SimulationClockSimulationSecondsToDays(
        clock,
        SimulationClockRealSecondsToSimulationSeconds(clock, realSeconds));
}

double SimulationClockSimulationSecondsToDays(const SimulationClock *clock,
                                              double simulationSeconds)
{
    if (clock == NULL || !PositiveFinite(clock->config.planetaryDaySeconds) ||
        !isfinite(simulationSeconds) || simulationSeconds < 0.0) {
        return 0.0;
    }

    double result = simulationSeconds / clock->config.planetaryDaySeconds;
    return isfinite(result) ? result : DBL_MAX;
}

double SimulationClockDayPhaseAt(const SimulationClock *clock,
                                 double simulationSeconds)
{
    if (clock == NULL) return 0.0;
    return SafePhase(simulationSeconds, clock->config.planetaryDaySeconds);
}

double SimulationClockYearPhaseAt(const SimulationClock *clock,
                                  double simulationSeconds)
{
    if (clock == NULL) return 0.0;
    return SafePhase(simulationSeconds, clock->config.planetaryYearSeconds);
}

double SimulationClockDayPhase(const SimulationClock *clock)
{
    return SimulationClockDayPhaseAt(clock,
                                     SimulationClockGetSimulationSeconds(clock));
}

double SimulationClockYearPhase(const SimulationClock *clock)
{
    return SimulationClockYearPhaseAt(clock,
                                      SimulationClockGetSimulationSeconds(clock));
}

unsigned SimulationClockAdvance(SimulationClock *clock,
                                double realDeltaSeconds,
                                SimulationClockStepFn step,
                                void *context)
{
    if (clock == NULL || !PositiveFinite(clock->config.fixedStepSeconds) ||
        clock->config.maxCatchUpSteps == 0) {
        return 0;
    }

    double fixedStep = clock->config.fixedStepSeconds;
    double maxAccumulator = fixedStep * (double)clock->config.maxCatchUpSteps;
    if (!PositiveFinite(maxAccumulator)) maxAccumulator = DBL_MAX;

    double scaled = SafeScale(realDeltaSeconds,
                              clock->config.simulationSecondsPerRealSecond,
                              maxAccumulator);
    double accumulator = SimulationClockGetAccumulatorSeconds(clock);
    if (accumulator >= maxAccumulator || scaled >= maxAccumulator ||
        accumulator > maxAccumulator - scaled) {
        accumulator = maxAccumulator;
    } else {
        accumulator += scaled;
    }

    unsigned steps = 0;
    while (steps < clock->config.maxCatchUpSteps && accumulator >= fixedStep) {
        accumulator -= fixedStep;
        if (accumulator < 0.0 || !isfinite(accumulator)) accumulator = 0.0;

        double current = SimulationClockGetSimulationSeconds(clock);
        if (current > DBL_MAX - fixedStep) {
            clock->simulationSeconds = DBL_MAX;
            accumulator = 0.0;
        } else {
            clock->simulationSeconds = current + fixedStep;
        }

        steps++;
        if (step != NULL) step(clock->simulationSeconds, fixedStep, context);
    }

    clock->accumulatorSeconds = accumulator;
    return steps;
}
