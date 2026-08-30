#ifndef VOXELCRAFT_SIMULATION_REPLAY_H
#define VOXELCRAFT_SIMULATION_REPLAY_H

#include <stdbool.h>
#include <stdint.h>

enum {
    SIMULATION_REPLAY_MAX_EVENTS = 2048
};

typedef struct SimulationReplayEvent {
    uint64_t tick;
    uint32_t action;
    int32_t value;
} SimulationReplayEvent;

typedef struct SimulationReplay {
    SimulationReplayEvent events[SIMULATION_REPLAY_MAX_EVENTS];
    uint64_t hash;
    uint64_t lastTick;
    unsigned eventCount;
    unsigned cursor;
    bool recording;
} SimulationReplay;

void SimulationReplayInit(SimulationReplay *replay);
bool SimulationReplayBeginRecording(SimulationReplay *replay);
bool SimulationReplayRecord(SimulationReplay *replay,
                            uint64_t tick,
                            uint32_t action,
                            int32_t value);
bool SimulationReplayFinishRecording(SimulationReplay *replay);
bool SimulationReplayBeginPlayback(SimulationReplay *replay);
bool SimulationReplayNext(SimulationReplay *replay,
                          SimulationReplayEvent *event);
uint64_t SimulationReplayHash(const SimulationReplay *replay);
unsigned SimulationReplayEventCount(const SimulationReplay *replay);

#endif
