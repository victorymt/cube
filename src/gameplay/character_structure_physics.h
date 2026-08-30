#ifndef VOXELCRAFT_CHARACTER_STRUCTURE_PHYSICS_H
#define VOXELCRAFT_CHARACTER_STRUCTURE_PHYSICS_H

#include <stdbool.h>

typedef struct CharacterPhysicsVec3 {
    double x;
    double y;
    double z;
} CharacterPhysicsVec3;

typedef struct CharacterBodyState {
    CharacterPhysicsVec3 position;
    CharacterPhysicsVec3 velocity;
    double massKg;
    double radiusMeters;
} CharacterBodyState;

typedef struct CharacterPhysicsEnvironment {
    double gravityMps2;
    double fluidDensityKgPerM3;
    double dragCoefficient;
    CharacterPhysicsVec3 windVelocity;
    double submergedFraction;
    double groundY;
    double restitution;
    double friction;
} CharacterPhysicsEnvironment;

typedef struct CharacterContact {
    bool touching;
    CharacterPhysicsVec3 normal;
    double penetrationMeters;
} CharacterContact;

typedef struct CharacterPhysicsStepResult {
    CharacterPhysicsVec3 dragForceN;
    CharacterPhysicsVec3 buoyancyForceN;
    double contactImpulseNs;
    double kineticEnergyJ;
} CharacterPhysicsStepResult;

CharacterPhysicsEnvironment CharacterPhysicsDefaultEnvironment(void);
bool CharacterBodyStateValid(const CharacterBodyState *state);
bool CharacterPhysicsStep(CharacterBodyState *state,
                          const CharacterPhysicsEnvironment *environment,
                          const CharacterContact *contact,
                          double seconds,
                          CharacterPhysicsStepResult *result);

typedef struct StructurePhysicsState {
    double massKg;
    double supportCapacityN;
    double stiffnessNPerMeter;
    double damage;
    double displacementMeters;
    bool collapsed;
} StructurePhysicsState;

typedef struct StructurePhysicsEnvironment {
    double gravityMps2;
    double windPressurePa;
    double loadedAreaM2;
    double damageRatePerSecond;
} StructurePhysicsEnvironment;

typedef struct StructurePhysicsStepResult {
    double totalLoadN;
    double capacityN;
    double overloadRatio;
    double damageAdded;
    bool collapsed;
} StructurePhysicsStepResult;

StructurePhysicsEnvironment StructurePhysicsDefaultEnvironment(void);
bool StructurePhysicsStateValid(const StructurePhysicsState *state);
bool StructurePhysicsStep(StructurePhysicsState *state,
                          const StructurePhysicsEnvironment *environment,
                          double externalLoadN,
                          double seconds,
                          StructurePhysicsStepResult *result);

typedef struct SurvivalState {
    double health;
    double hydrationLiters;
    double energyKJ;
    double coreTemperatureK;
    double sleepDebtHours;
    bool alive;
} SurvivalState;

typedef struct SurvivalEnvironment {
    double airTemperatureK;
    double relativeHumidity;
    double metabolicPowerW;
    double exertion;
    double waterIntakeLitersPerHour;
    double shelter;
    double clothingInsulation;
    bool resting;
} SurvivalEnvironment;

bool SurvivalStateValid(const SurvivalState *state);
SurvivalEnvironment SurvivalDefaultEnvironment(void);
bool SurvivalStep(SurvivalState *state,
                  const SurvivalEnvironment *environment,
                  double seconds);

#endif
