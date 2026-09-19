#ifndef VOXELCRAFT_GAME_ECOLOGY_H
#define VOXELCRAFT_GAME_ECOLOGY_H

#include <stdbool.h>
#include <stdio.h>

typedef struct GameRuntime GameRuntime;

typedef struct GameHomeDaylightSnapshot {
    double simulationTime;
    double dayPhase;
    bool cycleEnabled;
} GameHomeDaylightSnapshot;

bool GameWorldSimulationPaused(const GameRuntime *game);
bool GameWorldSimulationUserPaused(const GameRuntime *game);
unsigned GameAdvanceSimulationFrame(GameRuntime *game, double realDeltaSeconds);
void GameAdvanceSimulationStep(double simulationSeconds,
                               double stepSeconds, void *context);

/* Publish one immutable phase anchor for worker-thread weather queries. */
void GameSyncHomeDaylight(const GameRuntime *game);
GameHomeDaylightSnapshot GameCaptureHomeDaylight(void);
void GameRestoreHomeDaylight(GameHomeDaylightSnapshot snapshot);
float GameHomeDaylightAtSimulationTime(double simulationTime);
bool GameEcologySaveClock(FILE *file);
bool GameEcologyLoadClock(FILE *file);
void GameEcologyResetClock(void);
void GameEcologyRestoreClock(GameRuntime *game);

#endif
