#include "gameplay/character_structure_physics.h"

#include <math.h>

#define CHARACTER_PI 3.14159265358979323846

static double Clamp01(double value)
{
    if (!isfinite(value)) return 0.0;
    if (value < 0.0) return 0.0;
    if (value > 1.0) return 1.0;
    return value;
}

static double NonNegative(double value)
{
    return isfinite(value) && value > 0.0 ? value : 0.0;
}

static double PositiveOr(double value, double fallback)
{
    return isfinite(value) && value > 0.0 ? value : fallback;
}

static CharacterPhysicsVec3 Add(CharacterPhysicsVec3 a,
                                CharacterPhysicsVec3 b)
{
    return (CharacterPhysicsVec3){a.x + b.x, a.y + b.y, a.z + b.z};
}

static CharacterPhysicsVec3 Scale(CharacterPhysicsVec3 value, double scale)
{
    return (CharacterPhysicsVec3){value.x * scale, value.y * scale,
                                  value.z * scale};
}

static double Dot(CharacterPhysicsVec3 a, CharacterPhysicsVec3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static double Length(CharacterPhysicsVec3 value)
{
    return sqrt(fmax(0.0, Dot(value, value)));
}

static CharacterPhysicsEnvironment SanitizeCharacterEnvironment(
    const CharacterPhysicsEnvironment *input)
{
    CharacterPhysicsEnvironment result = CharacterPhysicsDefaultEnvironment();
    if (!input) return result;
    result = *input;
    result.gravityMps2 = PositiveOr(result.gravityMps2, 9.80665);
    result.fluidDensityKgPerM3 = NonNegative(result.fluidDensityKgPerM3);
    result.dragCoefficient = NonNegative(result.dragCoefficient);
    result.submergedFraction = Clamp01(result.submergedFraction);
    result.groundY = isfinite(result.groundY) ? result.groundY : 0.0;
    result.restitution = Clamp01(result.restitution);
    result.friction = Clamp01(result.friction);
    if (!isfinite(result.windVelocity.x)) result.windVelocity.x = 0.0;
    if (!isfinite(result.windVelocity.y)) result.windVelocity.y = 0.0;
    if (!isfinite(result.windVelocity.z)) result.windVelocity.z = 0.0;
    return result;
}

CharacterPhysicsEnvironment CharacterPhysicsDefaultEnvironment(void)
{
    return (CharacterPhysicsEnvironment){
        .gravityMps2 = 9.80665,
        .fluidDensityKgPerM3 = 1000.0,
        .dragCoefficient = 0.8,
        .windVelocity = {0.0, 0.0, 0.0},
        .submergedFraction = 0.0,
        .groundY = 0.0,
        .restitution = 0.1,
        .friction = 0.7,
    };
}

bool CharacterBodyStateValid(const CharacterBodyState *state)
{
    if (!state || !isfinite(state->massKg) || state->massKg <= 0.0 ||
        !isfinite(state->radiusMeters) || state->radiusMeters <= 0.0)
        return false;
    return isfinite(state->position.x) && isfinite(state->position.y) &&
           isfinite(state->position.z) && isfinite(state->velocity.x) &&
           isfinite(state->velocity.y) && isfinite(state->velocity.z);
}

bool CharacterPhysicsStep(CharacterBodyState *state,
                          const CharacterPhysicsEnvironment *environment,
                          const CharacterContact *contact,
                          double seconds,
                          CharacterPhysicsStepResult *result)
{
    if (!CharacterBodyStateValid(state) || !isfinite(seconds) || seconds < 0.0)
        return false;
    CharacterPhysicsEnvironment env =
        SanitizeCharacterEnvironment(environment);
    CharacterPhysicsStepResult local = {0};
    if (seconds == 0.0) {
        local.kineticEnergyJ = 0.5 * state->massKg *
                               Dot(state->velocity, state->velocity);
        if (result) *result = local;
        return true;
    }
    CharacterPhysicsVec3 relative = Add(state->velocity,
                                        Scale(env.windVelocity, -1.0));
    double speed = Length(relative);
    double area = CHARACTER_PI * state->radiusMeters * state->radiusMeters;
    double dragMagnitude = 0.5 * env.fluidDensityKgPerM3 *
                           env.dragCoefficient * area * speed;
    local.dragForceN = Scale(relative, -dragMagnitude);
    local.buoyancyForceN.y = env.submergedFraction *
                             env.fluidDensityKgPerM3 *
                             (4.0 / 3.0) * CHARACTER_PI *
                             state->radiusMeters * state->radiusMeters *
                             state->radiusMeters * env.gravityMps2;
    CharacterPhysicsVec3 force = local.dragForceN;
    force.y += local.buoyancyForceN.y - state->massKg * env.gravityMps2;
    state->velocity = Add(state->velocity,
                          Scale(force, seconds / state->massKg));
    state->position = Add(state->position, Scale(state->velocity, seconds));

    if (contact && contact->touching) {
        CharacterPhysicsVec3 normal = contact->normal;
        double normalLength = Length(normal);
        if (normalLength > 1e-9 && isfinite(contact->penetrationMeters) &&
            contact->penetrationMeters >= 0.0) {
            normal = Scale(normal, 1.0 / normalLength);
            state->position = Add(state->position,
                                  Scale(normal, contact->penetrationMeters));
            double normalVelocity = Dot(state->velocity, normal);
            if (normalVelocity < 0.0) {
                local.contactImpulseNs =
                    -(1.0 + env.restitution) * normalVelocity *
                    state->massKg;
                state->velocity = Add(state->velocity,
                                      Scale(normal, -normalVelocity *
                                                     (1.0 + env.restitution)));
                CharacterPhysicsVec3 tangent = Add(
                    state->velocity, Scale(normal, -Dot(state->velocity, normal)));
                state->velocity = Add(
                    Scale(normal, Dot(state->velocity, normal)),
                    Scale(tangent, fmax(0.0, 1.0 - env.friction * seconds)));
            }
        }
    }
    local.kineticEnergyJ = 0.5 * state->massKg *
                           Dot(state->velocity, state->velocity);
    if (result) *result = local;
    return CharacterBodyStateValid(state);
}

StructurePhysicsEnvironment StructurePhysicsDefaultEnvironment(void)
{
    return (StructurePhysicsEnvironment){
        .gravityMps2 = 9.80665,
        .windPressurePa = 0.0,
        .loadedAreaM2 = 1.0,
        .damageRatePerSecond = 0.01,
    };
}

bool StructurePhysicsStateValid(const StructurePhysicsState *state)
{
    return state && isfinite(state->massKg) && state->massKg >= 0.0 &&
           isfinite(state->supportCapacityN) && state->supportCapacityN > 0.0 &&
           isfinite(state->stiffnessNPerMeter) &&
           state->stiffnessNPerMeter > 0.0 && isfinite(state->damage) &&
           state->damage >= 0.0 && state->damage <= 1.0 &&
           isfinite(state->displacementMeters) && state->displacementMeters >= 0.0;
}

bool StructurePhysicsStep(StructurePhysicsState *state,
                          const StructurePhysicsEnvironment *environment,
                          double externalLoadN,
                          double seconds,
                          StructurePhysicsStepResult *result)
{
    if (!StructurePhysicsStateValid(state) || !isfinite(externalLoadN) ||
        externalLoadN < 0.0 || !isfinite(seconds) || seconds < 0.0)
        return false;
    StructurePhysicsEnvironment env =
        environment ? *environment : StructurePhysicsDefaultEnvironment();
    env.gravityMps2 = PositiveOr(env.gravityMps2, 9.80665);
    env.windPressurePa = NonNegative(env.windPressurePa);
    env.loadedAreaM2 = PositiveOr(env.loadedAreaM2, 1.0);
    env.damageRatePerSecond = NonNegative(env.damageRatePerSecond);
    StructurePhysicsStepResult local = {0};
    local.totalLoadN = state->massKg * env.gravityMps2 + externalLoadN +
                       env.windPressurePa * env.loadedAreaM2;
    local.capacityN = state->supportCapacityN * (1.0 - state->damage);
    local.overloadRatio = local.totalLoadN / fmax(1e-12, local.capacityN);
    if (!state->collapsed) {
        state->displacementMeters = local.totalLoadN /
                                    state->stiffnessNPerMeter;
        double overload = fmax(0.0, local.overloadRatio - 1.0);
        local.damageAdded = overload * env.damageRatePerSecond * seconds;
        state->damage = fmin(1.0, state->damage + local.damageAdded);
        if (state->damage >= 1.0) state->collapsed = true;
    }
    local.collapsed = state->collapsed;
    return StructurePhysicsStateValid(state) &&
           (!result || ((*result = local), true));
}

SurvivalEnvironment SurvivalDefaultEnvironment(void)
{
    return (SurvivalEnvironment){
        .airTemperatureK = 293.15,
        .relativeHumidity = 0.5,
        .metabolicPowerW = 120.0,
        .exertion = 0.0,
        .waterIntakeLitersPerHour = 0.0,
        .shelter = 0.0,
        .clothingInsulation = 0.7,
        .resting = false,
    };
}

bool SurvivalStateValid(const SurvivalState *state)
{
    return state && isfinite(state->health) && state->health >= 0.0 &&
           state->health <= 1.0 && isfinite(state->hydrationLiters) &&
           state->hydrationLiters >= 0.0 && isfinite(state->energyKJ) &&
           state->energyKJ >= 0.0 && isfinite(state->coreTemperatureK) &&
           isfinite(state->sleepDebtHours) && state->sleepDebtHours >= 0.0 &&
           state->sleepDebtHours <= 240.0;
}

static SurvivalEnvironment SanitizeSurvivalEnvironment(
    const SurvivalEnvironment *input)
{
    SurvivalEnvironment result = SurvivalDefaultEnvironment();
    if (!input) return result;
    result = *input;
    result.airTemperatureK = PositiveOr(result.airTemperatureK, 293.15);
    result.relativeHumidity = Clamp01(result.relativeHumidity);
    result.metabolicPowerW = NonNegative(result.metabolicPowerW);
    result.exertion = Clamp01(result.exertion);
    result.waterIntakeLitersPerHour = NonNegative(result.waterIntakeLitersPerHour);
    result.shelter = Clamp01(result.shelter);
    result.clothingInsulation = Clamp01(result.clothingInsulation);
    return result;
}

bool SurvivalStep(SurvivalState *state,
                  const SurvivalEnvironment *environment,
                  double seconds)
{
    if (!SurvivalStateValid(state) || !isfinite(seconds) || seconds < 0.0)
        return false;
    SurvivalEnvironment env = SanitizeSurvivalEnvironment(environment);
    if (seconds == 0.0) return true;
    double hours = seconds / 3600.0;
    double waterLoss = (2.5 / 24.0) * hours *
                       (1.0 + env.exertion) *
                       (1.0 + 0.5 * env.relativeHumidity) *
                       (1.0 + fmax(0.0, env.airTemperatureK - 295.0) * 0.03);
    state->hydrationLiters += env.waterIntakeLitersPerHour * hours;
    state->hydrationLiters = fmin(10.0, state->hydrationLiters - waterLoss);
    state->hydrationLiters = fmax(0.0, state->hydrationLiters);
    state->energyKJ = fmax(0.0, state->energyKJ -
                            env.metabolicPowerW * seconds / 1000.0);
    double targetTemperature = env.airTemperatureK +
                               (310.15 - env.airTemperatureK) * env.shelter;
    double relaxation = (1.0 - env.clothingInsulation) * 0.0005 * seconds;
    state->coreTemperatureK +=
        (targetTemperature - state->coreTemperatureK) * Clamp01(relaxation);
    if (env.resting)
        state->sleepDebtHours = fmax(0.0, state->sleepDebtHours - hours);
    else
        state->sleepDebtHours = fmin(240.0, state->sleepDebtHours + hours *
                                                     (0.1 + env.exertion));
    double stress = 0.0;
    if (state->hydrationLiters < 1.0) stress += (1.0 - state->hydrationLiters);
    if (state->energyKJ <= 0.0) stress += 0.5;
    if (state->coreTemperatureK < 305.15)
        stress += (305.15 - state->coreTemperatureK) / 20.0;
    if (state->coreTemperatureK > 312.15)
        stress += (state->coreTemperatureK - 312.15) / 20.0;
    stress += state->sleepDebtHours / 240.0 * 0.1;
    state->health = fmax(0.0, state->health - stress * seconds / 86400.0);
    state->alive = state->health > 0.0;
    return SurvivalStateValid(state);
}
