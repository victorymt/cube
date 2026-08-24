#include "core/simulation_time.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void TestInvalidTimesNormalizeToEpoch(void)
{
    assert(SimulationTimePeriodic(-1.0) == 0.0);
    assert(SimulationTimePeriodic(NAN) == 0.0);
    assert(SimulationTimePeriodic(INFINITY) == 0.0);
    assert(SimulationTimePeriodic(-INFINITY) == 0.0);
}

static void TestOrdinaryTimesPreserveElapsedValue(void)
{
    assert(SimulationTimePeriodic(123.75) == 123.75);
    assert(SimulationTimePeriodic(100000000.0) == 100000000.0);
}

static void TestLongTimesWrapToStablePeriod(void)
{
    assert(SimulationTimePeriodic(100000001.25) == 1.25);
    assert(SimulationTimePeriodic(1000000000.0 + 42.5) == 42.5);
}

int main(void)
{
    TestInvalidTimesNormalizeToEpoch();
    TestOrdinaryTimesPreserveElapsedValue();
    TestLongTimesWrapToStablePeriod();
    puts("simulation time tests passed");
    return 0;
}
