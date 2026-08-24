#include "core/simulation_time.h"

#include <math.h>

double SimulationTimePeriodic(double elapsedTime)
{
    if (!isfinite(elapsedTime) || elapsedTime < 0.0) return 0.0;
    return elapsedTime > 100000000.0
        ? fmod(elapsedTime, 1000000.0) : elapsedTime;
}
