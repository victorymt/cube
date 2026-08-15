#ifndef VOXELCRAFT_SPACE_SYSTEM_H
#define VOXELCRAFT_SPACE_SYSTEM_H

#include <stdbool.h>
#include <stdint.h>

#define SPACE_SYSTEM_MAX_PLANETS 6
#define SPACE_SYSTEM_MIN_ORBIT_GAME 4.0f

typedef struct SpaceSystemFormationInput {
    uint32_t seed;
    float stellarMassSolar;
    float stellarLuminositySolar;
    float stellarAgeGyr;
    int stellarCount;
    float innerStabilityLimitGame;
    float outerLimitGame;
} SpaceSystemFormationInput;

typedef struct SpaceSystemFormationPlanet {
    float orbitGame;
    float massEarth;
    float radiusEarth;
    bool gasGiant;
} SpaceSystemFormationPlanet;

typedef struct SpaceSystemFormation {
    float metallicity;
    float diskMassEarth;
    float snowLineGame;
    float habitableInnerGame;
    float habitableOuterGame;
    float innerStableOrbitGame;
    float outerStableOrbitGame;
    int planetCount;
    SpaceSystemFormationPlanet planets[SPACE_SYSTEM_MAX_PLANETS];
} SpaceSystemFormation;

bool SpaceSystemFormationGenerate(const SpaceSystemFormationInput *input,
                                  SpaceSystemFormation *out);

#endif
