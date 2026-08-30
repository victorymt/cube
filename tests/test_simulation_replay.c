#include "core/simulation_replay.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void TestRecordPlaybackAndHash(void)
{
    SimulationReplay replay;
    assert(SimulationReplayBeginRecording(&replay));
    assert(SimulationReplayRecord(&replay, 0u, 7u, 1));
    assert(SimulationReplayRecord(&replay, 10u, 8u, -2));
    assert(!SimulationReplayRecord(&replay, 9u, 8u, -2));
    uint64_t expectedHash = SimulationReplayHash(&replay);
    assert(expectedHash != 0u);
    assert(SimulationReplayFinishRecording(&replay));
    assert(SimulationReplayBeginPlayback(&replay));
    SimulationReplayEvent event;
    assert(SimulationReplayNext(&replay, &event));
    assert(event.tick == 0u && event.action == 7u && event.value == 1);
    assert(SimulationReplayNext(&replay, &event));
    assert(event.tick == 10u && event.value == -2);
    assert(!SimulationReplayNext(&replay, &event));
    assert(SimulationReplayHash(&replay) == expectedHash);
}

static void TestCapacityAndChangedInput(void)
{
    SimulationReplay first;
    SimulationReplay second;
    assert(SimulationReplayBeginRecording(&first));
    assert(SimulationReplayBeginRecording(&second));
    for (unsigned index = 0; index < SIMULATION_REPLAY_MAX_EVENTS; index++) {
        assert(SimulationReplayRecord(&first, index, 1u, (int32_t)index));
        assert(SimulationReplayRecord(&second, index, 1u, (int32_t)index));
    }
    assert(!SimulationReplayRecord(&first, SIMULATION_REPLAY_MAX_EVENTS,
                                   1u, 0));
    assert(SimulationReplayHash(&first) == SimulationReplayHash(&second));
    second.events[SIMULATION_REPLAY_MAX_EVENTS - 1u].action = 99u;
    assert(SimulationReplayHash(&first) != SimulationReplayHash(&second));
    assert(SimulationReplayEventCount(&first) == SIMULATION_REPLAY_MAX_EVENTS);
}

int main(void)
{
    TestRecordPlaybackAndHash();
    TestCapacityAndChangedInput();
    puts("simulation replay tests passed");
    return 0;
}
