#include "gameplay/character_structure_physics.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void TestCharacterGravityBuoyancyAndContact(void)
{
    CharacterBodyState body = {
        .position = {0.0, 10.0, 0.0}, .massKg = 70.0, .radiusMeters = 0.4};
    CharacterPhysicsEnvironment env = CharacterPhysicsDefaultEnvironment();
    CharacterPhysicsStepResult result;
    assert(CharacterPhysicsStep(&body, &env, NULL, 1.0, &result));
    assert(body.position.y < 10.0);
    assert(body.velocity.y < 0.0);
    env.submergedFraction = 1.0;
    body.velocity = (CharacterPhysicsVec3){0.0, 0.0, 0.0};
    assert(CharacterPhysicsStep(&body, &env, NULL, 0.1, &result));
    assert(result.buoyancyForceN.y > 0.0);
    CharacterContact contact = {
        .touching = true, .normal = {0.0, 1.0, 0.0},
        .penetrationMeters = 0.2};
    env.submergedFraction = 0.0;
    body.velocity = (CharacterPhysicsVec3){0.0, -2.0, 0.0};
    assert(CharacterPhysicsStep(&body, &env, &contact, 0.1, &result));
    assert(result.contactImpulseNs > 0.0);
}

static void TestStructureFailureAndSurvival(void)
{
    StructurePhysicsState structure = {
        .massKg = 100.0, .supportCapacityN = 1000.0,
        .stiffnessNPerMeter = 10000.0};
    StructurePhysicsEnvironment env = StructurePhysicsDefaultEnvironment();
    env.damageRatePerSecond = 0.1;
    StructurePhysicsStepResult result;
    assert(StructurePhysicsStep(&structure, &env, 10000.0, 20.0, &result));
    assert(result.overloadRatio > 1.0);
    assert(structure.collapsed);

    SurvivalState survivor = {
        .health = 1.0, .hydrationLiters = 4.0,
        .energyKJ = 5000.0, .coreTemperatureK = 310.15,
        .sleepDebtHours = 12.0, .alive = true};
    SurvivalEnvironment survival = SurvivalDefaultEnvironment();
    assert(SurvivalStep(&survivor, &survival, 3600.0));
    assert(survivor.hydrationLiters < 4.0);
    assert(survivor.energyKJ < 5000.0);
    assert(survivor.sleepDebtHours > 12.0);
    survival.resting = true;
    assert(SurvivalStep(&survivor, &survival, 3600.0));
    assert(survivor.sleepDebtHours < 13.0);
}

static void TestDeterminismAndInvalidInput(void)
{
    CharacterBodyState first = {
        .position = {1.0, 2.0, 3.0}, .velocity = {0.5, 0.0, -0.2},
        .massKg = 70.0, .radiusMeters = 0.4};
    CharacterBodyState second = first;
    CharacterPhysicsEnvironment env = CharacterPhysicsDefaultEnvironment();
    assert(CharacterPhysicsStep(&first, &env, NULL, 0.25, NULL));
    assert(CharacterPhysicsStep(&second, &env, NULL, 0.25, NULL));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(!CharacterPhysicsStep(&first, &env, NULL, NAN, NULL));
    assert(!StructurePhysicsStep(NULL, NULL, 0.0, 1.0, NULL));
    assert(!SurvivalStep(NULL, NULL, 1.0));
}

int main(void)
{
    TestCharacterGravityBuoyancyAndContact();
    TestStructureFailureAndSurvival();
    TestDeterminismAndInvalidInput();
    puts("character structure physics tests passed");
    return 0;
}
