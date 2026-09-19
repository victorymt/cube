#ifndef VOXELCRAFT_GAME_HYDROLOGY_H
#define VOXELCRAFT_GAME_HYDROLOGY_H

#include "world/local_climate.h"

#include <stdbool.h>

typedef struct GameRuntime GameRuntime;

struct GameRuntime;

void GameAdvanceHydrologySimulationStep(struct GameRuntime *game,
                                        double simulationSeconds,
                                        double stepSeconds);
bool GameHydrologyFeedbackAt(int x, int z, double simulationTime,
                             LocalClimateHydrologyFeedback *outFeedback,
                             void *userData);

#endif
