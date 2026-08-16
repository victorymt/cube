#ifndef VOXELCRAFT_PLANET_OBSERVATION_H
#define VOXELCRAFT_PLANET_OBSERVATION_H

#include "space/space.h"

typedef enum PlanetObservationPhase {
    PLANET_OBSERVATION_NIGHT = 0,
    PLANET_OBSERVATION_ASTRONOMICAL_TWILIGHT,
    PLANET_OBSERVATION_NAUTICAL_TWILIGHT,
    PLANET_OBSERVATION_CIVIL_TWILIGHT,
    PLANET_OBSERVATION_DAY,
    PLANET_OBSERVATION_ECLIPSE
} PlanetObservationPhase;

typedef struct PlanetObservationState {
    PlanetObservationPhase phase;
    float solarAltitudeRad;
    float twilightStrength;
    float skyBrightness;
    float horizonWarmth;
    float starVisibility;
    float moonVisibility;
    float moonHaloStrength;
    float eclipseDarkening;
    float opticalDepth;
    float atmosphereVisibility;
    bool valid;
} PlanetObservationState;

PlanetObservationState PlanetObservationEvaluate(
    const PlanetLightState *light, float opticalDepth, float mieStrength,
    float atmosphereVisibility);
const char *PlanetObservationPhaseName(PlanetObservationPhase phase);

#endif
