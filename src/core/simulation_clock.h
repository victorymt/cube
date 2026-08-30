#ifndef VOXELCRAFT_SIMULATION_CLOCK_H
#define VOXELCRAFT_SIMULATION_CLOCK_H

#include <stdbool.h>

/*
 * A simulation clock maps wall-clock durations to a deterministic simulation
 * timeline.  All durations are expressed in seconds unless stated otherwise.
 */
typedef struct SimulationClockConfig {
    /* Simulation seconds elapsed for one real second. */
    double simulationSecondsPerRealSecond;
    /* Duration of one fixed simulation step. */
    double fixedStepSeconds;
    /* Maximum number of fixed steps consumed by one Advance call. */
    unsigned maxCatchUpSteps;
    /* Length of one planetary day and year in simulation seconds. */
    double planetaryDaySeconds;
    double planetaryYearSeconds;
} SimulationClockConfig;

typedef struct SimulationClock {
    SimulationClockConfig config;
    double simulationSeconds;
    double accumulatorSeconds;
} SimulationClock;

typedef void (*SimulationClockStepFn)(double simulationSeconds,
                                      double stepSeconds,
                                      void *context);

enum {
    SIMULATION_CLOCK_DEFAULT_MAX_CATCH_UP_STEPS = 8
};

#define SIMULATION_CLOCK_DEFAULT_RATE 1.0
#define SIMULATION_CLOCK_DEFAULT_FIXED_STEP (1.0 / 60.0)
#define SIMULATION_CLOCK_DEFAULT_PLANETARY_DAY (24.0 * 60.0 * 60.0)
#define SIMULATION_CLOCK_DEFAULT_PLANETARY_YEAR (365.25 * 24.0 * 60.0 * 60.0)

/* Return a valid baseline configuration. */
SimulationClockConfig SimulationClockDefaultConfig(void);

/*
 * Initialize a clock instance.  A NULL configuration selects defaults;
 * invalid or non-finite fields are replaced by their respective defaults.
 */
bool SimulationClockInit(SimulationClock *clock,
                         const SimulationClockConfig *config);

/* Reset elapsed simulation time and the pending fixed-step accumulator. */
void SimulationClockReset(SimulationClock *clock, double simulationSeconds);

/* Read-only clock state helpers. */
double SimulationClockGetSimulationSeconds(const SimulationClock *clock);
double SimulationClockGetAccumulatorSeconds(const SimulationClock *clock);
double SimulationClockInterpolationAlpha(const SimulationClock *clock);

/* Convert finite, non-negative real durations using the configured rate. */
double SimulationClockRealSecondsToSimulationSeconds(
    const SimulationClock *clock,
    double realSeconds);
double SimulationClockRealSecondsToSimulationDays(const SimulationClock *clock,
                                                  double realSeconds);
double SimulationClockSimulationSecondsToDays(const SimulationClock *clock,
                                              double simulationSeconds);

/* Return normalized [0, 1) orbital phases for an arbitrary time or now. */
double SimulationClockDayPhaseAt(const SimulationClock *clock,
                                 double simulationSeconds);
double SimulationClockYearPhaseAt(const SimulationClock *clock,
                                  double simulationSeconds);
double SimulationClockDayPhase(const SimulationClock *clock);
double SimulationClockYearPhase(const SimulationClock *clock);

/*
 * Add a real-time duration and consume up to maxCatchUpSteps fixed steps.
 * Durations beyond the catch-up budget are discarded to prevent a spiral of
 * death.  The callback, when non-NULL, receives the post-step time.
 */
unsigned SimulationClockAdvance(SimulationClock *clock,
                                double realDeltaSeconds,
                                SimulationClockStepFn step,
                                void *context);

#endif
