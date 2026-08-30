#include "core/simulation_replay.h"

#include <stddef.h>

#define REPLAY_FNV_OFFSET UINT64_C(1469598103934665603)
#define REPLAY_FNV_PRIME UINT64_C(1099511628211)

static uint64_t MixByte(uint64_t hash, unsigned char byte)
{
    return (hash ^ (uint64_t)byte) * REPLAY_FNV_PRIME;
}

static uint64_t MixU64(uint64_t hash, uint64_t value)
{
    for (unsigned index = 0; index < 8u; index++) {
        hash = MixByte(hash, (unsigned char)(value & UINT64_C(0xff)));
        value >>= 8;
    }
    return hash;
}

static uint64_t HashEvents(const SimulationReplay *replay)
{
    uint64_t hash = REPLAY_FNV_OFFSET;
    if (!replay) return hash;
    for (unsigned index = 0; index < replay->eventCount; index++) {
        const SimulationReplayEvent *event = &replay->events[index];
        hash = MixU64(hash, event->tick);
        hash = MixU64(hash, event->action);
        hash = MixU64(hash, (uint32_t)event->value);
    }
    return MixU64(hash, replay->eventCount);
}

void SimulationReplayInit(SimulationReplay *replay)
{
    if (!replay) return;
    *replay = (SimulationReplay){0};
    replay->hash = REPLAY_FNV_OFFSET;
}

bool SimulationReplayBeginRecording(SimulationReplay *replay)
{
    if (!replay) return false;
    SimulationReplayInit(replay);
    replay->recording = true;
    return true;
}

bool SimulationReplayRecord(SimulationReplay *replay,
                            uint64_t tick,
                            uint32_t action,
                            int32_t value)
{
    if (!replay || !replay->recording ||
        replay->eventCount >= SIMULATION_REPLAY_MAX_EVENTS ||
        (replay->eventCount > 0u && tick < replay->lastTick)) {
        return false;
    }
    SimulationReplayEvent *event = &replay->events[replay->eventCount++];
    *event = (SimulationReplayEvent){
        .tick = tick, .action = action, .value = value};
    replay->lastTick = tick;
    replay->hash = HashEvents(replay);
    return true;
}

bool SimulationReplayFinishRecording(SimulationReplay *replay)
{
    if (!replay || !replay->recording) return false;
    replay->recording = false;
    replay->hash = HashEvents(replay);
    return true;
}

bool SimulationReplayBeginPlayback(SimulationReplay *replay)
{
    if (!replay || replay->recording) return false;
    if (replay->eventCount > SIMULATION_REPLAY_MAX_EVENTS) return false;
    replay->cursor = 0u;
    replay->hash = HashEvents(replay);
    return true;
}

bool SimulationReplayNext(SimulationReplay *replay,
                          SimulationReplayEvent *event)
{
    if (!replay || replay->recording || !event ||
        replay->cursor >= replay->eventCount) {
        return false;
    }
    *event = replay->events[replay->cursor++];
    return true;
}

uint64_t SimulationReplayHash(const SimulationReplay *replay)
{
    return HashEvents(replay);
}

unsigned SimulationReplayEventCount(const SimulationReplay *replay)
{
    return replay ? replay->eventCount : 0u;
}
