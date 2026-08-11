#include "space_remnant.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestRemnantInputContracts(void)
{
    SpaceRemnantState state;
    const SpaceRemnantState cleared = { 0 };
    memset(&state, 0xa5, sizeof(state));
    assert(!SpaceRemnantStateForProfile(NULL, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);
    assert(!SpaceRemnantStateForProfile(NULL, NULL));

    StellarProfile invalid = { 0 };
    memset(&state, 0xa5, sizeof(state));
    assert(!SpaceRemnantStateForProfile(&invalid, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);

    SpaceRemnantEnvironment environment = {
        .active = true,
        .remnantCount = 1,
        .radiationHazard = 0.4f,
        .ejectaDensity = 0.2f,
        .nearestShellDistanceGame = 12.0f
    };
    assert(SpaceRemnantEnvironmentIsValid(&environment));
    environment.radiationHazard = NAN;
    assert(!SpaceRemnantEnvironmentIsValid(&environment));
}

static void TestRemnantCausalEvolution(void)
{
    StellarProfile initial;
    assert(StellarProfileAtAge(12.0f, 0.0, 0x12345678u, &initial));
    double eventAgeGyr = initial.luminousLifetimeGyr;
    StellarProfile eventProfile;
    StellarProfile oneThousandYears;
    StellarProfile oneHundredThousandYears;
    StellarProfile fadedProfile;
    assert(StellarProfileAtAge(
        initial.initialMassSolar, eventAgeGyr + 0.000001,
        initial.evolutionSeed, &eventProfile));
    assert(StellarProfileAtAge(
        initial.initialMassSolar, eventAgeGyr + 0.000002,
        initial.evolutionSeed, &oneThousandYears));
    assert(StellarProfileAtAge(
        initial.initialMassSolar, eventAgeGyr + 0.0001,
        initial.evolutionSeed, &oneHundredThousandYears));
    assert(StellarProfileAtAge(
        initial.initialMassSolar, eventAgeGyr + 0.001,
        initial.evolutionSeed, &fadedProfile));
    assert(eventProfile.stage == STELLAR_STAGE_NEUTRON_STAR);

    SpaceRemnantState atEvent;
    SpaceRemnantState atOneThousandYears;
    SpaceRemnantState atOneHundredThousandYears;
    SpaceRemnantState faded;
    assert(SpaceRemnantStateForProfile(&eventProfile, &atEvent));
    assert(SpaceRemnantStateForProfile(
        &oneThousandYears, &atOneThousandYears));
    assert(SpaceRemnantStateForProfile(
        &oneHundredThousandYears, &atOneHundredThousandYears));
    assert(SpaceRemnantStateForProfile(&fadedProfile, &faded));
    assert(atEvent.active && atOneThousandYears.active &&
           atOneHundredThousandYears.active);
    assert(!faded.active);
    assert(atEvent.ageYears <= atOneThousandYears.ageYears);
    assert(atOneThousandYears.ageYears <
           atOneHundredThousandYears.ageYears);
    assert(atEvent.proxyShockRadiusGame <
           atOneThousandYears.proxyShockRadiusGame);
    assert(atOneThousandYears.proxyShockRadiusGame <
           atOneHundredThousandYears.proxyShockRadiusGame);
    assert(atEvent.ejectaStrength > atOneHundredThousandYears.ejectaStrength);
    assert(atEvent.coreRadiation >
           atOneHundredThousandYears.coreRadiation);
    assert(atEvent.physicalShockRadiusKm <
           atOneHundredThousandYears.physicalShockRadiusKm);

    float coreHazard = SpaceRemnantRadiationHazardAtDistance(&atEvent, 0.0);
    float farHazard = SpaceRemnantRadiationHazardAtDistance(
        &atEvent, atEvent.proxyShockRadiusGame * 8.0);
    float shellDensity = SpaceRemnantEjectaDensityAtDistance(
        &atOneThousandYears, atOneThousandYears.proxyShockRadiusGame);
    float centerDensity = SpaceRemnantEjectaDensityAtDistance(
        &atOneThousandYears, 0.0);
    assert(coreHazard > farHazard);
    assert(shellDensity > centerDensity);
    assert(coreHazard > 0.0f && farHazard >= 0.0f);
    assert(SpaceRemnantRadiationHazardAtDistance(
               &atEvent, NAN) == 0.0f);

    SpaceRemnantState repeated;
    assert(SpaceRemnantStateForProfile(&oneThousandYears, &repeated));
    assert(memcmp(&repeated, &atOneThousandYears,
                  sizeof(repeated)) == 0);
}

static void TestRemnantSeedAndTypeProperties(void)
{
    StellarProfile neutron;
    StellarProfile blackHole;
    assert(StellarProfileAtAge(12.0f, 0.0, 0x0badc0deu, &neutron));
    assert(StellarProfileAtAge(30.0f, 0.0, 0x0badc0deu, &blackHole));
    assert(StellarProfileAtAge(
        neutron.initialMassSolar,
        (double)neutron.luminousLifetimeGyr + 0.000001,
        neutron.evolutionSeed, &neutron));
    assert(StellarProfileAtAge(
        blackHole.initialMassSolar,
        (double)blackHole.luminousLifetimeGyr + 0.000001,
        blackHole.evolutionSeed, &blackHole));

    SpaceRemnantState neutronState;
    SpaceRemnantState blackHoleState;
    assert(SpaceRemnantStateForProfile(&neutron, &neutronState));
    assert(SpaceRemnantStateForProfile(&blackHole, &blackHoleState));
    assert(neutronState.active && !neutronState.blackHole);
    assert(blackHoleState.active && blackHoleState.blackHole);
    assert(neutronState.coreRadiation > blackHoleState.coreRadiation);

    StellarProfile otherSeedProfile;
    assert(StellarProfileAtAge(
        neutron.initialMassSolar,
        (double)neutron.luminousLifetimeGyr + 0.000001,
        0x10203040u, &otherSeedProfile));
    SpaceRemnantState otherSeed;
    assert(SpaceRemnantStateForProfile(&otherSeedProfile, &otherSeed));
    assert(memcmp(&neutronState, &otherSeed, sizeof(neutronState)) != 0);

    StellarProfile mainSequence;
    assert(StellarProfileAtAge(1.0f, 0.0, 0u, &mainSequence));
    SpaceRemnantState inactive;
    assert(SpaceRemnantStateForProfile(&mainSequence, &inactive));
    assert(!inactive.active);
    assert(SpaceRemnantStateIsValid(&inactive));
}

int main(void)
{
    TestRemnantInputContracts();
    TestRemnantCausalEvolution();
    TestRemnantSeedAndTypeProperties();
    puts("space remnant tests passed");
    return 0;
}
