#ifndef VOXELCRAFT_ECOLOGY_INTERNAL_H
#define VOXELCRAFT_ECOLOGY_INTERNAL_H

#include "ecology.h"

#include <stdint.h>

uint32_t EcologyMix(uint32_t value);
int EcologyFloorDivide(int value, int divisor);
uint32_t EcologyHash(int x, int z, uint32_t salt);
float EcologyClamp(float value);

uint32_t EcologyProfileGeneration(void);
PlanetEcologyTraits EcologyTraitsForProfile(
    const PlanetEcologyProfile *profile);
PlanetLocalEnvironment EcologyEnvironmentAt(
    int x, int z, double simulationTime, float daylight,
    float precipitationRate, float currentStorm, bool dynamic,
    const PlanetEcologyProfile *ecology);
PlanetEcologySuitability EcologyStaticSuitabilityForProfile(
    int x, int z, const PlanetEcologyProfile *profile);
PlanetLocalEcology EcologyDynamicLocalAt(
    int x, int z, double simulationTime, float daylight,
    const PlanetEcologyProfile *profile);

#endif

