#include "space_system.h"

#include "space_units.h"

#include <math.h>
#include <string.h>

#define SPACE_SYSTEM_DEFAULT_OUTER_GAME 650.0f
#define SPACE_SYSTEM_MIN_SPACING_GAME 38.0f
#define SPACE_SYSTEM_MIN_SPACING_RATIO 1.20f

static uint32_t SpaceSystemMix(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    value ^= value >> 16;
    return value;
}

static float SpaceSystemUnit(uint32_t seed)
{
    return (float)(SpaceSystemMix(seed) >> 8) * (1.0f / 16777216.0f);
}

static float SpaceSystemClamp(float value, float minimum, float maximum)
{
    if (!isfinite(value)) return minimum;
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

static float SpaceSystemSanitizeLimit(float value, float fallback)
{
    return isfinite(value) && value > 0.0f ? value : fallback;
}

static float SpaceSystemStableInnerOrbit(const SpaceSystemFormationInput *input)
{
    float inner = SpaceSystemSanitizeLimit(
        input->innerStabilityLimitGame, SPACE_SYSTEM_MIN_ORBIT_GAME);
    // A single star still needs room for the innermost planet and its proxy.
    return fmaxf(inner, SPACE_SYSTEM_MIN_ORBIT_GAME);
}

static int SpaceSystemPlanetCapacity(float inner, float outer)
{
    int capacity = 1;
    float orbit = inner;
    while (capacity < SPACE_SYSTEM_MAX_PLANETS) {
        float next = fmaxf(orbit + SPACE_SYSTEM_MIN_SPACING_GAME,
                           orbit * SPACE_SYSTEM_MIN_SPACING_RATIO);
        if (next > outer) break;
        orbit = next;
        capacity++;
    }
    return capacity;
}

static float SpaceSystemSnowLineGame(float luminositySolar)
{
    float luminosity = SpaceSystemClamp(luminositySolar, 0.001f, 100000.0f);
    float snowLineAu = SpaceSystemClamp(2.70f * sqrtf(luminosity), 0.55f, 12.0f);
    return snowLineAu * (float)SPACE_UNITS_GAME_DISTANCE_PER_AU;
}

static float SpaceSystemHabitableInnerGame(float luminositySolar)
{
    float luminosity = SpaceSystemClamp(luminositySolar, 0.001f, 100000.0f);
    return SpaceSystemClamp(0.75f * sqrtf(luminosity), 0.18f, 8.0f) *
           (float)SPACE_UNITS_GAME_DISTANCE_PER_AU;
}

static float SpaceSystemHabitableOuterGame(float luminositySolar)
{
    float luminosity = SpaceSystemClamp(luminositySolar, 0.001f, 100000.0f);
    return SpaceSystemClamp(1.70f * sqrtf(luminosity), 0.30f, 16.0f) *
           (float)SPACE_UNITS_GAME_DISTANCE_PER_AU;
}

static float SpaceSystemDiskMassEarth(float stellarMassSolar,
                                      float metallicity, uint32_t seed)
{
    float mass = SpaceSystemClamp(stellarMassSolar, 0.08f, 50.0f);
    float metal = SpaceSystemClamp(metallicity, 0.05f, 1.0f);
    float scatter = 0.55f + SpaceSystemUnit(seed ^ 0x9e3779b9u) * 0.90f;
    return SpaceSystemClamp(60.0f * mass * metal * scatter, 0.08f, 600.0f);
}

static float SpaceSystemOrbitForIndex(float inner, float outer, int count,
                                      int index, uint32_t seed)
{
    if (count <= 1) return inner;
    if (index <= 0) return inner;
    if (index >= count - 1) return outer;
    float fraction = (float)index / (float)(count - 1);
    // A logarithmic disk is closer to formation geometry than fixed linear
    // game-unit spacing. Interior jitter only moves inward so neither stable
    // boundary can be crossed.
    float orbit = expf(logf(inner) + (logf(outer) - logf(inner)) * fraction);
    float jitter = 0.985f + SpaceSystemUnit(seed ^ 0x51ed270bu) * 0.015f;
    return orbit * jitter;
}

bool SpaceSystemFormationGenerate(const SpaceSystemFormationInput *input,
                                  SpaceSystemFormation *out)
{
    if (!input || !out) return false;
    if (!isfinite(input->stellarMassSolar) ||
        !isfinite(input->stellarLuminositySolar) ||
        !isfinite(input->stellarAgeGyr)) {
        return false;
    }

    memset(out, 0, sizeof(*out));
    float stellarMass = SpaceSystemClamp(input->stellarMassSolar, 0.08f, 50.0f);
    float stellarLuminosity = SpaceSystemClamp(
        input->stellarLuminositySolar, 0.001f, 100000.0f);
    float age = SpaceSystemClamp(input->stellarAgeGyr, 0.0f, 20.0f);
    int stellarCount = input->stellarCount < 1 ? 1 :
                       input->stellarCount > 3 ? 3 : input->stellarCount;
    float inner = SpaceSystemStableInnerOrbit(input);
    float outer = SpaceSystemSanitizeLimit(
        input->outerLimitGame, SPACE_SYSTEM_DEFAULT_OUTER_GAME);
    outer = fmaxf(outer, inner + SPACE_SYSTEM_MIN_SPACING_GAME);
    outer = fminf(outer, 2400.0f);

    float ageMetallicityScale = 1.05f - age * 0.0175f;
    float metallicity = SpaceSystemClamp(
        (0.22f + SpaceSystemUnit(input->seed ^ 0xa511e9b3u) * 0.78f) *
        ageMetallicityScale, 0.05f, 1.0f);
    float snowLine = SpaceSystemSnowLineGame(stellarLuminosity);
    float habitableInner = SpaceSystemHabitableInnerGame(stellarLuminosity);
    float habitableOuter = SpaceSystemHabitableOuterGame(stellarLuminosity);
    float diskMass = SpaceSystemDiskMassEarth(
        stellarMass, metallicity, input->seed);
    diskMass = SpaceSystemClamp(
        diskMass * (1.0f - 0.10f * (float)(stellarCount - 1)),
        0.08f, 600.0f);
    int capacity = SpaceSystemPlanetCapacity(inner, outer);
    int massCapacity = (int)floorf(diskMass / 0.08f + 0.0001f);
    unsigned targetRoll = SpaceSystemMix(input->seed ^ 0x68bc21ebu);
    int target = 2 + (int)((targetRoll >> 8) % 4u);
    if (diskMass < 0.45f) target--;
    if (diskMass > 65.0f && target < SPACE_SYSTEM_MAX_PLANETS) target++;
    if (target < 1) target = 1;
    if (target > capacity) target = capacity;
    if (target > massCapacity) target = massCapacity;
    if (target > SPACE_SYSTEM_MAX_PLANETS) target = SPACE_SYSTEM_MAX_PLANETS;

    out->metallicity = metallicity;
    out->diskMassEarth = diskMass;
    out->snowLineGame = snowLine;
    out->habitableInnerGame = habitableInner;
    out->habitableOuterGame = habitableOuter;
    out->innerStableOrbitGame = inner;
    out->outerStableOrbitGame = outer;
    out->planetCount = target;

    float previousOrbit = 0.0f;
    float massRemaining = diskMass;
    for (int index = 0; index < target; index++) {
        uint32_t planetSeed = SpaceSystemMix(
            input->seed ^ (0x9e3779b9u * (uint32_t)(index + 1)));
        float orbit = SpaceSystemOrbitForIndex(
            inner, outer, target, index, planetSeed);
        if (index == 0) {
            orbit = fmaxf(orbit, inner);
        } else {
            float minimumOrbit = fmaxf(
                previousOrbit + SPACE_SYSTEM_MIN_SPACING_GAME,
                previousOrbit * SPACE_SYSTEM_MIN_SPACING_RATIO);
            orbit = fmaxf(orbit, minimumOrbit);
        }
        if (orbit > outer) orbit = outer;
        bool beyondSnowLine = orbit >= snowLine * 0.78f;
        float giantChance = beyondSnowLine
            ? SpaceSystemClamp(0.30f + metallicity * 0.52f +
                               SpaceSystemUnit(planetSeed ^ 0x94d049bbu) * 0.18f,
                               0.0f, 0.96f)
            : 0.0f;
        float reservedMass = 0.08f * (float)(target - index - 1);
        float allocatableMass = fmaxf(0.08f, massRemaining - reservedMass);
        bool gasGiant = beyondSnowLine &&
                        SpaceSystemUnit(planetSeed ^ 0x369dea0fu) < giantChance &&
                        allocatableMass > 12.0f;
        float massEarth;
        float radiusEarth;
        if (gasGiant) {
            massEarth = 10.0f + SpaceSystemUnit(planetSeed ^ 0x27d4eb2fu) *
                        fminf(48.0f, allocatableMass - 10.0f);
            radiusEarth = 2.6f + SpaceSystemUnit(planetSeed ^ 0x165667b1u) * 2.5f;
        } else {
            float maximumMass = fminf(4.0f, allocatableMass);
            float minimumMass = fminf(0.12f, maximumMass);
            massEarth = minimumMass +
                        SpaceSystemUnit(planetSeed ^ 0xcb1ab31fu) *
                        (maximumMass - minimumMass);
            radiusEarth = SpaceSystemClamp(
                powf(massEarth, 0.27f) *
                (0.88f + SpaceSystemUnit(planetSeed ^ 0x7feb352du) * 0.20f),
                0.35f, 1.75f);
        }
        massRemaining -= massEarth;
        out->planets[index] = (SpaceSystemFormationPlanet){
            .orbitGame = orbit,
            .massEarth = massEarth,
            .radiusEarth = radiusEarth,
            .gasGiant = gasGiant
        };
        previousOrbit = orbit;
    }

    // Multiple stars have a larger dynamical cavity; never expose a slot
    // inside the supplied stable boundary after jitter and clamping.
    return true;
}
