#include "space/space_internal.h"

static const char *const starNamePart1[] = {
    "Al", "Bel", "Cer", "Dra", "Eri", "Fen", "Gar", "Hal", "Ith", "Jun",
    "Kel", "Lor", "Mir", "Neb", "Or", "Pry", "Quel", "Rav", "Tha", "Umb",
    "Vex", "Wy", "Zor", "Xan"
};
static const char *const starNamePart2[] = {
    "a", "e", "i", "o", "u", "ae", "ia", "or", "yn", "ei"
};
static const char *const starNamePart3[] = {
    "va", "nis", "dar", "lune", "rax", "thys", "mar", "dus", "phe", "rith"
};

PlanetWorldContext planetWorld = { 0 };
HomeWorldContext homeWorld = {
    .surfaceActive = true,
    .returnPosition = { 0.5f, 12.0f, 0.5f }
};
// Persist monotonic elapsed time. Periodic consumers derive a bounded clock
// from it so long-running orbital precision does not erase stellar age.
double solarElapsedSimulationTime = 0.0;
// World generation uses global integer coordinates. Rendering and physics use
// this nearby local frame, which is periodically shifted during spaceflight.
int spaceOriginX = 0;
int spaceOriginZ = 0;
SpaceLoadError spaceLastLoadError = SPACE_LOAD_ERROR_NONE;

bool SpaceVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

int ClampCoordinate(int64_t value)
{
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return (int)value;
}

int SpaceLocalToGlobalX(int localX)
{
    return ClampCoordinate((int64_t)localX + (int64_t)spaceOriginX);
}

int SpaceLocalToGlobalZ(int localZ)
{
    return ClampCoordinate((int64_t)localZ + (int64_t)spaceOriginZ);
}

int SpaceGlobalToLocalX(int globalX)
{
    return ClampCoordinate((int64_t)globalX - (int64_t)spaceOriginX);
}

int SpaceGlobalToLocalZ(int globalZ)
{
    return ClampCoordinate((int64_t)globalZ - (int64_t)spaceOriginZ);
}

int SpaceSystemGlobalCoordinate(int anchor)
{
    return ClampCoordinate((int64_t)anchor * (int64_t)STAR_SYSTEM_SPACING);
}

int SpaceAnchorForLocalCoordinate(float local, int origin)
{
    int64_t global = (int64_t)llroundf(local) + (int64_t)origin;
    int64_t halfSpacing = STAR_SYSTEM_SPACING / 2;
    int64_t anchor = global >= 0 ? (global + halfSpacing) / STAR_SYSTEM_SPACING
                                 : (global - halfSpacing) / STAR_SYSTEM_SPACING;
    return ClampCoordinate(anchor);
}

int SpaceOriginX(void)
{
    return spaceOriginX;
}

int SpaceOriginZ(void)
{
    return spaceOriginZ;
}

void SpaceResetOrigin(void)
{
    spaceOriginX = 0;
    spaceOriginZ = 0;
}

void SpaceSaveOrigin(FILE *file)
{
    if (!file) return;
    fwrite(&spaceOriginX, sizeof(spaceOriginX), 1, file);
    fwrite(&spaceOriginZ, sizeof(spaceOriginZ), 1, file);
}

bool SpaceLoadOrigin(FILE *file)
{
    if (!file) return false;
    int loadedX = 0;
    int loadedZ = 0;
    if (fread(&loadedX, sizeof(loadedX), 1, file) != 1 ||
        fread(&loadedZ, sizeof(loadedZ), 1, file) != 1) {
        return false;
    }
    solarElapsedSimulationTime = 0.0;
    spaceOriginX = loadedX;
    spaceOriginZ = loadedZ;
    return true;
}

bool SpaceSaveState(FILE *file)
{
    if (!file) return false;
    uint32_t magic = SPACE_STATE_MAGIC;
    uint32_t version = SPACE_STATE_VERSION;
    int64_t originX = spaceOriginX;
    int64_t originZ = spaceOriginZ;
    double projectionScale = SPACE_UNITS_GAME_DISTANCE_PER_AU;
    return fwrite(&magic, sizeof(magic), 1, file) == 1 &&
           fwrite(&version, sizeof(version), 1, file) == 1 &&
           fwrite(&solarElapsedSimulationTime,
                  sizeof(solarElapsedSimulationTime), 1, file) == 1 &&
           fwrite(&originX, sizeof(originX), 1, file) == 1 &&
           fwrite(&originZ, sizeof(originZ), 1, file) == 1 &&
           fwrite(&projectionScale, sizeof(projectionScale), 1, file) == 1;
}

bool SpaceLoadState(FILE *file)
{
    spaceLastLoadError = SPACE_LOAD_ERROR_INVALID;
    if (!file) return false;
    uint32_t magic = 0;
    uint32_t version = 0;
    double loadedTime = 0.0;
    int64_t loadedX = 0;
    int64_t loadedZ = 0;
    double projectionScale = 0.0;
    if (fread(&magic, sizeof(magic), 1, file) != 1 ||
        fread(&version, sizeof(version), 1, file) != 1 ||
        magic != SPACE_STATE_MAGIC ||
        (version != SPACE_STATE_VERSION &&
         version != SPACE_STATE_LEGACY_PROJECTION_VERSION) ||
        fread(&loadedTime, sizeof(loadedTime), 1, file) != 1 ||
        fread(&loadedX, sizeof(loadedX), 1, file) != 1 ||
        fread(&loadedZ, sizeof(loadedZ), 1, file) != 1 ||
        fread(&projectionScale, sizeof(projectionScale), 1, file) != 1 ||
        !isfinite(loadedTime) || loadedTime < 0.0 ||
        loadedX < INT_MIN || loadedX > INT_MAX ||
        loadedZ < INT_MIN || loadedZ > INT_MAX ||
        !isfinite(projectionScale) || projectionScale <= 0.0) {
        return false;
    }
    if (!SpaceUnitsWithinRelativeError(
            projectionScale, SPACE_UNITS_GAME_DISTANCE_PER_AU,
            SPACE_UNITS_MAX_RELATIVE_ERROR)) {
        spaceLastLoadError = SPACE_LOAD_ERROR_INCOMPATIBLE_SCALE;
        return false;
    }
    solarElapsedSimulationTime = loadedTime;
    spaceOriginX = (int)loadedX;
    spaceOriginZ = (int)loadedZ;
    spaceLastLoadError = SPACE_LOAD_ERROR_NONE;
    return true;
}

SpaceLoadError SpaceLastLoadError(void)
{
    return spaceLastLoadError;
}

bool SpaceLoadLegacyState(FILE *file)
{
    spaceLastLoadError = SPACE_LOAD_ERROR_INVALID;
    if (!file) return false;
    double loadedTime = 0.0;
    int32_t loadedX = 0;
    int32_t loadedZ = 0;
    if (fread(&loadedTime, sizeof(loadedTime), 1, file) != 1 ||
        fread(&loadedX, sizeof(loadedX), 1, file) != 1 ||
        fread(&loadedZ, sizeof(loadedZ), 1, file) != 1 ||
        !isfinite(loadedTime) || loadedTime < 0.0) return false;
    solarElapsedSimulationTime = loadedTime;
    spaceOriginX = loadedX;
    spaceOriginZ = loadedZ;
    spaceLastLoadError = SPACE_LOAD_ERROR_NONE;
    return true;
}

bool HomeWorldSurfaceIsActive(void)
{
    return homeWorld.surfaceActive && !planetWorld.active;
}

Vector3 HomeWorldCenter(void)
{
    if (!homeWorld.surfaceActive && !planetWorld.active) {
        SolarSystemDef sol;
        if (StarSystemAt(0, 0, &sol) && sol.planetCount > 2) {
            return SolarSystemPlanetCenter(&sol, 2);
        }
    }
    return (Vector3){ (float)SpaceGlobalToLocalX(0), HOME_WORLD_CENTER_Y,
                      (float)SpaceGlobalToLocalZ(0) };
}

float HomeWorldProxyRadius(void)
{
    return HOME_WORLD_PROXY_RADIUS;
}

float HomeWorldSpaceFade(Vector3 position)
{
    if (!SpaceVectorIsFinite(position)) return 0.0f;
    if (planetWorld.active) return 0.0f;
    if (!homeWorld.surfaceActive) return 1.0f;
    return Clamp((position.y - SPACE_EXIT_Y) / (SPACE_ENTER_Y - SPACE_EXIT_Y),
                 0.0f, 1.0f);
}

float PlanetAtmosphereDepth(const PlanetProfile *profile)
{
    float density = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    float typeScale = 1.0f;
    switch (profile->atmosphereType) {
    case PLANET_ATMOSPHERE_NONE:       typeScale = 0.55f; break;
    case PLANET_ATMOSPHERE_THIN:       typeScale = 0.78f; break;
    case PLANET_ATMOSPHERE_BREATHABLE: typeScale = 1.00f; break;
    case PLANET_ATMOSPHERE_DENSE:      typeScale = 1.22f; break;
    case PLANET_ATMOSPHERE_CORROSIVE:  typeScale = 1.12f; break;
    default: break;
    }
    return PLANET_ATMOSPHERE_MIN_DEPTH + density * 48.0f * typeScale;
}

float PlanetWorldAtmosphereFade(Vector3 position)
{
    if (!SpaceVectorIsFinite(position)) return 0.0f;
    if (!planetWorld.active) return 0.0f;

    float depth = PlanetAtmosphereDepth(&planetWorld.profile);
    float progress = Clamp((position.y - PLANET_ATMOSPHERE_FADE_START) / depth,
                           0.0f, 1.0f);
    return progress * progress * (3.0f - 2.0f * progress);
}

void HomeWorldReset(void)
{
    homeWorld.surfaceActive = true;
    homeWorld.returnPosition = (Vector3){ 0.5f, 12.0f, 0.5f };
}

void HomeWorldRestoreLegacyState(const Player *player)
{
    HomeWorldRestoreLegacyStateForSpaceLayer(player, SPACE_LAYER_Y);
}

void HomeWorldRestoreLegacyStateForSpaceLayer(const Player *player,
                                              int storedLayerY)
{
    if (!player) return;
    homeWorld.surfaceActive = !planetWorld.active &&
                              player->position.y < (float)storedLayerY;
    homeWorld.returnPosition = homeWorld.surfaceActive
                                   ? player->position
                                   : (Vector3){ 0.5f, 12.0f, 0.5f };
}

void PlanetWorldMigrateSpaceLayer(int storedLayerY)
{
    if (!planetWorld.active || storedLayerY == SPACE_LAYER_Y) return;
    float offset = (float)(SPACE_LAYER_Y - storedLayerY);
    planetWorld.bodyCenter.y += offset;
    planetWorld.returnPosition.y += offset;
}

float SolarBodyTerrainProxyRadius(float spaceProxyRadius)
{
    // Planet profiles use a large radius so their streamed surface remains
    // comfortable to land on. Space uses a compact system map, so the proxy
    // sphere and its gravity envelope use a scaled radius to keep neighboring
    // orbital bodies from visually and physically intersecting.
    return fmaxf(20.0f, spaceProxyRadius * 0.52f + 2.0f);
}

bool PlanetWorldIsActive(void)
{
    return planetWorld.active;
}

uint32_t PlanetWorldSeed(void)
{
    return planetWorld.seed;
}

SolarBodyStyle PlanetWorldStyle(void)
{
    return planetWorld.style;
}

const PlanetProfile *PlanetWorldProfile(void)
{
    return &planetWorld.profile;
}

SpaceRemnantEnvironment PlanetWorldCurrentRemnantEnvironment(void)
{
    if (!planetWorld.active) return (SpaceRemnantEnvironment){ 0 };
    double simulationTime = SpaceElapsedSimulationTime();
    if (planetWorld.remnantEnvironmentSimulationTime == simulationTime) {
        return planetWorld.remnantEnvironment;
    }

    int systemAx = SpaceAnchorForLocalCoordinate(
        planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(
        planetWorld.bodyCenter.z, spaceOriginZ);
    int orbitIndex = planetWorld.planetIndex - 1;
    SolarSystemDef system;
    SolarSystemRuntimeState runtime;
    if (orbitIndex >= 0 && StarSystemAt(systemAx, systemAz, &system) &&
        SolarSystemEvaluateAtElapsedTime(&system, simulationTime, &runtime) &&
        orbitIndex < runtime.planetCount && runtime.planets[orbitIndex].valid &&
        runtime.planets[orbitIndex].profile.seed == planetWorld.profile.seed &&
        runtime.planets[orbitIndex].profile.receivedIrradiance ==
            planetWorld.profile.receivedIrradiance) {
        planetWorld.remnantEnvironment =
            runtime.planets[orbitIndex].remnantEnvironment;
    }
    planetWorld.remnantEnvironmentSimulationTime = simulationTime;
    return planetWorld.remnantEnvironment;
}

SpaceRemnantEnvironment PlanetWorldRemnantEnvironment(void)
{
    return PlanetWorldCurrentRemnantEnvironment();
}

float PlanetWorldGravityScale(void)
{
    return planetWorld.active ? planetWorld.profile.surfaceGravity : 1.0f;
}

bool PlanetWorldIsDarkSide(void)
{
    if (!planetWorld.active || !planetWorld.profile.tidallyLocked) return false;

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    int orbitIndex = planetWorld.planetIndex - 1;
    SolarSystemDef system;
    if (!StarSystemAt(systemAx, systemAz, &system) ||
        orbitIndex < 0 || orbitIndex >= system.planetCount) return false;

    Vector3 surfaceNormal = Vector3Subtract(planetWorld.returnPosition,
                                             planetWorld.bodyCenter);
    Vector3 currentCenter = SolarSystemPlanetCenter(&system, orbitIndex);
    Vector3 toStar = Vector3Subtract(system.center, currentCenter);
    if (Vector3LengthSqr(surfaceNormal) < 0.001f || Vector3LengthSqr(toStar) < 0.001f) {
        return false;
    }
    surfaceNormal = Vector3Normalize(surfaceNormal);
    toStar = Vector3Normalize(toStar);
    return Vector3DotProduct(surfaceNormal, toStar) < -0.08f;
}

int PlanetWorldOriginX(void)
{
    return planetWorld.originX;
}

int PlanetWorldOriginZ(void)
{
    return planetWorld.originZ;
}

const char *PlanetWorldName(void)
{
    return planetWorld.active ? planetWorld.name : "---";
}

void BuildStarName(unsigned int hash, char *out, size_t outSize)
{
    int p1 = (int)(hash % 24u);
    int p2 = (int)((hash >> 6) % 10u);
    int p3 = (int)((hash >> 12) % 10u);
    snprintf(out, outSize, "%s%s%s", starNamePart1[p1], starNamePart2[p2], starNamePart3[p3]);
}

void ApplyPrimaryStar(SolarSystemDef *system, StellarProfile star)
{
    system->star.spectrum = star.spectrum;
    system->star.stage = star.stage;
    system->star.initialMassSolar = star.initialMassSolar;
    system->star.massKg = star.massKg;
    system->star.radiusKm = star.radiusKm;
    system->star.massSolar = star.massSolar;
    system->star.radiusSolar = star.radiusSolar;
    system->star.temperatureK = star.temperatureK;
    system->star.luminositySolar = star.luminositySolar;
    system->star.ageGyr = star.ageGyr;
    system->star.mainSequenceLifetimeGyr = star.mainSequenceLifetimeGyr;
    system->star.luminousLifetimeGyr = star.luminousLifetimeGyr;
    system->spectrum = star.spectrum;
    system->starProxyRadius = SolarSystemStellarVisualRadius(&star);
}

void BuildSolSystem(SolarSystemDef *out)
{
    memset(out, 0, sizeof(*out));
    out->exists = true;
    out->anchorX = 0;
    out->anchorZ = 0;
    snprintf(out->name, sizeof(out->name), "Sol");
    ApplyPrimaryStar(out, StellarSolarProfile());
    out->center = (Vector3){ 0.0f, STAR_SYSTEM_MID_Y, 0.0f };
    out->formationMetallicity = 1.0f;
    out->formationDiskMassEarth = 60.0f;
    out->snowLineKm = 2.70 * SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    out->habitableZoneInnerKm = 0.75 * SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    out->habitableZoneOuterKm = 1.70 * SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    if (!SolarCatalogValidate()) return;
    out->planetCount = SolarCatalogPlanetCount();
    for (int i = 0; i < out->planetCount; i++) {
        const SolarCatalogPlanet *planet = SolarCatalogPlanetAt(i);
        float proxyRadius = planet->gasGiant ? 48.0f : 42.0f;
        out->planets[i] = (SolarPlanetDef){
            .bodyId = planet->bodyId,
            .semiMajorAxisKm = planet->semiMajorAxisAu *
                               SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
            .physicalRadiusKm = planet->radiusKm,
            .physicalMassKg = planet->massEarth *
                              SPACE_UNITS_EARTH_MASS_KG,
            .rotationPeriodSeconds = planet->rotationPeriodHours * 3600.0,
            .axialTiltRad = (float)(planet->axialTiltDeg * DEG2RAD),
            .formationMassEarth = (float)planet->massEarth,
            .spaceProxyRadius = proxyRadius,
            .yOffset = 0,
            .style = planet->style,
            .formationGasGiant = planet->gasGiant,
            .hasCanonicalOrbit = true,
            .orbitalEccentricity = planet->eccentricity,
            .orbitalInclinationRad = planet->inclinationDeg * DEG2RAD,
            .orbitalLongitudeAscendingNodeRad =
                planet->longitudeAscendingNodeDeg * DEG2RAD,
            .orbitalArgumentPeriapsisRad =
                planet->argumentPeriapsisDeg * DEG2RAD,
            .orbitalMeanAnomalyAtEpochRad =
                planet->meanAnomalyAtEpochDeg * DEG2RAD
        };
        snprintf(out->planets[i].name, sizeof(out->planets[i].name), "%s",
                 planet->name);
    }
    out->satelliteCount = SolarCatalogSatelliteCount();
    for (int i = 0; i < out->satelliteCount; i++) {
        const SolarCatalogSatellite *satellite = SolarCatalogSatelliteAt(i);
        int parentIndex = -1;
        for (int planet = 0; planet < out->planetCount; planet++) {
            if (out->planets[planet].bodyId == satellite->parentBodyId) {
                parentIndex = planet;
                break;
            }
        }
        out->satellites[i] = (SolarSatelliteDef){
            .bodyId = satellite->bodyId,
            .parentPlanetIndex = parentIndex,
            .orbit = satellite->orbit
        };
        snprintf(out->satellites[i].name,
                 sizeof(out->satellites[i].name), "%s", satellite->name);
    }
}

bool SolarSystemPlanetIndexIsValid(const SolarSystemDef *sys, int index)
{
    return sys && sys->planetCount >= 0 &&
           sys->planetCount <= MAX_SOLAR_PLANETS && index >= 0 &&
           index < sys->planetCount;
}

static uint32_t SolarPlanetWorldSeed(const SolarSystemDef *sys, int index)
{
    if (!SolarSystemPlanetIndexIsValid(sys, index) ||
        !SolarSystemPlanetDefinitionIsValid(&sys->planets[index])) {
        return DEFAULT_WORLD_SEED;
    }
    const SolarPlanetDef *def = &sys->planets[index];
    float orbitGame = (float)SpaceUnitsKilometersToGameDistance(
        def->semiMajorAxisKm);
    float legacyAngle =
        (float)(SolarSystemPlanetOrbitHash(sys, index) % 6283u) / 1000.0f;
    int legacyX = ClampCoordinate((int64_t)SpaceSystemGlobalCoordinate(sys->anchorX) +
                                  (int64_t)floorf(cosf(legacyAngle) * orbitGame));
    int legacyZ = ClampCoordinate((int64_t)SpaceSystemGlobalCoordinate(sys->anchorZ) +
                                  (int64_t)floorf(sinf(legacyAngle) * orbitGame));
    uint32_t seed = WorldHash3D(legacyX, index + 1, legacyZ);
    return seed == 0u ? DEFAULT_WORLD_SEED : seed;
}

PlanetProfile SolarPlanetProfileForSnapshot(
    const SolarSystemDef *sys, int index,
    const SolarSystemPhysicalSnapshot *snapshot)
{
    PlanetProfile profile = { 0 };
    if (!SolarSystemPlanetIndexIsValid(sys, index) || !snapshot ||
        !snapshot->valid ||
        snapshot->planetStatuses[index] != SOLAR_PLANET_STABLE) {
        return profile;
    }

    const SolarPlanetDef *def = &sys->planets[index];
    if (!SolarSystemPlanetDefinitionIsValid(def)) return profile;
    const SolarSystemPhysicalSummary *stellar = &snapshot->summary;

    PlanetProfileGenerationInput input = {
        .seed = SolarPlanetWorldSeed(sys, index),
        .semiMajorAxisKm = snapshot->planetOrbits[index].semiMajorAxisKm,
        .physicalRadiusKm = def->physicalRadiusKm,
        .formationMassEarth = def->formationMassEarth,
        .spaceProxyRadius = def->spaceProxyRadius,
        .stellarAgeGyr = stellar->ageGyr,
        .orbitalEccentricity = snapshot->planetOrbits[index].eccentricity,
        .orbitalMeanAnomalyAtEpochRad =
            snapshot->planetOrbits[index].meanAnomalyAtEpochRad,
        .orbitalPeriodGameTime =
            (float)SpaceUnitsSecondsToGameTime(
                SpaceUnitsKeplerPeriodSeconds(
                    snapshot->planetOrbits[index].semiMajorAxisKm,
                    snapshot->planetOrbits[index].centralMassKg)),
        .stellarCount = stellar->stellarCount,
        .planetIndex = index,
        .formationGasGiant = def->formationGasGiant,
        .forcedGasGiant =
            sys->anchorX == 0 && sys->anchorZ == 0 && def->bodyId == 5u
    };
    memcpy(input.stellarLuminositiesSolar,
           stellar->stellarLuminositiesSolar,
           sizeof(input.stellarLuminositiesSolar));
    if (!PlanetProfileGenerate(&input, &profile)) {
        return (PlanetProfile){ 0 };
    }
    if (sys->anchorX == 0 && sys->anchorZ == 0) {
        if (!SolarCatalogApplyPlanetProfile(index, &profile)) {
            return (PlanetProfile){ 0 };
        }
        double rotationGameTime = SpaceUnitsSecondsToGameTime(
            fabs(def->rotationPeriodSeconds));
        if (!(rotationGameTime > 0.0) || !isfinite(rotationGameTime)) {
            return (PlanetProfile){ 0 };
        }
        profile.rotationRate = (float)(360.0 / rotationGameTime);
        PlanetProfileDeriveSeasonalFields(&profile);
    }
    return profile;
}

PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index)
{
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    return SolarPlanetProfileForSnapshot(sys, index, snapshot);
}

void SpaceAdvanceTime(float gameTimeDelta)
{
    if (!(gameTimeDelta > 0.0f) || !isfinite(gameTimeDelta)) return;
    double advanced = solarElapsedSimulationTime + (double)gameTimeDelta;
    if (!isfinite(advanced)) return;
    solarElapsedSimulationTime = advanced;
}

double SpacePeriodicSimulationTime(double elapsedTime)
{
    if (!isfinite(elapsedTime) || elapsedTime < 0.0) return 0.0;
    return elapsedTime > 100000000.0
        ? fmod(elapsedTime, 1000000.0) : elapsedTime;
}

double SpaceSimulationTime(void)
{
    return solarElapsedSimulationTime;
}

double SpaceElapsedSimulationTime(void)
{
    return solarElapsedSimulationTime;
}
