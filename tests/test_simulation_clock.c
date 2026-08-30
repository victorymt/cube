#include "core/simulation_clock.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static int callbackCount;
static double callbackTime;
static double callbackStep;

static void RecordStep(double simulationSeconds, double stepSeconds, void *context)
{
    (void)context;
    callbackCount++;
    callbackTime = simulationSeconds;
    callbackStep = stepSeconds;
}

static int NearlyEqual(double left, double right)
{
    return fabs(left - right) < 1e-9;
}

static void TestDefaultsAndFiniteConfig(void)
{
    SimulationClockConfig defaults = SimulationClockDefaultConfig();
    assert(defaults.simulationSecondsPerRealSecond == 1.0);
    assert(defaults.fixedStepSeconds > 0.0);
    assert(defaults.maxCatchUpSteps > 0);
    assert(defaults.planetaryDaySeconds > 0.0);
    assert(defaults.planetaryYearSeconds > defaults.planetaryDaySeconds);

    SimulationClockConfig invalid = {
        .simulationSecondsPerRealSecond = NAN,
        .fixedStepSeconds = -1.0,
        .maxCatchUpSteps = 0,
        .planetaryDaySeconds = INFINITY,
        .planetaryYearSeconds = 0.0,
    };
    SimulationClock clock;
    assert(SimulationClockInit(&clock, &invalid));
    assert(isfinite(clock.config.simulationSecondsPerRealSecond));
    assert(isfinite(clock.config.fixedStepSeconds));
    assert(clock.config.maxCatchUpSteps > 0);
    assert(isfinite(clock.config.planetaryDaySeconds));
    assert(isfinite(clock.config.planetaryYearSeconds));
    assert(SimulationClockInit(NULL, NULL) == false);
}

static void TestConversionsAndPhases(void)
{
    SimulationClockConfig config = {
        .simulationSecondsPerRealSecond = 2.0,
        .fixedStepSeconds = 0.25,
        .maxCatchUpSteps = 4,
        .planetaryDaySeconds = 10.0,
        .planetaryYearSeconds = 40.0,
    };
    SimulationClock clock;
    assert(SimulationClockInit(&clock, &config));

    assert(NearlyEqual(SimulationClockRealSecondsToSimulationSeconds(&clock,
                                                                      3.5),
                       7.0));
    assert(NearlyEqual(SimulationClockRealSecondsToSimulationDays(&clock, 3.5),
                       0.7));
    assert(SimulationClockRealSecondsToSimulationSeconds(&clock, -1.0) ==
           0.0);
    assert(SimulationClockRealSecondsToSimulationSeconds(&clock, NAN) == 0.0);
    assert(SimulationClockSimulationSecondsToDays(&clock, 25.0) == 2.5);
    assert(SimulationClockSimulationSecondsToDays(&clock, -1.0) == 0.0);

    assert(NearlyEqual(SimulationClockDayPhaseAt(&clock, 25.0), 0.5));
    assert(NearlyEqual(SimulationClockYearPhaseAt(&clock, 50.0), 0.25));
    assert(SimulationClockDayPhaseAt(&clock, NAN) == 0.0);
    assert(SimulationClockYearPhaseAt(&clock, INFINITY) == 0.0);

    SimulationClockReset(&clock, 25.0);
    assert(NearlyEqual(SimulationClockDayPhase(&clock), 0.5));
    assert(NearlyEqual(SimulationClockYearPhase(&clock), 0.625));
    SimulationClockReset(&clock, NAN);
    assert(SimulationClockGetSimulationSeconds(&clock) == 0.0);
}

static void TestFixedStepAndCatchUpLimit(void)
{
    SimulationClockConfig config = {
        .simulationSecondsPerRealSecond = 1.0,
        .fixedStepSeconds = 0.1,
        .maxCatchUpSteps = 3,
        .planetaryDaySeconds = 10.0,
        .planetaryYearSeconds = 100.0,
    };
    SimulationClock clock;
    assert(SimulationClockInit(&clock, &config));

    callbackCount = 0;
    callbackTime = 0.0;
    callbackStep = 0.0;
    assert(SimulationClockAdvance(&clock, 0.25, RecordStep, NULL) == 2);
    assert(callbackCount == 2);
    assert(NearlyEqual(callbackTime, 0.2));
    assert(NearlyEqual(callbackStep, 0.1));
    assert(NearlyEqual(SimulationClockGetAccumulatorSeconds(&clock), 0.05));
    assert(NearlyEqual(SimulationClockInterpolationAlpha(&clock), 0.5));

    /* 2 seconds is capped at three steps; excess backlog is discarded. */
    assert(SimulationClockAdvance(&clock, 2.0, RecordStep, NULL) == 3);
    assert(callbackCount == 5);
    assert(NearlyEqual(SimulationClockGetSimulationSeconds(&clock), 0.5));
    assert(SimulationClockGetAccumulatorSeconds(&clock) >= 0.0);

    /* Invalid frame durations never contaminate state. */
    assert(SimulationClockAdvance(&clock, NAN, RecordStep, NULL) == 0);
    assert(SimulationClockAdvance(&clock, INFINITY, RecordStep, NULL) == 0);
    assert(SimulationClockAdvance(&clock, -1.0, RecordStep, NULL) == 0);
    assert(isfinite(SimulationClockGetSimulationSeconds(&clock)));
    assert(isfinite(SimulationClockGetAccumulatorSeconds(&clock)));

    /* A NULL callback is valid when callers only need clock advancement. */
    assert(SimulationClockAdvance(&clock, 0.1, NULL, NULL) == 1);
}

int main(void)
{
    TestDefaultsAndFiniteConfig();
    TestConversionsAndPhases();
    TestFixedStepAndCatchUpLimit();
    puts("simulation clock tests passed");
    return 0;
}
