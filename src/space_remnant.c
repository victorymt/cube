#include "space_remnant.h"

#include <math.h>
#include <stdint.h>

const double SPACE_REMNANT_ACTIVE_LIFETIME_YEARS = 250000.0;
const double SPACE_REMNANT_PARSEC_KM = 3.0856775814913673e13;
const float SPACE_REMNANT_MAX_PROXY_RADIUS_GAME = 36000.0f;

static float SpaceRemnantClamp(float value, float minimum, float maximum)
{
    return fminf(fmaxf(value, minimum), maximum);
}

static uint32_t SpaceRemnantMix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static double SpaceRemnantHashUnit(uint32_t value)
{
    return (double)(SpaceRemnantMix32(value) >> 8) / 16777215.0;
}

static bool SpaceRemnantProfileIsFinite(const StellarProfile *profile)
{
    return profile && profile->stage >= STELLAR_STAGE_MAIN_SEQUENCE &&
           profile->stage <= STELLAR_STAGE_BLACK_HOLE &&
           profile->ageGyr >= 0.0f &&
           isfinite(profile->ageGyr) && profile->luminousLifetimeGyr > 0.0f &&
           isfinite(profile->luminousLifetimeGyr);
}

bool SpaceRemnantStateIsValid(const SpaceRemnantState *state)
{
    if (!state) return false;
    if (!state->active) {
        return state->ageYears == 0.0 &&
               state->physicalShockRadiusKm == 0.0 &&
               state->proxyShockRadiusGame == 0.0f &&
               state->shellThicknessGame == 0.0f &&
               state->ejectaStrength == 0.0f &&
               state->coreRadiation == 0.0f &&
               state->ambientDensityCm3 == 0.0f &&
               state->explosionEnergyBethe == 0.0f;
    }
    return state->ageYears >= 0.0 &&
           state->ageYears <= SPACE_REMNANT_ACTIVE_LIFETIME_YEARS &&
           isfinite(state->ageYears) && state->physicalShockRadiusKm > 0.0 &&
           isfinite(state->physicalShockRadiusKm) &&
           state->proxyShockRadiusGame > 0.0f &&
           state->proxyShockRadiusGame <= SPACE_REMNANT_MAX_PROXY_RADIUS_GAME &&
           isfinite(state->proxyShockRadiusGame) &&
           state->shellThicknessGame > 0.0f &&
           isfinite(state->shellThicknessGame) &&
           state->ejectaStrength >= 0.0f && state->ejectaStrength <= 1.0f &&
           isfinite(state->ejectaStrength) && state->coreRadiation >= 0.0f &&
           state->coreRadiation <= 1.0f && isfinite(state->coreRadiation) &&
           state->ambientDensityCm3 > 0.0f &&
           isfinite(state->ambientDensityCm3) &&
           state->explosionEnergyBethe > 0.0f &&
           isfinite(state->explosionEnergyBethe);
}

bool SpaceRemnantStateForProfile(const StellarProfile *profile,
                                 SpaceRemnantState *out)
{
    if (!out) return false;
    *out = (SpaceRemnantState){ 0 };
    if (!SpaceRemnantProfileIsFinite(profile)) return false;
    if (profile->stage != STELLAR_STAGE_NEUTRON_STAR &&
        profile->stage != STELLAR_STAGE_BLACK_HOLE) {
        return true;
    }

    double ageYears = ((double)profile->ageGyr -
                       (double)profile->luminousLifetimeGyr) * 1.0e9;
    if (ageYears < 0.0 || !isfinite(ageYears)) return false;
    if (ageYears > SPACE_REMNANT_ACTIVE_LIFETIME_YEARS) return true;

    bool blackHole = profile->stage == STELLAR_STAGE_BLACK_HOLE;
    double ambientUnit = SpaceRemnantHashUnit(
        profile->evolutionSeed ^ 0x9e3779b9u);
    double energyUnit = SpaceRemnantHashUnit(
        profile->evolutionSeed ^ 0x243f6a88u);
    double ambientDensity = 0.20 + 1.80 * ambientUnit;
    double explosionEnergy = (blackHole ? 0.55 : 0.80) +
                             (blackHole ? 0.35 : 0.40) * energyUnit;
    double ageForRadius = fmax(ageYears, 1.0e-6);
    double freeExpansionPc = 0.0102 * ageForRadius;
    double sedovPc = 5.0 * pow(ageForRadius / 1000.0, 0.4) *
                     pow(explosionEnergy / ambientDensity, 0.2);
    double shockRadiusPc = fmax(1.0e-6, fmin(freeExpansionPc, sedovPc));
    double progress = ageYears / SPACE_REMNANT_ACTIVE_LIFETIME_YEARS;
    double proxyRadius = 600.0 +
                         (double)SPACE_REMNANT_MAX_PROXY_RADIUS_GAME *
                         pow(progress, 0.38);
    double shellThickness = fmax(200.0, proxyRadius * 0.13);
    double fade = pow(fmax(0.0, 1.0 - progress), 0.65);
    double radiationBase = blackHole ? 0.55 : 1.0;
    double coreRadiation = radiationBase * fmax(0.08, pow(fade, 0.55));

    *out = (SpaceRemnantState){
        .active = true,
        .blackHole = blackHole,
        .ageYears = ageYears,
        .physicalShockRadiusKm = shockRadiusPc * SPACE_REMNANT_PARSEC_KM,
        .proxyShockRadiusGame = (float)proxyRadius,
        .shellThicknessGame = (float)shellThickness,
        .ejectaStrength = (float)fade,
        .coreRadiation = (float)coreRadiation,
        .ambientDensityCm3 = (float)ambientDensity,
        .explosionEnergyBethe = (float)explosionEnergy
    };
    return SpaceRemnantStateIsValid(out);
}

static float SpaceRemnantShellProfile(const SpaceRemnantState *state,
                                      double distanceGame)
{
    double shellDistance = (distanceGame -
                            (double)state->proxyShockRadiusGame) /
                           (double)state->shellThicknessGame;
    return (float)exp(-0.5 * shellDistance * shellDistance);
}

float SpaceRemnantEjectaDensityAtDistance(
    const SpaceRemnantState *state, double distanceGame)
{
    if (!SpaceRemnantStateIsValid(state) || !(distanceGame >= 0.0) ||
        !isfinite(distanceGame)) return 0.0f;
    return SpaceRemnantClamp(
        state->ejectaStrength * SpaceRemnantShellProfile(state, distanceGame),
        0.0f, 1.0f);
}

float SpaceRemnantRadiationHazardAtDistance(
    const SpaceRemnantState *state, double distanceGame)
{
    if (!SpaceRemnantStateIsValid(state) || !(distanceGame >= 0.0) ||
        !isfinite(distanceGame)) return 0.0f;
    double coreScale = fmax(4.0,
                            (double)state->proxyShockRadiusGame * 0.12);
    double coreHazard = (double)state->coreRadiation /
                        (1.0 + distanceGame * distanceGame /
                                   (coreScale * coreScale));
    double shellHazard = 0.80 * (double)SpaceRemnantEjectaDensityAtDistance(
        state, distanceGame);
    return SpaceRemnantClamp((float)(1.0 -
                                     (1.0 - coreHazard) *
                                     (1.0 - shellHazard)), 0.0f, 1.0f);
}

bool SpaceRemnantEnvironmentIsValid(
    const SpaceRemnantEnvironment *environment)
{
    if (!environment || environment->remnantCount < 0 ||
        environment->remnantCount > 3 ||
        environment->radiationHazard < 0.0f ||
        environment->radiationHazard > 1.0f ||
        !isfinite(environment->radiationHazard) ||
        environment->ejectaDensity < 0.0f || environment->ejectaDensity > 1.0f ||
        !isfinite(environment->ejectaDensity) ||
        environment->nearestShellDistanceGame < 0.0f ||
        !isfinite(environment->nearestShellDistanceGame)) {
        return false;
    }
    if (!environment->active) {
        return environment->remnantCount == 0 &&
               environment->radiationHazard == 0.0f &&
               environment->ejectaDensity == 0.0f &&
               environment->nearestShellDistanceGame == 0.0f;
    }
    return environment->remnantCount > 0 &&
           environment->radiationHazard > 0.0f;
}
