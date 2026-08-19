#ifndef VOXELCRAFT_TORNADO_MODEL_H
#define VOXELCRAFT_TORNADO_MODEL_H

#include "world/weather_model.h"

#include "raylib.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum TornadoPhase {
    TORNADO_PHASE_INACTIVE = 0,
    TORNADO_PHASE_FORMING,
    TORNADO_PHASE_INTENSIFYING,
    TORNADO_PHASE_MATURE,
    TORNADO_PHASE_DISSIPATING
} TornadoPhase;

typedef struct TornadoFormationInput {
    WeatherFieldSample weather;
    bool atmosphereActive;
    bool supportsWaterCycle;
} TornadoFormationInput;

typedef struct TornadoState {
    bool active;
    bool forced;
    TornadoPhase phase;
    uint32_t seed;
    uint32_t surfaceId;
    Vector3 center;
    Vector3 velocity;
    float peakIntensity;
    float intensity;
    float radius;
    float influenceRadius;
    float funnelHeight;
    float maximumWindMps;
    float condensation;
    float dustLoading;
    float rotation;
    float rotationSign;
    float age;
    float lifetime;
} TornadoState;

typedef struct TornadoForceSample {
    Vector3 acceleration;
    float exposure;
    float localWindMps;
    float horizontalDistance;
} TornadoForceSample;

float TornadoFormationPotential(const TornadoFormationInput *input);
TornadoState TornadoModelCreate(uint32_t seed, uint32_t surfaceId,
                                Vector3 center, float peakIntensity,
                                float lifetime, bool forced);
void TornadoModelAdvance(TornadoState *state, float dt,
                         WeatherFieldSample weather, float groundY);
TornadoForceSample TornadoModelForceAt(const TornadoState *state,
                                       Vector3 position);
const char *TornadoPhaseName(TornadoPhase phase);

#endif
