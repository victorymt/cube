#include "space_system_physics.h"

#include "raymath.h"
#include "space_units.h"
#include "terrain.h"

#include <float.h>
#include <math.h>
#include <string.h>

static bool SolarSystemPhysicsVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static bool SolarSystemPhysicsStellarProfileIsValid(
    const StellarProfile *profile)
{
    if (!profile) return false;
    bool remnant = profile->stage >= STELLAR_STAGE_WHITE_DWARF;
    bool ageMatchesStage = remnant
        ? profile->ageGyr >= profile->luminousLifetimeGyr
        : profile->ageGyr <= profile->luminousLifetimeGyr;
    return
           profile->spectrum >= SPECTRUM_RED_DWARF &&
           profile->spectrum <= SPECTRUM_BLACK_HOLE &&
           profile->stage >= STELLAR_STAGE_MAIN_SEQUENCE &&
           profile->stage <= STELLAR_STAGE_BLACK_HOLE &&
           profile->initialMassSolar > 0.0f &&
           isfinite(profile->initialMassSolar) && profile->massKg > 0.0 &&
           isfinite(profile->massKg) && profile->radiusKm > 0.0 &&
           isfinite(profile->radiusKm) && profile->massSolar > 0.0f &&
           isfinite(profile->massSolar) && profile->radiusSolar > 0.0f &&
           isfinite(profile->radiusSolar) && profile->temperatureK > 0.0f &&
           isfinite(profile->temperatureK) &&
           profile->luminositySolar > 0.0f &&
           isfinite(profile->luminositySolar) && profile->ageGyr >= 0.0f &&
           isfinite(profile->ageGyr) &&
           profile->mainSequenceLifetimeGyr > 0.0f &&
           isfinite(profile->mainSequenceLifetimeGyr) &&
           profile->luminousLifetimeGyr >=
               profile->mainSequenceLifetimeGyr &&
           isfinite(profile->luminousLifetimeGyr) &&
           ageMatchesStage;
}

static bool SolarSystemPhysicsStellarProfileEquals(
    const StellarProfile *left, const StellarProfile *right)
{
    return left && right && left->spectrum == right->spectrum &&
           left->stage == right->stage &&
           left->initialMassSolar == right->initialMassSolar &&
           left->massKg == right->massKg &&
           left->radiusKm == right->radiusKm &&
           left->massSolar == right->massSolar &&
           left->radiusSolar == right->radiusSolar &&
           left->temperatureK == right->temperatureK &&
           left->luminositySolar == right->luminositySolar &&
           left->ageGyr == right->ageGyr &&
           left->mainSequenceLifetimeGyr ==
               right->mainSequenceLifetimeGyr &&
           left->luminousLifetimeGyr == right->luminousLifetimeGyr;
}

static bool SolarSystemPhysicsSatelliteOrbitIsValid(
    const SpaceSatelliteOrbit *orbit)
{
    if (!orbit) return false;
    if (!orbit->exists) return true;
    return orbit->semiMajorAxisKm > 0.0 &&
           isfinite(orbit->semiMajorAxisKm) && orbit->eccentricity >= 0.0 &&
           orbit->eccentricity < 1.0 && isfinite(orbit->eccentricity) &&
           isfinite(orbit->inclinationRad) &&
           isfinite(orbit->longitudeAscendingNodeRad) &&
           isfinite(orbit->argumentPeriapsisRad) &&
           isfinite(orbit->meanAnomalyAtEpochRad) && orbit->radiusKm > 0.0 &&
           isfinite(orbit->radiusKm) && orbit->massKg > 0.0 &&
           isfinite(orbit->massKg);
}

bool SolarSystemPlanetDefinitionIsValid(const SolarPlanetDef *planet)
{
    return planet && planet->semiMajorAxisKm > 0.0 &&
           isfinite(planet->semiMajorAxisKm) && planet->physicalRadiusKm > 0.0 &&
           isfinite(planet->physicalRadiusKm) &&
           planet->formationMassEarth >= 0.0f &&
           isfinite(planet->formationMassEarth) &&
           planet->spaceProxyRadius > 0.0f &&
           isfinite(planet->spaceProxyRadius);
}

static uint32_t SolarLightHash(const SolarSystemDef *sys)
{
    return WorldHash2DBits((uint32_t)sys->anchorX * 113u + 41u,
                           (uint32_t)sys->anchorZ * 71u + 19u);
}

uint32_t SolarSystemPlanetOrbitHash(const SolarSystemDef *sys, int index)
{
    if (!sys) return 0u;
    return WorldHash2DBits(
        (uint32_t)sys->anchorX * 53u + (uint32_t)index * 7u + 1u,
        (uint32_t)sys->anchorZ * 29u + (uint32_t)index * 3u + 2u);
}

uint32_t SolarSystemPlanetPlaneHash(const SolarSystemDef *sys)
{
    if (!sys) return 0u;
    return WorldHash2DBits((uint32_t)sys->anchorX * 79u + 11u,
                           (uint32_t)sys->anchorZ * 97u + 23u);
}

int SolarSystemStellarVisualRadius(const StellarProfile *star)
{
    if (!star || star->radiusKm <= 0.0) return 9;
    if (star->stage == STELLAR_STAGE_WHITE_DWARF) return 7;
    if (star->stage == STELLAR_STAGE_NEUTRON_STAR) return 5;
    if (star->stage == STELLAR_STAGE_BLACK_HOLE) {
        return (int)roundf(Clamp(5.0f + 0.18f * star->massSolar,
                                 6.0f, 9.0f));
    }
    float radiusSolar = (float)(star->radiusKm /
                                SPACE_UNITS_SOLAR_RADIUS_KM);
    float radius = 13.0f + 2.5f * log2f(radiusSolar);
    float maximum = star->stage == STELLAR_STAGE_RED_GIANT ? 28.0f : 21.0f;
    return (int)roundf(Clamp(radius, 7.0f, maximum));
}

static StellarProfile SolarCompanionProfile(const SolarSystemDef *system,
                                            uint32_t seed, float minimumRatio,
                                            float maximumRatio)
{
    float unit = (float)(seed & 0xffffu) / 65535.0f;
    float ratio = Lerp(minimumRatio, maximumRatio, unit);
    float primaryInitialMass = fmaxf(system->star.initialMassSolar, 0.08f);
    float maximumMass = fmaxf(primaryInitialMass * 0.96f, 0.08f);
    float companionMass = Clamp(primaryInitialMass * ratio, 0.08f, maximumMass);
    StellarProfile companion = { 0 };
    if (!StellarProfileAtAge(companionMass, system->star.ageGyr, seed,
                             &companion) ||
        companion.stage > STELLAR_STAGE_RED_GIANT) {
        StellarProfileAtAge(0.08f, system->star.ageGyr, seed, &companion);
    }
    return companion;
}

static int SolarSystemStellarCount(const SolarSystemDef *sys,
                                   uint32_t *outHash)
{
    if (!sys) return 0;
    uint32_t hash = SolarLightHash(sys);
    if (outHash) *outHash = hash;
    float primaryMass = fmaxf(sys->star.massSolar, 0.08f);
    unsigned int multipleRoll = hash % 1000u;
    unsigned int binaryThreshold = primaryMass < 0.60f ? 250u :
                                   (primaryMass < 1.40f ? 440u : 680u);
    unsigned int tripleThreshold = primaryMass < 0.60f ? 30u :
                                   (primaryMass < 1.40f ? 80u : 160u);
    int count = multipleRoll < tripleThreshold ? 3 :
                (multipleRoll < binaryThreshold ? 2 : 1);
    if (sys->anchorX == 0 && sys->anchorZ == 0) count = 1;
    return count;
}

static int SolarSystemStellarProfiles(const SolarSystemDef *sys,
                                      StellarProfile *out, int maxCount,
                                      uint32_t *outHash)
{
    if (!sys || !out || maxCount <= 0) return 0;
    int count = SolarSystemStellarCount(sys, outHash);
    if (count > maxCount) count = maxCount;
    out[0] = sys->star;
    if (count == 1) return count;

    uint32_t hash = outHash ? *outHash : SolarLightHash(sys);
    out[1] = SolarCompanionProfile(sys, hash ^ 0x94d049bbu, 0.18f, 0.92f);
    if (count == 2) return count;
    out[2] = SolarCompanionProfile(sys, hash ^ 0x369dea0fu, 0.10f, 0.62f);
    return count;
}

static SpaceBarycenterOrbit SolarSystemStellarOrbit(
    const StellarProfile *profiles, int count, uint32_t hash)
{
    SpaceBarycenterOrbit orbit = { .bodyCount = count };
    for (int i = 0; i < count; i++) orbit.massKg[i] = profiles[i].massKg;
    if (count <= 1) return orbit;

    float innerSeparationGame = count == 3 ?
        30.0f + (float)((hash >> 8) % 13u) :
        34.0f + (float)((hash >> 8) % 25u);
    orbit.innerSeparationKm = SpaceUnitsGameDistanceToKilometers(
        innerSeparationGame);
    orbit.innerPhaseRad = (double)(hash % 6283u) / 1000.0;
    orbit.innerInclinationRad =
        ((double)((hash >> 16) % 17u) - 8.0) * 0.004;
    orbit.innerNodeRad = (double)((hash >> 4) % 6283u) / 1000.0;
    if (count == 3) {
        float outerRatio = 3.6f + (float)((hash >> 20) % 7u) * 0.1f;
        orbit.outerSeparationKm = SpaceUnitsGameDistanceToKilometers(
            innerSeparationGame * outerRatio);
        orbit.outerPhaseRad = (double)((hash >> 5) % 6283u) / 1000.0;
        orbit.outerInclinationRad =
            ((double)((hash >> 25) % 15u) - 7.0) * 0.007;
        orbit.outerNodeRad = (double)((hash >> 11) % 6283u) / 1000.0;
    }
    return orbit;
}

static bool SolarSystemPlanetOrbitBuild(
    const SolarSystemDef *sys, int index, double centralMassKg,
    double semiMajorAxisScale,
    SpaceKeplerOrbit *out)
{
    if (!out) return false;
    *out = (SpaceKeplerOrbit){ 0 };
    if (!sys || index < 0 || index >= sys->planetCount ||
        index >= MAX_SOLAR_PLANETS || !(centralMassKg > 0.0) ||
        !isfinite(centralMassKg) || !(semiMajorAxisScale > 0.0) ||
        !isfinite(semiMajorAxisScale)) {
        return false;
    }
    const SolarPlanetDef *planet = &sys->planets[index];
    uint32_t orbitHash = SolarSystemPlanetOrbitHash(sys, index);
    uint32_t planeHash = SolarSystemPlanetPlaneHash(sys);
    double systemInclination =
        ((double)((planeHash >> 6) % 25u) - 12.0) * 0.0055;
    double planetInclination =
        ((double)((orbitHash >> 22) % 9u) - 4.0) * 0.0020;
    double inclination = fmax(-0.074, fmin(0.074,
                                           systemInclination +
                                           planetInclination));
    double systemNode = (double)((planeHash >> 13) % 6283u) / 1000.0;
    double nodeOffset =
        ((double)((orbitHash >> 7) % 17u) - 8.0) * 0.005;
    double eccentricity =
        0.015 + (double)((orbitHash >> 17) % 180u) / 1000.0;
    if (sys->anchorX == 0 && sys->anchorZ == 0) {
        static const double solEccentricities[MAX_SOLAR_PLANETS] = {
            0.08, 0.04, 0.02, 0.11, 0.15, 0.06
        };
        eccentricity = solEccentricities[index];
    }
    double semiMajorAxisKm = planet->semiMajorAxisKm * semiMajorAxisScale;
    double canonicalSemiMajorAxisGame = SpaceUnitsKilometersToGameDistance(
        planet->semiMajorAxisKm);
    double hostCellLimit =
        694.0 / fmax(canonicalSemiMajorAxisGame, 1.0) - 1.0;
    eccentricity = fmax(0.0, fmin(eccentricity,
        fmin(0.05, fmax(hostCellLimit, 0.0))));

    SpaceKeplerOrbit orbit = {
        .semiMajorAxisKm = semiMajorAxisKm,
        .centralMassKg = centralMassKg,
        .eccentricity = eccentricity,
        .inclinationRad = inclination,
        .longitudeAscendingNodeRad = systemNode + nodeOffset,
        .argumentPeriapsisRad =
            (double)((orbitHash >> 3) % 6283u) / 1000.0,
        .meanAnomalyAtEpochRad = (double)(orbitHash % 6283u) / 1000.0
    };
    if (!SpaceKeplerOrbitIsValid(&orbit)) return false;
    *out = orbit;
    return true;
}

typedef struct SolarSystemMassLossEvent {
    double ageGyr;
    double beforeMassKg;
    double afterMassKg;
    uint32_t starMask;
} SolarSystemMassLossEvent;

static bool SolarSystemStellarProfilesAtAge(
    const SolarSystemPhysicalSnapshot *base, double ageGyr,
    StellarProfile *out)
{
    if (!base || !out || !isfinite(ageGyr) || ageGyr < 0.0) return false;
    for (int i = 0; i < base->summary.stellarCount; i++) {
        const StellarProfile *initial = &base->stellarProfiles[i];
        if (!StellarProfileAtAge(initial->initialMassSolar, ageGyr,
                                 initial->evolutionSeed, &out[i]) ||
            !SolarSystemPhysicsStellarProfileIsValid(&out[i])) {
            return false;
        }
    }
    return true;
}

static bool SolarSystemTotalStellarMassAtAge(
    const SolarSystemPhysicalSnapshot *base, double ageGyr,
    double *outMassKg)
{
    if (!base || !outMassKg || !isfinite(ageGyr) || ageGyr < 0.0) {
        return false;
    }
    StellarProfile profiles[MAX_SOLAR_LIGHTS];
    if (!SolarSystemStellarProfilesAtAge(base, ageGyr, profiles)) return false;
    double totalMassKg = 0.0;
    for (int i = 0; i < base->summary.stellarCount; i++) {
        totalMassKg += profiles[i].massKg;
    }
    if (!(totalMassKg > 0.0) || !isfinite(totalMassKg)) return false;
    *outMassKg = totalMassKg;
    return true;
}

static int SolarSystemMassLossEvents(
    const SolarSystemPhysicalSnapshot *base, double requestedAgeGyr,
    SolarSystemMassLossEvent *out, int maxCount)
{
    if (!base || !out || maxCount < base->summary.stellarCount ||
        !isfinite(requestedAgeGyr)) {
        return -1;
    }
    int count = 0;
    double baseAgeGyr = (double)base->summary.ageGyr;
    for (int star = 0; star < base->summary.stellarCount; star++) {
        const StellarProfile *profile = &base->stellarProfiles[star];
        double eventAgeGyr = (double)profile->luminousLifetimeGyr;
        // Core collapse is impulsive; winds and low-mass envelope loss remain
        // part of the adiabatic mass evolution between these events.
        if (profile->initialMassSolar < 8.0f ||
            profile->stage >= STELLAR_STAGE_NEUTRON_STAR ||
            eventAgeGyr < baseAgeGyr || eventAgeGyr >= requestedAgeGyr) {
            continue;
        }
        out[count++] = (SolarSystemMassLossEvent){
            .ageGyr = eventAgeGyr,
            .starMask = UINT32_C(1) << star
        };
    }
    for (int i = 1; i < count; i++) {
        SolarSystemMassLossEvent event = out[i];
        int destination = i;
        while (destination > 0 &&
               out[destination - 1].ageGyr > event.ageGyr) {
            out[destination] = out[destination - 1];
            destination--;
        }
        out[destination] = event;
    }
    int uniqueCount = 0;
    for (int i = 0; i < count; i++) {
        if (uniqueCount > 0 &&
            out[uniqueCount - 1].ageGyr == out[i].ageGyr) {
            out[uniqueCount - 1].starMask |= out[i].starMask;
            continue;
        }
        SolarSystemMassLossEvent event = out[i];
        if (!SolarSystemTotalStellarMassAtAge(
                base, event.ageGyr, &event.beforeMassKg) ||
            !SolarSystemTotalStellarMassAtAge(
                base, nextafter(event.ageGyr, INFINITY),
                &event.afterMassKg) ||
            !(event.afterMassKg < event.beforeMassKg)) {
            return -1;
        }
        out[uniqueCount++] = event;
    }
    return uniqueCount;
}

static uint32_t SolarSystemPhysicsMix32(uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352du;
    value ^= value >> 15;
    value *= 0x846ca68bu;
    return value ^ (value >> 16);
}

static double SolarSystemPhysicsHashUnit(uint32_t value)
{
    return (double)(SolarSystemPhysicsMix32(value) >> 8) /
           16777215.0;
}

static Vector3 SolarSystemNatalKickGame(const StellarProfile *remnant,
                                        uint32_t salt)
{
    if (!remnant || (remnant->stage != STELLAR_STAGE_NEUTRON_STAR &&
                     remnant->stage != STELLAR_STAGE_BLACK_HOLE)) {
        return (Vector3){ 0 };
    }
    double speedKmPerSecond = remnant->stage == STELLAR_STAGE_NEUTRON_STAR
        ? 120.0 + 360.0 * SolarSystemPhysicsHashUnit(
                              remnant->evolutionSeed ^ salt ^ 0x51ed270bu)
        : 20.0 + 100.0 * SolarSystemPhysicsHashUnit(
                             remnant->evolutionSeed ^ salt ^ 0x94d049bbu);
    double cosinePolar = 2.0 * SolarSystemPhysicsHashUnit(
        remnant->evolutionSeed ^ salt ^ 0xa511e9b3u) - 1.0;
    double azimuth = 6.28318530717958647692 *
        SolarSystemPhysicsHashUnit(
            remnant->evolutionSeed ^ salt ^ 0x6d2b79f5u);
    double horizontal = sqrt(fmax(0.0, 1.0 - cosinePolar * cosinePolar));
    double speedGame = SpaceUnitsKilometersPerSecondToGameVelocity(
        speedKmPerSecond);
    return (Vector3){
        (float)(speedGame * horizontal * cos(azimuth)),
        (float)(speedGame * cosinePolar),
        (float)(speedGame * horizontal * sin(azimuth))
    };
}

static Vector3 SolarSystemPhysicsSubtract(Vector3 left, Vector3 right)
{
    return (Vector3){ left.x - right.x, left.y - right.y, left.z - right.z };
}

static Vector3 SolarSystemPhysicsMassCenter(
    const SpaceBarycenterBodyState *states, const double *masses,
    int first, int count, bool velocity)
{
    double totalMass = 0.0;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    for (int i = first; i < first + count; i++) {
        Vector3 value = velocity ? states[i].velocityGame :
                                   states[i].offsetGame;
        totalMass += masses[i];
        x += masses[i] * value.x;
        y += masses[i] * value.y;
        z += masses[i] * value.z;
    }
    if (!(totalMass > 0.0) || !isfinite(totalMass)) {
        return (Vector3){ NAN, NAN, NAN };
    }
    return (Vector3){ (float)(x / totalMass), (float)(y / totalMass),
                      (float)(z / totalMass) };
}

static bool SolarSystemPhysicsRecenterStates(
    SpaceBarycenterBodyState *states, const double *masses, int count)
{
    Vector3 center = SolarSystemPhysicsMassCenter(
        states, masses, 0, count, false);
    Vector3 velocity = SolarSystemPhysicsMassCenter(
        states, masses, 0, count, true);
    if (!SolarSystemPhysicsVectorIsFinite(center) ||
        !SolarSystemPhysicsVectorIsFinite(velocity)) {
        return false;
    }
    for (int i = 0; i < count; i++) {
        states[i].offsetGame = SolarSystemPhysicsSubtract(
            states[i].offsetGame, center);
        states[i].velocityGame = SolarSystemPhysicsSubtract(
            states[i].velocityGame, velocity);
    }
    return true;
}

static SpaceKeplerState SolarSystemPhysicsRelativeState(
    const SpaceBarycenterBodyState *left,
    const SpaceBarycenterBodyState *right)
{
    return (SpaceKeplerState){
        .positionGame = SolarSystemPhysicsSubtract(
            right->offsetGame, left->offsetGame),
        .velocityGame = SolarSystemPhysicsSubtract(
            right->velocityGame, left->velocityGame)
    };
}

static void SolarSystemStellarOrbitSetInner(
    SpaceBarycenterOrbit *orbit, const SpaceKeplerOrbit *inner)
{
    orbit->innerSeparationKm = inner->semiMajorAxisKm;
    orbit->innerEccentricity = inner->eccentricity;
    orbit->innerPhaseRad = inner->meanAnomalyAtEpochRad;
    orbit->innerArgumentPeriapsisRad = inner->argumentPeriapsisRad;
    orbit->innerInclinationRad = inner->inclinationRad;
    orbit->innerNodeRad = inner->longitudeAscendingNodeRad;
}

static void SolarSystemStellarOrbitSetOuter(
    SpaceBarycenterOrbit *orbit, const SpaceKeplerOrbit *outer)
{
    orbit->outerSeparationKm = outer->semiMajorAxisKm;
    orbit->outerEccentricity = outer->eccentricity;
    orbit->outerPhaseRad = outer->meanAnomalyAtEpochRad;
    orbit->outerArgumentPeriapsisRad = outer->argumentPeriapsisRad;
    orbit->outerInclinationRad = outer->inclinationRad;
    orbit->outerNodeRad = outer->longitudeAscendingNodeRad;
}

static void SolarSystemStellarOrbitStoreFreeFlight(
    SpaceBarycenterOrbit *orbit,
    const SpaceBarycenterBodyState *states, int count)
{
    orbit->motion = SPACE_BARYCENTER_FREE_FLIGHT;
    for (int i = 0; i < count; i++) {
        orbit->freeFlightOffsetGame[i] = states[i].offsetGame;
        orbit->freeFlightVelocityGame[i] = states[i].velocityGame;
    }
}

static bool SolarSystemStellarOrbitStoreOuterFreeFlight(
    SpaceBarycenterOrbit *orbit,
    const SpaceBarycenterBodyState *states, const double *masses)
{
    Vector3 innerCenter = SolarSystemPhysicsMassCenter(
        states, masses, 0, 2, false);
    Vector3 innerVelocity = SolarSystemPhysicsMassCenter(
        states, masses, 0, 2, true);
    if (!SolarSystemPhysicsVectorIsFinite(innerCenter) ||
        !SolarSystemPhysicsVectorIsFinite(innerVelocity)) {
        return false;
    }
    orbit->motion = SPACE_BARYCENTER_OUTER_FREE_FLIGHT;
    orbit->outerFreeOffsetGame = SolarSystemPhysicsSubtract(
        states[2].offsetGame, innerCenter);
    orbit->outerFreeVelocityGame = SolarSystemPhysicsSubtract(
        states[2].velocityGame, innerVelocity);
    return SolarSystemPhysicsVectorIsFinite(orbit->outerFreeOffsetGame) &&
           SolarSystemPhysicsVectorIsFinite(orbit->outerFreeVelocityGame);
}

static bool SolarSystemStellarOrbitApplyMasses(
    SpaceBarycenterOrbit *orbit, const double *newMasses, int count)
{
    if (!orbit || !newMasses || orbit->bodyCount != count) return false;
    double oldInnerMass = count > 1
        ? orbit->massKg[0] + orbit->massKg[1] : 0.0;
    double newInnerMass = count > 1
        ? newMasses[0] + newMasses[1] : 0.0;
    double oldTotalMass = 0.0;
    double newTotalMass = 0.0;
    for (int i = 0; i < count; i++) {
        if (!(newMasses[i] > 0.0) || !isfinite(newMasses[i])) return false;
        oldTotalMass += orbit->massKg[i];
        newTotalMass += newMasses[i];
    }
    if (count > 1 && orbit->motion != SPACE_BARYCENTER_FREE_FLIGHT) {
        orbit->innerSeparationKm *= oldInnerMass / newInnerMass;
    }
    if (count == 3 && orbit->motion == SPACE_BARYCENTER_BOUND) {
        orbit->outerSeparationKm *= oldTotalMass / newTotalMass;
    }
    if (orbit->motion == SPACE_BARYCENTER_FREE_FLIGHT) {
        SpaceBarycenterBodyState states[MAX_SOLAR_LIGHTS] = { 0 };
        for (int i = 0; i < count; i++) {
            states[i].offsetGame = orbit->freeFlightOffsetGame[i];
            states[i].velocityGame = orbit->freeFlightVelocityGame[i];
        }
        if (!SolarSystemPhysicsRecenterStates(states, newMasses, count)) {
            return false;
        }
        for (int i = 0; i < count; i++) {
            orbit->freeFlightOffsetGame[i] = states[i].offsetGame;
            orbit->freeFlightVelocityGame[i] = states[i].velocityGame;
        }
    }
    for (int i = 0; i < count; i++) orbit->massKg[i] = newMasses[i];
    return true;
}

static bool SolarSystemStellarOrbitApplyEvent(
    SpaceBarycenterOrbit *orbit,
    const StellarProfile *beforeProfiles,
    const StellarProfile *afterProfiles,
    uint32_t starMask, uint32_t eventSalt, int count)
{
    double beforeMasses[MAX_SOLAR_LIGHTS] = { 0 };
    double afterMasses[MAX_SOLAR_LIGHTS] = { 0 };
    for (int i = 0; i < count; i++) {
        beforeMasses[i] = beforeProfiles[i].massKg;
        afterMasses[i] = afterProfiles[i].massKg;
    }
    if (!SolarSystemStellarOrbitApplyMasses(orbit, beforeMasses, count)) {
        return false;
    }
    if (count == 1) {
        orbit->massKg[0] = afterMasses[0];
        return true;
    }

    uint32_t phaseHash = eventSalt ^ starMask;
    for (int i = 0; i < count; i++) {
        phaseHash ^= SolarSystemPhysicsMix32(beforeProfiles[i].evolutionSeed +
                                             (uint32_t)i);
    }
    // Event phases use a bounded gameplay clock. Advancing free-flight
    // remnants by stellar lifetimes would exceed the compressed world scale.
    double eventClock = 32.0 * SolarSystemPhysicsHashUnit(phaseHash);
    SpaceBarycenterBodyState states[MAX_SOLAR_LIGHTS];
    if (SpaceBarycenterSolve(orbit, eventClock, states,
                             MAX_SOLAR_LIGHTS) != count) {
        return false;
    }
    for (int star = 0; star < count; star++) {
        if ((starMask & (UINT32_C(1) << star)) == 0u) continue;
        Vector3 kick = SolarSystemNatalKickGame(
            &afterProfiles[star], eventSalt ^ (uint32_t)star);
        states[star].velocityGame.x += kick.x;
        states[star].velocityGame.y += kick.y;
        states[star].velocityGame.z += kick.z;
    }
    if (!SolarSystemPhysicsRecenterStates(states, afterMasses, count)) {
        return false;
    }
    for (int i = 0; i < count; i++) orbit->massKg[i] = afterMasses[i];

    if (orbit->motion == SPACE_BARYCENTER_FREE_FLIGHT) {
        SolarSystemStellarOrbitStoreFreeFlight(orbit, states, count);
        return true;
    }

    SpaceKeplerState innerState = SolarSystemPhysicsRelativeState(
        &states[0], &states[1]);
    SpaceKeplerOrbit innerOrbit;
    if (!SpaceKeplerOrbitFromState(
            &innerState, afterMasses[0] + afterMasses[1], &innerOrbit)) {
        SolarSystemStellarOrbitStoreFreeFlight(orbit, states, count);
        return true;
    }
    SolarSystemStellarOrbitSetInner(orbit, &innerOrbit);
    if (count == 2) {
        orbit->motion = SPACE_BARYCENTER_BOUND;
        return true;
    }
    if (orbit->motion == SPACE_BARYCENTER_OUTER_FREE_FLIGHT) {
        return SolarSystemStellarOrbitStoreOuterFreeFlight(
            orbit, states, afterMasses);
    }

    Vector3 innerCenter = SolarSystemPhysicsMassCenter(
        states, afterMasses, 0, 2, false);
    Vector3 innerVelocity = SolarSystemPhysicsMassCenter(
        states, afterMasses, 0, 2, true);
    SpaceBarycenterBodyState innerBody = {
        .offsetGame = innerCenter,
        .velocityGame = innerVelocity
    };
    SpaceKeplerState outerState = SolarSystemPhysicsRelativeState(
        &innerBody, &states[2]);
    SpaceKeplerOrbit outerOrbit;
    if (!SpaceKeplerOrbitFromState(
            &outerState, afterMasses[0] + afterMasses[1] + afterMasses[2],
            &outerOrbit)) {
        return SolarSystemStellarOrbitStoreOuterFreeFlight(
            orbit, states, afterMasses);
    }
    SolarSystemStellarOrbitSetOuter(orbit, &outerOrbit);
    orbit->motion = SPACE_BARYCENTER_BOUND;
    return true;
}

static bool SolarSystemStellarOrbitEvolve(
    const SolarSystemPhysicalSnapshot *base,
    const StellarProfile *finalProfiles,
    const SolarSystemMassLossEvent *events, int eventCount,
    SpaceBarycenterOrbit *out)
{
    if (!base || !finalProfiles || !events || !out || eventCount < 0 ||
        eventCount > MAX_SOLAR_LIGHTS) {
        return false;
    }
    int count = base->summary.stellarCount;
    if (count < 1 || count > MAX_SOLAR_LIGHTS ||
        base->stellarOrbit.bodyCount != count) {
        return false;
    }
    SpaceBarycenterOrbit evolved = base->stellarOrbit;
    for (int event = 0; event < eventCount; event++) {
        StellarProfile beforeProfiles[MAX_SOLAR_LIGHTS];
        StellarProfile afterProfiles[MAX_SOLAR_LIGHTS];
        if (!SolarSystemStellarProfilesAtAge(
                base, events[event].ageGyr, beforeProfiles) ||
            !SolarSystemStellarProfilesAtAge(
                base, nextafter(events[event].ageGyr, INFINITY),
                afterProfiles) ||
            !SolarSystemStellarOrbitApplyEvent(
                &evolved, beforeProfiles, afterProfiles,
                events[event].starMask,
                SolarSystemPhysicsMix32((uint32_t)event ^
                                        base->stellarHash), count)) {
            return false;
        }
    }
    double finalMasses[MAX_SOLAR_LIGHTS] = { 0 };
    for (int i = 0; i < count; i++) finalMasses[i] = finalProfiles[i].massKg;
    if (!SolarSystemStellarOrbitApplyMasses(
            &evolved, finalMasses, count)) {
        return false;
    }
    SpaceBarycenterBodyState states[MAX_SOLAR_LIGHTS];
    if (SpaceBarycenterSolve(&evolved, 0.0, states,
                             MAX_SOLAR_LIGHTS) != count) {
        return false;
    }
    *out = evolved;
    return true;
}

static bool SolarSystemMaximumGiantReachKm(
    const SolarSystemPhysicalSnapshot *base, double requestedAgeGyr,
    double *outReachKm)
{
    if (!base || !outReachKm || !isfinite(requestedAgeGyr)) return false;
    double excursionKm = 0.0;
    // Circumbinary planets must clear both the giant envelope and the star's
    // conservative maximum displacement from the system barycenter.
    if (base->summary.stellarCount == 2) {
        excursionKm = base->stellarOrbit.innerSeparationKm * 1.2;
    } else if (base->summary.stellarCount == 3) {
        excursionKm = (base->stellarOrbit.innerSeparationKm +
                       base->stellarOrbit.outerSeparationKm) * 1.2;
    }
    if (excursionKm < 0.0 || !isfinite(excursionKm)) return false;

    double reachKm = 0.0;
    double baseAgeGyr = (double)base->summary.ageGyr;
    for (int star = 0; star < base->summary.stellarCount; star++) {
        const StellarProfile *initial = &base->stellarProfiles[star];
        double giantStartGyr = (double)initial->mainSequenceLifetimeGyr;
        double giantEndGyr = (double)initial->luminousLifetimeGyr;
        if (requestedAgeGyr <= giantStartGyr || baseAgeGyr > giantEndGyr) {
            continue;
        }
        double terminalAgeGyr = fmin(requestedAgeGyr, giantEndGyr);
        if (terminalAgeGyr < baseAgeGyr) continue;
        StellarProfile giant;
        if (!StellarProfileAtAge(initial->initialMassSolar,
                                 terminalAgeGyr, initial->evolutionSeed,
                                 &giant) ||
            !SolarSystemPhysicsStellarProfileIsValid(&giant)) {
            return false;
        }
        reachKm = fmax(reachKm, giant.radiusKm + excursionKm);
    }
    if (reachKm < 0.0 || !isfinite(reachKm)) return false;
    *outReachKm = reachKm;
    return true;
}

static bool SolarSystemPlanetOrbitEvolve(
    const SolarSystemDef *sys, int index,
    const SolarSystemPhysicalSnapshot *base, double finalMassKg,
    double giantReachKm, const SolarSystemMassLossEvent *events,
    int eventCount, bool stellarSystemDisrupted,
    SolarPlanetDynamicalStatus *outStatus,
    SpaceKeplerOrbit *outOrbit)
{
    if (!sys || !base || !events || !outStatus || !outOrbit || index < 0 ||
        index >= sys->planetCount || !(finalMassKg > 0.0) ||
        !isfinite(finalMassKg) || giantReachKm < 0.0 ||
        !isfinite(giantReachKm) || eventCount < 0 ||
        eventCount > MAX_SOLAR_LIGHTS) {
        return false;
    }
    *outStatus = SOLAR_PLANET_STABLE;
    *outOrbit = (SpaceKeplerOrbit){ 0 };
    const SpaceKeplerOrbit *initial = &base->planetOrbits[index];
    double periapsisKm = initial->semiMajorAxisKm *
                         (1.0 - initial->eccentricity);
    if (periapsisKm <= giantReachKm +
                        sys->planets[index].physicalRadiusKm) {
        *outStatus = SOLAR_PLANET_ENGULFED;
        return true;
    }
    if (stellarSystemDisrupted) {
        *outStatus = SOLAR_PLANET_EJECTED;
        return true;
    }

    double semiMajorAxisKm = initial->semiMajorAxisKm;
    double eccentricity = initial->eccentricity;
    double previousMassKg = base->summary.totalMassKg;
    for (int event = 0; event < eventCount; event++) {
        double beforeMassKg = events[event].beforeMassKg;
        double afterMassKg = events[event].afterMassKg;
        if (!(beforeMassKg > 0.0) || !(afterMassKg > 0.0) ||
            !isfinite(beforeMassKg) || !isfinite(afterMassKg) ||
            !(afterMassKg < beforeMassKg)) {
            return false;
        }
        semiMajorAxisKm *= previousMassKg / beforeMassKg;
        if (2.0 * afterMassKg <= beforeMassKg) {
            *outStatus = SOLAR_PLANET_EJECTED;
            return true;
        }
        semiMajorAxisKm *= afterMassKg /
                           (2.0 * afterMassKg - beforeMassKg);
        // Generated planets are nearly circular, so use the deterministic
        // phase-independent eccentricity response to instantaneous mass loss.
        double impulseEccentricity =
            (beforeMassKg - afterMassKg) / afterMassKg;
        eccentricity += impulseEccentricity * (1.0 - eccentricity);
        if (!(semiMajorAxisKm > 0.0) || !isfinite(semiMajorAxisKm) ||
            eccentricity >= 1.0 || !isfinite(eccentricity)) {
            *outStatus = SOLAR_PLANET_EJECTED;
            return true;
        }
        previousMassKg = afterMassKg;
    }
    semiMajorAxisKm *= previousMassKg / finalMassKg;
    SpaceKeplerOrbit evolved = *initial;
    evolved.semiMajorAxisKm = semiMajorAxisKm;
    evolved.centralMassKg = finalMassKg;
    evolved.eccentricity = eccentricity;
    if (!SpaceKeplerOrbitIsValid(&evolved)) {
        *outStatus = SOLAR_PLANET_EJECTED;
        return true;
    }
    *outOrbit = evolved;
    return true;
}

bool SolarSystemPhysicalSnapshotBuild(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sys || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }

    SolarSystemPhysicalSnapshot snapshot = { 0 };

    uint32_t hash = 0;
    int count = SolarSystemStellarProfiles(
        sys, snapshot.stellarProfiles, MAX_SOLAR_LIGHTS, &hash);
    if (count <= 0 || count > MAX_SOLAR_LIGHTS) return false;

    snapshot.stellarHash = hash;
    snapshot.summary.stellarCount = count;
    snapshot.summary.ageGyr = sys->star.ageGyr;
    for (int i = 0; i < count; i++) {
        if (!SolarSystemPhysicsStellarProfileIsValid(
                &snapshot.stellarProfiles[i])) {
            return false;
        }
        snapshot.summary.totalMassKg += snapshot.stellarProfiles[i].massKg;
        snapshot.summary.totalLuminositySolar +=
            snapshot.stellarProfiles[i].luminositySolar;
        snapshot.summary.stellarLuminositiesSolar[i] =
            snapshot.stellarProfiles[i].luminositySolar;
    }
    if (!(snapshot.summary.totalMassKg > 0.0) ||
        !isfinite(snapshot.summary.totalMassKg) ||
        !(snapshot.summary.totalLuminositySolar > 0.0f) ||
        !isfinite(snapshot.summary.totalLuminositySolar) ||
        snapshot.summary.ageGyr < 0.0f ||
        !isfinite(snapshot.summary.ageGyr)) {
        return false;
    }

    snapshot.stellarOrbit = SolarSystemStellarOrbit(
        snapshot.stellarProfiles, count, hash);
    if (count <= 1) {
        snapshot.minimumPlanetOrbitGame = 180.0f;
    } else {
        double separationKm = count == 3
            ? snapshot.stellarOrbit.outerSeparationKm
            : snapshot.stellarOrbit.innerSeparationKm;
        float minimum = (float)SpaceUnitsKilometersToGameDistance(
            separationKm) * (count == 3 ? 3.0f : 2.8f);
        snapshot.minimumPlanetOrbitGame = fmaxf(180.0f, minimum);
    }
    if (!(snapshot.minimumPlanetOrbitGame > 0.0f) ||
        !isfinite(snapshot.minimumPlanetOrbitGame)) {
        return false;
    }
    for (int index = 0; index < sys->planetCount; index++) {
        snapshot.planetStatuses[index] = SOLAR_PLANET_STABLE;
        if (!SolarSystemPlanetDefinitionIsValid(&sys->planets[index]) ||
            !SolarSystemPlanetOrbitBuild(
                sys, index, snapshot.summary.totalMassKg, 1.0,
                &snapshot.planetOrbits[index])) {
            return false;
        }
    }
    snapshot.valid = true;
    *out = snapshot;
    return true;
}

bool SolarSystemPhysicalSnapshotEvolve(
    const SolarSystemDef *sys, double ageOffsetGyr,
    SolarSystemPhysicalSnapshot *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!sys || !isfinite(ageOffsetGyr) || ageOffsetGyr < 0.0) return false;

    SolarSystemPhysicalSnapshot baseScratch;
    const SolarSystemPhysicalSnapshot *base =
        SolarSystemPhysicalSnapshotForSystem(sys, &baseScratch);
    if (!base) return false;
    if (ageOffsetGyr == 0.0) {
        *out = *base;
        return true;
    }

    SolarSystemPhysicalSnapshot evolved = *base;
    evolved.satellitesBuilt = false;
    memset(evolved.satelliteOrbits, 0, sizeof(evolved.satelliteOrbits));
    evolved.summary.totalMassKg = 0.0;
    evolved.summary.totalLuminositySolar = 0.0f;
    memset(evolved.summary.stellarLuminositiesSolar, 0,
           sizeof(evolved.summary.stellarLuminositiesSolar));
    for (int i = 0; i < evolved.summary.stellarCount; i++) {
        const StellarProfile *initial = &base->stellarProfiles[i];
        double requestedAge = (double)initial->ageGyr + ageOffsetGyr;
        if (!StellarProfileAtAge(
                initial->initialMassSolar, requestedAge,
                initial->evolutionSeed, &evolved.stellarProfiles[i]) ||
            !SolarSystemPhysicsStellarProfileIsValid(
                &evolved.stellarProfiles[i])) {
            return false;
        }
        evolved.summary.totalMassKg += evolved.stellarProfiles[i].massKg;
        evolved.summary.totalLuminositySolar +=
            evolved.stellarProfiles[i].luminositySolar;
        evolved.summary.stellarLuminositiesSolar[i] =
            evolved.stellarProfiles[i].luminositySolar;
    }
    double requestedSystemAge = (double)base->summary.ageGyr + ageOffsetGyr;
    evolved.summary.ageGyr = (float)fmin(requestedSystemAge,
                                         (double)FLT_MAX);
    if (!(evolved.summary.totalMassKg > 0.0) ||
        !isfinite(evolved.summary.totalMassKg) ||
        !(evolved.summary.totalLuminositySolar > 0.0f) ||
        !isfinite(evolved.summary.totalLuminositySolar)) {
        return false;
    }

    SolarSystemMassLossEvent events[MAX_SOLAR_LIGHTS] = { 0 };
    int eventCount = SolarSystemMassLossEvents(
        base, requestedSystemAge, events, MAX_SOLAR_LIGHTS);
    if (eventCount < 0 || !SolarSystemStellarOrbitEvolve(
                              base, evolved.stellarProfiles,
                              events, eventCount,
                              &evolved.stellarOrbit)) {
        return false;
    }
    if (evolved.summary.stellarCount <= 1 ||
        evolved.stellarOrbit.motion != SPACE_BARYCENTER_BOUND) {
        evolved.minimumPlanetOrbitGame = 180.0f;
    } else {
        double separationKm = evolved.summary.stellarCount == 3
            ? evolved.stellarOrbit.outerSeparationKm
            : evolved.stellarOrbit.innerSeparationKm;
        float minimum = (float)SpaceUnitsKilometersToGameDistance(
            separationKm) *
            (evolved.summary.stellarCount == 3 ? 3.0f : 2.8f);
        evolved.minimumPlanetOrbitGame = fmaxf(180.0f, minimum);
    }
    double giantReachKm = 0.0;
    if (!SolarSystemMaximumGiantReachKm(
            base, requestedSystemAge, &giantReachKm)) {
        return false;
    }
    for (int index = 0; index < sys->planetCount; index++) {
        if (!SolarSystemPlanetOrbitEvolve(
                sys, index, base, evolved.summary.totalMassKg,
                giantReachKm, events, eventCount,
                evolved.stellarOrbit.motion != SPACE_BARYCENTER_BOUND,
                &evolved.planetStatuses[index],
                &evolved.planetOrbits[index])) {
            return false;
        }
    }
    evolved.valid = true;
    *out = evolved;
    return true;
}

bool SolarSystemPhysicalSnapshotBuildSatellites(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out)
{
    if (!out) return false;
    out->satellitesBuilt = false;
    memset(out->satelliteOrbits, 0, sizeof(out->satelliteOrbits));
    if (!sys || !out->valid || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS ||
        !(out->summary.totalMassKg > 0.0) ||
        !isfinite(out->summary.totalMassKg)) {
        return false;
    }

    SpaceSatelliteOrbit satelliteOrbits[MAX_SOLAR_PLANETS] = { 0 };
    for (int index = 0; index < sys->planetCount; index++) {
        if (!SolarSystemPlanetDefinitionIsValid(&sys->planets[index])) {
            return false;
        }
        if (out->planetStatuses[index] < SOLAR_PLANET_STABLE ||
            out->planetStatuses[index] > SOLAR_PLANET_EJECTED) {
            return false;
        }
        if (out->planetStatuses[index] != SOLAR_PLANET_STABLE) continue;
        PlanetProfile profile = SolarPlanetProfile(sys, index);
        double earthMasses = SpaceUnitsKilogramsToGameMass(profile.massKg);
        double occurrence = profile.hasSolidSurface
            ? 0.18 + Clamp((float)((earthMasses - 0.45) / 2.5),
                           0.0f, 1.0f) * 0.10
            : 0.82;
        if (profile.tidallyLocked) occurrence *= 0.22;
        bool forceMoon = sys->anchorX == 0 && sys->anchorZ == 0 &&
                         index == 2;
        if (!SpaceSatelliteGenerate(
                profile.seed ^ 0xb5297a4du, profile.massKg,
                profile.physicalRadiusKm,
                out->planetOrbits[index].semiMajorAxisKm,
                out->summary.totalMassKg, occurrence, forceMoon,
                &satelliteOrbits[index])) {
            return false;
        }
    }
    memcpy(out->satelliteOrbits, satelliteOrbits,
           sizeof(out->satelliteOrbits));
    out->satellitesBuilt = true;
    return true;
}

static bool SolarSystemPhysicalSnapshotIsUsable(
    const SolarSystemDef *sys, const SolarSystemPhysicalSnapshot *snapshot)
{
    if (!sys || !snapshot || !snapshot->valid || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS ||
        snapshot->summary.stellarCount <= 0 ||
        snapshot->summary.stellarCount > MAX_SOLAR_LIGHTS ||
        snapshot->stellarOrbit.bodyCount != snapshot->summary.stellarCount ||
        snapshot->stellarOrbit.motion != SPACE_BARYCENTER_BOUND ||
        snapshot->stellarOrbit.innerEccentricity != 0.0 ||
        snapshot->stellarOrbit.outerEccentricity != 0.0 ||
        snapshot->stellarOrbit.innerArgumentPeriapsisRad != 0.0 ||
        snapshot->stellarOrbit.outerArgumentPeriapsisRad != 0.0 ||
        !(snapshot->minimumPlanetOrbitGame > 0.0f) ||
        !isfinite(snapshot->minimumPlanetOrbitGame)) {
        return false;
    }

    uint32_t expectedHash = 0;
    if (SolarSystemStellarCount(sys, &expectedHash) !=
            snapshot->summary.stellarCount ||
        snapshot->stellarHash != expectedHash ||
        !SolarSystemPhysicsStellarProfileEquals(
            &snapshot->stellarProfiles[0], &sys->star)) {
        return false;
    }

    double totalMassKg = 0.0;
    float totalLuminositySolar = 0.0f;
    for (int i = 0; i < snapshot->summary.stellarCount; i++) {
        const StellarProfile *star = &snapshot->stellarProfiles[i];
        if (!SolarSystemPhysicsStellarProfileIsValid(star) ||
            star->ageGyr != snapshot->summary.ageGyr ||
            snapshot->summary.stellarLuminositiesSolar[i] !=
                star->luminositySolar ||
            snapshot->stellarOrbit.massKg[i] != star->massKg) {
            return false;
        }
        totalMassKg += star->massKg;
        totalLuminositySolar += star->luminositySolar;
    }
    if (!(totalMassKg > 0.0) || !isfinite(totalMassKg) ||
        !(totalLuminositySolar > 0.0f) ||
        !isfinite(totalLuminositySolar) ||
        snapshot->summary.totalMassKg != totalMassKg ||
        snapshot->summary.totalLuminositySolar != totalLuminositySolar) {
        return false;
    }

    SpaceBarycenterBodyState stellarStates[MAX_SOLAR_LIGHTS];
    if (SpaceBarycenterSolve(&snapshot->stellarOrbit, 0.0, stellarStates,
                             MAX_SOLAR_LIGHTS) !=
        snapshot->summary.stellarCount) {
        return false;
    }
    for (int i = 0; i < sys->planetCount; i++) {
        const SpaceKeplerOrbit *orbit = &snapshot->planetOrbits[i];
        if (!SolarSystemPlanetDefinitionIsValid(&sys->planets[i]) ||
            snapshot->planetStatuses[i] != SOLAR_PLANET_STABLE ||
            !SpaceKeplerOrbitIsValid(orbit) ||
            orbit->semiMajorAxisKm != sys->planets[i].semiMajorAxisKm ||
            orbit->centralMassKg != totalMassKg) {
            return false;
        }
        if (snapshot->satellitesBuilt &&
            !SolarSystemPhysicsSatelliteOrbitIsValid(
                &snapshot->satelliteOrbits[i])) {
            return false;
        }
    }
    return true;
}

const SolarSystemPhysicalSnapshot *SolarSystemPhysicalSnapshotForSystem(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *scratch)
{
    if (!sys) return NULL;
    if (SolarSystemPhysicalSnapshotIsUsable(sys, &sys->physicalSnapshot)) {
        return &sys->physicalSnapshot;
    }
    if (!scratch || !SolarSystemPhysicalSnapshotBuild(sys, scratch)) return NULL;
    return scratch;
}

int SolarSystemPhysicalSnapshotStellarBodiesAtTime(
    const SolarSystemDef *sys, const SolarSystemPhysicalSnapshot *snapshot,
    double simulationTime, SolarStellarBody *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int clearCount = maxCount < MAX_SOLAR_LIGHTS
        ? maxCount : MAX_SOLAR_LIGHTS;
    memset(out, 0, sizeof(*out) * (size_t)clearCount);
    if (!sys || !snapshot || !snapshot->valid ||
        !isfinite(simulationTime) ||
        !SolarSystemPhysicsVectorIsFinite(sys->center)) {
        return 0;
    }
    int count = snapshot->summary.stellarCount;
    if (count <= 0 || count > MAX_SOLAR_LIGHTS || count > maxCount) return 0;

    SpaceBarycenterBodyState states[MAX_SOLAR_LIGHTS];
    if (SpaceBarycenterSolve(&snapshot->stellarOrbit, simulationTime, states,
                             MAX_SOLAR_LIGHTS) != count) {
        return 0;
    }
    for (int i = 0; i < count; i++) {
        const StellarProfile *star = &snapshot->stellarProfiles[i];
        float proxyRadius = (float)SolarSystemStellarVisualRadius(star);
        if (!SolarSystemPhysicsStellarProfileIsValid(star) ||
            !(proxyRadius > 0.0f) || !isfinite(proxyRadius)) {
            memset(out, 0, sizeof(*out) * (size_t)clearCount);
            return 0;
        }
        out[i] = (SolarStellarBody){
            .center = Vector3Add(sys->center, states[i].offsetGame),
            .velocity = states[i].velocityGame,
            .stellar = *star,
            .spectrum = star->spectrum,
            .spaceProxyRadius = proxyRadius,
            .luminosity = star->luminositySolar,
            .index = i,
            .primary = i == 0
        };
        if (!SolarSystemPhysicsVectorIsFinite(out[i].center) ||
            !SolarSystemPhysicsVectorIsFinite(out[i].velocity)) {
            memset(out, 0, sizeof(*out) * (size_t)clearCount);
            return 0;
        }
    }
    return count;
}
