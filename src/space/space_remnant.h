#ifndef VOXELCRAFT_SPACE_REMNANT_H
#define VOXELCRAFT_SPACE_REMNANT_H

#include "space/stellar.h"

#include <stdbool.h>

extern const double SPACE_REMNANT_ACTIVE_LIFETIME_YEARS;
extern const double SPACE_REMNANT_PARSEC_KM;
extern const float SPACE_REMNANT_MAX_PROXY_RADIUS_GAME;

typedef struct SpaceRemnantState {
    bool active;
    bool blackHole;
    double ageYears;
    double physicalShockRadiusKm;
    float proxyShockRadiusGame;
    float shellThicknessGame;
    float ejectaStrength;
    float coreRadiation;
    float ambientDensityCm3;
    float explosionEnergyBethe;
} SpaceRemnantState;

typedef struct SpaceRemnantEnvironment {
    bool active;
    int remnantCount;
    float radiationHazard;
    float ejectaDensity;
    float nearestShellDistanceGame;
} SpaceRemnantEnvironment;

bool SpaceRemnantStateForProfile(const StellarProfile *profile,
                                 SpaceRemnantState *out);
bool SpaceRemnantStateIsValid(const SpaceRemnantState *state);
float SpaceRemnantRadiationHazardAtDistance(
    const SpaceRemnantState *state, double distanceGame);
float SpaceRemnantEjectaDensityAtDistance(
    const SpaceRemnantState *state, double distanceGame);
bool SpaceRemnantEnvironmentIsValid(
    const SpaceRemnantEnvironment *environment);

#endif
