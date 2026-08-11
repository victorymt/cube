#include "space.h"

#include "raymath.h"
#include "chunks.h"
#include "ecology.h"
#include "terrain.h"
#include "particles.h"
#include "space_barycenter.h"
#include "space_illumination.h"
#include "space_physics.h"
#include "space_query_cache.h"
#include "space_satellite.h"
#include "space_system.h"
#include "space_system_physics.h"
#include "space_units.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <float.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ASTEROID_SPACING 26
#define ASTEROID_PROBABILITY 55u
#define SPACE_MESH_REBUILDS_PER_FRAME 2
#define STAR_SYSTEM_MID_Y ((float)SPACE_LAYER_Y + (float)SPACE_LAYER_HEIGHT * 0.5f)
#define STAR_SYSTEM_VERTICAL_RANGE 46.0f
#define STAR_SKY_PHYSICAL_DISTANCE 700.0f
#define STAR_SKY_FULL_LATITUDE_DISTANCE 5000.0f
#define STAR_SKY_LATITUDE_SCALE 0.92f
#define SPACE_GRAVITY_QUERY_RADIUS (STAR_SYSTEM_SPACING * 0.58f)
#define SPACE_STAR_ENCOUNTER_RADIUS_GAME SPACE_GRAVITY_QUERY_RADIUS
#define SPACE_MAX_PLANET_ENCOUNTER_RADIUS_GAME 170.0f
#define SPACE_MAX_SYSTEM_QUERY_DISTANCE (STAR_NAVIGATION_RANGE * 4.0f)
#define PLANET_WORLD_STATE_VERSION 2u

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

typedef struct PlanetWorldContext {
    bool active;
    uint32_t seed;
    SolarBodyStyle style;
    PlanetProfile profile;
    int originX;
    int originZ;
    int planetIndex;
    Vector3 bodyCenter;
    Vector3 returnPosition;
    float spaceProxyRadius;
    char name[32];
} PlanetWorldContext;

typedef struct HomeWorldContext {
    bool surfaceActive;
    Vector3 returnPosition;
} HomeWorldContext;

#define HOME_WORLD_PROXY_RADIUS 62.0f
#define HOME_WORLD_CENTER_Y (-30.0f)
#define HOME_WORLD_LANDING_MARGIN 20.0f
#define PLANET_ATMOSPHERE_FADE_START ((float)WORLD_HEIGHT + 18.0f)
#define PLANET_ATMOSPHERE_MIN_DEPTH 64.0f

static PlanetWorldContext planetWorld = { 0 };
static HomeWorldContext homeWorld = {
    .surfaceActive = true,
    .returnPosition = { 0.5f, 12.0f, 0.5f }
};
static double solarSimulationTime = 0.0;
// World generation uses global integer coordinates. Rendering and physics use
// this nearby local frame, which is periodically shifted during spaceflight.
static int spaceOriginX = 0;
static int spaceOriginZ = 0;

static bool SpaceVectorIsFinite(Vector3 value)
{
    return isfinite(value.x) && isfinite(value.y) && isfinite(value.z);
}

static Vector3 PlanetWorldSpaceDirection(Vector3 skyDirection);
static bool SolarSystemApplyFormation(SolarSystemDef *sys, uint32_t seed);
static bool PlanetProfileIsValid(const PlanetProfile *profile);

#define SPACE_REBASE_THRESHOLD (STAR_SYSTEM_SPACING * 12)

static int ClampCoordinate(int64_t value)
{
    if (value > INT_MAX) return INT_MAX;
    if (value < INT_MIN) return INT_MIN;
    return (int)value;
}

static int SpaceLocalToGlobalX(int localX)
{
    return ClampCoordinate((int64_t)localX + (int64_t)spaceOriginX);
}

static int SpaceLocalToGlobalZ(int localZ)
{
    return ClampCoordinate((int64_t)localZ + (int64_t)spaceOriginZ);
}

static int SpaceGlobalToLocalX(int globalX)
{
    return ClampCoordinate((int64_t)globalX - (int64_t)spaceOriginX);
}

static int SpaceGlobalToLocalZ(int globalZ)
{
    return ClampCoordinate((int64_t)globalZ - (int64_t)spaceOriginZ);
}

static int SpaceSystemGlobalCoordinate(int anchor)
{
    return ClampCoordinate((int64_t)anchor * (int64_t)STAR_SYSTEM_SPACING);
}

static int SpaceAnchorForLocalCoordinate(float local, int origin)
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
    solarSimulationTime = 0.0;
    spaceOriginX = loadedX;
    spaceOriginZ = loadedZ;
    return true;
}

bool SpaceSaveState(FILE *file)
{
    if (!file) return false;
    return fwrite(&solarSimulationTime, sizeof(solarSimulationTime), 1, file) == 1 &&
           fwrite(&spaceOriginX, sizeof(spaceOriginX), 1, file) == 1 &&
           fwrite(&spaceOriginZ, sizeof(spaceOriginZ), 1, file) == 1;
}

bool SpaceLoadState(FILE *file)
{
    if (!file) return false;
    double loadedTime = 0.0;
    int loadedX = 0;
    int loadedZ = 0;
    if (fread(&loadedTime, sizeof(loadedTime), 1, file) != 1 ||
        fread(&loadedX, sizeof(loadedX), 1, file) != 1 ||
        fread(&loadedZ, sizeof(loadedZ), 1, file) != 1 ||
        !isfinite(loadedTime) || loadedTime < 0.0 ||
        loadedTime > 100000000.0) {
        return false;
    }
    solarSimulationTime = loadedTime;
    spaceOriginX = loadedX;
    spaceOriginZ = loadedZ;
    return true;
}

bool HomeWorldSurfaceIsActive(void)
{
    return homeWorld.surfaceActive && !planetWorld.active;
}

Vector3 HomeWorldCenter(void)
{
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

static float PlanetAtmosphereDepth(const PlanetProfile *profile)
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
    if (!player) return;
    homeWorld.surfaceActive = !planetWorld.active &&
                              player->position.y < (float)SPACE_LAYER_Y;
    homeWorld.returnPosition = homeWorld.surfaceActive
                                   ? player->position
                                   : (Vector3){ 0.5f, 12.0f, 0.5f };
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

static void BuildStarName(int ax, int az, char *out, size_t outSize)
{
    unsigned int h = WorldHash2D(ax * 31 + 7, az * 17 + 5);
    int p1 = (int)(h % 24u);
    int p2 = (int)((h >> 6) % 10u);
    int p3 = (int)((h >> 12) % 10u);
    snprintf(out, outSize, "%s%s%s", starNamePart1[p1], starNamePart2[p2], starNamePart3[p3]);
}

static void ApplyPrimaryStar(SolarSystemDef *system, StellarProfile star)
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

static double SolidPlanetRadiusKilometersForProxy(float proxyRadius)
{
    float radiusEarth = 0.72f + (proxyRadius - 40.0f) * 0.095f;
    radiusEarth = Clamp(radiusEarth, 0.62f, 1.55f);
    return (double)radiusEarth * SPACE_UNITS_EARTH_RADIUS_KM;
}

static void BuildSolSystem(SolarSystemDef *out)
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
    out->planetCount = 6;
    static const float orbitGameDistances[6] = {
        180.0f, 260.0f, 340.0f, 430.0f, 520.0f, 650.0f
    };
    static const float proxyRadii[6] = {
        44.0f, 42.0f, 46.0f, 48.0f, 45.0f, 40.0f
    };
    static const SolarBodyStyle styles[6] = {
        SOLAR_STYLE_LAVA, SOLAR_STYLE_ICE, SOLAR_STYLE_DESERT,
        SOLAR_STYLE_GAS, SOLAR_STYLE_CRATER, SOLAR_STYLE_LAVA
    };
    for (int i = 0; i < 6; i++) {
        out->planets[i] = (SolarPlanetDef){
            .semiMajorAxisKm = SpaceUnitsGameDistanceToKilometers(
                orbitGameDistances[i]),
            .physicalRadiusKm = SolidPlanetRadiusKilometersForProxy(
                proxyRadii[i]),
            .formationMassEarth = 0.0f,
            .spaceProxyRadius = proxyRadii[i],
            .yOffset = 0,
            .style = styles[i],
            .formationGasGiant = styles[i] == SOLAR_STYLE_GAS
        };
    }
}

static bool SolarSystemPlanetIndexIsValid(const SolarSystemDef *sys, int index)
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

PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index)
{
    PlanetProfile profile = { 0 };
    if (!SolarSystemPlanetIndexIsValid(sys, index)) return profile;

    const SolarPlanetDef *def = &sys->planets[index];
    if (!SolarSystemPlanetDefinitionIsValid(def)) return profile;
    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    if (!snapshot) return profile;
    const SolarSystemPhysicalSummary *stellar = &snapshot->summary;

    PlanetProfileGenerationInput input = {
        .seed = SolarPlanetWorldSeed(sys, index),
        .semiMajorAxisKm = def->semiMajorAxisKm,
        .physicalRadiusKm = def->physicalRadiusKm,
        .formationMassEarth = def->formationMassEarth,
        .spaceProxyRadius = def->spaceProxyRadius,
        .stellarAgeGyr = stellar->ageGyr,
        .orbitalEccentricity = snapshot->planetOrbits[index].eccentricity,
        .orbitalPeriodGameTime =
            (float)SolarSystemPlanetOrbitPeriodGameTime(sys, index),
        .stellarCount = stellar->stellarCount,
        .planetIndex = index,
        .formationGasGiant = def->formationGasGiant,
        .forcedGasGiant =
            sys->anchorX == 0 && sys->anchorZ == 0 && index == 3
    };
    memcpy(input.stellarLuminositiesSolar,
           stellar->stellarLuminositiesSolar,
           sizeof(input.stellarLuminositiesSolar));
    if (!PlanetProfileGenerate(&input, &profile)) {
        return (PlanetProfile){ 0 };
    }
    return profile;
}

void SpaceAdvanceTime(float gameTimeDelta)
{
    if (!(gameTimeDelta > 0.0f) || !isfinite(gameTimeDelta)) return;
    solarSimulationTime += (double)gameTimeDelta;
    if (solarSimulationTime > 100000000.0) {
        solarSimulationTime = fmod(solarSimulationTime, 1000000.0);
    }
}

double SpaceSimulationTime(void)
{
    return solarSimulationTime;
}

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
           (float)solarSimulationTime * body->profile.rotationRate;
}

static bool StarSystemDefinitionAt(int ax, int az, SolarSystemDef *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (ax == 0 && az == 0) {
        BuildSolSystem(out);
        if (!SolarSystemPhysicalSnapshotBuild(out, &out->physicalSnapshot)) {
            return false;
        }
        out->center.x = 0.0f;
        out->center.z = 0.0f;
        for (int i = 0; i < out->planetCount; i++) {
            out->planets[i].style = SolarPlanetProfile(out, i).style;
        }
        return SolarSystemPhysicalSnapshotBuildSatellites(
            out, &out->physicalSnapshot);
    }

    out->exists = false;
    out->anchorX = ax;
    out->anchorZ = az;
    out->planetCount = 0;

    unsigned int roll = WorldHash2D(ax, az);
    if (roll % 100u >= STAR_SYSTEM_PROBABILITY) return false;

    unsigned int h = WorldHash2D(ax * 31 + 7, az * 17 + 5);
    out->exists = true;
    BuildStarName(ax, az, out->name, sizeof(out->name));
    ApplyPrimaryStar(out, StellarGenerate(h ^ 0xd1b54a35u));
    int verticalOffset = (int)((h >> 14) % 93u) - 46;
    out->center = (Vector3){
        (float)SpaceSystemGlobalCoordinate(ax),
        STAR_SYSTEM_MID_Y + (float)verticalOffset,
        (float)SpaceSystemGlobalCoordinate(az)
    };
    if (!SolarSystemApplyFormation(out, h)) return false;
    for (int i = 0; i < out->planetCount; i++) {
        out->planets[i].style = SolarPlanetProfile(out, i).style;
    }
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
    return SolarSystemPlanetPositionAtTime(sys, index, solarSimulationTime);
}

bool SolarSystemPlanetStateAtTime(const SolarSystemDef *sys, int index,
                                  double simulationTime,
                                  SolarPlanetOrbitalState *out)
{
    if (!out) return false;
    *out = (SolarPlanetOrbitalState){ 0 };
    if (!sys || !isfinite(simulationTime) ||
        sys->planetCount < 0 || sys->planetCount > MAX_SOLAR_PLANETS ||
        index < 0 || index >= sys->planetCount ||
        !SpaceVectorIsFinite(sys->center)) return false;

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    if (!snapshot) return false;

    SpaceKeplerState relative;
    if (!SpaceKeplerStateAtTime(&snapshot->planetOrbits[index],
                                simulationTime, &relative)) {
        return false;
    }
    out->center = Vector3Add(sys->center, relative.positionGame);
    out->velocity = relative.velocityGame;
    if (!SpaceVectorIsFinite(out->center) ||
        !SpaceVectorIsFinite(out->velocity)) {
        *out = (SolarPlanetOrbitalState){ 0 };
        return false;
    }
    return true;
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
            !isfinite(body->spaceProxyRadius)) {
            return false;
        }
    }
    for (int planet = 0; planet < runtime->planetCount; planet++) {
        const SolarPlanetRuntimeState *state = &runtime->planets[planet];
        if (!state->valid || !isfinite(state->center.x) ||
            !isfinite(state->center.y) || !isfinite(state->center.z) ||
            !isfinite(state->velocity.x) || !isfinite(state->velocity.y) ||
            !isfinite(state->velocity.z) ||
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

static bool SolarSystemEvaluateUncachedAtTime(
    const SolarSystemDef *sys, double simulationTime,
    SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!sys || !isfinite(simulationTime) || sys->planetCount < 0 ||
        sys->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(sys, &scratch);
    if (!snapshot) return false;
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
        SolarPlanetOrbitalState orbitalState;
        if (!SolarSystemPlanetStateAtTime(sys, index, simulationTime,
                                          &orbitalState)) {
            return false;
        }
        planet->profile = SolarPlanetProfile(sys, index);
        if (!PlanetProfileIsValid(&planet->profile) ||
            !(planet->profile.massKg > 0.0) ||
            !(planet->profile.physicalRadiusKm > 0.0) ||
            !(planet->profile.spaceProxyRadius > 0.0f)) {
            return false;
        }
        planet->center = orbitalState.center;
        planet->velocity = orbitalState.velocity;
        planet->currentIrradianceEarth = SolarSystemIrradianceAt(
            sources, stellarCount, planet->center);
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

static void SolarSystemRuntimeToRelative(const SolarSystemDef *system,
                                         SolarSystemRuntimeState *runtime)
{
    if (!system || !runtime) return;
    for (int star = 0; star < runtime->stellarCount; star++) {
        runtime->stars[star].center = Vector3Subtract(
            runtime->stars[star].center, system->center);
    }
    for (int planet = 0; planet < runtime->planetCount; planet++) {
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
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.innerPhaseRad));
    hash = SolarSystemSignatureMix(
        hash, SolarSystemDoubleBits(snapshot->stellarOrbit.outerPhaseRad));
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
    hash = SolarSystemSignatureMix(hash, (uint32_t)system->planetCount);
    for (int i = 0; i < system->planetCount; i++) {
        const SolarPlanetDef *planet = &system->planets[i];
        const SpaceKeplerOrbit *orbit = &snapshot->planetOrbits[i];
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->semiMajorAxisKm));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(planet->physicalRadiusKm));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(planet->formationMassEarth));
        hash = SolarSystemSignatureMix(
            hash, SolarSystemFloatBits(planet->spaceProxyRadius));
        hash = SolarSystemSignatureMix(hash, (uint32_t)planet->yOffset);
        hash = SolarSystemSignatureMix(hash, (uint32_t)planet->style);
        hash = SolarSystemSignatureMix(hash, planet->formationGasGiant);
        hash = SolarSystemSignatureMix(
            hash, SolarSystemDoubleBits(orbit->eccentricity));
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
        for (int i = 0; i < system->planetCount; i++) {
            const SpaceSatelliteOrbit *orbit = &snapshot->satelliteOrbits[i];
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

static bool SolarSystemEvaluateCachedAtTime(
    const SolarSystemDef *system, double simulationTime,
    SolarSystemRuntimeState *out)
{
    if (!out) return false;
    *out = (SolarSystemRuntimeState){ 0 };
    if (!system || !isfinite(simulationTime) ||
        !SpaceVectorIsFinite(system->center) || system->planetCount < 0 ||
        system->planetCount > MAX_SOLAR_PLANETS) {
        return false;
    }
    if (!system->physicalSnapshot.valid) {
        return SolarSystemEvaluateUncachedAtTime(system, simulationTime, out);
    }
    SolarSystemPhysicalSnapshot signatureScratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(system, &signatureScratch);
    if (!snapshot) return false;
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
    if (!SolarSystemEvaluateUncachedAtTime(system, simulationTime,
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

bool SolarSystemEvaluateAtTime(const SolarSystemDef *sys,
                               double simulationTime,
                               SolarSystemRuntimeState *out)
{
    return SolarSystemEvaluateCachedAtTime(sys, simulationTime, out);
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

static bool SolarSystemApplyFormation(SolarSystemDef *sys, uint32_t seed)
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
    return SolarSystemEvaluateAtTime(sys, solarSimulationTime, &runtime)
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

static Vector3 PlanetSurfaceNormalAt(Vector3 surfacePosition)
{
    float radius = fmaxf(planetWorld.spaceProxyRadius, 24.0f);
    float longitude = surfacePosition.x / (radius * 0.82f);
    float latitude = surfacePosition.z / (radius * 0.82f);
    float cosLatitude = cosf(latitude);
    return Vector3Normalize((Vector3){
        sinf(longitude) * cosLatitude,
        cosf(longitude) * cosLatitude,
        sinf(latitude)
    });
}

static Vector3 PlanetRotateY(Vector3 direction, float angle)
{
    float c = cosf(angle);
    float s = sinf(angle);
    return (Vector3){
        direction.x * c + direction.z * s,
        direction.y,
        -direction.x * s + direction.z * c
    };
}

static SpaceSatelliteVector3 SatelliteVectorFromGame(Vector3 gameVector)
{
    double scale = SPACE_UNITS_KILOMETERS_PER_GAME_DISTANCE;
    return (SpaceSatelliteVector3){
        (double)gameVector.x * scale,
        (double)gameVector.y * scale,
        (double)gameVector.z * scale
    };
}

static SpaceSatelliteVector3 SatelliteVectorFromDirection(Vector3 direction,
                                                           double lengthKm)
{
    return (SpaceSatelliteVector3){
        (double)direction.x * lengthKm,
        (double)direction.y * lengthKm,
        (double)direction.z * lengthKm
    };
}

static Vector3 SatelliteVectorToRaylib(SpaceSatelliteVector3 value)
{
    return (Vector3){ (float)value.x, (float)value.y, (float)value.z };
}

static SpaceSatelliteVector3 SatelliteVectorSubtract(
    SpaceSatelliteVector3 a, SpaceSatelliteVector3 b)
{
    return (SpaceSatelliteVector3){ a.x - b.x, a.y - b.y, a.z - b.z };
}

static double SatelliteVectorLength(SpaceSatelliteVector3 value)
{
    return sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

static double SatelliteVectorDot(SpaceSatelliteVector3 a,
                                 SpaceSatelliteVector3 b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

static SpaceSatelliteVector3 SatelliteVectorNormalize(
    SpaceSatelliteVector3 value)
{
    double length = SatelliteVectorLength(value);
    if (!(length > 0.0)) return (SpaceSatelliteVector3){ 0 };
    return (SpaceSatelliteVector3){ value.x / length, value.y / length,
                                    value.z / length };
}

bool SolarPlanetSatelliteOrbit(const SolarSystemDef *system, int planetIndex,
                               const PlanetProfile *profile,
                               SpaceSatelliteOrbit *out)
{
    if (!out) return false;
    *out = (SpaceSatelliteOrbit){ 0 };
    if (!system || !profile || system->planetCount < 0 ||
        system->planetCount > MAX_SOLAR_PLANETS || planetIndex < 0 ||
        planetIndex >= system->planetCount) {
        return false;
    }

    SolarSystemPhysicalSnapshot scratch;
    const SolarSystemPhysicalSnapshot *snapshot =
        SolarSystemPhysicalSnapshotForSystem(system, &scratch);
    if (!snapshot) return false;
    if (!snapshot->satellitesBuilt) {
        scratch = *snapshot;
        if (!SolarSystemPhysicalSnapshotBuildSatellites(system, &scratch)) {
            return false;
        }
        snapshot = &scratch;
    }
    const SpaceSatelliteOrbit orbit = snapshot->satelliteOrbits[planetIndex];
    if (!orbit.exists || !(orbit.semiMajorAxisKm > 0.0) ||
        !isfinite(orbit.semiMajorAxisKm) || orbit.eccentricity < 0.0 ||
        orbit.eccentricity >= 1.0 || !isfinite(orbit.eccentricity) ||
        !isfinite(orbit.inclinationRad) ||
        !isfinite(orbit.longitudeAscendingNodeRad) ||
        !isfinite(orbit.argumentPeriapsisRad) ||
        !isfinite(orbit.meanAnomalyAtEpochRad) || !(orbit.radiusKm > 0.0) ||
        !isfinite(orbit.radiusKm) || !(orbit.massKg > 0.0) ||
        !isfinite(orbit.massKg)) {
        return false;
    }
    *out = orbit;
    return true;
}

static float PlanetRingShadowForPoint(Vector3 surfacePosition, Vector3 sunDirection)
{
    if (!planetWorld.profile.hasRings) return 0.0f;

    Vector3 surfaceNormal = PlanetSurfaceNormalAt(surfacePosition);
    Vector3 surfacePoint = Vector3Scale(surfaceNormal,
                                        planetWorld.spaceProxyRadius);
    float tilt = planetWorld.profile.ringTilt;
    Vector3 ringNormal = { 0.0f, cosf(tilt), sinf(tilt) };
    float denominator = Vector3DotProduct(ringNormal, sunDirection);
    if (fabsf(denominator) < 0.0001f) return 0.0f;

    float distanceAlongRay = -Vector3DotProduct(ringNormal, surfacePoint) / denominator;
    if (distanceAlongRay <= 0.0f) return 0.0f;

    Vector3 ringHit = Vector3Add(surfacePoint,
                                 Vector3Scale(sunDirection, distanceAlongRay));
    float ringRadius = Vector3Length(ringHit);
    float innerRadius = planetWorld.spaceProxyRadius * 1.30f;
    float outerRadius = planetWorld.spaceProxyRadius * 1.86f;
    if (ringRadius < innerRadius || ringRadius > outerRadius) return 0.0f;

    float band = (ringRadius - innerRadius) / fmaxf(outerRadius - innerRadius, 0.001f);
    return 0.42f + 0.22f * (0.5f + 0.5f * sinf(band * 18.0f));
}

static bool PlanetWorldMoonGeometryAt(
    const SolarPlanetRuntimeState *runtimePlanet,
    SpaceSatelliteVector3 observerPositionKm, float spinPhase,
    PlanetLightState *out, SpaceSatelliteOrbit *outOrbit,
    SpaceSatelliteVector3 *outPositionKm)
{
    if (!runtimePlanet || !out || !outOrbit || !outPositionKm ||
        !runtimePlanet->satelliteOrbit.exists) {
        return false;
    }
    *outOrbit = runtimePlanet->satelliteOrbit;
    *outPositionKm = runtimePlanet->satelliteState.positionKm;
    SpaceSatelliteVector3 observerToSatelliteKm = SatelliteVectorSubtract(
        *outPositionKm, observerPositionKm);
    Vector3 moonInertialDirection = Vector3Normalize(
        SatelliteVectorToRaylib(observerToSatelliteKm));
    out->moonDirection = PlanetRotateY(
        Vector3Normalize(PlanetWorldSkyDirection(moonInertialDirection)),
        -spinPhase);
    double moonDistanceKm = SatelliteVectorLength(observerToSatelliteKm);
    if (moonDistanceKm > outOrbit->radiusKm) {
        out->moonAngularRadius = (float)asin(Clamp(
            (float)(outOrbit->radiusKm / moonDistanceKm), 0.0f, 1.0f));
    }
    out->hasMoon = true;
    return true;
}

static double PlanetWorldStellarOccultationAt(
    int sourceIndex, int sourceCount, const SolarLightSource *sources,
    Vector3 planetCenter, float sourceDistance,
    SpaceSatelliteVector3 sourcePositionKm)
{
    double occultationTotal = 0.0;
    for (int otherIndex = 0; otherIndex < sourceCount; otherIndex++) {
        if (sourceIndex == otherIndex) continue;
        Vector3 toOther = Vector3Subtract(
            sources[otherIndex].center, planetCenter);
        float otherDistance = Vector3Length(toOther);
        if (otherDistance >= sourceDistance || otherDistance < 0.001f) {
            continue;
        }
        SpaceSatelliteVector3 otherPositionKm =
            SatelliteVectorFromGame(toOther);
        double occultation = SpaceIlluminationOccultationFraction(
            (SpaceIlluminationBody){
                .positionKm = {
                    otherPositionKm.x, otherPositionKm.y,
                    otherPositionKm.z
                },
                .radiusKm = sources[otherIndex].stellar.radiusKm
            },
            (SpaceIlluminationBody){
                .positionKm = {
                    sourcePositionKm.x, sourcePositionKm.y,
                    sourcePositionKm.z
                },
                .radiusKm = sources[sourceIndex].stellar.radiusKm
            });
        occultationTotal = 1.0 -
            (1.0 - occultationTotal) * (1.0 - occultation);
    }
    return occultationTotal;
}

static void PlanetWorldMoonIlluminationAt(
    const SolarLightSource *sources, int sourceCount,
    Vector3 planetCenter, SpaceSatelliteVector3 observerPositionKm,
    SpaceSatelliteVector3 satellitePositionKm, double satelliteRadiusKm,
    double planetRadiusKm, const SpaceSatelliteVector3 *sourcePositionsKm,
    PlanetLightState *out)
{
    SpaceSatelliteVector3 moonToObserver = SatelliteVectorNormalize(
        SatelliteVectorSubtract(observerPositionKm, satellitePositionKm));
    double illuminatedWeight = 0.0;
    double moonLightWeight = 0.0;
    for (int i = 0; i < sourceCount; i++) {
        SpaceSatelliteVector3 moonToSource = SatelliteVectorNormalize(
            SatelliteVectorSubtract(sourcePositionsKm[i],
                                    satellitePositionKm));
        double phase = (double)Clamp(
            (float)((1.0 +
                     SatelliteVectorDot(moonToSource, moonToObserver)) *
                    0.5),
            0.0f, 1.0f);
        double umbra = SpaceSatellitePlanetUmbraFraction(
            satellitePositionKm, satelliteRadiusKm, planetRadiusKm,
            sourcePositionsKm[i], sources[i].stellar.radiusKm);
        double sourceWeight = SolarLightIrradianceAt(&sources[i],
                                                     planetCenter);
        illuminatedWeight += phase * sourceWeight * (1.0 - umbra);
        moonLightWeight += sourceWeight;
        out->moonUmbra = fmaxf(out->moonUmbra, (float)umbra);
    }
    if (moonLightWeight > 0.0) {
        out->moonIllumination = Clamp(
            (float)(illuminatedWeight / moonLightWeight), 0.0f, 1.0f);
    }
}

static bool PlanetWorldLightStateForFiniteSurface(
    Vector3 surfacePosition, PlanetLightState *out)
{
    if (!out) return false;
    *out = (PlanetLightState){ 0 };
    if (!planetWorld.active || !planetWorld.profile.hasSolidSurface) {
        return false;
    }

    SolarSystemDef system = { 0 };
    if (!SurfaceHostSystem(&system)) return false;
    int orbitIndex = planetWorld.planetIndex - 1;
    if (orbitIndex < 0 || orbitIndex >= system.planetCount) return false;

    SolarSystemRuntimeState runtime;
    if (!SolarSystemEvaluateAtTime(&system, solarSimulationTime, &runtime)) {
        return false;
    }
    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    int sourceCount = SolarSystemRuntimeLightSources(
        &runtime, sources, MAX_SOLAR_LIGHTS);
    if (sourceCount <= 0) return false;

    if (orbitIndex >= MAX_SOLAR_PLANETS ||
        !runtime.planets[orbitIndex].valid) return false;
    Vector3 planetCenter = runtime.planets[orbitIndex].center;
    float spinPhase = (float)(planetWorld.seed & 0xffffu) / 65535.0f * 2.0f * PI +
                      (float)solarSimulationTime * planetWorld.profile.rotationRate * DEG2RAD;
    Vector3 surfaceNormal = PlanetSurfaceNormalAt(surfacePosition);
    Vector3 inertialSurfaceNormal = PlanetWorldSpaceDirection(
        PlanetRotateY(surfaceNormal, spinPhase));
    SpaceSatelliteVector3 observerPositionKm = SatelliteVectorFromDirection(
        Vector3Normalize(inertialSurfaceNormal),
        planetWorld.profile.physicalRadiusKm);

    const SolarPlanetRuntimeState *runtimePlanet =
        &runtime.planets[orbitIndex];
    SpaceSatelliteOrbit satellite = { 0 };
    bool hasMoon = false;
    SpaceSatelliteVector3 satellitePositionKm = { 0 };
    hasMoon = PlanetWorldMoonGeometryAt(
        runtimePlanet, observerPositionKm, spinPhase, out, &satellite,
        &satellitePositionKm);

    float totalWeight = 0.0f;
    Vector3 weightedDirection = Vector3Zero();
    float weightedR = 0.0f;
    float weightedG = 0.0f;
    float weightedB = 0.0f;
    SpaceSatelliteVector3 sourcePositionsKm[MAX_SOLAR_LIGHTS] = { 0 };

    for (int i = 0; i < sourceCount; i++) {
        Vector3 toSource = Vector3Subtract(sources[i].center, planetCenter);
        float distance = Vector3Length(toSource);
        if (distance < 0.001f) continue;
        sourcePositionsKm[i] = SatelliteVectorFromGame(toSource);

        // Convert the inertial star direction into the rotating planet frame.
        // The inverse rotation keeps a tidally locked face pointed at its star.
        Vector3 direction = PlanetWorldSkyDirection(toSource);
        direction = PlanetRotateY(Vector3Normalize(direction), -spinPhase);
        float weight = SolarLightIrradianceAt(&sources[i], planetCenter);
        double stellarOccultation = PlanetWorldStellarOccultationAt(
            i, sourceCount, sources, planetCenter, distance,
            sourcePositionsKm[i]);
        float sourceVisibility = 1.0f;
        if (stellarOccultation > 0.001) {
            weight *= fmaxf(0.01f, 1.0f - (float)stellarOccultation);
            sourceVisibility = fmaxf(
                0.06f, 1.0f - (float)stellarOccultation * 0.94f);
            out->sourceOccultations[i] = (float)stellarOccultation;
            out->eclipse = fmaxf(out->eclipse, (float)stellarOccultation);
            out->specialEclipse = true;
        }
        if (hasMoon) {
            double occultation = SpaceSatelliteSolarOccultationFraction(
                observerPositionKm, satellitePositionKm, satellite.radiusKm,
                sourcePositionsKm[i], sources[i].stellar.radiusKm);
            double combinedOccultation = 1.0 -
                (1.0 - stellarOccultation) * (1.0 - occultation);
            out->sourceOccultations[i] = (float)combinedOccultation;
            if (occultation > 0.001) {
                weight *= fmaxf(0.01f, 1.0f - (float)occultation);
                sourceVisibility *= fmaxf(0.06f,
                                          1.0f - (float)occultation * 0.94f);
                out->eclipse = fmaxf(out->eclipse, (float)occultation);
                out->specialEclipse = true;
            }
        }
        Color color = SpectrumColor(sources[i].spectrum);
        out->sourceDirections[i] = direction;
        out->sourceColors[i] = color;
        out->sourceIntensities[i] = weight;
        out->sourceVisibility[i] = sourceVisibility;
        totalWeight += weight;
        weightedDirection = Vector3Add(weightedDirection, Vector3Scale(direction, weight));
        weightedR += (float)color.r * weight;
        weightedG += (float)color.g * weight;
        weightedB += (float)color.b * weight;
    }

    if (Vector3LengthSqr(weightedDirection) < 0.000001f ||
        totalWeight <= 0.0f) {
        *out = (PlanetLightState){ 0 };
        return false;
    }
    Vector3 sunDirection = Vector3Normalize(weightedDirection);
    float incidence = Vector3DotProduct(surfaceNormal, sunDirection);
    float incidentIrradiance = fmaxf(incidence, 0.0f) * totalWeight;
    float daylight = 1.0f - expf(-incidentIrradiance * 1.45f);

    if (hasMoon) {
        PlanetWorldMoonIlluminationAt(
            sources, sourceCount, planetCenter, observerPositionKm,
            satellitePositionKm, satellite.radiusKm,
            planetWorld.profile.physicalRadiusKm, sourcePositionsKm, out);
    }

    out->sunDirection = sunDirection;
    out->daylight = daylight;
    out->sunset = incidence > 0.0f ?
                  powf(1.0f - Clamp(incidence, 0.0f, 1.0f), 2.0f) *
                  Clamp(sqrtf(totalWeight), 0.0f, 1.0f) : 0.0f;
    out->ringShadow = PlanetRingShadowForPoint(surfacePosition, sunDirection);
    out->daylight *= (1.0f - out->ringShadow * 0.72f);
    out->daylight = Clamp(out->daylight, 0.0f, 1.0f);
    out->totalIntensity = totalWeight;
    out->sourceCount = sourceCount;
    out->starColor = (Color){
        (unsigned char)Clamp(weightedR / totalWeight, 0.0f, 255.0f),
        (unsigned char)Clamp(weightedG / totalWeight, 0.0f, 255.0f),
        (unsigned char)Clamp(weightedB / totalWeight, 0.0f, 255.0f),
        255
    };
    return true;
}

bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out)
{
    if (!out) return false;
    *out = (PlanetLightState){ 0 };
    if (!SpaceVectorIsFinite(surfacePosition)) return false;
    return PlanetWorldLightStateForFiniteSurface(surfacePosition, out);
}

float PlanetWorldDaylightAt(Vector3 surfacePosition)
{
    PlanetLightState state;
    return PlanetWorldLightStateAt(surfacePosition, &state) ? state.daylight : 0.0f;
}

Vector3 PlanetWorldSpaceReference(void)
{
    if (!planetWorld.active) return Vector3Zero();

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    int orbitIndex = planetWorld.planetIndex - 1;
    SolarSystemDef system;
    if (StarSystemAt(systemAx, systemAz, &system) &&
        orbitIndex >= 0 && orbitIndex < system.planetCount) {
        return SolarSystemPlanetCenter(&system, orbitIndex);
    }
    return planetWorld.bodyCenter;
}

Vector3 PlanetWorldSkyDirection(Vector3 worldDirection)
{
    if (!SpaceVectorIsFinite(worldDirection)) return Vector3Zero();
    if (!planetWorld.active) return worldDirection;

    Vector3 up = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(up) < 0.001f) up = (Vector3){ 0.0f, 1.0f, 0.0f };
    else up = Vector3Normalize(up);

    Vector3 reference = fabsf(up.y) > 0.92f ? (Vector3){ 0.0f, 0.0f, 1.0f }
                                            : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 east = Vector3Normalize(Vector3CrossProduct(reference, up));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(up, east));
    return (Vector3){
        Vector3DotProduct(worldDirection, east),
        Vector3DotProduct(worldDirection, up),
        Vector3DotProduct(worldDirection, north)
    };
}

static Vector3 PlanetWorldSpaceDirection(Vector3 skyDirection)
{
    if (!SpaceVectorIsFinite(skyDirection)) return Vector3Zero();
    if (!planetWorld.active) return skyDirection;

    Vector3 up = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(up) < 0.001f) up = (Vector3){ 0.0f, 1.0f, 0.0f };
    else up = Vector3Normalize(up);

    Vector3 reference = fabsf(up.y) > 0.92f ? (Vector3){ 0.0f, 0.0f, 1.0f }
                                            : (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 east = Vector3Normalize(Vector3CrossProduct(reference, up));
    Vector3 north = Vector3Normalize(Vector3CrossProduct(up, east));
    return Vector3Add(Vector3Add(Vector3Scale(east, skyDirection.x),
                                 Vector3Scale(up, skyDirection.y)),
                      Vector3Scale(north, skyDirection.z));
}

bool SurfaceHostSystem(SolarSystemDef *out)
{
    if (!out) return false;
    *out = (SolarSystemDef){ 0 };
    if (HomeWorldSurfaceIsActive()) return StarSystemAt(0, 0, out);
    if (!planetWorld.active) return false;

    int systemAx = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.x, spaceOriginX);
    int systemAz = SpaceAnchorForLocalCoordinate(planetWorld.bodyCenter.z, spaceOriginZ);
    return StarSystemAt(systemAx, systemAz, out);
}

Color SpectrumColor(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return (Color){ 255, 120, 90, 255 };
    case SPECTRUM_ORANGE:    return (Color){ 255, 170, 90, 255 };
    case SPECTRUM_YELLOW:    return (Color){ 255, 214, 120, 255 };
    case SPECTRUM_BLUE_WHITE: return (Color){ 190, 210, 255, 255 };
    case SPECTRUM_RED_GIANT: return (Color){ 255, 90, 60, 255 };
    default:                 return (Color){ 255, 214, 120, 255 };
    }
}

const char *SpectrumName(SpectrumType type)
{
    switch (type) {
    case SPECTRUM_RED_DWARF: return "Red Dwarf";
    case SPECTRUM_ORANGE:    return "Orange Star";
    case SPECTRUM_YELLOW:    return "Yellow Sun";
    case SPECTRUM_BLUE_WHITE: return "Blue-White Star";
    case SPECTRUM_RED_GIANT: return "Red Giant";
    default:                 return "Star";
    }
}

const char *SolarStyleName(SolarBodyStyle style)
{
    switch (style) {
    case SOLAR_STYLE_LAVA:   return "Lava Planet";
    case SOLAR_STYLE_ICE:    return "Ice Planet";
    case SOLAR_STYLE_DESERT: return "Desert Planet";
    case SOLAR_STYLE_GAS:    return "Gas Giant";
    case SOLAR_STYLE_CRATER: return "Cratered World";
    case SOLAR_STYLE_TEMPERATE: return "Temperate World";
    default:                 return "Planet";
    }
}

const char *PlanetAtmosphereName(PlanetAtmosphereType type)
{
    switch (type) {
    case PLANET_ATMOSPHERE_NONE:       return "Airless";
    case PLANET_ATMOSPHERE_THIN:       return "Thin atmosphere";
    case PLANET_ATMOSPHERE_BREATHABLE: return "Breathable atmosphere";
    case PLANET_ATMOSPHERE_DENSE:      return "Dense atmosphere";
    case PLANET_ATMOSPHERE_CORROSIVE:  return "Corrosive atmosphere";
    default:                           return "Unknown atmosphere";
    }
}

static bool SpacePointInSolarSystemBubble(int x, int z)
{
    int centerAx = FloorDivInt(SpaceLocalToGlobalX(x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ(z), STAR_SYSTEM_SPACING);
    for (int ax = centerAx - 1; ax <= centerAx + 1; ax++) {
        for (int az = centerAz - 1; az <= centerAz + 1; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            float dx = (float)x - sys.center.x;
            float dz = (float)z - sys.center.z;
            // The largest orbit plus a small margin defines the clean system area.
            if (dx * dx + dz * dz <= 780.0f * 780.0f) return true;
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

static bool SpaceQueryVectorIsFinite(Vector3 value)
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

static float PlanetEncounterRadiusGame(double semiMajorAxisKm,
                                       double bodyMassKg,
                                       double parentMassKg,
                                       float landingRadiusGame)
{
    if (!(semiMajorAxisKm > 0.0) || !(bodyMassKg > 0.0) ||
        !(parentMassKg > 0.0) || !(landingRadiusGame > 0.0f)) {
        return 0.0f;
    }
    float orbitRadiusGame = (float)SpaceUnitsKilometersToGameDistance(
        semiMajorAxisKm);
    float minimum = landingRadiusGame * 2.20f;
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
        .center = center,
        .velocity = planet->velocity,
        .physicalRadiusKm = profile->physicalRadiusKm,
        .semiMajorAxisKm = system->planets[index].semiMajorAxisKm,
        .parentMassKg = parentMassKg,
        .spaceProxyRadius = profile->spaceProxyRadius,
        .landingProxyRadius = landingRadius,
        .encounterRadiusGame = PlanetEncounterRadiusGame(
            system->planets[index].semiMajorAxisKm, profile->massKg,
            parentMassKg, landingRadius),
        .currentIrradianceEarth = planet->currentIrradianceEarth,
        .dist = Vector3Distance(center, observer),
        .isStar = false,
        .index = index + 1,
        .systemAnchorX = system->anchorX,
        .systemAnchorZ = system->anchorZ,
        .worldSeed = profile->seed,
        .hostStar = system->star,
        .spectrum = system->spectrum,
        .style = profile->style,
        .profile = *profile
    };
    snprintf(out->name, sizeof(out->name), "%s", system->name);
    return true;
}

static bool PlanetBodyInfoForSystem(const SolarSystemDef *system, int index,
                                    Vector3 observer, SpaceBodyInfo *out)
{
    SolarSystemRuntimeState runtime;
    return SolarSystemEvaluateCachedAtTime(system, solarSimulationTime,
                                           &runtime) &&
           PlanetBodyInfoForRuntime(system, &runtime, index, observer, out);
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
            if (!SolarSystemEvaluateCachedAtTime(
                    &sys, solarSimulationTime, &runtime)) {
                continue;
            }
            int starCount = runtime.stellarCount;
            double parentMassKg = runtime.totalStellarMassKg;
            for (int starIndex = 0; starIndex < starCount; starIndex++) {
                float starDist = Vector3Distance(
                    runtime.stars[starIndex].center, pos);
                if (starDist > maxDist) continue;
                SpaceBodyInfo body = {
                    .center = runtime.stars[starIndex].center,
                    .velocity = runtime.stars[starIndex].velocity,
                    .physicalRadiusKm = runtime.stars[starIndex].stellar.radiusKm,
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
                    .spectrum = runtime.stars[starIndex].spectrum
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
        if (!SolarSystemEvaluateAtTime(system, solarSimulationTime,
                                       &runtime)) {
            continue;
        }
        for (int planetIndex = 0; planetIndex < runtime.planetCount;
             planetIndex++) {
            const SolarPlanetRuntimeState *planet =
                &runtime.planets[planetIndex];
            if (!planet->satelliteOrbit.exists) continue;
            float distance = Vector3Distance(planet->satelliteCenter, pos);
            if (distance > maxDist) continue;
            double hillSphereKm = SpaceUnitsHillSphereKm(
                planet->satelliteOrbit.semiMajorAxisKm,
                planet->satelliteOrbit.massKg, planet->profile.massKg);
            double physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
                planet->satelliteOrbit.radiusKm);
            float minimumEncounter = (float)(physicalRadiusGame * 2.20);
            float encounter = fmaxf(
                minimumEncounter,
                fminf((float)SpaceUnitsKilometersToGameDistance(
                          hillSphereKm * 0.50), 24.0f));
            SpaceSatelliteInfo candidate = {
                .center = planet->satelliteCenter,
                .velocity = planet->satelliteVelocity,
                .physicalRadiusKm = planet->satelliteOrbit.radiusKm,
                .massKg = planet->satelliteOrbit.massKg,
                .semiMajorAxisKm = planet->satelliteOrbit.semiMajorAxisKm,
                .encounterRadiusGame = encounter,
                .dist = distance,
                .isSatellite = true,
                .parentPlanetIndex = planetIndex,
                .systemAnchorX = system->anchorX,
                .systemAnchorZ = system->anchorZ,
                .worldSeed = planet->profile.seed,
                .orbit = planet->satelliteOrbit,
                .state = planet->satelliteState
            };
            snprintf(candidate.name, sizeof(candidate.name), "%s Moon %c",
                     system->name, 'a' + planetIndex);
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
    if (SpaceSatellitesNear(observer, 1000.0f, &satellite, 1) != 1) {
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
    if (!SolarSystemEvaluateAtTime(&system, solarSimulationTime, &runtime) ||
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
    out->withinErrorBudget = isfinite(out->physicalRadiusGame) &&
                             out->physicalRadiusGame > 0.0 &&
                             isfinite(out->physicalGravityMetersPerSecondSquared) &&
                             isfinite(out->orbitalSpeedKilometersPerSecond) &&
                             out->sphereOfInfluenceKm > 0.0 &&
                             out->hillSphereKm > 0.0 &&
                             out->encounterRadiusGame >=
                                 (float)(out->physicalRadiusGame * 2.19);
    return out->withinErrorBudget;
}

static void HomeBodyInfoForObserver(Vector3 observer, SpaceBodyInfo *out)
{
    if (!out) return;
    float radius = HomeWorldProxyRadius();
    *out = (SpaceBodyInfo){
        .center = HomeWorldCenter(),
        .velocity = { 0 },
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
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
        !isfinite(body->currentIrradianceEarth)) {
        return false;
    }
    double parentMassKg = body->parentMassKg > 0.0
        ? body->parentMassKg : body->hostStar.massKg;
    if (!(parentMassKg > 0.0) || !isfinite(parentMassKg)) return false;
    if (body->index > 0) {
        snprintf(out->bodyName, sizeof(out->bodyName), "%s %c", body->name,
                 'a' + body->index - 1);
    } else {
        snprintf(out->bodyName, sizeof(out->bodyName), "%s", body->name);
    }
    out->physicalRadiusKm = body->physicalRadiusKm;
    out->physicalRadiusGame = SpaceUnitsKilometersToGameDistance(
        body->physicalRadiusKm);
    out->visualRadiusGame = body->spaceProxyRadius;
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
                                     out->landingRadiusGame);
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
        int count = SpaceBodiesNear(observer, 700.0f, bodies, 48);
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
            found = home.dist <= 700.0f;
        }
    }
    return found && SpaceBodyScaleDiagnostics(&selected, out);
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
        int count = SpaceBodiesNear(origin, 700.0f, bodies, 48);
        for (int i = 0; i < count; i++) {
            Vector3 to = Vector3Subtract(bodies[i].center, origin);
            float proj = Vector3DotProduct(to, direction);
            if (proj < 0.0f || proj > best) continue;
            Vector3 closest = Vector3Add(origin, Vector3Scale(direction, proj));
            Vector3 diff = Vector3Subtract(closest, bodies[i].center);
            float lateral = sqrtf(diff.x * diff.x + diff.y * diff.y + diff.z * diff.z);
            float radius = SolarBodyTerrainProxyRadius(
                bodies[i].spaceProxyRadius);
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
    *out = (SpaceBodyInfo){
        .center = system->center,
        .physicalRadiusKm = system->star.radiusKm,
        .spaceProxyRadius = (float)system->starProxyRadius,
        .dist = Vector3Distance(starOrigin, system->center),
        .isStar = true,
        .index = 0,
        .systemAnchorX = system->anchorX,
        .systemAnchorZ = system->anchorZ,
        .hostStar = system->star,
        .spectrum = system->spectrum,
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
        if (homeDistance <= HOME_WORLD_PROXY_RADIUS + 25.0f) {
            best = homeDistance;
            *gravityDir = homeDistance > 0.001f
                              ? Vector3Scale(Vector3Subtract(homeCenter, position),
                                             1.0f / homeDistance)
                              : (Vector3){ 0.0f, -1.0f, 0.0f };
            *surfaceDist = homeDistance - HOME_WORLD_PROXY_RADIUS;
            if (gravityScale) *gravityScale = 1.0f;
            found = true;
        }
    }
    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        float terrainR = SolarBodyTerrainProxyRadius(
            bodies[i].spaceProxyRadius);
        if (bodies[i].dist > terrainR + 25.0f) continue;
        if (bodies[i].dist < best) {
            best = bodies[i].dist;
            *gravityDir = Vector3Normalize(Vector3Subtract(bodies[i].center, position));
            *surfaceDist = best - terrainR;
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
    float homeRadius = HomeWorldProxyRadius();
    AddSpaceGravityCandidate(
        candidates, &candidateCount, 64,
        (SpacePhysicsGravityBody){
            .center = HomeWorldCenter(),
            .softeningRadiusGame = homeRadius,
            .gravitationalParameterGame = (float)
                SpaceUnitsProxyGravitationalParameterGame(
                SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM,
                homeRadius),
            .encounterRadiusGame = homeRadius * 2.15f,
            .hierarchy = 1
        },
        Vector3Zero(),
        SPACE_GRAVITY_PRIMARY_HOME, "Home");

    SpaceBodyInfo bodies[48];
    int bodyCount = SpaceBodiesNear(position, SPACE_GRAVITY_QUERY_RADIUS,
                                    bodies, 48);
    for (int i = 0; i < bodyCount; i++) {
        if (bodies[i].isStar) {
            AddSpaceGravityCandidate(
                candidates, &candidateCount, 64,
                (SpacePhysicsGravityBody){
                    .center = bodies[i].center,
                    .softeningRadiusGame = bodies[i].spaceProxyRadius,
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

        float terrainRadius = bodies[i].landingProxyRadius;
        float soi = bodies[i].encounterRadiusGame;
        float mu = (float)SpaceUnitsProxyGravitationalParameterGame(
            bodies[i].profile.massKg, bodies[i].profile.physicalRadiusKm,
            terrainRadius);
        char planetName[40];
        snprintf(planetName, sizeof(planetName), "%s %c", bodies[i].name,
                 'a' + planetIndex);
        AddSpaceGravityCandidate(
            candidates, &candidateCount, 64,
            (SpacePhysicsGravityBody){
                .center = bodies[i].center,
                .softeningRadiusGame = terrainRadius,
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

static Vector3 PlanetReturnPosition(Vector3 center, float radius, Vector3 outward)
{
    const float orbitDistance = radius + 14.0f;
    const float minY = (float)SPACE_LAYER_Y + 2.5f;
    const float maxY = (float)SPACE_LAYER_TOP - 3.5f;
    Vector3 candidate = Vector3Add(center, Vector3Scale(outward, orbitDistance));
    if (candidate.y >= minY && candidate.y <= maxY) return candidate;

    candidate.y = Clamp(candidate.y, minY, maxY);
    float dy = candidate.y - center.y;
    float horizontalDistance = sqrtf(fmaxf(0.0f, orbitDistance * orbitDistance - dy * dy));
    Vector2 horizontal = { outward.x, outward.z };
    if (Vector2LengthSqr(horizontal) < 0.001f) horizontal = (Vector2){ 1.0f, 0.0f };
    else horizontal = Vector2Normalize(horizontal);
    candidate.x = center.x + horizontal.x * horizontalDistance;
    candidate.z = center.z + horizontal.y * horizontalDistance;
    return candidate;
}

bool PlanetWorldLandingTarget(Vector3 position, SpaceBodyInfo *out)
{
    if (!out) return false;
    *out = (SpaceBodyInfo){ 0 };
    if (!SpaceQueryVectorIsFinite(position)) return false;
    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(position, 160.0f, bodies, 48);
    float bestGap = 1e30f;
    bool found = false;

    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        if (!bodies[i].profile.hasSolidSurface) continue;
        float radius = SolarBodyTerrainProxyRadius(
            bodies[i].spaceProxyRadius);
        float gap = fabsf(bodies[i].dist - radius);
        if (gap > 20.0f || gap >= bestGap) continue;
        bestGap = gap;
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
    player->position = (Vector3){ homeCenter.x, SPACE_ENTER_Y + 2.0f, homeCenter.z };
    player->floating = false;
    player->onGround = false;
    SetImportMessage("Left Homeworld atmosphere. Spaceflight is now three-dimensional.");
    return true;
}

bool HomeWorldCanEnter(Vector3 position)
{
    if (homeWorld.surfaceActive || planetWorld.active) return false;

    Vector3 center = HomeWorldCenter();
    float surfaceGap = fabsf(Vector3Distance(position, center) -
                             HOME_WORLD_PROXY_RADIUS);
    return surfaceGap <= HOME_WORLD_LANDING_MARGIN;
}

static void HomeWorldActivateSurface(void)
{
    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    homeWorld.surfaceActive = true;
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
    int groundY = TerrainHeight(landingX, landingZ, terrainMode);
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
    int groundY = TerrainHeight(landingX, landingZ, terrainMode);
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
    snprintf(next.name, sizeof(next.name), "%.28s %c", body->name,
             'a' + (body->index > 0 ? body->index - 1 : 0));

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
    next.returnPosition = PlanetReturnPosition(body->center,
                                               next.spaceProxyRadius, outward);

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
    int shipGround = PlanetTerrainHeight(shipX, shipZ);
    int playerX = shipX + 3;
    int playerZ = shipZ;
    int playerGround = PlanetTerrainHeight(playerX, playerZ);
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
    SetBlock(shipX, shipGround + 1, shipZ, BLOCK_SPACESHIP);
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
                                         solarSimulationTime,
                                         &orbitalState)) {
            returnPosition = PlanetReturnPosition(
                orbitalState.center, planetWorld.spaceProxyRadius, outward);
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

static bool PlanetProfileIsValid(const PlanetProfile *profile)
{
    if (!profile || profile->style < SOLAR_STYLE_SUN ||
        profile->style > SOLAR_STYLE_TEMPERATE ||
        profile->atmosphereType < PLANET_ATMOSPHERE_NONE ||
        profile->atmosphereType > PLANET_ATMOSPHERE_CORROSIVE) {
        return false;
    }

    return isfinite(profile->physicalRadiusKm) &&
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
           isfinite(profile->axialTilt) &&
           isfinite(profile->seasonPhase) &&
           isfinite(profile->yearLength) && profile->yearLength >= 0.0f &&
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
    uint8_t hasSolidSurface = profile->hasSolidSurface ? 1u : 0u;
    uint8_t hasRings = profile->hasRings ? 1u : 0u;
    uint8_t tidallyLocked = profile->tidallyLocked ? 1u : 0u;

#define WRITE_PROFILE_FIELD(field) \
    fwrite(&profile->field, sizeof(profile->field), 1, file) == 1
    return WRITE_PROFILE_FIELD(seed) &&
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
           WRITE_PROFILE_FIELD(axialTilt) &&
           WRITE_PROFILE_FIELD(seasonPhase) &&
           WRITE_PROFILE_FIELD(yearLength) &&
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
    uint8_t hasSolidSurface = 0;
    uint8_t hasRings = 0;
    uint8_t tidallyLocked = 0;

#define READ_PROFILE_FIELD(field) \
    (fread(&loaded.field, sizeof(loaded.field), 1, file) == 1)
    if (!READ_PROFILE_FIELD(seed) ||
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
        !READ_PROFILE_FIELD(greenhouseEffect) ||
        !READ_PROFILE_FIELD(axialTilt) ||
        !READ_PROFILE_FIELD(seasonPhase) ||
        !READ_PROFILE_FIELD(yearLength) ||
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
    if (!PlanetProfileIsValid(&loaded)) return false;

    *outProfile = loaded;
    return true;
}

bool PlanetWorldSaveState(FILE *file)
{
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

    if (!file || !PlanetProfileIsValid(&planetWorld.profile)) return false;

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

    if (!file ||
        fread(&versionOrActive, sizeof(versionOrActive), 1, file) != 1) {
        return false;
    }
    bool hasProfile = versionOrActive == PLANET_WORLD_STATE_VERSION;
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

    if (style > (uint32_t)SOLAR_STYLE_TEMPERATE ||
        planetIndex < 0 || !isfinite(loaded.spaceProxyRadius) ||
        loaded.spaceProxyRadius < 0.0f ||
        !isfinite(bodyCenter[0]) || !isfinite(bodyCenter[1]) || !isfinite(bodyCenter[2]) ||
        !isfinite(returnPosition[0]) || !isfinite(returnPosition[1]) ||
        !isfinite(returnPosition[2])) {
        return false;
    }

    loaded.active = active != 0;
    loaded.style = (SolarBodyStyle)style;
    loaded.originX = (int)originX;
    loaded.originZ = (int)originZ;
    loaded.planetIndex = (int)planetIndex;
    loaded.bodyCenter = (Vector3){ bodyCenter[0], bodyCenter[1], bodyCenter[2] };
    loaded.returnPosition = (Vector3){ returnPosition[0], returnPosition[1], returnPosition[2] };
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

SpaceChunk spaceChunks[MAX_SPACE_CHUNKS];
static BlockEdit spaceEdits[MAX_SPACE_EDITS];
static int spaceEditCount = 0;

typedef struct SpaceGenJob {
    bool inUse;
    bool started;
    bool done;
    bool canceled;
    int cx;
    int cz;
    int slotIndex;
    SpaceChunk result;
} SpaceGenJob;

static SpaceGenJob spaceGenJobs[MAX_SPACE_GEN_JOBS];
static pthread_mutex_t spaceGenMutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t spaceGenCond = PTHREAD_COND_INITIALIZER;
static pthread_t spaceGenThread;
static bool spaceGenThreadStarted = false;
static bool spaceGenShutdown = false;

static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz);
static void *SpaceGenWorker(void *arg);
static void CancelSpaceGenForSlot(int slotIndex);
static void CancelAllSpaceGenJobs(void);

void SpaceInit(void)
{
    memset(spaceChunks, 0, sizeof(spaceChunks));
    memset(spaceGenJobs, 0, sizeof(spaceGenJobs));
    spaceEditCount = 0;

    pthread_mutex_lock(&spaceGenMutex);
    spaceGenShutdown = false;
    pthread_mutex_unlock(&spaceGenMutex);

    if (pthread_create(&spaceGenThread, NULL, SpaceGenWorker, NULL) == 0) {
        spaceGenThreadStarted = true;
    }
}

void SpaceShutdown(void)
{
    if (!spaceGenThreadStarted) return;

    pthread_mutex_lock(&spaceGenMutex);
    spaceGenShutdown = true;
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (!spaceGenJobs[i].inUse) continue;
        spaceGenJobs[i].canceled = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);

    pthread_join(spaceGenThread, NULL);
    spaceGenThreadStarted = false;
    memset(spaceGenJobs, 0, sizeof(spaceGenJobs));
}

static SpaceChunk *FindSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return &spaceChunks[i];
    }
    return NULL;
}

static bool FindPendingSpaceChunk(int cx, int cz)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].generating && spaceChunks[i].cx == cx && spaceChunks[i].cz == cz) return true;
    }
    return false;
}

static SpaceChunk *AllocateSpaceChunkSlot(int cx, int cz)
{
    SpaceChunk *empty = NULL;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded && !spaceChunks[i].generating) {
            empty = &spaceChunks[i];
            break;
        }
    }
    if (!empty) return NULL;
    memset(empty, 0, sizeof(*empty));
    empty->cx = cx;
    empty->cz = cz;
    return empty;
}

static void UnloadSpaceChunkModel(SpaceChunk *chunk)
{
    if (chunk->hasModel) {
        UnloadModel(chunk->model);
        chunk->hasModel = false;
    }
    if (chunk->hasWaterModel) {
        UnloadModel(chunk->waterModel);
        chunk->hasWaterModel = false;
    }
}

static void ApplySpaceEditsToChunk(SpaceChunk *chunk)
{
    for (int i = 0; i < spaceEditCount; i++) {
        const BlockEdit *edit = &spaceEdits[i];
        if (edit->y < SPACE_LAYER_Y || edit->y >= SPACE_LAYER_TOP) continue;
        int localX = SpaceGlobalToLocalX(edit->x);
        int localZ = SpaceGlobalToLocalZ(edit->z);
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(localX, localZ, &editCx, &editCz, &editLx, &editLz);
        if (editCx == chunk->cx && editCz == chunk->cz) {
            chunk->blocks[editLx][edit->y - SPACE_LAYER_Y][editLz] = (unsigned short)edit->type;
        }
    }
}


static void GenerateSpaceChunk(SpaceChunk *chunk, int cx, int cz)
{
    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                chunk->blocks[lx][ly][lz] = (unsigned short)BLOCK_AIR;
            }
        }
    }

    int startX = cx * CHUNK_SIZE;
    int startZ = cz * CHUNK_SIZE;
    int minAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX - 8), ASTEROID_SPACING);
    int maxAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX + CHUNK_SIZE + 8),
                                 ASTEROID_SPACING);
    int minAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ - 8), ASTEROID_SPACING);
    int maxAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ + CHUNK_SIZE + 8),
                                 ASTEROID_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX, anchorZ) % 100u >= ASTEROID_PROBABILITY) continue;

            int wx = SpaceGlobalToLocalX(ClampCoordinate((int64_t)anchorX * ASTEROID_SPACING));
            int wz = SpaceGlobalToLocalZ(ClampCoordinate((int64_t)anchorZ * ASTEROID_SPACING));
            if (SpacePointInSolarSystemBubble(wx, wz)) continue;
            int wy = SPACE_LAYER_Y + 8 + (int)(WorldHash2D(anchorX + 3, anchorZ) % (unsigned int)(WORLD_HEIGHT - 16));
            int radius = 3 + (int)(WorldHash2D(anchorX, anchorZ + 7) % 5u);
            float radiusSqr = (float)(radius * radius);
            float shellSqr = (float)((radius - 1) * (radius - 1));

            for (int lx = 0; lx < CHUNK_SIZE; lx++) {
                for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
                    for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                        if (chunk->blocks[lx][ly][lz] != 0) continue;

                        int bx = startX + lx;
                        int by = SPACE_LAYER_Y + ly;
                        int bz = startZ + lz;
                        float dx = (float)(bx - wx);
                        float dy = (float)(by - wy);
                        float dz = (float)(bz - wz);
                        float distSqr = dx * dx + dy * dy + dz * dz;
                        if (distSqr >= radiusSqr) continue;

                        BlockType type = (distSqr >= shellSqr) ? BLOCK_MOON_SAND : BLOCK_MOON_ROCK;
                        if (WorldHash3D(SpaceLocalToGlobalX(bx), by,
                                        SpaceLocalToGlobalZ(bz)) % 89u == 0u) {
                            type = BLOCK_METEORITE;
                        }
                        chunk->blocks[lx][ly][lz] = (unsigned short)type;
                    }
                }
            }
        }
    }

    chunk->loaded = true;
    chunk->dirty = true;
}

static SpaceGenJob *NextSpaceGenJobLocked(void)
{
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (spaceGenJobs[i].inUse && !spaceGenJobs[i].started && !spaceGenJobs[i].done) {
            return &spaceGenJobs[i];
        }
    }
    return NULL;
}

static void *SpaceGenWorker(void *arg)
{
    (void)arg;

    for (;;) {
        pthread_mutex_lock(&spaceGenMutex);
        SpaceGenJob *job = NULL;
        while (!spaceGenShutdown && !(job = NextSpaceGenJobLocked())) {
            pthread_cond_wait(&spaceGenCond, &spaceGenMutex);
        }
        if (spaceGenShutdown) {
            pthread_mutex_unlock(&spaceGenMutex);
            break;
        }

        job->started = true;
        int cx = job->cx;
        int cz = job->cz;
        pthread_mutex_unlock(&spaceGenMutex);

        GenerateSpaceChunk(&job->result, cx, cz);

        pthread_mutex_lock(&spaceGenMutex);
        job->done = true;
        pthread_cond_broadcast(&spaceGenCond);
        pthread_mutex_unlock(&spaceGenMutex);
    }

    return NULL;
}

static bool SubmitSpaceGenJob(SpaceChunk *chunk)
{
    if (!spaceGenThreadStarted) return false;

    pthread_mutex_lock(&spaceGenMutex);
    SpaceGenJob *job = NULL;
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        if (!spaceGenJobs[i].inUse) {
            job = &spaceGenJobs[i];
            break;
        }
    }
    if (!job) {
        pthread_mutex_unlock(&spaceGenMutex);
        return false;
    }

    *job = (SpaceGenJob){
        .inUse = true,
        .cx = chunk->cx,
        .cz = chunk->cz,
        .slotIndex = (int)(chunk - spaceChunks),
        .result = { .cx = chunk->cx, .cz = chunk->cz }
    };
    pthread_cond_signal(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
    return true;
}

static void CancelSpaceGenForSlot(int slotIndex)
{
    pthread_mutex_lock(&spaceGenMutex);
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        SpaceGenJob *job = &spaceGenJobs[i];
        if (!job->inUse || job->slotIndex != slotIndex) continue;
        job->canceled = true;
        if (!job->started) job->done = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
}

static void CancelAllSpaceGenJobs(void)
{
    pthread_mutex_lock(&spaceGenMutex);
    for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
        SpaceGenJob *job = &spaceGenJobs[i];
        if (!job->inUse) continue;
        job->canceled = true;
        if (!job->started) job->done = true;
    }
    pthread_cond_broadcast(&spaceGenCond);
    pthread_mutex_unlock(&spaceGenMutex);
}

static void DrainCanceledSpaceGenJobs(void)
{
    if (!spaceGenThreadStarted) return;

    pthread_mutex_lock(&spaceGenMutex);
    for (;;) {
        bool waiting = false;
        for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
            SpaceGenJob *job = &spaceGenJobs[i];
            if (!job->inUse) continue;
            if (job->done) {
                job->inUse = false;
                continue;
            }
            waiting = true;
        }
        if (!waiting) break;
        pthread_cond_wait(&spaceGenCond, &spaceGenMutex);
    }
    pthread_mutex_unlock(&spaceGenMutex);
}

void SpaceProcessFinishedGenJobs(void)
{
    for (;;) {
        pthread_mutex_lock(&spaceGenMutex);
        SpaceGenJob *job = NULL;
        for (int i = 0; i < MAX_SPACE_GEN_JOBS; i++) {
            if (spaceGenJobs[i].inUse && spaceGenJobs[i].done) {
                job = &spaceGenJobs[i];
                break;
            }
        }
        if (!job) {
            pthread_mutex_unlock(&spaceGenMutex);
            return;
        }

        int slotIndex = job->slotIndex;
        if (!job->canceled && slotIndex >= 0 && slotIndex < MAX_SPACE_CHUNKS) {
            SpaceChunk *chunk = &spaceChunks[slotIndex];
            if (chunk->generating && chunk->cx == job->cx && chunk->cz == job->cz) {
                memcpy(chunk->blocks, job->result.blocks, sizeof(chunk->blocks));
                chunk->loaded = true;
                chunk->generating = false;
                chunk->dirty = true;
                ApplySpaceEditsToChunk(chunk);
            }
        } else if (slotIndex >= 0 && slotIndex < MAX_SPACE_CHUNKS) {
            SpaceChunk *chunk = &spaceChunks[slotIndex];
            if (chunk->generating && chunk->cx == job->cx && chunk->cz == job->cz) {
                chunk->generating = false;
            }
        }

        job->inUse = false;
        pthread_cond_broadcast(&spaceGenCond);
        pthread_mutex_unlock(&spaceGenMutex);
    }
}

static void SpaceRememberEdit(int x, int y, int z, BlockType type)
{
    int globalX = SpaceLocalToGlobalX(x);
    int globalZ = SpaceLocalToGlobalZ(z);
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].x == globalX && spaceEdits[i].y == y &&
            spaceEdits[i].z == globalZ) {
            spaceEdits[i].type = type;
            return;
        }
    }
    if (spaceEditCount < MAX_SPACE_EDITS) {
        spaceEdits[spaceEditCount++] = (BlockEdit){ globalX, y, globalZ, type };
    }
}

static void RebuildSpaceChunkMesh(SpaceChunk *chunk)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };

    int nearbyTorchIndices[MAX_TORCH_LIGHTS];
    int nearbyTorchCount = CollectNearbyTorchLights(
        chunk->cx * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cx * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE - (int)TORCH_LIGHT_RADIUS,
        chunk->cz * CHUNK_SIZE + CHUNK_SIZE - 1 + (int)TORCH_LIGHT_RADIUS,
        nearbyTorchIndices);

    UnloadSpaceChunkModel(chunk);

    Mesh solidMesh = { 0 };
    Mesh waterMesh = { 0 };
    bool hasSolid = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, false, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &solidMesh);
    bool hasWater = BuildMeshData((const unsigned short (*)[CHUNK_SIZE])chunk->blocks,
                                  SPACE_LAYER_HEIGHT, SPACE_LAYER_Y,
                                  chunk->cx, chunk->cz, true, faces,
                                  nearbyTorchIndices, nearbyTorchCount, &waterMesh);

    if (hasSolid) {
        UploadMesh(&solidMesh, false);
        chunk->model = LoadModelFromMesh(solidMesh);
        SetMaterialTexture(&chunk->model.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasModel = true;
    }
    if (hasWater) {
        UploadMesh(&waterMesh, false);
        chunk->waterModel = LoadModelFromMesh(waterMesh);
        SetMaterialTexture(&chunk->waterModel.materials[0], MATERIAL_MAP_DIFFUSE, blockAtlas);
        chunk->hasWaterModel = true;
    }
    chunk->dirty = false;
}

void UpdateSpaceChunks(Vector3 playerPosition, int groundRenderDistance, int generationPerFrame)
{
    int renderDist = SPACE_RENDER_DISTANCE_CHUNKS;
    if (groundRenderDistance < renderDist) renderDist = groundRenderDistance;

    int playerCx = 0;
    int playerCz = 0;
    int playerLx = 0;
    int playerLz = 0;
    WorldToChunkLocal((int)floorf(playerPosition.x), (int)floorf(playerPosition.z),
                      &playerCx, &playerCz, &playerLx, &playerLz);

    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded && !spaceChunks[i].generating) continue;
        if (abs(spaceChunks[i].cx - playerCx) > renderDist ||
            abs(spaceChunks[i].cz - playerCz) > renderDist) {
            if (spaceChunks[i].generating) CancelSpaceGenForSlot(i);
            if (spaceChunks[i].loaded) UnloadSpaceChunkModel(&spaceChunks[i]);
            spaceChunks[i].loaded = false;
            spaceChunks[i].generating = false;
            spaceChunks[i].dirty = false;
        }
    }

    if (playerPosition.y < 50.0f) return;

    int missingChunks[MAX_SPACE_CHUNKS][2];
    int missingCount = 0;
    for (int dz = -renderDist; dz <= renderDist; dz++) {
        for (int dx = -renderDist; dx <= renderDist; dx++) {
            int cx = playerCx + dx;
            int cz = playerCz + dz;
            if (FindSpaceChunk(cx, cz) || FindPendingSpaceChunk(cx, cz)) continue;

            int insert = missingCount;
            int distance = abs(dx) > abs(dz) ? abs(dx) : abs(dz);
            while (insert > 0) {
                int prevDx = missingChunks[insert - 1][0] - playerCx;
                int prevDz = missingChunks[insert - 1][1] - playerCz;
                int prevDistance = abs(prevDx) > abs(prevDz) ? abs(prevDx) : abs(prevDz);
                if (prevDistance <= distance) break;
                missingChunks[insert][0] = missingChunks[insert - 1][0];
                missingChunks[insert][1] = missingChunks[insert - 1][1];
                insert--;
            }
            missingChunks[insert][0] = cx;
            missingChunks[insert][1] = cz;
            missingCount++;
        }
    }

    int generated = 0;
    for (int i = 0; i < missingCount && generated < generationPerFrame; i++) {
        int cx = missingChunks[i][0];
        int cz = missingChunks[i][1];
        SpaceChunk *chunk = AllocateSpaceChunkSlot(cx, cz);
        if (!chunk) break;
        chunk->cx = cx;
        chunk->cz = cz;
        chunk->generating = true;
        if (!SubmitSpaceGenJob(chunk)) {
            if (spaceGenThreadStarted) {
                chunk->generating = false;
                break;
            }
            GenerateSpaceChunk(chunk, cx, cz);
            ApplySpaceEditsToChunk(chunk);
            chunk->loaded = true;
            chunk->generating = false;
            chunk->dirty = true;
        }
        generated++;
    }

    int rebuilt = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (!spaceChunks[i].loaded || !spaceChunks[i].dirty) continue;
        RebuildSpaceChunkMesh(&spaceChunks[i]);
        if (++rebuilt >= SPACE_MESH_REBUILDS_PER_FRAME) break;
    }
}

BlockType SpaceBlockAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return BLOCK_AIR;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return BLOCK_AIR;
    return (BlockType)chunk->blocks[lx][y - SPACE_LAYER_Y][lz];
}

bool SpaceBlockReadyAt(int x, int y, int z)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return true;

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    return chunk != NULL && chunk->loaded && !chunk->generating;
}

void SpaceSetBlock(int x, int y, int z, BlockType type)
{
    if (y < SPACE_LAYER_Y || y >= SPACE_LAYER_TOP) return;

    SpaceRememberEdit(x, y, z, type);

    int cx = 0;
    int cz = 0;
    int lx = 0;
    int lz = 0;
    WorldToChunkLocal(x, z, &cx, &cz, &lx, &lz);
    SpaceChunk *chunk = FindSpaceChunk(cx, cz);
    if (!chunk) return;
    chunk->blocks[lx][y - SPACE_LAYER_Y][lz] = (unsigned short)type;
    chunk->dirty = true;
}

void SpaceSaveEdits(FILE *file)
{
    if (!file) return;
    uint32_t count = (uint32_t)spaceEditCount;
    fwrite(&count, sizeof(count), 1, file);
    if (spaceEditCount > 0) {
        fwrite(spaceEdits, sizeof(BlockEdit), (size_t)spaceEditCount, file);
    }
}

void SpaceLoadEdits(FILE *file)
{
    if (!file) return;
    spaceEditCount = 0;

    uint32_t count = 0;
    if (fread(&count, sizeof(count), 1, file) != 1) return;

    if (count > MAX_SPACE_EDITS) return;

    for (uint32_t i = 0; i < count; i++) {
        BlockEdit edit;
        if (fread(&edit, sizeof(edit), 1, file) != 1) break;
        if (edit.y < SPACE_LAYER_Y || edit.y >= SPACE_LAYER_TOP) continue;
        if (!IsValidBlockType(edit.type)) continue;
        if (spaceEditCount < MAX_SPACE_EDITS) spaceEdits[spaceEditCount++] = edit;
    }
}

void UnloadAllSpaceChunks(void)
{
    CancelAllSpaceGenJobs();
    DrainCanceledSpaceGenJobs();
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) UnloadSpaceChunkModel(&spaceChunks[i]);
        spaceChunks[i].loaded = false;
        spaceChunks[i].generating = false;
        spaceChunks[i].dirty = false;
    }
}

void SpaceReset(void)
{
    UnloadAllSpaceChunks();
    spaceEditCount = 0;
    solarSimulationTime = 0.0;
    SpaceQueryCacheClear();
    SpaceResetOrigin();
    PlanetWorldReset();
    HomeWorldReset();
}

bool SpaceRebasePlayer(Player *player)
{
    if (!player || HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) return false;
    if (fabsf(player->position.x) < (float)SPACE_REBASE_THRESHOLD &&
        fabsf(player->position.z) < (float)SPACE_REBASE_THRESHOLD) {
        return false;
    }

    int64_t stepX = (int64_t)llroundf(player->position.x / (float)STAR_SYSTEM_SPACING) *
                    STAR_SYSTEM_SPACING;
    int64_t stepZ = (int64_t)llroundf(player->position.z / (float)STAR_SYSTEM_SPACING) *
                    STAR_SYSTEM_SPACING;
    if (stepX == 0 && stepZ == 0) return false;

    int64_t nextOriginX = (int64_t)spaceOriginX + stepX;
    int64_t nextOriginZ = (int64_t)spaceOriginZ + stepZ;
    if (nextOriginX > INT_MAX || nextOriginX < INT_MIN ||
        nextOriginZ > INT_MAX || nextOriginZ < INT_MIN) {
        return false;
    }

    // Wait for workers before changing the frame they use for procedural data.
    UnloadAllSpaceChunks();
    spaceOriginX = (int)nextOriginX;
    spaceOriginZ = (int)nextOriginZ;
    player->position.x -= (float)stepX;
    player->position.z -= (float)stepZ;
    RebuildTorchList();
    SpaceRebuildTorchList();
    return true;
}

int GetActiveSpaceChunkCount(void)
{
    int count = 0;
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        if (spaceChunks[i].loaded) count++;
    }
    return count;
}

void SpaceRebuildTorchList(void)
{
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].type == BLOCK_TORCH) {
            TorchLightAdd(SpaceGlobalToLocalX(spaceEdits[i].x), spaceEdits[i].y,
                          SpaceGlobalToLocalZ(spaceEdits[i].z));
        }
    }
}

int GetSpaceEditCount(void)
{
    return spaceEditCount;
}

void SpaceUpdateSolarGlow(Vector3 playerPosition)
{
    SpaceBodyInfo bodies[STAR_NAVIGATION_MAX_SYSTEMS];
    int bodyCount = SpaceBodiesNear(playerPosition, 60.0f, bodies,
                                     STAR_NAVIGATION_MAX_SYSTEMS);
    for (int i = 0; i < bodyCount; i++) {
        if (!bodies[i].isStar) continue;

        float dist = bodies[i].dist;
        int count = dist < 24.0f ? 3 : 1;
        Color glow = SpectrumColor(bodies[i].spectrum);
        float spread = fmaxf(3.0f, bodies[i].spaceProxyRadius * 0.55f);
        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread,
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread,
                ((float)rand() / (float)RAND_MAX - 0.5f) * spread
            };
            ParticlesEmitOne(Vector3Add(bodies[i].center, offset),
                             (Vector3){ ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f,
                                        0.2f + (float)rand() / (float)RAND_MAX * 0.5f,
                                        ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f },
                             glow,
                             (Vector3){ 0.14f, 0.14f, 0.14f },
                             1.8f, 0.0f);
        }
    }
}
