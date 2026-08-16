#include "space/space_internal.h"

static uint32_t PlanetBodyTextureHash(const SpaceBodyInfo *body)
{
    uint32_t hash = body->worldSeed ^ 0x57ec91u;
    hash ^= (uint32_t)body->index * 0x8da6b343u;
    hash ^= (uint32_t)(body->systemAnchorX ^ body->systemAnchorZ) * 0xcb1ab31fu;
    hash ^= hash >> 16;
    hash *= 0x7feb352du;
    hash ^= hash >> 15;
    hash *= 0x846ca68bu;
    return hash ^ (hash >> 16);
}

float PlanetBodyTextureRotation(const SpaceBodyInfo *body)
{
    if (!body) return 0.0f;
    return (float)((PlanetBodyTextureHash(body) >> 8) % 360u) +
           (float)SpacePeriodicSimulationTime(
               SpaceElapsedSimulationTime()) * body->profile.rotationRate;
}

static bool StarSystemDefinitionAt(int ax, int az, SolarSystemDef *out)
{
    if (!out) return false;
    SolarSystemDef system = {
        .anchorX = ax,
        .anchorZ = az
    };
    if (ax == 0 && az == 0) {
        BuildSolSystem(&system);
        if (!SolarSystemPhysicalSnapshotBuild(
                &system, &system.physicalSnapshot)) {
            return false;
        }
        system.center.x = 0.0f;
        system.center.z = 0.0f;
        for (int i = 0; i < system.planetCount; i++) {
            system.planets[i].style = SolarPlanetProfile(&system, i).style;
        }
        if (!SolarSystemPhysicalSnapshotBuildSatellites(
                &system, &system.physicalSnapshot)) {
            return false;
        }
        *out = system;
        return true;
    }

    unsigned int roll = WorldHash2D(ax, az);
    if (roll % 100u >= STAR_SYSTEM_PROBABILITY) {
        *out = system;
        return false;
    }

    unsigned int h = WorldHash2DBits(
        (uint32_t)ax * 31u + 7u, (uint32_t)az * 17u + 5u);
    system.exists = true;
    BuildStarName(h, system.name, sizeof(system.name));
    ApplyPrimaryStar(&system, StellarGenerate(h ^ 0xd1b54a35u));
    int verticalOffset = (int)((h >> 14) % 93u) - 46;
    system.center = (Vector3){
        (float)SpaceSystemGlobalCoordinate(ax),
        STAR_SYSTEM_MID_Y + (float)verticalOffset,
        (float)SpaceSystemGlobalCoordinate(az)
    };
    if (!SolarSystemApplyFormation(&system, h)) return false;
    for (int i = 0; i < system.planetCount; i++) {
        system.planets[i].style = SolarPlanetProfile(&system, i).style;
    }
    *out = system;
    return true;
}

static void ProjectSolarSystemCenter(SolarSystemDef *system)
{
    if (!system) return;
    system->center.x = (float)SpaceGlobalToLocalX(
        SpaceSystemGlobalCoordinate(system->anchorX));
    system->center.z = (float)SpaceGlobalToLocalZ(
        SpaceSystemGlobalCoordinate(system->anchorZ));
}

bool StarSystemAt(int ax, int az, SolarSystemDef *out)
{
    if (!out) return false;
    uint32_t worldSeed = WorldGetSeed();
    if (!SpaceQueryDefinitionCacheGet(worldSeed, ax, az, out)) {
        if (!StarSystemDefinitionAt(ax, az, out)) {
            out->exists = false;
        }
        SpaceQueryDefinitionCachePut(worldSeed, ax, az, out);
    }
    ProjectSolarSystemCenter(out);
    return out->exists;
}

Vector3 SolarSystemApparentDirection(const SolarSystemDef *sys, Vector3 observer)
{
    if (!sys || !SpaceVectorIsFinite(sys->center) ||
        !SpaceVectorIsFinite(observer)) return Vector3Zero();

    Vector3 toStar = Vector3Subtract(sys->center, observer);
    float distance = Vector3Length(toStar);
    if (!isfinite(distance) || distance < 0.001f) return Vector3Zero();

    float horizontalDistance = sqrtf(toStar.x * toStar.x + toStar.z * toStar.z);
    if (!isfinite(horizontalDistance)) return Vector3Zero();
    if (horizontalDistance < 0.001f) return Vector3Scale(toStar, 1.0f / distance);

    // Space voxels use a compact vertical layer. Expand that stable system
    // offset into galactic latitude for the distant sky, then converge to the
    // physical direction before the streamed star body becomes visible.
    float physicalLatitude = atan2f(toStar.y, horizontalDistance);
    float systemOffset = Clamp((sys->center.y - STAR_SYSTEM_MID_Y) /
                               STAR_SYSTEM_VERTICAL_RANGE, -1.0f, 1.0f);
    float expandedLatitude = physicalLatitude + systemOffset * STAR_SKY_LATITUDE_SCALE;
    float blend = Clamp((distance - STAR_SKY_PHYSICAL_DISTANCE) /
                        (STAR_SKY_FULL_LATITUDE_DISTANCE - STAR_SKY_PHYSICAL_DISTANCE),
                        0.0f, 1.0f);
    blend = blend * blend * (3.0f - 2.0f * blend);
    float latitude = Lerp(physicalLatitude, expandedLatitude, blend);
    latitude = Clamp(latitude, -1.20f, 1.20f);

    float horizontalScale = cosf(latitude) / horizontalDistance;
    Vector3 result = {
        toStar.x * horizontalScale,
        sinf(latitude),
        toStar.z * horizontalScale
    };
    return SpaceVectorIsFinite(result) ? result : Vector3Zero();
}

Vector3 SolarSystemPlanetPositionAtTime(const SolarSystemDef *sys, int index,
                                        double simulationTime)
{
    SolarPlanetOrbitalState state;
    return SolarSystemPlanetStateAtTime(sys, index, simulationTime, &state)
        ? state.center : Vector3Zero();
}

Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index)
{
    return SolarSystemPlanetPositionAtTime(
        sys, index, SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()));
}

static bool SolarSystemPlanetStateForSnapshotAtTime(
    const SolarSystemDef *sys, const SolarSystemPhysicalSnapshot *snapshot,
    int index, double simulationTime, SolarPlanetOrbitalState *out)
{
    if (!out) return false;
    *out = (SolarPlanetOrbitalState){ 0 };
    if (!sys || !snapshot || !snapshot->valid || !isfinite(simulationTime) ||
        sys->planetCount < 0 || sys->planetCount > MAX_SOLAR_PLANETS ||
        index < 0 || index >= sys->planetCount ||
        snapshot->planetStatuses[index] != SOLAR_PLANET_STABLE ||
        !SpaceVectorIsFinite(sys->center)) return false;

    SpaceKeplerState relative;
    if (!SpaceKeplerStateAtTime(&snapshot->planetOrbits[index],
                                simulationTime, &relative)) {
        return false;
    }
    out->center = Vector3Add(sys->center, relative.positionGame);
    out->velocity = relative.velocityGame;
    out->celestialPosition = (CelestialPosition){
        .systemAnchorX = sys->anchorX,
        .systemAnchorZ = sys->anchorZ,
        .offsetKm = relative.positionKm
    };
    out->celestialVelocityKmPerSecond = relative.velocityKmPerSecond;
    if (!SpaceVectorIsFinite(out->center) ||
        !SpaceVectorIsFinite(out->velocity)) {
        *out = (SolarPlanetOrbitalState){ 0 };
        return false;
    }
    return true;
}

bool SolarSystemPlanetStateAtTime(const SolarSystemDef *sys, int index,
                                  double simulationTime,
                                  SolarPlanetOrbitalState *out)
{
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    return SolarSystemPlanetStateForSnapshotAtTime(
        sys, snapshot, index, simulationTime, out);
}

double SolarSystemPlanetOrbitPeriodSeconds(const SolarSystemDef *sys, int index)
{
    if (!SolarSystemPlanetIndexIsValid(sys, index)) return 0.0;

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    if (!snapshot) return 0.0;
    return SpaceUnitsKeplerPeriodSeconds(
        snapshot->planetOrbits[index].semiMajorAxisKm,
        snapshot->planetOrbits[index].centralMassKg);
}

double SolarSystemPlanetOrbitPeriodGameTime(const SolarSystemDef *sys, int index)
{
    return SpaceUnitsSecondsToGameTime(
        SolarSystemPlanetOrbitPeriodSeconds(sys, index));
}

float SolarSystemParkingRadiusGame(const SolarSystemDef *sys)
{
    if (!sys || !(sys->star.radiusKm > 0.0) ||
        !isfinite(sys->star.radiusKm)) return 0.0f;
    float stellarClearance = (float)SpaceUnitsKilometersToGameDistance(
        sys->star.radiusKm * 12.0);
    return fmaxf(stellarClearance,
                 (float)SPACE_UNITS_GAME_DISTANCE_PER_AU * 1.5f);
}

float SolarSystemPlanetParkingRadiusGame(const SolarSystemDef *sys, int index)
{
    if (!SolarSystemPlanetIndexIsValid(sys, index)) return 0.0f;
    PlanetProfile profile = SolarPlanetProfile(sys, index);
    if (!(profile.physicalRadiusKm > 0.0) ||
        !isfinite(profile.physicalRadiusKm)) return 0.0f;

    float physicalRadius = (float)SpaceUnitsKilometersToGameDistance(
        profile.physicalRadiusKm);
    float atmosphereClearance = profile.atmosphereType ==
                                PLANET_ATMOSPHERE_NONE
        ? physicalRadius
        : physicalRadius * 1.12f;
    float ringClearance = profile.hasRings ? physicalRadius * 1.86f * 1.12f :
                                             physicalRadius;
    return fmaxf(physicalRadius * 8.0f,
                 fmaxf(atmosphereClearance, ringClearance));
}

float SolarSystemPlanetEncounterRadiusGame(const SolarSystemDef *sys, int index)
{
    if (!SolarSystemPlanetIndexIsValid(sys, index)) return 0.0f;
    PlanetProfile profile = SolarPlanetProfile(sys, index);
    double parentMassKg = SolarSystemStellarMassKg(sys);
    if (!(profile.massKg > 0.0) || !(profile.physicalRadiusKm > 0.0) ||
        !(parentMassKg > 0.0)) return 0.0f;

    double soiKm = SpaceUnitsLaplaceSphereOfInfluenceKm(
        sys->planets[index].semiMajorAxisKm, profile.massKg, parentMassKg);
    float physicalRadius = (float)SpaceUnitsKilometersToGameDistance(
        profile.physicalRadiusKm);
    float physicalEncounter = physicalRadius * 64.0f;
    float soiEncounter = (float)SpaceUnitsKilometersToGameDistance(soiKm * 0.15);
    return fmaxf(physicalEncounter, soiEncounter);
}

float SolarSystemPlanetSupercruiseExitRadiusGame(const SolarSystemDef *sys,
                                                 int index)
{
    if (!SolarSystemPlanetIndexIsValid(sys, index)) return 0.0f;
    PlanetProfile profile = SolarPlanetProfile(sys, index);
    if (!(profile.physicalRadiusKm > 0.0) ||
        !isfinite(profile.physicalRadiusKm)) return 0.0f;

    float physicalRadius = (float)SpaceUnitsKilometersToGameDistance(
        profile.physicalRadiusKm);
    float parkingRadius = SolarSystemPlanetParkingRadiusGame(sys, index);
    float encounterRadius = SolarSystemPlanetEncounterRadiusGame(sys, index);
    float cruiseRadius = fmaxf(physicalRadius * 16.0f,
                               parkingRadius * 2.0f);
    if (encounterRadius > 0.0f) {
        cruiseRadius = fminf(cruiseRadius, encounterRadius * 0.5f);
    }
    return fmaxf(cruiseRadius, parkingRadius * 1.15f);
}

float HomeWorldParkingRadiusGame(void)
{
    SolarSystemDef sol;
    if (StarSystemAt(0, 0, &sol) && sol.planetCount > 2) {
        float parking = SolarSystemPlanetParkingRadiusGame(&sol, 2);
        if (parking > 0.0f && isfinite(parking)) return parking;
    }
    return (float)SpaceUnitsKilometersToGameDistance(
        SPACE_UNITS_EARTH_RADIUS_KM * 16.0);
}

bool SolarSystemPhysicalSummaryForSystem(
    const SolarSystemDef *sys, SolarSystemPhysicalSummary *out)
{
    if (!out) return false;
    *out = (SolarSystemPhysicalSummary){ 0 };

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    if (!snapshot || snapshot->summary.stellarCount <= 0 ||
        snapshot->summary.stellarCount > MAX_SOLAR_LIGHTS ||
        !(snapshot->summary.totalMassKg > 0.0) ||
        !isfinite(snapshot->summary.totalMassKg) ||
        !(snapshot->summary.totalLuminositySolar > 0.0f) ||
        !isfinite(snapshot->summary.totalLuminositySolar) ||
        snapshot->summary.ageGyr < 0.0f ||
        !isfinite(snapshot->summary.ageGyr)) {
        return false;
    }
    for (int i = 0; i < snapshot->summary.stellarCount; i++) {
        if (!(snapshot->summary.stellarLuminositiesSolar[i] > 0.0f) ||
            !isfinite(snapshot->summary.stellarLuminositiesSolar[i])) {
            return false;
        }
    }
    *out = snapshot->summary;
    return true;
}

double SolarSystemStellarMassKg(const SolarSystemDef *sys)
{
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    return snapshot ? snapshot->summary.totalMassKg : 0.0;
}

int SolarSystemStellarBodiesAtTime(const SolarSystemDef *sys,
                                   double simulationTime,
                                   SolarStellarBody *out, int maxCount)
{
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    return SolarSystemPhysicalSnapshotStellarBodiesAtTime(
        sys, snapshot, simulationTime, out, maxCount);
}

static bool SolarSystemRuntimeGeometryIsFinite(
    const SolarSystemRuntimeState *runtime)
{
    if (!runtime || !isfinite(runtime->simulationTime) ||
        runtime->stellarCount <= 0 || runtime->stellarCount > MAX_SOLAR_LIGHTS ||
        runtime->planetCount < 0 || runtime->planetCount > MAX_SOLAR_PLANETS ||
        !(runtime->totalStellarMassKg > 0.0) ||
        !isfinite(runtime->totalStellarMassKg)) {
        return false;
    }
    for (int star = 0; star < runtime->stellarCount; star++) {
        const SolarStellarBody *body = &runtime->stars[star];
        if (!isfinite(body->center.x) || !isfinite(body->center.y) ||
            !isfinite(body->center.z) || !isfinite(body->velocity.x) ||
            !isfinite(body->velocity.y) || !isfinite(body->velocity.z) ||
            !(body->stellar.massKg > 0.0) ||
            !isfinite(body->stellar.massKg) ||
            !(body->stellar.radiusKm > 0.0) ||
            !isfinite(body->stellar.radiusKm) ||
            !(body->stellar.luminositySolar > 0.0f) ||
            !isfinite(body->stellar.luminositySolar) ||
            !(body->luminosity > 0.0f) || !isfinite(body->luminosity) ||
            !(body->spaceProxyRadius > 0.0f) ||
            !isfinite(body->spaceProxyRadius) ||
            !SpaceRemnantStateIsValid(&body->remnant)) {
            return false;
        }
    }
    for (int planet = 0; planet < runtime->planetCount; planet++) {
        const SolarPlanetRuntimeState *state = &runtime->planets[planet];
        if (state->dynamicalStatus < SOLAR_PLANET_STABLE ||
            state->dynamicalStatus > SOLAR_PLANET_EJECTED) {
            return false;
        }
        if (!SpaceRemnantEnvironmentIsValid(&state->remnantEnvironment)) {
            return false;
        }
        if (state->dynamicalStatus != SOLAR_PLANET_STABLE) {
            if (state->valid || state->satelliteOrbit.exists ||
                state->remnantEnvironment.active) {
                return false;
            }
            continue;
        }
        if (!state->valid || !isfinite(state->center.x) ||
            !isfinite(state->center.y) || !isfinite(state->center.z) ||
            !isfinite(state->velocity.x) || !isfinite(state->velocity.y) ||
            !isfinite(state->velocity.z) ||
            !(state->semiMajorAxisKm > 0.0) ||
            !isfinite(state->semiMajorAxisKm) ||
            !isfinite(state->currentIrradianceEarth) ||
            state->currentIrradianceEarth < 0.0f) {
            return false;
        }
        if (state->satelliteOrbit.exists &&
            (!isfinite(state->satelliteState.positionKm.x) ||
             !isfinite(state->satelliteState.positionKm.y) ||
             !isfinite(state->satelliteState.positionKm.z) ||
             !isfinite(state->satelliteState.velocityKmPerSecond.x) ||
             !isfinite(state->satelliteState.velocityKmPerSecond.y) ||
             !isfinite(state->satelliteState.velocityKmPerSecond.z) ||
             !isfinite(state->satelliteCenter.x) ||
             !isfinite(state->satelliteCenter.y) ||
             !isfinite(state->satelliteCenter.z) ||
             !isfinite(state->satelliteVelocity.x) ||
             !isfinite(state->satelliteVelocity.y) ||
             !isfinite(state->satelliteVelocity.z))) {
            return false;
        }
    }
    return true;
}

static bool SolarSystemEvaluateSnapshotAtTime(
    const SolarSystemDef *sys,
    const SolarSystemPhysicalSnapshot *physicalSnapshot,
    double simulationTime, SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!sys || !physicalSnapshot || !physicalSnapshot->valid ||
        !isfinite(simulationTime) || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot = physicalSnapshot;
    if (!snapshot->satellitesBuilt) {
        scratch = *snapshot;
        if (!SolarSystemPhysicalSnapshotBuildSatellites(sys, &scratch)) {
            return false;
        }
        snapshot = &scratch;
    }

    int stellarCount = SolarSystemPhysicalSnapshotStellarBodiesAtTime(
        sys, snapshot, simulationTime, out->stars, MAX_SOLAR_LIGHTS);
    if (stellarCount <= 0 || stellarCount > MAX_SOLAR_LIGHTS) return false;
    out->simulationTime = simulationTime;
    out->stellarCount = stellarCount;
    out->planetCount = sys->planetCount;
    out->totalStellarMassKg = snapshot->summary.totalMassKg;
    if (!(out->totalStellarMassKg > 0.0)) return false;
    // The remnant sampler shares the public runtime validity contract while
    // the remaining planet fields are being populated below.
    out->valid = true;

    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    for (int star = 0; star < stellarCount; star++) {
        sources[star] = (SolarLightSource){
            .center = out->stars[star].center,
            .stellar = out->stars[star].stellar,
            .spectrum = out->stars[star].spectrum,
            .spaceProxyRadius = out->stars[star].spaceProxyRadius,
            .luminosity = out->stars[star].luminosity,
            .primary = out->stars[star].primary
        };
    }
    for (int index = 0; index < sys->planetCount; index++) {
        SolarPlanetRuntimeState *planet = &out->planets[index];
        planet->dynamicalStatus = snapshot->planetStatuses[index];
        if (planet->dynamicalStatus < SOLAR_PLANET_STABLE ||
            planet->dynamicalStatus > SOLAR_PLANET_EJECTED) {
            return false;
        }
        if (planet->dynamicalStatus != SOLAR_PLANET_STABLE) continue;
        SolarPlanetOrbitalState orbitalState;
        if (!SolarSystemPlanetStateForSnapshotAtTime(
                sys, snapshot, index, simulationTime, &orbitalState)) {
            return false;
        }
        planet->profile = SolarPlanetProfileForSnapshot(sys, index, snapshot);
        if (!PlanetProfileIsValid(&planet->profile) ||
            !(planet->profile.massKg > 0.0) ||
            !(planet->profile.physicalRadiusKm > 0.0) ||
            !(planet->profile.spaceProxyRadius > 0.0f)) {
            return false;
        }
        planet->center = orbitalState.center;
        planet->velocity = orbitalState.velocity;
        planet->celestialPosition = orbitalState.celestialPosition;
        planet->celestialVelocityKmPerSecond =
            orbitalState.celestialVelocityKmPerSecond;
        planet->semiMajorAxisKm =
            snapshot->planetOrbits[index].semiMajorAxisKm;
        planet->currentIrradianceEarth = SolarSystemIrradianceAt(
            sources, stellarCount, planet->center);
        if (!SolarSystemRemnantEnvironmentAt(
                out, planet->center, &planet->remnantEnvironment)) {
            return false;
        }
        planet->satelliteOrbit = snapshot->satelliteOrbits[index];
        if (planet->satelliteOrbit.exists) {
            if (!SpaceSatelliteStateAtSeconds(
                    &planet->satelliteOrbit, planet->profile.massKg,
                    SpaceUnitsGameTimeToSeconds(simulationTime),
                    &planet->satelliteState)) {
                return false;
            }
            SpaceSatelliteVector3 relativePosition =
                planet->satelliteState.positionKm;
            SpaceSatelliteVector3 relativeVelocity =
                planet->satelliteState.velocityKmPerSecond;
            planet->satelliteCenter = Vector3Add(planet->center, (Vector3){
                (float)SpaceUnitsKilometersToGameDistance(relativePosition.x),
                (float)SpaceUnitsKilometersToGameDistance(relativePosition.y),
                (float)SpaceUnitsKilometersToGameDistance(relativePosition.z)
            });
            planet->satelliteVelocity = Vector3Add(planet->velocity, (Vector3){
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    relativeVelocity.x),
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    relativeVelocity.y),
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    relativeVelocity.z)
            });
        }
        planet->valid = isfinite(planet->currentIrradianceEarth) &&
                        planet->currentIrradianceEarth >= 0.0f;
        if (!planet->valid) return false;
    }
    out->valid = true;
    if (!SolarSystemRuntimeGeometryIsFinite(out)) {
        *out = (SolarSystemRuntimeState){ 0 };
        return false;
    }
    return true;
}

static bool SolarSystemEvaluateUncachedAtTime(
    const SolarSystemDef *sys, double simulationTime,
    SolarSystemRuntimeState *out)
{
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    return SolarSystemEvaluateSnapshotAtTime(sys, snapshot, simulationTime,
                                             out);
}

static void SolarSystemRuntimeToRelative(const SolarSystemDef *system,
                                         SolarSystemRuntimeState *runtime)
{
    if (!system || !runtime) return;
    for (int star = 0; star < runtime->stellarCount; star++) {
        runtime->stars[star].center = Vector3Subtract(
            runtime->stars[star].center, system->center);
    }
    for (int planet = 0; planet < runtime->planetCount; planet++) {
        if (!runtime->planets[planet].valid) continue;
        runtime->planets[planet].center = Vector3Subtract(
            runtime->planets[planet].center, system->center);
        if (runtime->planets[planet].satelliteOrbit.exists) {
            runtime->planets[planet].satelliteCenter = Vector3Subtract(
                runtime->planets[planet].satelliteCenter, system->center);
        }
    }
}

static void SolarSystemRuntimeProject(const SolarSystemDef *system,
                                      SolarSystemRuntimeState *runtime)
{
    if (!system || !runtime) return;
    for (int star = 0; star < runtime->stellarCount; star++) {
        runtime->stars[star].center = Vector3Add(
            runtime->stars[star].center, system->center);
    }
    for (int planet = 0; planet < runtime->planetCount; planet++) {
        if (!runtime->planets[planet].valid) continue;
        runtime->planets[planet].center = Vector3Add(
            runtime->planets[planet].center, system->center);
        if (runtime->planets[planet].satelliteOrbit.exists) {
            runtime->planets[planet].satelliteCenter = Vector3Add(
                runtime->planets[planet].satelliteCenter, system->center);
        }
    }
}

static uint64_t SolarSystemSignatureMix(uint64_t hash, uint64_t value)
{
    return (hash ^ value) * UINT64_C(0x100000001b3);
}

static uint64_t SolarSystemDoubleBits(double value)
{
    uint64_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t SolarSystemFloatBits(float value)
{
    uint32_t bits = 0;
    memcpy(&bits, &value, sizeof(bits));
    return bits;
}

static uint64_t SolarSystemRuntimeCacheSignature(
    const SolarSystemDef *system, const SolarSystemPhysicalSnapshot *snapshot)
{
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    hash = SolarSystemSignatureMix(hash, snapshot->stellarHash);
    hash = SolarSystemSignatureMix(hash,
                                   (uint32_t)snapshot->summary.stellarCount);
    for (int i = 0; i < snapshot->summary.stellarCount; i++) {
        const StellarProfile *star = &snapshot->stellarProfiles[i];
        hash = SolarSystemSignatureMix(hash, (uint32_t)star->spectrum);
        hash = SolarSystemSignatureMix(hash, (uint32_t)star->stage);
        hash = SolarSystemSignatureMix(hash, star->evolutionSeed);
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->initialMassSolar));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(star->massKg));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(star->radiusKm));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->massSolar));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->radiusSolar));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->temperatureK));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->luminositySolar));
        hash = SolarSystemSignatureMix(hash,
                                       SolarSystemFloatBits(star->ageGyr));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->mainSequenceLifetimeGyr));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(star->luminousLifetimeGyr));
    }
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.innerSeparationKm));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.outerSeparationKm));
    hash = SolarSystemSignatureMix(
        hash, (uint32_t)snapshot->stellarOrbit.motion);
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.innerEccentricity));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.outerEccentricity));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.innerPhaseRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.outerPhaseRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.innerArgumentPeriapsisRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.outerArgumentPeriapsisRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.innerInclinationRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(
                  snapshot->stellarOrbit.outerInclinationRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.innerNodeRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.outerNodeRad));
    for (int i = 0; i < snapshot->summary.stellarCount; i++) {
        const Vector3 offset =
            snapshot->stellarOrbit.freeFlightOffsetGame[i];
        const Vector3 velocity =
            snapshot->stellarOrbit.freeFlightVelocityGame[i];
        hash = SolarSystemSignatureMix(hash, SolarSystemFloatBits(offset.x));
        hash = SolarSystemSignatureMix(hash, SolarSystemFloatBits(offset.y));
        hash = SolarSystemSignatureMix(hash, SolarSystemFloatBits(offset.z));
        hash = SolarSystemSignatureMix(hash,
                                       SolarSystemFloatBits(velocity.x));
        hash = SolarSystemSignatureMix(hash,
                                       SolarSystemFloatBits(velocity.y));
        hash = SolarSystemSignatureMix(hash,
                                       SolarSystemFloatBits(velocity.z));
    }
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeOffsetGame.x));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeOffsetGame.y));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeOffsetGame.z));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeVelocityGame.x));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeVelocityGame.y));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemFloatBits(
                  snapshot->stellarOrbit.outerFreeVelocityGame.z));
    hash = SolarSystemSignatureMix(hash, (uint32_t)system->planetCount);
    for (int i = 0; i < system->planetCount; i++) {
        const SolarPlanetDef *planet = &system->planets[i];
        const SpaceKeplerOrbit *orbit = &snapshot->planetOrbits[i];
        hash = SolarSystemSignatureMix(
            hash, (uint32_t)snapshot->planetStatuses[i]);
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->semiMajorAxisKm));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->physicalRadiusKm));
        hash = SolarSystemSignatureMix(hash, planet->bodyId);
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->physicalMassKg));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->rotationPeriodSeconds));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(planet->axialTiltRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(planet->formationMassEarth));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(planet->spaceProxyRadius));
        hash = SolarSystemSignatureMix(hash, (uint32_t)planet->yOffset);
        hash = SolarSystemSignatureMix(hash, (uint32_t)planet->style);
        hash = SolarSystemSignatureMix(hash, planet->formationGasGiant);
        hash = SolarSystemSignatureMix(hash, planet->hasCanonicalOrbit);
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->orbitalEccentricity));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->orbitalInclinationRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(
                      planet->orbitalLongitudeAscendingNodeRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(
                      planet->orbitalArgumentPeriapsisRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(
                      planet->orbitalMeanAnomalyAtEpochRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->eccentricity));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->semiMajorAxisKm));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->centralMassKg));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->inclinationRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->longitudeAscendingNodeRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->argumentPeriapsisRad));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->meanAnomalyAtEpochRad));
    }
    hash = SolarSystemSignatureMix(hash, snapshot->satellitesBuilt);
    if (snapshot->satellitesBuilt) {
        hash = SolarSystemSignatureMix(
            hash, (uint32_t)snapshot->satelliteCount);
        for (int i = 0; i < snapshot->satelliteCount; i++) {
            const SpaceSatelliteOrbit *orbit =
                &snapshot->allSatelliteOrbits[i];
            hash = SolarSystemSignatureMix(
                hash, (uint32_t)snapshot->satelliteParentPlanetIndices[i]);
            hash = SolarSystemSignatureMix(hash, orbit->exists);
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->semiMajorAxisKm));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->eccentricity));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->inclinationRad));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(
                          orbit->longitudeAscendingNodeRad));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->argumentPeriapsisRad));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->meanAnomalyAtEpochRad));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->radiusKm));
            hash = SolarSystemSignatureMix(
                hash, SolarSystemDoubleBits(orbit->massKg));
        }
    }
    return hash;
}

static bool SolarSystemEvaluateCachedSnapshotAtTime(
    const SolarSystemDef *system,
    const SolarSystemPhysicalSnapshot *snapshot, double simulationTime,
    SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!system || !snapshot || !snapshot->valid ||
        !isfinite(simulationTime) ||
        !SpaceVectorIsFinite(system->center) || system->planetCount < 0 ||
        system->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }
    uint64_t systemSignature = SolarSystemRuntimeCacheSignature(
        system, snapshot);
    uint32_t worldSeed = WorldGetSeed();
    if (SpaceQueryRuntimeCacheGet(worldSeed, system->anchorX,
                                  system->anchorZ, systemSignature,
                                  simulationTime, out)) {
        if (!out->valid || !SolarSystemRuntimeGeometryIsFinite(out)) {
            *out = (SolarSystemRuntimeState){ 0 };
            return false;
        }
        SolarSystemRuntimeProject(system, out);
        if (!SolarSystemRuntimeGeometryIsFinite(out)) {
            *out = (SolarSystemRuntimeState){ 0 };
            return false;
        }
        return true;
    }

    SolarSystemRuntimeState computed;
    if (!SolarSystemEvaluateSnapshotAtTime(system, snapshot, simulationTime,
                                           &computed)) {
        return false;
    }
    SolarSystemRuntimeToRelative(system, &computed);
    SpaceQueryRuntimeCachePut(worldSeed, system->anchorX, system->anchorZ,
                              systemSignature, simulationTime, &computed);
    *out = computed;
    SolarSystemRuntimeProject(system, out);
    if (!out->valid || !SolarSystemRuntimeGeometryIsFinite(out)) {
        *out = (SolarSystemRuntimeState){ 0 };
        return false;
    }
    return true;
}

static bool SolarSystemEvaluateCachedAtTime(
    const SolarSystemDef *system, double simulationTime,
    SolarSystemRuntimeState *out)
{
    if (!system || !system->physicalSnapshot.valid) {
        return SolarSystemEvaluateUncachedAtTime(system, simulationTime, out);
    }
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(system, &scratch);
    return SolarSystemEvaluateCachedSnapshotAtTime(
        system, snapshot, simulationTime, out);
}

bool SolarSystemEvaluateAtTime(const SolarSystemDef *sys,
                               double simulationTime,
                               SolarSystemRuntimeState *out)
{
    return SolarSystemEvaluateCachedAtTime(sys, simulationTime, out);
}

bool SolarSystemEvaluateAtElapsedTime(const SolarSystemDef *sys,
                                      double elapsedTime,
                                      SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!sys || !isfinite(elapsedTime) || elapsedTime < 0.0) return false;

    SolarSystemPhysicalSnapshot evolved;
    double ageOffsetGyr = SpaceUnitsGameTimeToGigayears(elapsedTime);
    if (!SolarSystemPhysicalSnapshotEvolve(sys, ageOffsetGyr, &evolved)) {
        return false;
    }
    return SolarSystemEvaluateCachedSnapshotAtTime(
        sys, &evolved, SpacePeriodicSimulationTime(elapsedTime), out);
}

static bool SpaceRemnantQueryPositionIsFinite(Vector3 position)
{
    const float coordinateLimit = (float)(INT_MAX - 4096);
    return SpaceVectorIsFinite(position) &&
           fabsf(position.x) <= coordinateLimit &&
           fabsf(position.y) <= coordinateLimit &&
           fabsf(position.z) <= coordinateLimit;
}

bool SolarSystemRemnantEnvironmentAt(
    const SolarSystemRuntimeState *runtime, Vector3 position,
    SpaceRemnantEnvironment *out)
{
    if (!out) return false;
    *out = (SpaceRemnantEnvironment){
        .nearestShellDistanceGame = INFINITY
    };
    if (!runtime || !runtime->valid ||
        !SpaceRemnantQueryPositionIsFinite(position) ||
        runtime->stellarCount <= 0 ||
        runtime->stellarCount > MAX_SOLAR_LIGHTS) {
        *out = (SpaceRemnantEnvironment){ 0 };
        return false;
    }
    for (int i = 0; i < runtime->stellarCount; i++) {
        const SolarStellarBody *star = &runtime->stars[i];
        if (!SpaceRemnantStateIsValid(&star->remnant)) {
            *out = (SpaceRemnantEnvironment){ 0 };
            return false;
        }
        if (!star->remnant.active) continue;
        double distanceGame = Vector3Distance(star->center, position);
        float hazard = SpaceRemnantRadiationHazardAtDistance(
            &star->remnant, distanceGame);
        float ejecta = SpaceRemnantEjectaDensityAtDistance(
            &star->remnant, distanceGame);
        out->active = true;
        out->remnantCount++;
        out->radiationHazard = 1.0f -
            (1.0f - out->radiationHazard) * (1.0f - hazard);
        out->ejectaDensity = fmaxf(out->ejectaDensity, ejecta);
        float shellDistance = fabsf(
            (float)distanceGame - star->remnant.proxyShockRadiusGame);
        out->nearestShellDistanceGame = fminf(
            out->nearestShellDistanceGame, shellDistance);
    }
    if (!out->active) out->nearestShellDistanceGame = 0.0f;
    if (!SpaceRemnantEnvironmentIsValid(out)) {
        *out = (SpaceRemnantEnvironment){ 0 };
        return false;
    }
    return true;
}

bool SpaceRemnantEnvironmentAt(Vector3 position,
                               SpaceRemnantEnvironment *out)
{
    if (!out) return false;
    *out = (SpaceRemnantEnvironment){ 0 };
    if (!SpaceRemnantQueryPositionIsFinite(position)) return false;
    out->nearestShellDistanceGame = INFINITY;
    SpaceBodyInfo bodies[STAR_NAVIGATION_MAX_SYSTEMS];
    int count = SpaceBodiesNear(
        position, SPACE_REMNANT_MAX_PROXY_RADIUS_GAME + 5000.0f,
        bodies, STAR_NAVIGATION_MAX_SYSTEMS);
    for (int i = 0; i < count; i++) {
        if (!bodies[i].isStar || !bodies[i].remnant.active) continue;
        float hazard = SpaceRemnantRadiationHazardAtDistance(
            &bodies[i].remnant, bodies[i].dist);
        float ejecta = SpaceRemnantEjectaDensityAtDistance(
            &bodies[i].remnant, bodies[i].dist);
        out->active = true;
        out->remnantCount++;
        out->radiationHazard = 1.0f -
            (1.0f - out->radiationHazard) * (1.0f - hazard);
        out->ejectaDensity = fmaxf(out->ejectaDensity, ejecta);
        out->nearestShellDistanceGame = fminf(
            out->nearestShellDistanceGame,
            fabsf(bodies[i].dist - bodies[i].remnant.proxyShockRadiusGame));
    }
    if (!out->active) out->nearestShellDistanceGame = 0.0f;
    if (!SpaceRemnantEnvironmentIsValid(out)) {
        *out = (SpaceRemnantEnvironment){ 0 };
        return false;
    }
    return true;
}

int SolarSystemRuntimeLightSources(const SolarSystemRuntimeState *runtime,
                                   SolarLightSource *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int clearCount = maxCount < MAX_SOLAR_LIGHTS
        ? maxCount : MAX_SOLAR_LIGHTS;
    memset(out, 0, sizeof(*out) * (size_t)clearCount);
    if (!runtime || !runtime->valid ||
        !SolarSystemRuntimeGeometryIsFinite(runtime) ||
        runtime->stellarCount <= 0 ||
        runtime->stellarCount > MAX_SOLAR_LIGHTS ||
        runtime->stellarCount > maxCount) {
        return 0;
    }
    for (int i = 0; i < runtime->stellarCount; i++) {
        out[i] = (SolarLightSource){
            .center = runtime->stars[i].center,
            .stellar = runtime->stars[i].stellar,
            .spectrum = runtime->stars[i].spectrum,
            .spaceProxyRadius = runtime->stars[i].spaceProxyRadius,
            .luminosity = runtime->stars[i].luminosity,
            .primary = runtime->stars[i].primary
        };
    }
    return runtime->stellarCount;
}

bool SolarSystemApplyFormation(SolarSystemDef *sys, uint32_t seed)
{
    if (!sys) return false;
    SolarSystemDef formed = *sys;
    SolarSystemPhysicalSnapshot snapshot;
    if (!SolarSystemPhysicalSnapshotBuild(&formed, &snapshot)) return false;

    SpaceSystemFormation formation;
    SpaceSystemFormationInput input = {
        .seed = seed,
        .stellarMassSolar =
            (float)(snapshot.summary.totalMassKg /
                    SPACE_UNITS_SOLAR_MASS_KG),
        .stellarLuminositySolar = snapshot.summary.totalLuminositySolar,
        .stellarAgeGyr = snapshot.summary.ageGyr,
        .stellarCount = snapshot.summary.stellarCount,
        .innerStabilityLimitGame = snapshot.minimumPlanetOrbitGame,
        .outerLimitGame = 650.0f
    };
    if (!SpaceSystemFormationGenerate(&input, &formation)) return false;

    formed.formationMetallicity = formation.metallicity;
    formed.formationDiskMassEarth = formation.diskMassEarth;
    formed.snowLineKm = SpaceUnitsGameDistanceToKilometers(
        formation.snowLineGame);
    formed.habitableZoneInnerKm = SpaceUnitsGameDistanceToKilometers(
        formation.habitableInnerGame);
    formed.habitableZoneOuterKm = SpaceUnitsGameDistanceToKilometers(
        formation.habitableOuterGame);
    formed.planetCount = formation.planetCount;
    for (int index = 0; index < formation.planetCount; index++) {
        const SpaceSystemFormationPlanet *planet = &formation.planets[index];
        uint32_t planetHash = SolarSystemPlanetOrbitHash(&formed, index);
        float proxyRadius = planet->gasGiant
            ? 47.0f + (float)((planetHash >> 6) % 4u)
            : 40.0f + (float)((planetHash >> 6) % 9u);
        formed.planets[index] = (SolarPlanetDef){
            .bodyId = (uint32_t)(index + 1),
            .semiMajorAxisKm = SpaceUnitsGameDistanceToKilometers(
                planet->orbitGame),
            .physicalRadiusKm = (double)planet->radiusEarth *
                                SPACE_UNITS_EARTH_RADIUS_KM,
            .formationMassEarth = planet->massEarth,
            .spaceProxyRadius = proxyRadius,
            .yOffset = 0,
            .style = planet->gasGiant ? SOLAR_STYLE_GAS :
                                        SOLAR_STYLE_CRATER,
            .formationGasGiant = planet->gasGiant
        };
    }
    if (!SolarSystemPhysicalSnapshotBuild(
            &formed, &formed.physicalSnapshot)) {
        return false;
    }
    if (!SolarSystemPhysicalSnapshotBuildSatellites(
            &formed, &formed.physicalSnapshot)) {
        return false;
    }
    *sys = formed;
    return true;
}

int SolarSystemLightSources(const SolarSystemDef *sys, SolarLightSource *out,
                            int maxCount)
{
    if (!out || maxCount <= 0) return 0;
    int clearCount = maxCount < MAX_SOLAR_LIGHTS
        ? maxCount : MAX_SOLAR_LIGHTS;
    memset(out, 0, sizeof(*out) * (size_t)clearCount);
    SolarSystemRuntimeState runtime;
    return SolarSystemEvaluateAtElapsedTime(
        sys, SpaceElapsedSimulationTime(), &runtime)
        ? SolarSystemRuntimeLightSources(&runtime, out, maxCount) : 0;
}

float SolarLightIrradianceAt(const SolarLightSource *source, Vector3 point)
{
    if (!source || !SpaceVectorIsFinite(source->center) ||
        !SpaceVectorIsFinite(point) ||
        !(source->luminosity > 0.0f) || !isfinite(source->luminosity)) {
        return 0.0f;
    }
    double dx = (double)source->center.x - point.x;
    double dy = (double)source->center.y - point.y;
    double dz = (double)source->center.z - point.z;
    double distanceGameSquared = dx * dx + dy * dy + dz * dz;
    if (!isfinite(distanceGameSquared) || distanceGameSquared < 0.0) {
        return 0.0f;
    }
    double distanceKm = SpaceUnitsGameDistanceToKilometers(
        sqrt(distanceGameSquared));
    double minimumDistanceAu = SpaceUnitsGameDistanceToKilometers(1.0) /
                               SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    if (!isfinite(distanceKm) || !isfinite(minimumDistanceAu) ||
        !(minimumDistanceAu > 0.0)) {
        return 0.0f;
    }
    distanceKm = fmax(distanceKm,
                      minimumDistanceAu * SPACE_UNITS_ASTRONOMICAL_UNIT_KM);
    double irradiance = SpaceIlluminationIrradianceEarth(
        source->luminosity, distanceKm);
    if (!isfinite(irradiance) || irradiance < 0.0 || irradiance > FLT_MAX) {
        return 0.0f;
    }
    return (float)irradiance;
}

float SolarSystemIrradianceAt(const SolarLightSource *sources, int sourceCount,
                              Vector3 point)
{
    if (!sources || sourceCount <= 0 || sourceCount > MAX_SOLAR_LIGHTS ||
        !SpaceVectorIsFinite(point)) return 0.0f;
    double total = 0.0;
    for (int i = 0; i < sourceCount; i++) {
        float irradiance = SolarLightIrradianceAt(&sources[i], point);
        if (!isfinite(irradiance) || irradiance < 0.0f) return 0.0f;
        total += irradiance;
        if (!isfinite(total) || total > FLT_MAX) return 0.0f;
    }
    return (float)total;
}
