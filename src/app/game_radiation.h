#ifndef VOXELCRAFT_GAME_RADIATION_H
#define VOXELCRAFT_GAME_RADIATION_H

struct GameRuntime;

void GameAdvanceRadiationSimulationStep(struct GameRuntime *game,
                                         double simulationSeconds,
                                         double stepSeconds);

#endif
