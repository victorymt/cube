#ifndef VOXELCRAFT_SIMULATION_TIME_H
#define VOXELCRAFT_SIMULATION_TIME_H

/*
 * Return a bounded phase for deterministic periodic simulation consumers.
 * Invalid and negative elapsed times normalize to the clear-weather epoch.
 */
double SimulationTimePeriodic(double elapsedTime);

#endif
