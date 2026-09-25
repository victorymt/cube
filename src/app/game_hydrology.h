#ifndef VOXELCRAFT_GAME_HYDROLOGY_H
#define VOXELCRAFT_GAME_HYDROLOGY_H

#include "world/local_climate.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct GameRuntime GameRuntime;

struct GameRuntime;

/* Counts/timings describe the latest simulation step, not a render frame.
 * Cursor/pass state is transient and is reset when the runtime is rebound. */
typedef struct GameHydrologyInputStats {
    unsigned weatherSamples;
    unsigned weatherReusedCells;
    unsigned weatherPending;
    unsigned weatherCursor;
    uint64_t weatherPasses;
    unsigned terrainSamples;
    unsigned terrainPending;
    double inputsMainCpuMs;
    double solverMainCpuMs;
    bool active;
    bool weatherReady;
    bool weatherUniform;
} GameHydrologyInputStats;

bool GameHydrologyGetInputStats(const struct GameRuntime *game,
                                GameHydrologyInputStats *outStats);

void GameAdvanceHydrologySimulationStep(struct GameRuntime *game,
                                        double simulationSeconds,
                                        double stepSeconds);
bool GameHydrologyFeedbackAt(int x, int z, double simulationTime,
                             LocalClimateHydrologyFeedback *outFeedback,
                             void *userData);

#endif
