#include "space/space_internal.h"

bool SpacePointInSolarSystemBubble(int x, int z)
{
    int centerAx = FloorDivInt(SpaceLocalToGlobalX(x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ(z), STAR_SYSTEM_SPACING);
    for (int ax = centerAx - 1; ax <= centerAx + 1; ax++) {
        for (int az = centerAz - 1; az <= centerAz + 1; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            float dx = (float)x - sys.center.x;
            float dz = (float)z - sys.center.z;
            // Keep every stable orbit and its navigation corridor asteroid-free.
            float clearRadius = SOLAR_SYSTEM_QUERY_RADIUS;
            if (dx * dx + dz * dz <= clearRadius * clearRadius) return true;
        }
    }
    return false;
}

static int CompareSystemQueryCandidate(const SolarSystemDef *left,
                                       float leftDistance,
                                       const SolarSystemDef *right,
                                       float rightDistance)
{
    if (leftDistance < rightDistance) return -1;
    if (leftDistance > rightDistance) return 1;
    if (left->anchorX < right->anchorX) return -1;
    if (left->anchorX > right->anchorX) return 1;
    if (left->anchorZ < right->anchorZ) return -1;
    if (left->anchorZ > right->anchorZ) return 1;
    return 0;
}

static int CompareSpaceBodyQueryCandidate(const SpaceBodyInfo *left,
                                          const SpaceBodyInfo *right)
{
    if (left->dist < right->dist) return -1;
    if (left->dist > right->dist) return 1;
    if (left->systemAnchorX < right->systemAnchorX) return -1;
    if (left->systemAnchorX > right->systemAnchorX) return 1;
    if (left->systemAnchorZ < right->systemAnchorZ) return -1;
    if (left->systemAnchorZ > right->systemAnchorZ) return 1;
    if (left->isStar != right->isStar) return left->isStar ? -1 : 1;
    if (left->index < right->index) return -1;
    if (left->index > right->index) return 1;
    return 0;
}

bool SpaceQueryVectorIsFinite(Vector3 value)
{
    const float coordinateLimit = (float)(INT_MAX - 4096);
    return SpaceVectorIsFinite(value) &&
           fabsf(value.x) <= coordinateLimit &&
           fabsf(value.y) <= coordinateLimit &&
           fabsf(value.z) <= coordinateLimit;
}

static bool SpaceQueryRadiusAnchors(float maxDist, int *out)
{
    if (!out || !isfinite(maxDist) || maxDist < 0.0f ||
        maxDist > SPACE_MAX_SYSTEM_QUERY_DISTANCE) return false;
    double radius = (double)maxDist / (double)STAR_SYSTEM_SPACING;
    if (!isfinite(radius) || radius > (double)(INT_MAX - 2)) return false;
    *out = (int)radius + 1;
    return true;
}

int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount)
{
    int radiusAnchors = 0;
    if (!out || maxCount <= 0 || !SpaceQueryVectorIsFinite(pos) ||
        !SpaceQueryRadiusAnchors(maxDist, &radiusAnchors)) {
        return 0;
    }

    int centerAx = FloorDivInt(SpaceLocalToGlobalX((int)floorf(pos.x)),
                               STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ((int)floorf(pos.z)),
                               STAR_SYSTEM_SPACING);

    int storageLimit = maxCount;
    if (storageLimit > STAR_SYSTEM_QUERY_MAX) storageLimit = STAR_SYSTEM_QUERY_MAX;
    SolarSystemDef found[STAR_SYSTEM_QUERY_MAX];
    float dists[STAR_SYSTEM_QUERY_MAX];
    int foundCount = 0;

    for (int ax = centerAx - radiusAnchors; ax <= centerAx + radiusAnchors; ax++) {
        for (int az = centerAz - radiusAnchors; az <= centerAz + radiusAnchors; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;
            float dx = sys.center.x - pos.x;
            float dz = sys.center.z - pos.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d > maxDist) continue;
            if (foundCount < storageLimit) {
                found[foundCount] = sys;
                dists[foundCount] = d;
                foundCount++;
            } else {
                int farthest = 0;
                for (int i = 1; i < foundCount; i++) {
                    if (dists[i] > dists[farthest]) farthest = i;
                }
                if (CompareSystemQueryCandidate(
                        &sys, d, &found[farthest], dists[farthest]) < 0) {
                    found[farthest] = sys;
                    dists[farthest] = d;
                }
            }
        }
    }

    for (int i = 0; i < foundCount; i++) {
        int best = i;
        for (int j = i + 1; j < foundCount; j++) {
            if (CompareSystemQueryCandidate(
                    &found[j], dists[j], &found[best], dists[best]) < 0) {
                best = j;
            }
        }
        if (best != i) {
            SolarSystemDef tmpSys = found[i];
            found[i] = found[best];
            found[best] = tmpSys;
            float tmpDist = dists[i];
            dists[i] = dists[best];
            dists[best] = tmpDist;
        }
        out[i] = found[i];
    }
    return foundCount;
}

bool FindNearestSystem(Vector3 pos, float maxDist, SolarSystemDef *out, float *outDist)
{
    if (!out) return false;
    *out = (SolarSystemDef){ 0 };
    if (outDist) *outDist = 0.0f;
    if (!SpaceQueryVectorIsFinite(pos)) return false;
    SolarSystemDef sys;
    int count = StarSystemsNear(pos, maxDist, &sys, 1);
    if (count < 1) return false;
    *out = sys;
    if (outDist) {
        float dx = sys.center.x - pos.x;
        float dz = sys.center.z - pos.z;
        *outDist = sqrtf(dx * dx + dz * dz);
    }
    return true;
}

float PlanetEncounterRadiusGame(double semiMajorAxisKm,
                                double bodyMassKg,
                                double parentMassKg,
                                double physicalRadiusKm)
{
    if (!(semiMajorAxisKm > 0.0) || !(bodyMassKg > 0.0) ||
        !(parentMassKg > 0.0) || !(physicalRadiusKm > 0.0)) {
        return 0.0f;
    }
    float orbitRadiusGame = (float)SpaceUnitsKilometersToGameDistance(
        semiMajorAxisKm);
    float minimum = (float)SpaceUnitsKilometersToGameDistance(
        physicalRadiusKm * 4.0);
    float maximum = fmaxf(minimum,
                          fminf(orbitRadiusGame * 0.36f,
                                SPACE_MAX_PLANET_ENCOUNTER_RADIUS_GAME));
    float physical = (float)SpaceUnitsKilometersToGameDistance(
        SpaceUnitsLaplaceSphereOfInfluenceKm(
            semiMajorAxisKm, bodyMassKg, parentMassKg));
    return Clamp(physical, minimum, maximum);
}

static bool PlanetBodyInfoForRuntime(const SolarSystemDef *system,
                                     const SolarSystemRuntimeState *runtime,
                                     int index, Vector3 observer,
                                     SpaceBodyInfo *out)
{
    if (!system || !runtime || !runtime->valid || !out || index < 0 ||
        index >= system->planetCount || index >= MAX_SOLAR_PLANETS ||
        !runtime->planets[index].valid) {
        return false;
    }
    const SolarPlanetRuntimeState *planet = &runtime->planets[index];
    const PlanetProfile *profile = &planet->profile;
    Vector3 center = planet->center;
    double parentMassKg = runtime->totalStellarMassKg;
    float landingRadius = SolarBodyTerrainProxyRadius(profile->spaceProxyRadius);
    *out = (SpaceBodyInfo){
        .celestialPosition = planet->celestialPosition,
        .celestialVelocityKmPerSecond =
            planet->celestialVelocityKmPerSecond,
        .center = center,
        .velocity = planet->velocity,
        .physicalRadiusKm = profile->physicalRadiusKm,
        .physicalRadiusGame = (float)SpaceUnitsKilometersToGameDistance(
            profile->physicalRadiusKm),
        .semiMajorAxisKm = planet->semiMajorAxisKm,
        .parentMassKg = parentMassKg,
        .spaceProxyRadius = profile->spaceProxyRadius,
        .landingProxyRadius = landingRadius,
        .encounterRadiusGame = PlanetEncounterRadiusGame(
            planet->semiMajorAxisKm, profile->massKg,
            parentMassKg, profile->physicalRadiusKm),
        .currentIrradianceEarth = planet->currentIrradianceEarth,
        .dist = Vector3Distance(center, observer),
        .isStar = false,
        .bodyId = system->planets[index].bodyId,
        .index = index + 1,
        .systemAnchorX = system->anchorX,
        .systemAnchorZ = system->anchorZ,
        .worldSeed = profile->seed,
        .remnantEnvironment = planet->remnantEnvironment,
        .hostStar = runtime->stars[0].stellar,
        .spectrum = runtime->stars[0].spectrum,
        .style = profile->style,
        .profile = *profile
    };
    snprintf(out->name, sizeof(out->name), "%s",
             system->planets[index].name[0]
                 ? system->planets[index].name : system->name);
    return true;
}

static bool PlanetBodyInfoForSystem(const SolarSystemDef *system, int index,
                                    Vector3 observer, SpaceBodyInfo *out)
{
    SolarSystemRuntimeState runtime;
    return SolarSystemEvaluateAtElapsedTime(
               system, SpaceElapsedSimulationTime(), &runtime) &&
           PlanetBodyInfoForRuntime(system, &runtime, index, observer, out);
}

bool SpacePlanetBodyAt(int systemAnchorX, int systemAnchorZ, int planetIndex,
                       Vector3 observer, SpaceBodyInfo *out)
{
    if (!out) return false;
    *out = (SpaceBodyInfo){ 0 };
    if (!SpaceQueryVectorIsFinite(observer)) return false;

    SolarSystemDef system;
    if (!StarSystemAt(systemAnchorX, systemAnchorZ, &system) ||
        planetIndex < 0 || planetIndex >= system.planetCount) {
        return false;
    }
    return PlanetBodyInfoForSystem(&system, planetIndex, observer, out);
}

int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount)
{
    int radiusAnchors = 0;
    if (!out || maxCount <= 0 || !SpaceQueryVectorIsFinite(pos) ||
        !SpaceQueryRadiusAnchors(maxDist, &radiusAnchors)) {
        return 0;
    }
    int count = 0;
    int centerAx = FloorDivInt(SpaceLocalToGlobalX((int)floorf(pos.x)),
                               STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ((int)floorf(pos.z)),
                               STAR_SYSTEM_SPACING);

    for (int ax = centerAx - radiusAnchors; ax <= centerAx + radiusAnchors; ax++) {
        for (int az = centerAz - radiusAnchors; az <= centerAz + radiusAnchors; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            SolarSystemRuntimeState runtime;
            if (!SolarSystemEvaluateAtElapsedTime(
                    &sys, SpaceElapsedSimulationTime(), &runtime)) {
                continue;
            }
            int starCount = runtime.stellarCount;
            double parentMassKg = runtime.totalStellarMassKg;
            for (int starIndex = 0; starIndex < starCount; starIndex++) {
                float starDist = Vector3Distance(
                    runtime.stars[starIndex].center, pos);
                if (starDist > maxDist) continue;
                SpaceBodyInfo body = {
                    .celestialPosition =
                        runtime.stars[starIndex].celestialPosition,
                    .celestialVelocityKmPerSecond =
                        runtime.stars[starIndex].celestialVelocityKmPerSecond,
                    .center = runtime.stars[starIndex].center,
                    .velocity = runtime.stars[starIndex].velocity,
                    .physicalRadiusKm = runtime.stars[starIndex].stellar.radiusKm,
                    .physicalRadiusGame =
                        (float)SpaceUnitsKilometersToGameDistance(
                            runtime.stars[starIndex].stellar.radiusKm),
                    .parentMassKg = parentMassKg,
                    .spaceProxyRadius = runtime.stars[starIndex].spaceProxyRadius,
                    .landingProxyRadius = runtime.stars[starIndex].spaceProxyRadius,
                    .encounterRadiusGame = SPACE_STAR_ENCOUNTER_RADIUS_GAME,
                    .dist = starDist,
                    .isStar = true,
                    .index = runtime.stars[starIndex].index,
                    .systemAnchorX = ax,
                    .systemAnchorZ = az,
                    .hostStar = runtime.stars[starIndex].stellar,
                    .spectrum = runtime.stars[starIndex].spectrum,
                    .remnant = runtime.stars[starIndex].remnant
                };
                if (starIndex == 0) {
                    snprintf(body.name, sizeof(body.name), "%s",
                             sys.name);
                } else {
                    snprintf(body.name, sizeof(body.name),
                             "%.*s %c", (int)sizeof(body.name) - 3,
                             sys.name, 'A' + starIndex);
                }
                if (count < maxCount) {
                    out[count++] = body;
                } else {
                    int worst = 0;
                    for (int i = 1; i < count; i++) {
                        if (CompareSpaceBodyQueryCandidate(
                                &out[worst], &out[i]) < 0) {
                            worst = i;
                        }
                    }
                    if (CompareSpaceBodyQueryCandidate(&body, &out[worst]) < 0) {
                        out[worst] = body;
                    }
                }
            }

            for (int i = 0; i < sys.planetCount; i++) {
                SpaceBodyInfo body;
                if (!PlanetBodyInfoForRuntime(&sys, &runtime, i, pos, &body) ||
                    body.dist > maxDist) continue;
                if (count < maxCount) {
                    out[count++] = body;
                } else {
                    int worst = 0;
                    for (int candidate = 1; candidate < count; candidate++) {
                        if (CompareSpaceBodyQueryCandidate(
                                &out[worst], &out[candidate]) < 0) {
                            worst = candidate;
                        }
                    }
                    if (CompareSpaceBodyQueryCandidate(&body, &out[worst]) < 0) {
                        out[worst] = body;
                    }
                }
            }
        }
    }

    for (int i = 0; i < count; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++) {
            if (CompareSpaceBodyQueryCandidate(&out[j], &out[best]) < 0) {
                best = j;
            }
        }
        if (best != i) {
            SpaceBodyInfo tmp = out[i];
            out[i] = out[best];
            out[best] = tmp;
        }
    }
    return count;
}

static int CompareSpaceSatelliteQueryCandidate(
    const SpaceSatelliteInfo *left, const SpaceSatelliteInfo *right)
{
    if (left->dist < right->dist) return -1;
    if (left->dist > right->dist) return 1;
    if (left->systemAnchorX < right->systemAnchorX) return -1;
    if (left->systemAnchorX > right->systemAnchorX) return 1;
    if (left->systemAnchorZ < right->systemAnchorZ) return -1;
    if (left->systemAnchorZ > right->systemAnchorZ) return 1;
    if (left->parentPlanetIndex < right->parentPlanetIndex) return -1;
    if (left->parentPlanetIndex > right->parentPlanetIndex) return 1;
    return 0;
}

static void InsertSpaceSatelliteQueryCandidate(
    SpaceSatelliteInfo candidate, SpaceSatelliteInfo *out, int *count,
    int maxCount)
{
    if (*count < maxCount) {
        out[(*count)++] = candidate;
        return;
    }
    int worst = 0;
    for (int i = 1; i < *count; i++) {
        if (CompareSpaceSatelliteQueryCandidate(&out[worst], &out[i]) < 0) {
            worst = i;
        }
    }
    if (CompareSpaceSatelliteQueryCandidate(&candidate, &out[worst]) < 0) {
        out[worst] = candidate;
    }
}

int SpaceSatellitesNear(Vector3 pos, float maxDist,
                        SpaceSatelliteInfo *out, int maxCount)
{
    if (!out || maxCount <= 0 || !SpaceQueryVectorIsFinite(pos) ||
        !isfinite(maxDist) || maxDist < 0.0f) {
        return 0;
    }
    SolarSystemDef systems[STAR_NAVIGATION_MAX_SYSTEMS];
    float systemRange = maxDist + 800.0f;
    int systemCount = StarSystemsNear(pos, systemRange, systems,
                                       STAR_NAVIGATION_MAX_SYSTEMS);
    int count = 0;
    for (int systemIndex = 0; systemIndex < systemCount; systemIndex++) {
        SolarSystemDef *system = &systems[systemIndex];
        SolarSystemRuntimeState runtime;
        if (!SolarSystemEvaluateAtElapsedTime(
                system, SpaceElapsedSimulationTime(), &runtime)) {
            continue;
        }
        SolarSystemPhysicalSnapshot scratch;
        const SolarSystemPhysicalSnapshot *snapshot =
            SolarSystemPhysicalSnapshotForSystem(system, &scratch);
        if (!snapshot) continue;
        if (!snapshot->satellitesBuilt) {
            scratch = *snapshot;
            if (!SolarSystemPhysicalSnapshotBuildSatellites(system, &scratch)) {
                continue;
            }
            snapshot = &scratch;
        }
        for (int satelliteIndex = 0;
             satelliteIndex < snapshot->satelliteCount; satelliteIndex++) {
            int planetIndex =
                snapshot->satelliteParentPlanetIndices[satelliteIndex];
            if (planetIndex < 0 || planetIndex >= runtime.planetCount) continue;
            const SolarPlanetRuntimeState *planet = &runtime.planets[planetIndex];
            if (!planet->valid) continue;
            SpaceSatelliteOrbit orbit =
                snapshot->allSatelliteOrbits[satelliteIndex];
            SpaceSatelliteState state;
            if (!SpaceSatelliteStateAtSeconds(
                    &orbit, planet->profile.massKg,
                    SpaceUnitsGameTimeToSeconds(runtime.simulationTime),
                    &state)) continue;
            Vector3 center = Vector3Add(planet->center, (Vector3){
                (float)SpaceUnitsKilometersToGameDistance(state.positionKm.x),
                (float)SpaceUnitsKilometersToGameDistance(state.positionKm.y),
                (float)SpaceUnitsKilometersToGameDistance(state.positionKm.z)
            });
            Vector3 velocity = Vector3Add(planet->velocity, (Vector3){
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    state.velocityKmPerSecond.x),
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    state.velocityKmPerSecond.y),
                (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                    state.velocityKmPerSecond.z)
            });
            float distance = Vector3Distance(center, pos);
            if (distance > maxDist) continue;
            double hillSphereKm = SpaceUnitsHillSphereKm(
                orbit.semiMajorAxisKm, orbit.massKg, planet->profile.massKg);
            double physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
                orbit.radiusKm);
            float minimumEncounter = (float)(physicalRadiusGame * 2.20);
            float encounter = fmaxf(
                minimumEncounter,
                fminf((float)SpaceUnitsKilometersToGameDistance(
                          hillSphereKm * 0.50), 24.0f));
            SpaceSatelliteInfo candidate = {
                .center = center,
                .velocity = velocity,
                .physicalRadiusKm = orbit.radiusKm,
                .massKg = orbit.massKg,
                .semiMajorAxisKm = orbit.semiMajorAxisKm,
                .encounterRadiusGame = encounter,
                .dist = distance,
                .isSatellite = true,
                .index = satelliteIndex,
                .parentPlanetIndex = planetIndex,
                .systemAnchorX = system->anchorX,
                .systemAnchorZ = system->anchorZ,
                .worldSeed = planet->profile.seed ^
                             (uint32_t)(satelliteIndex + 1) * 0x9e3779b9u,
                .orbit = orbit,
                .state = state
            };
            if (system->satelliteCount > satelliteIndex) {
                candidate.bodyId = system->satellites[satelliteIndex].bodyId;
                snprintf(candidate.name, sizeof(candidate.name), "%s",
                         system->satellites[satelliteIndex].name);
            } else {
                snprintf(candidate.name, sizeof(candidate.name), "%s Moon %c",
                         system->name, 'a' + planetIndex);
            }
            InsertSpaceSatelliteQueryCandidate(candidate, out, &count,
                                                maxCount);
        }
    }
    for (int i = 0; i < count; i++) {
        int best = i;
        for (int j = i + 1; j < count; j++) {
            if (CompareSpaceSatelliteQueryCandidate(&out[j], &out[best]) < 0) {
                best = j;
            }
        }
        if (best != i) {
            SpaceSatelliteInfo temporary = out[i];
            out[i] = out[best];
            out[best] = temporary;
        }
    }
    return count;
}

bool SpaceSatelliteScaleDiagnosticsAt(
    Vector3 observer, SpaceSatelliteScaleDiagnostics *out)
{
    if (!out) return false;
    *out = (SpaceSatelliteScaleDiagnostics){ 0 };
    if (!SpaceQueryVectorIsFinite(observer)) return false;
    SpaceSatelliteInfo satellite;
    if (SpaceSatellitesNear(observer, SOLAR_SYSTEM_QUERY_RADIUS,
                            &satellite, 1) != 1) {
        return false;
    }
    double physicalGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        satellite.massKg, satellite.physicalRadiusKm);
    double orbitalSpeed = sqrt(
        satellite.state.velocityKmPerSecond.x *
            satellite.state.velocityKmPerSecond.x +
        satellite.state.velocityKmPerSecond.y *
            satellite.state.velocityKmPerSecond.y +
        satellite.state.velocityKmPerSecond.z *
            satellite.state.velocityKmPerSecond.z);
    SolarSystemDef system;
    if (!StarSystemAt(satellite.systemAnchorX, satellite.systemAnchorZ,
                      &system)) {
        return false;
    }
    SolarSystemRuntimeState runtime;
    if (!SolarSystemEvaluateAtElapsedTime(
            &system, SpaceElapsedSimulationTime(), &runtime) ||
        satellite.parentPlanetIndex < 0 ||
        satellite.parentPlanetIndex >= runtime.planetCount) {
        return false;
    }
    const PlanetProfile *parent =
        &runtime.planets[satellite.parentPlanetIndex].profile;
    double stableHillSphereKm = SpaceUnitsHillSphereKm(
        satellite.semiMajorAxisKm, satellite.massKg, parent->massKg);
    double sphereOfInfluenceKm = SpaceUnitsLaplaceSphereOfInfluenceKm(
        satellite.semiMajorAxisKm, satellite.massKg, parent->massKg);
    out->center = satellite.center;
    out->velocity = satellite.velocity;
    snprintf(out->bodyName, sizeof(out->bodyName), "%s", satellite.name);
    out->physicalRadiusKm = satellite.physicalRadiusKm;
    out->physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
        satellite.physicalRadiusKm);
    out->physicalGravityMetersPerSecondSquared = physicalGravity * 1000.0;
    out->orbitalSpeedKilometersPerSecond = orbitalSpeed;
    out->sphereOfInfluenceKm = sphereOfInfluenceKm;
    out->hillSphereKm = stableHillSphereKm;
    out->encounterRadiusGame = satellite.encounterRadiusGame;
    out->distanceGame = satellite.dist;
    if (!SpaceVectorIsFinite(out->center) ||
        !SpaceVectorIsFinite(out->velocity) ||
        !(out->physicalRadiusKm > 0.0) ||
        !isfinite(out->physicalRadiusKm) ||
        !(out->physicalRadiusGame > 0.0) ||
        !isfinite(out->physicalRadiusGame) ||
        !(out->physicalGravityMetersPerSecondSquared > 0.0) ||
        !isfinite(out->physicalGravityMetersPerSecondSquared) ||
        (out->orbitalSpeedKilometersPerSecond < 0.0) ||
        !isfinite(out->orbitalSpeedKilometersPerSecond) ||
        !(out->sphereOfInfluenceKm > 0.0) ||
        !isfinite(out->sphereOfInfluenceKm) ||
        !(out->hillSphereKm > 0.0) ||
        !isfinite(out->hillSphereKm) ||
        !(out->encounterRadiusGame > 0.0f) ||
        !isfinite(out->encounterRadiusGame) ||
        (out->distanceGame < 0.0f) || !isfinite(out->distanceGame)) {
        *out = (SpaceSatelliteScaleDiagnostics){ 0 };
        return false;
    }
    out->withinErrorBudget = out->encounterRadiusGame >=
                             (float)(out->physicalRadiusGame * 2.19);
    return true;
}

static void HomeBodyInfoForObserver(Vector3 observer, SpaceBodyInfo *out)
{
    if (!out) return;
    float radius = HomeWorldProxyRadius();
    *out = (SpaceBodyInfo){
        .center = HomeWorldCenter(),
        .velocity = { 0 },
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
        .physicalRadiusGame = (float)SpaceUnitsKilometersToGameDistance(
            SPACE_UNITS_EARTH_RADIUS_KM),
        .semiMajorAxisKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
        .parentMassKg = SPACE_UNITS_SOLAR_MASS_KG,
        .spaceProxyRadius = radius,
        .landingProxyRadius = radius,
        .encounterRadiusGame = radius * 2.15f,
        .currentIrradianceEarth = 1.0,
        .dist = Vector3Distance(HomeWorldCenter(), observer),
        .isStar = false,
        .index = 0,
        .worldSeed = DEFAULT_WORLD_SEED,
        .style = SOLAR_STYLE_TEMPERATE,
        .profile = {
            .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
            .massKg = SPACE_UNITS_EARTH_MASS_KG,
            .receivedIrradiance = 1.0,
            .radiativeTempK = 255.0f,
            .equilibriumTempK = 288.0f,
            .surfacePressureAtm = 1.0f,
            .atmosphereDensity = 0.78f,
            .oceanCoverage = 0.48f,
            .cloudCoverage = 0.58f,
            .windStrength = 0.42f,
            .hasSolidSurface = true
        }
    };
    double orbitVelocity = SpaceUnitsCircularOrbitVelocityKilometersPerSecond(
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_SOLAR_MASS_KG);
    out->velocity.x = (float)SpaceUnitsKilometersPerSecondToGameVelocity(
        orbitVelocity);
    snprintf(out->name, sizeof(out->name), "Home");
}

static bool SpaceScaleDiagnosticsIsFinite(
    const SpaceScaleDiagnostics *diagnostics)
{
    if (!diagnostics || !isfinite(diagnostics->physicalRadiusKm) ||
        !isfinite(diagnostics->physicalRadiusGame) ||
        !isfinite(diagnostics->visualRadiusGame) ||
        !isfinite(diagnostics->landingRadiusGame) ||
        !isfinite(diagnostics->landingRadiusScale) ||
        !isfinite(diagnostics->physicalGravityMetersPerSecondSquared) ||
        !isfinite(diagnostics->physicalGravityEarth) ||
        !isfinite(diagnostics->gameplaySurfaceGravity) ||
        !isfinite(diagnostics->orbitalSpeedKilometersPerSecond) ||
        !isfinite(diagnostics->orbitalSpeedGame) ||
        !isfinite(diagnostics->sphereOfInfluenceKm) ||
        !isfinite(diagnostics->hillSphereKm) ||
        !isfinite(diagnostics->physicalSphereOfInfluenceGame) ||
        !isfinite(diagnostics->encounterRadiusGame) ||
        !isfinite(diagnostics->encounterRadiusScale) ||
        !isfinite(diagnostics->currentIrradianceEarth) ||
        !isfinite(diagnostics->climateIrradianceEarth) ||
        !isfinite(diagnostics->radiativeTemperatureK) ||
        !isfinite(diagnostics->surfaceTemperatureK) ||
        !isfinite(diagnostics->maxRelativeError)) {
        return false;
    }
    return diagnostics->physicalRadiusKm > 0.0 &&
           diagnostics->physicalRadiusGame > 0.0 &&
           diagnostics->visualRadiusGame > 0.0f &&
           diagnostics->landingRadiusGame > 0.0f &&
           diagnostics->landingRadiusScale > 0.0 &&
           diagnostics->physicalGravityMetersPerSecondSquared >= 0.0 &&
           diagnostics->physicalGravityEarth >= 0.0 &&
           diagnostics->gameplaySurfaceGravity >= 0.0 &&
           diagnostics->orbitalSpeedKilometersPerSecond >= 0.0 &&
           diagnostics->orbitalSpeedGame >= 0.0 &&
           diagnostics->sphereOfInfluenceKm > 0.0 &&
           diagnostics->hillSphereKm > 0.0 &&
           diagnostics->physicalSphereOfInfluenceGame > 0.0 &&
           diagnostics->encounterRadiusGame > 0.0f &&
           diagnostics->encounterRadiusScale > 0.0 &&
           diagnostics->currentIrradianceEarth >= 0.0 &&
           diagnostics->climateIrradianceEarth > 0.0 &&
           diagnostics->radiativeTemperatureK > 0.0f &&
           diagnostics->surfaceTemperatureK > 0.0f &&
           diagnostics->maxRelativeError >= 0.0;
}

bool SpaceBodyScaleDiagnostics(const SpaceBodyInfo *body,
                               SpaceScaleDiagnostics *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!body || body->isStar ||
        !SpaceVectorIsFinite(body->center) ||
        !SpaceVectorIsFinite(body->velocity) ||
        !(body->physicalRadiusKm > 0.0) ||
        !isfinite(body->physicalRadiusKm) ||
        !(body->semiMajorAxisKm > 0.0) ||
        !isfinite(body->semiMajorAxisKm) ||
        !(body->profile.massKg > 0.0) ||
        !isfinite(body->profile.massKg) ||
        !(body->profile.physicalRadiusKm > 0.0) ||
        !isfinite(body->profile.physicalRadiusKm) ||
        !(body->profile.receivedIrradiance > 0.0) ||
        !isfinite(body->profile.receivedIrradiance) ||
        !(body->profile.radiativeTempK > 0.0f) ||
        !isfinite(body->profile.radiativeTempK) ||
        !(body->profile.equilibriumTempK > 0.0f) ||
        !isfinite(body->profile.equilibriumTempK) ||
        !(body->spaceProxyRadius > 0.0f) ||
        !isfinite(body->spaceProxyRadius) ||
        (body->landingProxyRadius < 0.0f) ||
        !isfinite(body->landingProxyRadius) ||
        (body->encounterRadiusGame < 0.0f) ||
        !isfinite(body->encounterRadiusGame) ||
        (body->currentIrradianceEarth < 0.0) ||
        !isfinite(body->currentIrradianceEarth) ||
        !SpaceRemnantEnvironmentIsValid(&body->remnantEnvironment)) {
        return false;
    }
    double parentMassKg = body->parentMassKg > 0.0
        ? body->parentMassKg : body->hostStar.massKg;
    if (!(parentMassKg > 0.0) || !isfinite(parentMassKg)) return false;
    if (body->index > 0) {
        snprintf(out->bodyName, sizeof(out->bodyName), "%s", body->name);
    } else {
        snprintf(out->bodyName, sizeof(out->bodyName), "%s", body->name);
    }
    out->physicalRadiusKm = body->physicalRadiusKm;
    out->physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
        body->physicalRadiusKm);
    out->visualRadiusGame = body->physicalRadiusGame > 0.0f
        ? body->physicalRadiusGame
        : (float)out->physicalRadiusGame;
    out->landingRadiusGame = body->landingProxyRadius > 0.0f
        ? body->landingProxyRadius
        : SolarBodyTerrainProxyRadius(body->spaceProxyRadius);
    out->landingRadiusScale = SpaceUnitsProxyRadiusScale(
        body->physicalRadiusKm, out->landingRadiusGame);

    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    double physicalGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        body->profile.massKg, body->physicalRadiusKm);
    double proxyMu = SpaceUnitsProxyGravitationalParameterGame(
        body->profile.massKg, body->physicalRadiusKm,
        out->landingRadiusGame);
    out->physicalGravityMetersPerSecondSquared = physicalGravity * 1000.0;
    out->physicalGravityEarth = earthGravity > 0.0
        ? physicalGravity / earthGravity : 0.0;
    out->gameplaySurfaceGravity = out->landingRadiusGame > 0.0f
        ? proxyMu / ((double)out->landingRadiusGame * out->landingRadiusGame)
        : 0.0;

    out->orbitalSpeedGame = sqrt((double)body->velocity.x * body->velocity.x +
                                 (double)body->velocity.y * body->velocity.y +
                                 (double)body->velocity.z * body->velocity.z);
    out->orbitalSpeedKilometersPerSecond =
        SpaceUnitsGameVelocityToKilometersPerSecond(out->orbitalSpeedGame);
    out->sphereOfInfluenceKm = SpaceUnitsLaplaceSphereOfInfluenceKm(
        body->semiMajorAxisKm, body->profile.massKg, parentMassKg);
    out->hillSphereKm = SpaceUnitsHillSphereKm(
        body->semiMajorAxisKm, body->profile.massKg, parentMassKg);
    out->physicalSphereOfInfluenceGame = SpaceUnitsKilometersToGameDistance(
        out->sphereOfInfluenceKm);
    out->encounterRadiusGame = body->encounterRadiusGame > 0.0f
        ? body->encounterRadiusGame
        : PlanetEncounterRadiusGame(body->semiMajorAxisKm,
                                     body->profile.massKg, parentMassKg,
                                     body->physicalRadiusKm);
    out->encounterRadiusScale = out->physicalSphereOfInfluenceGame > 0.0
        ? out->encounterRadiusGame / out->physicalSphereOfInfluenceGame : 0.0;
    out->currentIrradianceEarth = body->currentIrradianceEarth > 0.0
        ? body->currentIrradianceEarth : body->profile.receivedIrradiance;
    out->climateIrradianceEarth = body->profile.receivedIrradiance;
    out->radiativeTemperatureK = body->profile.radiativeTempK;
    out->surfaceTemperatureK = body->profile.equilibriumTempK;

    double radiusRoundTrip = SpaceUnitsRelativeError(
        SpaceUnitsGameDistanceToKilometers(out->physicalRadiusGame),
        out->physicalRadiusKm);
    double proxyGravityRoundTrip = SpaceUnitsRelativeError(
        out->gameplaySurfaceGravity,
        SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME *
        out->physicalGravityEarth);
    double speedRoundTrip = SpaceUnitsRelativeError(
        SpaceUnitsKilometersPerSecondToGameVelocity(
            out->orbitalSpeedKilometersPerSecond), out->orbitalSpeedGame);
    double soiRoundTrip = SpaceUnitsRelativeError(
        SpaceUnitsGameDistanceToKilometers(out->physicalSphereOfInfluenceGame),
        out->sphereOfInfluenceKm);
    out->maxRelativeError = fmax(fmax(radiusRoundTrip, proxyGravityRoundTrip),
                                 fmax(speedRoundTrip, soiRoundTrip));
    out->encounterRadiusClamped =
        fabs(out->encounterRadiusGame - out->physicalSphereOfInfluenceGame) >
        SPACE_UNITS_MAX_RELATIVE_ERROR *
        fmax(fabs(out->physicalSphereOfInfluenceGame), 1.0);
    out->withinErrorBudget = out->maxRelativeError <=
                             SPACE_UNITS_MAX_RELATIVE_ERROR;
    if (!SpaceScaleDiagnosticsIsFinite(out)) {
        memset(out, 0, sizeof(*out));
        return false;
    }
    return true;
}

bool SpaceScaleDiagnosticsAt(Vector3 observer, SpaceScaleDiagnostics *out)
{
    if (!out) return false;
    *out = (SpaceScaleDiagnostics){ 0 };
    if (!SpaceQueryVectorIsFinite(observer)) return false;

    SpaceBodyInfo selected = { 0 };
    bool found = false;
    if (PlanetWorldIsActive()) {
        SolarSystemDef system;
        int index = planetWorld.planetIndex - 1;
        if (SurfaceHostSystem(&system) &&
            PlanetBodyInfoForSystem(&system, index,
                                    PlanetWorldSpaceReference(), &selected)) {
            found = true;
        }
    } else if (HomeWorldSurfaceIsActive()) {
        HomeBodyInfoForObserver(observer, &selected);
        found = true;
    } else {
        SpaceBodyInfo bodies[48];
        int count = SpaceBodiesNear(observer, SOLAR_SYSTEM_QUERY_RADIUS,
                                    bodies, 48);
        for (int i = 0; i < count; i++) {
            if (bodies[i].isStar) continue;
            if (!found || bodies[i].dist < selected.dist) {
                selected = bodies[i];
                found = true;
            }
        }
        SpaceBodyInfo home;
        HomeBodyInfoForObserver(observer, &home);
        if (!found || home.dist < selected.dist) {
            selected = home;
            found = home.dist <= SOLAR_SYSTEM_QUERY_RADIUS;
        }
    }
    return found && SpaceBodyScaleDiagnostics(&selected, out);
}

#define SPACE_PLANET_NAVIGATION_ASSIST_ANGLE (4.0f * DEG2RAD)

bool SpacePlanetNavigationPick(Vector3 origin, Vector3 direction,
                               SpaceBodyInfo *out)
{
    if (!out) return false;
    *out = (SpaceBodyInfo){ 0 };
    if (planetWorld.active || !SpaceQueryVectorIsFinite(origin) ||
        !SpaceQueryVectorIsFinite(direction) ||
        Vector3LengthSqr(direction) < 0.000001f) {
        return false;
    }
    direction = Vector3Normalize(direction);

    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(origin, SOLAR_SYSTEM_QUERY_RADIUS, bodies, 48);
    float bestAlignment = -1.0f;
    float bestDistance = INFINITY;
    bool found = false;
    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        Vector3 toBody = Vector3Subtract(bodies[i].center, origin);
        float distance = Vector3Length(toBody);
        if (!(distance > 0.0f)) continue;
        float alignment = Vector3DotProduct(
            Vector3Scale(toBody, 1.0f / distance), direction);
        float radiusRatio = Clamp(bodies[i].physicalRadiusGame / distance,
                                  0.0f, 1.0f);
        float apparentRadius = asinf(radiusRatio);
        float selectionAngle = fmaxf(SPACE_PLANET_NAVIGATION_ASSIST_ANGLE,
                                     apparentRadius);
        if (alignment < cosf(selectionAngle)) continue;
        if (alignment < bestAlignment - 0.000001f ||
            (fabsf(alignment - bestAlignment) <= 0.000001f &&
             distance >= bestDistance)) {
            continue;
        }
        bestAlignment = alignment;
        bestDistance = distance;
        *out = bodies[i];
        found = true;
    }
    return found;
}

bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out)
{
    if (!out) return false;
    *out = (SpaceBodyInfo){ 0 };
    if (!SpaceQueryVectorIsFinite(origin) ||
        !SpaceQueryVectorIsFinite(direction) ||
        Vector3LengthSqr(direction) < 0.000001f) return false;
    direction = Vector3Normalize(direction);

    float best = 1e30f;
    bool found = false;

    if (!planetWorld.active) {
        SpaceBodyInfo bodies[48];
        int count = SpaceBodiesNear(origin, SOLAR_SYSTEM_QUERY_RADIUS,
                                    bodies, 48);
        for (int i = 0; i < count; i++) {
            Vector3 to = Vector3Subtract(bodies[i].center, origin);
            float proj = Vector3DotProduct(to, direction);
            if (proj < 0.0f || proj > best) continue;
            Vector3 closest = Vector3Add(origin, Vector3Scale(direction, proj));
            Vector3 diff = Vector3Subtract(closest, bodies[i].center);
            float lateral = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
            float markerRadius = bodies[i].dist * tanf(0.45f * DEG2RAD);
            float radius = fmaxf(bodies[i].physicalRadiusGame,
                                 markerRadius);
            if (lateral > radius) continue;
            best = proj;
            *out = bodies[i];
            found = true;
        }
    }
    if (found) return true;

    Vector3 starOrigin = planetWorld.active ? PlanetWorldSpaceReference() : origin;
    Vector3 starDirection = planetWorld.active ? PlanetWorldSpaceDirection(direction) : direction;
    SolarSystemDef systems[STAR_NAVIGATION_MAX_SYSTEMS];
    int systemCount = StarSystemsNear(starOrigin, STAR_NAVIGATION_RANGE, systems,
                                      STAR_NAVIGATION_MAX_SYSTEMS);
    SolarSystemDef host = { 0 };
    bool skipHost = (HomeWorldSurfaceIsActive() || planetWorld.active) &&
                    SurfaceHostSystem(&host);
    float bestAlignment = cosf(0.9f * DEG2RAD);
    int bestSystem = -1;

    for (int i = 0; i < systemCount; i++) {
        if (skipHost && systems[i].anchorX == host.anchorX &&
            systems[i].anchorZ == host.anchorZ) {
            continue;
        }
        Vector3 toStar = Vector3Subtract(systems[i].center, starOrigin);
        float distance = Vector3Length(toStar);
        if (distance < 0.01f) continue;
        Vector3 apparentDirection = SolarSystemApparentDirection(&systems[i], starOrigin);
        float alignment = Vector3DotProduct(apparentDirection, starDirection);
        if (alignment <= bestAlignment) continue;
        bestAlignment = alignment;
        bestSystem = i;
    }

    if (bestSystem < 0) return false;
    const SolarSystemDef *system = &systems[bestSystem];
    SolarSystemRuntimeState runtime;
    if (!SolarSystemEvaluateAtElapsedTime(
            system, SpaceElapsedSimulationTime(), &runtime) ||
        runtime.stellarCount <= 0) {
        return false;
    }
    const SolarStellarBody *primary = &runtime.stars[0];
    *out = (SpaceBodyInfo){
        .center = primary->center,
        .velocity = primary->velocity,
        .physicalRadiusKm = primary->stellar.radiusKm,
        .physicalRadiusGame = (float)SpaceUnitsKilometersToGameDistance(
            primary->stellar.radiusKm),
        .parentMassKg = runtime.totalStellarMassKg,
        .spaceProxyRadius = primary->spaceProxyRadius,
        .landingProxyRadius = primary->spaceProxyRadius,
        .encounterRadiusGame = SPACE_STAR_ENCOUNTER_RADIUS_GAME,
        .dist = Vector3Distance(starOrigin, primary->center),
        .isStar = true,
        .index = 0,
        .systemAnchorX = system->anchorX,
        .systemAnchorZ = system->anchorZ,
        .hostStar = primary->stellar,
        .spectrum = primary->spectrum,
        .style = SOLAR_STYLE_SUN
    };
    snprintf(out->name, sizeof(out->name), "%s", system->name);
    return true;
}

bool PlanetSurfaceAt(Vector3 position, Vector3 *gravityDir, float *surfaceDist,
                     float *gravityScale)
{
    if (!gravityDir || !surfaceDist) return false;
    *gravityDir = (Vector3){ 0 };
    *surfaceDist = 0.0f;
    if (gravityScale) *gravityScale = 0.0f;
    if (!SpaceQueryVectorIsFinite(position)) return false;

    SpaceBodyInfo bodies[8];
    int count = SpaceBodiesNear(position, 96.0f, bodies, 8);

    float best = 1e30f;
    bool found = false;
    if (!homeWorld.surfaceActive && !planetWorld.active) {
        Vector3 homeCenter = HomeWorldCenter();
        float homeDistance = Vector3Distance(position, homeCenter);
        float homePhysicalRadius =
            (float)SpaceUnitsKilometersToGameDistance(
                SPACE_UNITS_EARTH_RADIUS_KM);
        float homeApproachRadius = HomeWorldParkingRadiusGame() * 1.25f;
        float legacySurfaceGap = fabsf(homeDistance -
                                       HOME_WORLD_PROXY_RADIUS);
        if (homeDistance <= homeApproachRadius ||
            legacySurfaceGap <= HOME_WORLD_LANDING_MARGIN) {
            best = homeDistance;
            *gravityDir = homeDistance > 0.001f
                              ? Vector3Scale(Vector3Subtract(homeCenter, position),
                                             1.0f / homeDistance)
                              : (Vector3){ 0.0f, -1.0f, 0.0f };
            *surfaceDist = homeDistance - homePhysicalRadius;
            if (gravityScale) *gravityScale = 1.0f;
            found = true;
        }
    }
    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        float physicalRadius = bodies[i].physicalRadiusGame;
        float parkingRadius = fmaxf(physicalRadius * 8.0f,
                                    bodies[i].encounterRadiusGame * 0.15f);
        float legacyRadius = SolarBodyTerrainProxyRadius(
            bodies[i].spaceProxyRadius);
        float legacySurfaceGap = fabsf(bodies[i].dist - legacyRadius);
        if (bodies[i].dist > parkingRadius * 1.25f &&
            legacySurfaceGap > 20.0f) continue;
        if (bodies[i].dist < best) {
            best = bodies[i].dist;
            *gravityDir = Vector3Normalize(Vector3Subtract(bodies[i].center, position));
            *surfaceDist = best - physicalRadius;
            if (gravityScale) *gravityScale = bodies[i].profile.surfaceGravity;
            found = true;
        }
    }
    return found;
}

typedef struct SpaceGravityCandidate {
    SpacePhysicsGravityBody body;
    Vector3 velocity;
    SpaceGravityPrimaryKind kind;
    char name[40];
} SpaceGravityCandidate;

static void AddSpaceGravityCandidate(SpaceGravityCandidate *candidates, int *count,
                                     int capacity, SpacePhysicsGravityBody body,
                                     Vector3 velocity, SpaceGravityPrimaryKind kind,
                                     const char *name)
{
    if (!candidates || !count || *count >= capacity) return;
    candidates[*count] = (SpaceGravityCandidate){
        .body = body,
        .velocity = velocity,
        .kind = kind
    };
    snprintf(candidates[*count].name, sizeof(candidates[*count].name), "%s",
             name ? name : "Unknown");
    (*count)++;
}

bool SpaceGravityAt(Vector3 position, SpaceGravitySample *out)
{
    if (!out) return false;
    *out = (SpaceGravitySample){ 0 };
    if (!SpaceQueryVectorIsFinite(position) ||
        HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) return false;

    SpaceGravityCandidate candidates[64];
    int candidateCount = 0;

    SpaceBodyInfo bodies[48];
    int bodyCount = SpaceBodiesNear(position, SPACE_GRAVITY_QUERY_RADIUS,
                                    bodies, 48);
    for (int i = 0; i < bodyCount; i++) {
        if (bodies[i].isStar) {
            AddSpaceGravityCandidate(
                candidates, &candidateCount, 64,
                (SpacePhysicsGravityBody){
                    .center = bodies[i].center,
                    .softeningRadiusGame = bodies[i].physicalRadiusGame,
                    .gravitationalParameterGame = (float)
                        SpaceUnitsGravitationalParameterGame(
                            bodies[i].hostStar.massKg),
                    .encounterRadiusGame = SPACE_STAR_ENCOUNTER_RADIUS_GAME,
                    .hierarchy = 0
                },
                bodies[i].velocity,
                SPACE_GRAVITY_PRIMARY_STAR, bodies[i].name);
            continue;
        }

        int planetIndex = bodies[i].index - 1;
        if (planetIndex < 0 || bodies[i].semiMajorAxisKm <= 0.0) continue;

        float physicalRadius = bodies[i].physicalRadiusGame;
        float soi = bodies[i].encounterRadiusGame;
        float mu = (float)SpaceUnitsGravitationalParameterGame(
            bodies[i].profile.massKg);
        char planetName[40];
        snprintf(planetName, sizeof(planetName), "%s", bodies[i].name);
        AddSpaceGravityCandidate(
            candidates, &candidateCount, 64,
            (SpacePhysicsGravityBody){
                .center = bodies[i].center,
                .softeningRadiusGame = physicalRadius,
                .gravitationalParameterGame = mu,
                .encounterRadiusGame = soi,
                .hierarchy = 1
            },
            bodies[i].velocity,
            SPACE_GRAVITY_PRIMARY_PLANET, planetName);
    }

    SpacePhysicsGravityBody physicsBodies[64];
    for (int i = 0; i < candidateCount; i++) {
        physicsBodies[i] = candidates[i].body;
    }
    int selected = SpacePhysicsSelectPrimary(position, physicsBodies,
                                              candidateCount);
    if (selected < 0) return false;

    const SpaceGravityCandidate *primary = &candidates[selected];
    float distance = Vector3Distance(position, primary->body.center);
    Vector3 acceleration = { 0 };
    if (primary->kind == SPACE_GRAVITY_PRIMARY_STAR) {
        for (int i = 0; i < candidateCount; i++) {
            if (candidates[i].kind != SPACE_GRAVITY_PRIMARY_STAR) continue;
            acceleration = Vector3Add(
                acceleration,
                SpacePhysicsGravityAcceleration(position, &candidates[i].body));
        }
    } else {
        acceleration = SpacePhysicsGravityAcceleration(position, &primary->body);
        for (int i = 0; i < candidateCount; i++) {
            if (candidates[i].kind != SPACE_GRAVITY_PRIMARY_STAR) continue;
            acceleration = Vector3Add(
                acceleration,
                SpacePhysicsGravityAcceleration(position, &candidates[i].body));
        }
    }
    *out = (SpaceGravitySample){
        .active = true,
        .kind = primary->kind,
        .center = primary->body.center,
        .primaryVelocity = primary->velocity,
        .acceleration = acceleration,
        .distance = distance,
        .surfaceDistance = distance - primary->body.softeningRadiusGame,
        .encounterRadiusGame = primary->body.encounterRadiusGame,
        .gravitationalParameterGame =
            primary->body.gravitationalParameterGame
    };
    snprintf(out->name, sizeof(out->name), "%s", primary->name);
    return true;
}
