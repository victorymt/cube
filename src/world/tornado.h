#ifndef VOXELCRAFT_TORNADO_H
#define VOXELCRAFT_TORNADO_H

#include "world/tornado_model.h"

#define TORNADO_TICK_RATE 4.0f
#define TORNADO_DAMAGE_SAMPLES_PER_TICK 4u

typedef struct TornadoStats {
    uint64_t ticks;
    uint64_t formationAttempts;
    uint32_t naturalFormations;
    uint32_t forcedFormations;
    uint32_t processedDamageSamples;
    uint32_t blockDamageEvents;
    uint32_t debrisEmitted;
    uint32_t dustEmitted;
    uint32_t droppedEffects;
} TornadoStats;

void TornadoInit(bool damageEnabled);
void TornadoReset(void);
void TornadoSuspend(void);
void TornadoSetDamageEnabled(bool enabled);
bool TornadoDamageEnabled(void);
void TornadoSetParticleScale(float scale);
void TornadoUpdate(float dt, Vector3 observerPosition,
                   WeatherFieldSample weather);
void TornadoStepTicks(unsigned ticks, Vector3 observerPosition,
                      WeatherFieldSample weather);
bool TornadoForce(Vector3 observerPosition, float intensity,
                  unsigned frames, float distance,
                  WeatherFieldSample weather);
void TornadoClear(void);
TornadoState TornadoCurrent(void);
TornadoStats TornadoGetStats(void);
unsigned TornadoForcedFramesRemaining(void);
TornadoForceSample TornadoForceAt(Vector3 position);
float TornadoDistanceTo(Vector3 position);

#endif
