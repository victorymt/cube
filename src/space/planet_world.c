#include "space/space_internal.h"

static Vector3 PlanetReturnPosition(Vector3 center, float parkingRadius,
                                    Vector3 outward)
{
    if (!(parkingRadius > 0.0f) || !isfinite(parkingRadius)) return center;
    if (Vector3LengthSqr(outward) < 0.000001f) {
        outward = (Vector3){ 1.0f, 0.0f, 0.0f };
    } else {
        outward = Vector3Normalize(outward);
    }
    return Vector3Add(center, Vector3Scale(outward, parkingRadius));
}

bool PlanetWorldLandingTarget(Vector3 position, SpaceBodyInfo *out)
{
    if (!out) return false;
    *out = (SpaceBodyInfo){ 0 };
    if (!SpaceQueryVectorIsFinite(position)) return false;
    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(position, PLANET_LANDING_QUERY_RADIUS,
                                bodies, 48);
    float bestApproach = 1e30f;
    bool found = false;

    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        if (!bodies[i].profile.hasSolidSurface) continue;
        float parkingRadius = fmaxf(bodies[i].physicalRadiusGame * 8.0f,
                                    bodies[i].encounterRadiusGame * 0.15f);
        float approach = parkingRadius > 0.0f
            ? bodies[i].dist / parkingRadius
            : INFINITY;
        float legacyRadius = SolarBodyTerrainProxyRadius(
            bodies[i].spaceProxyRadius);
        bool legacyApproach = fabsf(bodies[i].dist - legacyRadius) <= 20.0f;
        if ((approach > 1.25f && !legacyApproach) ||
            approach >= bestApproach) continue;
        bestApproach = approach;
        *out = bodies[i];
        found = true;
    }
    return found;
}

bool HomeWorldTryLaunch(Player *player)
{
    if (!player || !homeWorld.surfaceActive || planetWorld.active ||
        player->position.y < SPACE_ENTER_Y) {
        return false;
    }

    homeWorld.returnPosition = player->position;
    DrainChunkGen();
    UnloadAllChunks();
    homeWorld.surfaceActive = false;
    RebuildTorchList();
    ClearUndoHistory();

    Vector3 homeCenter = HomeWorldCenter();
    float parkingRadius = HomeWorldParkingRadiusGame();
    Vector3 orbitalVelocity = Vector3Zero();
    SolarSystemDef sol;
    SolarPlanetOrbitalState earthState;
    if (StarSystemAt(0, 0, &sol) && sol.planetCount > 2 &&
        SolarSystemPlanetStateAtTime(
            &sol, 2,
            SpacePeriodicSimulationTime(SpaceElapsedSimulationTime()),
            &earthState)) {
        homeCenter = earthState.center;
        orbitalVelocity = earthState.velocity;
    }
    Vector3 forward = Vector3Normalize((Vector3){
        sinf(player->yaw) * cosf(player->pitch),
        sinf(player->pitch),
        cosf(player->yaw) * cosf(player->pitch)
    });
    player->position = Vector3Subtract(
        homeCenter, Vector3Scale(forward, parkingRadius));
    player->velocity = Vector3Add(player->velocity, orbitalVelocity);
    player->floating = false;
    player->onGround = false;
    SetImportMessage("Left Homeworld atmosphere. Spaceflight is now three-dimensional.");
    return true;
}

bool HomeWorldCanEnter(Vector3 position)
{
    if (homeWorld.surfaceActive || planetWorld.active) return false;

    Vector3 center = HomeWorldCenter();
    float distance = Vector3Distance(position, center);
    if (distance <= HomeWorldParkingRadiusGame() * 1.25f) return true;
    float legacySurfaceGap = fabsf(distance - HOME_WORLD_PROXY_RADIUS);
    return legacySurfaceGap <= HOME_WORLD_LANDING_MARGIN;
}

static void HomeWorldActivateSurface(void)
{
    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    homeWorld.surfaceActive = true;
    WorldSetNetherActive(false);
    RebuildTorchList();
    ClearUndoHistory();
}

bool HomeWorldBeginDescent(Player *player, Vector3 *outLandingPosition)
{
    if (outLandingPosition) *outLandingPosition = (Vector3){ 0 };
    if (!player) return false;
    if (!HomeWorldCanEnter(player->position)) return false;

    int landingX = (int)floorf(homeWorld.returnPosition.x);
    int landingZ = (int)floorf(homeWorld.returnPosition.z);
    int groundY = 0;
    if (!FindSafeSurfaceLanding(landingX, landingZ, 128, 2,
                                &landingX, &landingZ, &groundY)) {
        groundY = TerrainHeight(landingX, landingZ, WorldTerrainMode());
    }
    Vector3 landing = { (float)landingX + 0.5f, (float)groundY + 3.0f,
                        (float)landingZ + 0.5f };

    HomeWorldActivateSurface();
    player->position = (Vector3){ landing.x, SPACE_ENTER_Y - 2.0f, landing.z - 96.0f };
    player->velocity = Vector3Zero();
    player->yaw = 0.0f;
    player->pitch = -0.62f;
    player->floating = true;
    player->onGround = false;
    if (outLandingPosition) *outLandingPosition = landing;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    SetImportMessage("Crossing Homeworld upper atmosphere.");
    return true;
}

bool HomeWorldTryEnter(Player *player)
{
    if (!player) return false;
    if (!HomeWorldCanEnter(player->position)) return false;

    HomeWorldActivateSurface();

    int landingX = (int)floorf(homeWorld.returnPosition.x);
    int landingZ = (int)floorf(homeWorld.returnPosition.z);
    int groundY = 0;
    if (!FindSafeSurfaceLanding(landingX, landingZ, 128, 2,
                                &landingX, &landingZ, &groundY)) {
        groundY = TerrainHeight(landingX, landingZ, WorldTerrainMode());
    }
    player->position = (Vector3){ (float)landingX + 0.5f, (float)groundY + 3.0f,
                                  (float)landingZ + 0.5f };
    player->velocity = Vector3Zero();
    player->floating = false;
    player->onGround = false;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    SetImportMessage("Landed on Homeworld.");
    return true;
}

static void PlanetWorldActivate(const SpaceBodyInfo *body, Vector3 approachPosition)
{
    PlanetWorldContext next = { 0 };
    next.active = true;
    next.profile = body->profile;
    next.style = body->profile.style;
    next.planetIndex = body->index;
    next.bodyCenter = body->center;
    next.spaceProxyRadius = SolarBodyTerrainProxyRadius(
        body->spaceProxyRadius);
    next.seed = body->worldSeed;
    snprintf(next.name, sizeof(next.name), "%s", body->name);

    Vector3 outward = Vector3Subtract(approachPosition, body->center);
    if (Vector3LengthSqr(outward) < 0.001f) outward = (Vector3){ 0.0f, 1.0f, 0.0f };
    else outward = Vector3Normalize(outward);
    // The visible texture rotates with the body. Invert that rotation before
    // turning an approach vector into a persistent surface-map coordinate.
    Vector3 surfaceNormal = Vector3RotateByAxisAngle(
        outward, (Vector3){ 0.0f, 1.0f, 0.0f },
        -PlanetBodyTextureRotation(body) * DEG2RAD);
    float longitude = atan2f(surfaceNormal.z, surfaceNormal.x);
    float latitude = asinf(Clamp(surfaceNormal.y, -1.0f, 1.0f));
    next.originX = (int)lroundf(longitude *
                                 (PLANET_GLOBAL_CIRCUMFERENCE_BLOCKS / (2.0f * PI)));
    next.originZ = (int)lroundf(latitude *
                                 (PLANET_GLOBAL_POLE_TO_POLE_BLOCKS / PI));
    next.returnPosition = approachPosition;
    next.remnantEnvironment = body->remnantEnvironment;
    next.remnantEnvironmentSimulationTime = -1.0;

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    planetWorld = next;
    RebuildTorchList();
    ClearUndoHistory();
}

static Vector3 PlanetWorldLandingPosition(int *outShipX, int *outShipZ,
                                          int *outShipGround)
{
    int shipX = 0;
    int shipZ = 0;
    int shipGround = 0;
    if (!FindSafeSurfaceLanding(shipX, shipZ, 128, 3,
                                &shipX, &shipZ, &shipGround)) {
        shipGround = PlanetTerrainHeight(shipX, shipZ);
    }
    int playerX = shipX + 3;
    int playerZ = shipZ;
    int playerGround = 0;
    if (!FindSafeSurfaceLanding(playerX, playerZ, 16, 0,
                                &playerX, &playerZ, &playerGround)) {
        playerGround = PlanetTerrainHeight(playerX, playerZ);
    }
    if (outShipX) *outShipX = shipX;
    if (outShipZ) *outShipZ = shipZ;
    if (outShipGround) *outShipGround = shipGround;
    return (Vector3){ (float)playerX + 0.5f, (float)playerGround + 2.0f,
                      (float)playerZ + 0.5f };
}

bool PlanetWorldBeginDescent(Player *player, Vector3 *outLandingPosition)
{
    if (outLandingPosition) *outLandingPosition = (Vector3){ 0 };
    if (!player || planetWorld.active || homeWorld.surfaceActive) return false;

    SpaceBodyInfo body;
    if (!PlanetWorldLandingTarget(player->position, &body)) return false;
    Vector3 approachPosition = player->position;
    PlanetWorldActivate(&body, approachPosition);

    Vector3 landing = PlanetWorldLandingPosition(NULL, NULL, NULL);
    float entryAngle = (float)(planetWorld.seed % 6283u) * 0.001f;
    Vector3 forward = { sinf(entryAngle), 0.0f, cosf(entryAngle) };
    float entryY = PLANET_ATMOSPHERE_FADE_START +
                   PlanetAtmosphereDepth(&planetWorld.profile) + 4.0f;
    player->position = Vector3Subtract(landing, Vector3Scale(forward, 96.0f));
    player->position.y = entryY;
    player->velocity = Vector3Zero();
    player->yaw = atan2f(forward.x, forward.z);
    player->pitch = -0.62f;
    player->floating = true;
    player->onGround = false;
    if (outLandingPosition) *outLandingPosition = landing;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    SetImportMessage(TextFormat("Crossing %s upper atmosphere.", planetWorld.name));
    return true;
}

bool PlanetWorldTryEnter(Player *player)
{
    if (!player || planetWorld.active || homeWorld.surfaceActive) return false;

    SpaceBodyInfo body;
    if (!PlanetWorldLandingTarget(player->position, &body)) return false;
    Vector3 approachPosition = player->position;
    PlanetWorldActivate(&body, approachPosition);

    int shipX = 0;
    int shipZ = 0;
    int shipGround = 0;
    player->position = PlanetWorldLandingPosition(&shipX, &shipZ, &shipGround);
    player->velocity = Vector3Zero();
    player->floating = false;
    player->onGround = false;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    SetImportMessage(TextFormat("Landed on %s - %s. Age %.1f Gyr. Biosphere: %s / %s / %s.",
                                planetWorld.name,
                                PlanetBiomeName(PlanetBiomeAt((int)floorf(player->position.x),
                                                               (int)floorf(player->position.z))),
                                planetWorld.profile.ageGyr,
                                PlanetEcologyBiomassName(), PlanetEcologyChemistryName(),
                                PlanetEcologyBodyPlanName()));
    return true;
}

bool PlanetWorldTryLaunch(Player *player)
{
    if (!player || !planetWorld.active ||
        PlanetWorldAtmosphereFade(player->position) < 1.0f) {
        return false;
    }

    Vector3 returnPosition = planetWorld.returnPosition;
    Vector3 launchVelocity = PlanetWorldSpaceDirection(player->velocity);
    Vector3 localForward = Vector3Normalize((Vector3){
        sinf(player->yaw) * cosf(player->pitch),
        sinf(player->pitch),
        cosf(player->yaw) * cosf(player->pitch)
    });
    Vector3 spaceForward = Vector3Normalize(PlanetWorldSpaceDirection(localForward));
    float launchYaw = atan2f(spaceForward.x, spaceForward.z);
    float launchPitch = asinf(Clamp(spaceForward.y, -1.0f, 1.0f));
    Vector3 outward = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(outward) < 0.001f) outward = (Vector3){ 1.0f, 0.0f, 0.0f };
    else outward = Vector3Normalize(outward);

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    SolarSystemDef system;
    int orbitIndex = planetWorld.planetIndex - 1;
    if (StarSystemAt(systemAx, systemAz, &system) &&
        orbitIndex >= 0 && orbitIndex < system.planetCount) {
        SolarPlanetOrbitalState orbitalState;
        if (SolarSystemPlanetStateAtTime(&system, orbitIndex,
                                         SpacePeriodicSimulationTime(
                                             SpaceElapsedSimulationTime()),
                                         &orbitalState)) {
            float parkingRadius = SolarSystemPlanetParkingRadiusGame(
                &system, orbitIndex);
            returnPosition = PlanetReturnPosition(
                orbitalState.center, parkingRadius, outward);
            launchVelocity = Vector3Add(launchVelocity,
                                        orbitalState.velocity);
        }
    }
    char planetName[32];
    snprintf(planetName, sizeof(planetName), "%s", planetWorld.name);

    DrainChunkGen();
    UnloadAllChunks();
    planetWorld.active = false;
    homeWorld.surfaceActive = false;
    RebuildTorchList();
    ClearUndoHistory();
    player->position = returnPosition;
    player->velocity = launchVelocity;
    player->yaw = launchYaw;
    player->pitch = launchPitch;
    player->floating = false;
    player->onGround = false;
    SetImportMessage(TextFormat("Left %s atmosphere.", planetName));
    return true;
}

void PlanetWorldReset(void)
{
    memset(&planetWorld, 0, sizeof(planetWorld));
}

static bool PlanetProfileUnitValue(float value)
{
    return isfinite(value) && value >= 0.0f && value <= 1.0f;
}

void PlanetProfileDeriveSeasonalFields(PlanetProfile *profile)
{
    if (!profile) return;
    float thermalBuffer = Clamp(
        profile->atmosphereDensity * 0.48f + profile->oceanCoverage * 0.72f +
            profile->cloudCoverage * 0.18f,
        0.0f, 0.88f);
    profile->seasonalTemperatureAmplitudeK =
        profile->hasSolidSurface ?
            sinf(Clamp(profile->axialTilt, 0.0f, 0.5f * PI)) *
                (38.0f + 36.0f * (1.0f - thermalBuffer))
            : 0.0f;
    float eccentricity = Clamp(profile->orbitalEccentricity, 0.0f, 0.95f);
    float eccentricityForcing = Clamp(
        2.0f * eccentricity /
            fmaxf(1.0f - eccentricity * eccentricity, 0.05f),
        0.0f, 3.0f);
    profile->orbitalTemperatureAmplitudeK =
        profile->hasSolidSurface ?
            profile->equilibriumTempK * 0.25f * eccentricityForcing *
                (1.0f - thermalBuffer * 0.72f)
            : 0.0f;
    profile->polarIceVariability = profile->hasSolidSurface ? Clamp(
        (profile->seasonalTemperatureAmplitudeK +
         profile->orbitalTemperatureAmplitudeK * 0.45f) / 58.0f *
            (0.35f + profile->iceCoverage * 0.65f),
        0.0f, 1.0f) : 0.0f;
    profile->seasonalHumidityBias = profile->hasSolidSurface ? Clamp(
        (profile->seasonalTemperatureAmplitudeK +
         profile->orbitalTemperatureAmplitudeK * 0.25f) / 82.0f *
            (0.25f + profile->oceanCoverage * 0.75f),
        0.0f, 1.0f) : 0.0f;
}

bool PlanetProfileIsValid(const PlanetProfile *profile)
{
    if (!profile || profile->style < SOLAR_STYLE_SUN ||
        profile->style > SOLAR_STYLE_TEMPERATE ||
        profile->atmosphereType < PLANET_ATMOSPHERE_NONE ||
        profile->atmosphereType > PLANET_ATMOSPHERE_CORROSIVE) {
        return false;
    }

    return isfinite(profile->physicalRadiusKm) &&
           profile->canonicalBodyId <= 8u &&
           profile->physicalRadiusKm >= 0.0 &&
           isfinite(profile->massKg) && profile->massKg >= 0.0 &&
           isfinite(profile->spaceProxyRadius) &&
           profile->spaceProxyRadius >= 0.0f &&
           isfinite(profile->surfaceGravity) &&
           profile->surfaceGravity >= 0.0f &&
           isfinite(profile->receivedIrradiance) &&
           profile->receivedIrradiance >= 0.0 &&
           isfinite(profile->radiativeTempK) &&
           profile->radiativeTempK >= 0.0f &&
           isfinite(profile->equilibriumTempK) &&
           profile->equilibriumTempK >= 0.0f &&
           isfinite(profile->surfacePressureAtm) &&
           profile->surfacePressureAtm >= 0.0f &&
           PlanetProfileUnitValue(profile->atmosphereDensity) &&
           PlanetProfileUnitValue(profile->oceanCoverage) &&
           PlanetProfileUnitValue(profile->iceCoverage) &&
           PlanetProfileUnitValue(profile->cloudCoverage) &&
           isfinite(profile->terrainRoughness) &&
           profile->terrainRoughness >= 0.0f &&
           isfinite(profile->ageGyr) && profile->ageGyr >= 0.0f &&
           isfinite(profile->rotationRate) &&
           profile->rotationRate >= 0.0f &&
           PlanetProfileUnitValue(profile->tidalLockFactor) &&
           isfinite(profile->ringTilt) &&
           PlanetProfileUnitValue(profile->albedo) &&
           isfinite(profile->greenhouseEffect) &&
           profile->greenhouseEffect >= 0.0f &&
           isfinite(profile->orbitalEccentricity) &&
           profile->orbitalEccentricity >= 0.0f &&
           profile->orbitalEccentricity < 1.0f &&
           isfinite(profile->orbitalMeanAnomalyAtEpoch) &&
           isfinite(profile->axialTilt) &&
           isfinite(profile->seasonPhase) &&
           isfinite(profile->yearLength) && profile->yearLength >= 0.0f &&
           isfinite(profile->seasonalTemperatureAmplitudeK) &&
           profile->seasonalTemperatureAmplitudeK >= 0.0f &&
           isfinite(profile->orbitalTemperatureAmplitudeK) &&
           profile->orbitalTemperatureAmplitudeK >= 0.0f &&
           PlanetProfileUnitValue(profile->polarIceVariability) &&
           PlanetProfileUnitValue(profile->seasonalHumidityBias) &&
           isfinite(profile->prevailingWindAngle) &&
           PlanetProfileUnitValue(profile->windStrength) &&
           PlanetProfileUnitValue(profile->volcanicActivity) &&
           PlanetProfileUnitValue(profile->impactRate);
}

bool PlanetProfileSaveState(FILE *file, const PlanetProfile *profile)
{
    if (!file || !PlanetProfileIsValid(profile)) return false;

    uint32_t style = (uint32_t)profile->style;
    uint32_t atmosphereType = (uint32_t)profile->atmosphereType;
    uint32_t magic = PLANET_PROFILE_STATE_MAGIC;
    uint32_t version = PLANET_PROFILE_STATE_VERSION;
    uint8_t hasSolidSurface = profile->hasSolidSurface ? 1u : 0u;
    uint8_t hasRings = profile->hasRings ? 1u : 0u;
    uint8_t tidallyLocked = profile->tidallyLocked ? 1u : 0u;

#define WRITE_PROFILE_FIELD(field) \
    fwrite(&profile->field, sizeof(profile->field), 1, file) == 1
    return fwrite(&magic, sizeof(magic), 1, file) == 1 &&
           fwrite(&version, sizeof(version), 1, file) == 1 &&
           WRITE_PROFILE_FIELD(seed) &&
           WRITE_PROFILE_FIELD(canonicalBodyId) &&
           fwrite(&style, sizeof(style), 1, file) == 1 &&
           fwrite(&atmosphereType, sizeof(atmosphereType), 1, file) == 1 &&
           WRITE_PROFILE_FIELD(physicalRadiusKm) &&
           WRITE_PROFILE_FIELD(massKg) &&
           WRITE_PROFILE_FIELD(spaceProxyRadius) &&
           WRITE_PROFILE_FIELD(surfaceGravity) &&
           WRITE_PROFILE_FIELD(receivedIrradiance) &&
           WRITE_PROFILE_FIELD(radiativeTempK) &&
           WRITE_PROFILE_FIELD(equilibriumTempK) &&
           WRITE_PROFILE_FIELD(surfacePressureAtm) &&
           WRITE_PROFILE_FIELD(atmosphereDensity) &&
           WRITE_PROFILE_FIELD(oceanCoverage) &&
           WRITE_PROFILE_FIELD(iceCoverage) &&
           WRITE_PROFILE_FIELD(cloudCoverage) &&
           WRITE_PROFILE_FIELD(terrainRoughness) &&
           WRITE_PROFILE_FIELD(ageGyr) &&
           WRITE_PROFILE_FIELD(rotationRate) &&
           WRITE_PROFILE_FIELD(tidalLockFactor) &&
           WRITE_PROFILE_FIELD(ringTilt) &&
           WRITE_PROFILE_FIELD(albedo) &&
           WRITE_PROFILE_FIELD(greenhouseEffect) &&
           WRITE_PROFILE_FIELD(orbitalEccentricity) &&
           WRITE_PROFILE_FIELD(orbitalMeanAnomalyAtEpoch) &&
           WRITE_PROFILE_FIELD(axialTilt) &&
           WRITE_PROFILE_FIELD(seasonPhase) &&
           WRITE_PROFILE_FIELD(yearLength) &&
           WRITE_PROFILE_FIELD(seasonalTemperatureAmplitudeK) &&
           WRITE_PROFILE_FIELD(orbitalTemperatureAmplitudeK) &&
           WRITE_PROFILE_FIELD(polarIceVariability) &&
           WRITE_PROFILE_FIELD(seasonalHumidityBias) &&
           WRITE_PROFILE_FIELD(prevailingWindAngle) &&
           WRITE_PROFILE_FIELD(windStrength) &&
           WRITE_PROFILE_FIELD(volcanicActivity) &&
           WRITE_PROFILE_FIELD(impactRate) &&
           fwrite(&hasSolidSurface, sizeof(hasSolidSurface), 1, file) == 1 &&
           fwrite(&hasRings, sizeof(hasRings), 1, file) == 1 &&
           fwrite(&tidallyLocked, sizeof(tidallyLocked), 1, file) == 1;
#undef WRITE_PROFILE_FIELD
}

bool PlanetProfileLoadState(FILE *file, PlanetProfile *outProfile)
{
    if (!outProfile) return false;
    *outProfile = (PlanetProfile){ 0 };
    if (!file) return false;

    PlanetProfile loaded = { 0 };
    uint32_t style = 0;
    uint32_t atmosphereType = 0;
    uint32_t marker = 0;
    uint32_t version = 0;
    uint8_t hasSolidSurface = 0;
    uint8_t hasRings = 0;
    uint8_t tidallyLocked = 0;

#define READ_PROFILE_FIELD(field) \
    (fread(&loaded.field, sizeof(loaded.field), 1, file) == 1)
    if (fread(&marker, sizeof(marker), 1, file) != 1) return false;
    bool legacyLayout = marker != PLANET_PROFILE_STATE_MAGIC;
    if (legacyLayout) {
        loaded.seed = marker;
    } else if (fread(&version, sizeof(version), 1, file) != 1 ||
               (version != 2u && version != PLANET_PROFILE_STATE_VERSION) ||
               !READ_PROFILE_FIELD(seed) ||
               (version >= 3u && !READ_PROFILE_FIELD(canonicalBodyId))) {
        return false;
    }
    if (
        fread(&style, sizeof(style), 1, file) != 1 ||
        fread(&atmosphereType, sizeof(atmosphereType), 1, file) != 1 ||
        !READ_PROFILE_FIELD(physicalRadiusKm) ||
        !READ_PROFILE_FIELD(massKg) ||
        !READ_PROFILE_FIELD(spaceProxyRadius) ||
        !READ_PROFILE_FIELD(surfaceGravity) ||
        !READ_PROFILE_FIELD(receivedIrradiance) ||
        !READ_PROFILE_FIELD(radiativeTempK) ||
        !READ_PROFILE_FIELD(equilibriumTempK) ||
        !READ_PROFILE_FIELD(surfacePressureAtm) ||
        !READ_PROFILE_FIELD(atmosphereDensity) ||
        !READ_PROFILE_FIELD(oceanCoverage) ||
        !READ_PROFILE_FIELD(iceCoverage) ||
        !READ_PROFILE_FIELD(cloudCoverage) ||
        !READ_PROFILE_FIELD(terrainRoughness) ||
        !READ_PROFILE_FIELD(ageGyr) ||
        !READ_PROFILE_FIELD(rotationRate) ||
        !READ_PROFILE_FIELD(tidalLockFactor) ||
        !READ_PROFILE_FIELD(ringTilt) ||
        !READ_PROFILE_FIELD(albedo) ||
        !READ_PROFILE_FIELD(greenhouseEffect)) {
        return false;
    }
    if (!legacyLayout &&
        (!READ_PROFILE_FIELD(orbitalEccentricity) ||
         !READ_PROFILE_FIELD(orbitalMeanAnomalyAtEpoch))) {
        return false;
    }
    if (
        !READ_PROFILE_FIELD(axialTilt) ||
        !READ_PROFILE_FIELD(seasonPhase) ||
        !READ_PROFILE_FIELD(yearLength)) {
        return false;
    }
    if (!legacyLayout &&
        (!READ_PROFILE_FIELD(seasonalTemperatureAmplitudeK) ||
         !READ_PROFILE_FIELD(orbitalTemperatureAmplitudeK) ||
         !READ_PROFILE_FIELD(polarIceVariability) ||
         !READ_PROFILE_FIELD(seasonalHumidityBias))) {
        return false;
    }
    if (
        !READ_PROFILE_FIELD(prevailingWindAngle) ||
        !READ_PROFILE_FIELD(windStrength) ||
        !READ_PROFILE_FIELD(volcanicActivity) ||
        !READ_PROFILE_FIELD(impactRate) ||
        fread(&hasSolidSurface, sizeof(hasSolidSurface), 1, file) != 1 ||
        fread(&hasRings, sizeof(hasRings), 1, file) != 1 ||
        fread(&tidallyLocked, sizeof(tidallyLocked), 1, file) != 1) {
        return false;
    }
#undef READ_PROFILE_FIELD

    if (style > (uint32_t)SOLAR_STYLE_TEMPERATE ||
        atmosphereType > (uint32_t)PLANET_ATMOSPHERE_CORROSIVE ||
        hasSolidSurface > 1u || hasRings > 1u || tidallyLocked > 1u) {
        return false;
    }
    loaded.style = (SolarBodyStyle)style;
    loaded.atmosphereType = (PlanetAtmosphereType)atmosphereType;
    loaded.hasSolidSurface = hasSolidSurface != 0u;
    loaded.hasRings = hasRings != 0u;
    loaded.tidallyLocked = tidallyLocked != 0u;
    if (legacyLayout) {
        loaded.orbitalEccentricity = 0.0f;
        loaded.orbitalMeanAnomalyAtEpoch = 0.0f;
        PlanetProfileDeriveSeasonalFields(&loaded);
    }
    if (!PlanetProfileIsValid(&loaded)) return false;

    *outProfile = loaded;
    return true;
}

bool PlanetWorldSaveState(FILE *file)
{
    SpaceRemnantEnvironment currentRemnant =
        PlanetWorldCurrentRemnantEnvironment();
    uint8_t version = PLANET_WORLD_STATE_VERSION;
    uint8_t active = planetWorld.active ? 1u : 0u;
    uint32_t style = (uint32_t)planetWorld.style;
    int32_t originX = (int32_t)planetWorld.originX;
    int32_t originZ = (int32_t)planetWorld.originZ;
    int32_t planetIndex = (int32_t)planetWorld.planetIndex;
    float bodyCenter[3] = { planetWorld.bodyCenter.x, planetWorld.bodyCenter.y,
                            planetWorld.bodyCenter.z };
    float returnPosition[3] = { planetWorld.returnPosition.x, planetWorld.returnPosition.y,
                                planetWorld.returnPosition.z };
    uint8_t remnantActive = currentRemnant.active ? 1u : 0u;
    int32_t remnantCount = currentRemnant.remnantCount;
    float remnantHazard = currentRemnant.radiationHazard;
    float remnantEjecta = currentRemnant.ejectaDensity;
    float remnantShell = currentRemnant.nearestShellDistanceGame;

    if (!file || !PlanetProfileIsValid(&planetWorld.profile) ||
        !SpaceRemnantEnvironmentIsValid(&currentRemnant)) {
        return false;
    }

    return fwrite(&version, sizeof(version), 1, file) == 1 &&
           fwrite(&active, sizeof(active), 1, file) == 1 &&
           fwrite(&planetWorld.seed, sizeof(planetWorld.seed), 1, file) == 1 &&
           fwrite(&style, sizeof(style), 1, file) == 1 &&
           fwrite(&originX, sizeof(originX), 1, file) == 1 &&
           fwrite(&originZ, sizeof(originZ), 1, file) == 1 &&
           fwrite(&planetIndex, sizeof(planetIndex), 1, file) == 1 &&
           fwrite(bodyCenter, sizeof(bodyCenter), 1, file) == 1 &&
           fwrite(returnPosition, sizeof(returnPosition), 1, file) == 1 &&
           fwrite(&planetWorld.spaceProxyRadius,
                  sizeof(planetWorld.spaceProxyRadius), 1, file) == 1 &&
           fwrite(planetWorld.name, sizeof(planetWorld.name), 1, file) == 1 &&
           fwrite(&remnantActive, sizeof(remnantActive), 1, file) == 1 &&
           fwrite(&remnantCount, sizeof(remnantCount), 1, file) == 1 &&
           fwrite(&remnantHazard, sizeof(remnantHazard), 1, file) == 1 &&
           fwrite(&remnantEjecta, sizeof(remnantEjecta), 1, file) == 1 &&
           fwrite(&remnantShell, sizeof(remnantShell), 1, file) == 1 &&
           PlanetProfileSaveState(file, &planetWorld.profile);
}

bool PlanetWorldLoadState(FILE *file)
{
    PlanetWorldContext loaded = { 0 };
    uint8_t versionOrActive = 0;
    uint8_t active = 0;
    uint32_t style = 0;
    int32_t originX = 0;
    int32_t originZ = 0;
    int32_t planetIndex = 0;
    float bodyCenter[3] = { 0 };
    float returnPosition[3] = { 0 };
    uint8_t remnantActive = 0;
    int32_t remnantCount = 0;
    float remnantHazard = 0.0f;
    float remnantEjecta = 0.0f;
    float remnantShell = 0.0f;

    if (!file ||
        fread(&versionOrActive, sizeof(versionOrActive), 1, file) != 1) {
        return false;
    }
    bool hasProfile = versionOrActive == 2u ||
                      versionOrActive == PLANET_WORLD_STATE_VERSION;
    bool hasRemnantEnvironment = versionOrActive == PLANET_WORLD_STATE_VERSION;
    if (hasProfile) {
        if (fread(&active, sizeof(active), 1, file) != 1) return false;
    } else {
        active = versionOrActive;
    }

    if (active > 1u ||
        fread(&loaded.seed, sizeof(loaded.seed), 1, file) != 1 ||
        fread(&style, sizeof(style), 1, file) != 1 ||
        fread(&originX, sizeof(originX), 1, file) != 1 ||
        fread(&originZ, sizeof(originZ), 1, file) != 1 ||
        fread(&planetIndex, sizeof(planetIndex), 1, file) != 1 ||
        fread(bodyCenter, sizeof(bodyCenter), 1, file) != 1 ||
        fread(returnPosition, sizeof(returnPosition), 1, file) != 1 ||
        fread(&loaded.spaceProxyRadius,
              sizeof(loaded.spaceProxyRadius), 1, file) != 1 ||
        fread(loaded.name, sizeof(loaded.name), 1, file) != 1) {
        return false;
    }
    if (hasRemnantEnvironment &&
        (fread(&remnantActive, sizeof(remnantActive), 1, file) != 1 ||
         fread(&remnantCount, sizeof(remnantCount), 1, file) != 1 ||
         fread(&remnantHazard, sizeof(remnantHazard), 1, file) != 1 ||
         fread(&remnantEjecta, sizeof(remnantEjecta), 1, file) != 1 ||
         fread(&remnantShell, sizeof(remnantShell), 1, file) != 1)) {
        return false;
    }

    if (style > (uint32_t)SOLAR_STYLE_TEMPERATE ||
        planetIndex < 0 || !isfinite(loaded.spaceProxyRadius) ||
        loaded.spaceProxyRadius < 0.0f ||
        !isfinite(bodyCenter[0]) || !isfinite(bodyCenter[1]) || !isfinite(bodyCenter[2]) ||
        !isfinite(returnPosition[0]) || !isfinite(returnPosition[1]) ||
        !isfinite(returnPosition[2]) || remnantActive > 1u) {
        return false;
    }

    loaded.active = active != 0;
    loaded.style = (SolarBodyStyle)style;
    loaded.originX = (int)originX;
    loaded.originZ = (int)originZ;
    loaded.planetIndex = (int)planetIndex;
    loaded.bodyCenter = (Vector3){ bodyCenter[0], bodyCenter[1], bodyCenter[2] };
    loaded.returnPosition = (Vector3){ returnPosition[0], returnPosition[1], returnPosition[2] };
    loaded.remnantEnvironment = (SpaceRemnantEnvironment){
        .active = remnantActive != 0u,
        .remnantCount = (int)remnantCount,
        .radiationHazard = remnantHazard,
        .ejectaDensity = remnantEjecta,
        .nearestShellDistanceGame = remnantShell
    };
    loaded.remnantEnvironmentSimulationTime = -1.0;
    if (!SpaceRemnantEnvironmentIsValid(&loaded.remnantEnvironment)) {
        return false;
    }
    if (hasProfile) {
        if (!PlanetProfileLoadState(file, &loaded.profile) ||
            loaded.profile.seed != loaded.seed ||
            loaded.profile.style != loaded.style) {
            return false;
        }
    } else {
        loaded.profile = PlanetProfileGenerateLegacy(loaded.seed, loaded.style,
                                                     loaded.spaceProxyRadius);
    }
    loaded.name[sizeof(loaded.name) - 1] = '\0';
    planetWorld = loaded;
    return true;
}

bool HomeWorldSaveState(FILE *file)
{
    if (!file) return false;
    uint8_t surfaceActive = homeWorld.surfaceActive ? 1u : 0u;
    float returnPosition[3] = {
        homeWorld.returnPosition.x,
        homeWorld.returnPosition.y,
        homeWorld.returnPosition.z
    };
    return fwrite(&surfaceActive, sizeof(surfaceActive), 1, file) == 1 &&
           fwrite(returnPosition, sizeof(returnPosition), 1, file) == 1;
}

bool HomeWorldLoadState(FILE *file)
{
    if (!file) return false;
    uint8_t surfaceActive = 0;
    float returnPosition[3] = { 0 };
    if (fread(&surfaceActive, sizeof(surfaceActive), 1, file) != 1 ||
        fread(returnPosition, sizeof(returnPosition), 1, file) != 1) {
        return false;
    }
    if (surfaceActive > 1u ||
        !isfinite(returnPosition[0]) || !isfinite(returnPosition[1]) ||
        !isfinite(returnPosition[2])) {
        return false;
    }

    homeWorld.surfaceActive = surfaceActive != 0 && !planetWorld.active;
    homeWorld.returnPosition = (Vector3){
        returnPosition[0], returnPosition[1], returnPosition[2]
    };
    return true;
}
