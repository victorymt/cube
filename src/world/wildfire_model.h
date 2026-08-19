#ifndef VOXELCRAFT_WILDFIRE_MODEL_H
#define VOXELCRAFT_WILDFIRE_MODEL_H

#include <stdbool.h>

typedef enum WildfirePhase {
    WILDFIRE_PHASE_INACTIVE = 0,
    WILDFIRE_PHASE_IGNITING,
    WILDFIRE_PHASE_FLAMING,
    WILDFIRE_PHASE_SMOLDERING
} WildfirePhase;

typedef struct WildfireEnvironment {
    float temperatureK;
    float relativeHumidity;
    float rain;
    float wind;
    float gust;
    float waterExposure;
    float suppression;
} WildfireEnvironment;

typedef struct WildfireState {
    WildfirePhase phase;
    float intensity;
    float fuel;
    float moisture;
    float ageSeconds;
    float heatOutput;
    float smokeOutput;
} WildfireState;

typedef struct WildfireSpreadInput {
    float sourceIntensity;
    float targetFlammability;
    float targetMoisture;
    float wind;
    float gust;
    float windAngle;
    float offsetX;
    float offsetZ;
    float slope;
} WildfireSpreadInput;

bool WildfireModelNormalize(WildfireState *state);
float WildfireEquilibriumMoisture(WildfireEnvironment environment);
bool WildfireCanIgnite(float flammability, float moisture,
                       float ignitionIntensity);
WildfireState WildfireModelCreate(float flammability, float fuelLoad,
                                  float moisture, float ignitionIntensity);
void WildfireModelAdvance(WildfireState *state, float dt,
                          float flammability,
                          WildfireEnvironment environment);
void WildfireModelApplySuppression(WildfireState *state, float amount);
float WildfireSpreadProbability(WildfireSpreadInput input);
float WildfireHeatExposure(const WildfireState *state, float distance,
                           float shelter, float immersion);
float WildfireSmokeExposure(const WildfireState *state, float distance,
                            float shelter, float immersion);
const char *WildfirePhaseName(WildfirePhase phase);

#endif
