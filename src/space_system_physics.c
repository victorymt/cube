#include "space_system_physics.h"

#include "raymath.h"
#include "space_units.h"
#include "terrain.h"

#include <math.h>
#include <string.h>

static bool SolarSystemPhysicsVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static uint32_t SolarLightHash(const SolarSystemDef *sys)
{
    return WorldHash2D(sys->anchorX * 113 + 41, sys->anchorZ * 71 + 19);
}

uint32_t SolarSystemPlanetOrbitHash(const SolarSystemDef *sys, int index)
{
    if (!sys) return 0u;
    return WorldHash2D(sys->anchorX * 53 + index * 7 + 1,
                       sys->anchorZ * 29 + index * 3 + 2);
}

uint32_t SolarSystemPlanetPlaneHash(const SolarSystemDef *sys)
{
    if (!sys) return 0u;
    return WorldHash2D(sys->anchorX * 79 + 11, sys->anchorZ * 97 + 23);
}

int SolarSystemStellarVisualRadius(const StellarProfile *star)
{
    if (!star || star->radiusKm <= 0.0) return 9;
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
                             &companion)) {
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
    SpaceKeplerOrbit *out)
{
    if (!sys || !out || index < 0 || index >= sys->planetCount ||
        index >= MAX_SOLAR_PLANETS) {
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
    double semiMajorAxisGame = SpaceUnitsKilometersToGameDistance(
        planet->semiMajorAxisKm);
    double hostCellLimit = 694.0 / fmax(semiMajorAxisGame, 1.0) - 1.0;
    eccentricity = fmax(0.0, fmin(eccentricity,
        fmin(0.05, fmax(hostCellLimit, 0.0))));

    *out = (SpaceKeplerOrbit){
        .semiMajorAxisKm = planet->semiMajorAxisKm,
        .centralMassKg = centralMassKg,
        .eccentricity = eccentricity,
        .inclinationRad = inclination,
        .longitudeAscendingNodeRad = systemNode + nodeOffset,
        .argumentPeriapsisRad =
            (double)((orbitHash >> 3) % 6283u) / 1000.0,
        .meanAnomalyAtEpochRad = (double)(orbitHash % 6283u) / 1000.0
    };
    return SpaceKeplerOrbitIsValid(out);
}

bool SolarSystemPhysicalSnapshotBuild(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out)
{
    if (!sys || !out || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }
    memset(out, 0, sizeof(*out));

    uint32_t hash = 0;
    int count = SolarSystemStellarProfiles(
        sys, out->stellarProfiles, MAX_SOLAR_LIGHTS, &hash);
    if (count <= 0 || count > MAX_SOLAR_LIGHTS) return false;

    out->stellarHash = hash;
    out->summary.stellarCount = count;
    out->summary.ageGyr = sys->star.ageGyr;
    for (int i = 0; i < count; i++) {
        out->summary.totalMassKg += out->stellarProfiles[i].massKg;
        out->summary.totalLuminositySolar +=
            fmaxf(out->stellarProfiles[i].luminositySolar, 0.0f);
        out->summary.stellarLuminositiesSolar[i] =
            out->stellarProfiles[i].luminositySolar;
    }
    if (!(out->summary.totalMassKg > 0.0)) {
        out->summary.totalMassKg = SPACE_UNITS_SOLAR_MASS_KG;
    }
    if (!(out->summary.totalLuminositySolar > 0.0f)) {
        out->summary.totalLuminositySolar =
            sys->star.luminositySolar > 0.0f ? sys->star.luminositySolar : 1.0f;
    }

    out->stellarOrbit = SolarSystemStellarOrbit(
        out->stellarProfiles, count, hash);
    if (count <= 1) {
        out->minimumPlanetOrbitGame = 180.0f;
    } else {
        double separationKm = count == 3
            ? out->stellarOrbit.outerSeparationKm
            : out->stellarOrbit.innerSeparationKm;
        float minimum = (float)SpaceUnitsKilometersToGameDistance(
            separationKm) * (count == 3 ? 3.0f : 2.8f);
        out->minimumPlanetOrbitGame = fmaxf(180.0f, minimum);
    }
    for (int index = 0; index < sys->planetCount; index++) {
        if (!SolarSystemPlanetOrbitBuild(
                sys, index, out->summary.totalMassKg,
                &out->planetOrbits[index])) {
            return false;
        }
    }
    out->valid = true;
    return true;
}

bool SolarSystemPhysicalSnapshotBuildSatellites(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *out)
{
    if (!sys || !out || !out->valid || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS ||
        out->summary.totalMassKg <= 0.0) {
        return false;
    }
    memset(out->satelliteOrbits, 0, sizeof(out->satelliteOrbits));
    for (int index = 0; index < sys->planetCount; index++) {
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
                sys->planets[index].semiMajorAxisKm,
                out->summary.totalMassKg, occurrence, forceMoon,
                &out->satelliteOrbits[index])) {
            return false;
        }
    }
    out->satellitesBuilt = true;
    return true;
}

const SolarSystemPhysicalSnapshot *SolarSystemPhysicalSnapshotForSystem(
    const SolarSystemDef *sys, SolarSystemPhysicalSnapshot *scratch)
{
    if (!sys) return NULL;
    if (sys->physicalSnapshot.valid &&
        sys->physicalSnapshot.summary.stellarCount > 0 &&
        sys->physicalSnapshot.summary.stellarCount <= MAX_SOLAR_LIGHTS) {
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
        float proxyRadius = i == 0 ? (float)sys->starProxyRadius :
                                    (float)SolarSystemStellarVisualRadius(star);
        if (!(star->massKg > 0.0) || !isfinite(star->massKg) ||
            !(star->radiusKm > 0.0) || !isfinite(star->radiusKm) ||
            !(star->luminositySolar > 0.0f) ||
            !isfinite(star->luminositySolar) ||
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
