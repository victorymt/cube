#include "space.h"

#include "raymath.h"
#include "chunks.h"
#include "ecology.h"
#include "terrain.h"
#include "particles.h"
#include "space_physics.h"
#include "space_units.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
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
#define SPACE_PROXY_EARTH_SURFACE_ACCEL_GAME 4.5f
#define SPACE_MAX_PLANET_ENCOUNTER_RADIUS_GAME 170.0f

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
    fwrite(&spaceOriginX, sizeof(spaceOriginX), 1, file);
    fwrite(&spaceOriginZ, sizeof(spaceOriginZ), 1, file);
}

bool SpaceLoadOrigin(FILE *file)
{
    int loadedX = 0;
    int loadedZ = 0;
    if (fread(&loadedX, sizeof(loadedX), 1, file) != 1 ||
        fread(&loadedZ, sizeof(loadedZ), 1, file) != 1) {
        return false;
    }
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

static int StellarVisualRadius(const StellarProfile *star)
{
    if (!star || star->radiusKm <= 0.0) return 9;
    float radiusSolar = (float)(star->radiusKm /
                                SPACE_UNITS_SOLAR_RADIUS_KM);
    float radius = 13.0f + 2.5f * log2f(radiusSolar);
    float maximum = star->stage == STELLAR_STAGE_RED_GIANT ? 28.0f : 21.0f;
    return (int)roundf(Clamp(radius, 7.0f, maximum));
}

static void ApplyPrimaryStar(SolarSystemDef *system, StellarProfile star)
{
    system->star = star;
    system->spectrum = star.spectrum;
    system->starProxyRadius = StellarVisualRadius(&star);
}

static double SolarSystemStarMassKg(const SolarSystemDef *system)
{
    return system && system->star.massKg > 0.0 ? system->star.massKg :
                                                 SPACE_UNITS_SOLAR_MASS_KG;
}

static float SolarSystemStarLuminosity(const SolarSystemDef *system)
{
    return system && system->star.luminositySolar > 0.0f ?
           system->star.luminositySolar : 1.0f;
}

static double SolidPlanetRadiusKilometersForProxy(float proxyRadius)
{
    float radiusEarth = 0.72f + (proxyRadius - 40.0f) * 0.095f;
    radiusEarth = Clamp(radiusEarth, 0.62f, 1.55f);
    return (double)radiusEarth * SPACE_UNITS_EARTH_RADIUS_KM;
}

static void BuildSolSystem(SolarSystemDef *out)
{
    out->exists = true;
    out->anchorX = 0;
    out->anchorZ = 0;
    snprintf(out->name, sizeof(out->name), "Sol");
    ApplyPrimaryStar(out, StellarSolarProfile());
    out->center = (Vector3){ 0.0f, STAR_SYSTEM_MID_Y, 0.0f };
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
            .spaceProxyRadius = proxyRadii[i],
            .yOffset = 0,
            .style = styles[i]
        };
    }
}

static uint32_t SolarLightHash(const SolarSystemDef *sys)
{
    return WorldHash2D(sys->anchorX * 113 + 41, sys->anchorZ * 71 + 19);
}

static unsigned int SolarOrbitHash(const SolarSystemDef *sys, int index)
{
    return WorldHash2D(sys->anchorX * 53 + index * 7 + 1,
                       sys->anchorZ * 29 + index * 3 + 2);
}

static unsigned int SolarPlaneHash(const SolarSystemDef *sys)
{
    return WorldHash2D(sys->anchorX * 79 + 11, sys->anchorZ * 97 + 23);
}

static uint32_t SolarPlanetWorldSeed(const SolarSystemDef *sys, int index)
{
    const SolarPlanetDef *def = &sys->planets[index];
    float orbitGame = (float)SpaceUnitsKilometersToGameDistance(
        def->semiMajorAxisKm);
    float legacyAngle = (float)(SolarOrbitHash(sys, index) % 6283u) / 1000.0f;
    int legacyX = ClampCoordinate((int64_t)SpaceSystemGlobalCoordinate(sys->anchorX) +
                                  (int64_t)floorf(cosf(legacyAngle) * orbitGame));
    int legacyZ = ClampCoordinate((int64_t)SpaceSystemGlobalCoordinate(sys->anchorZ) +
                                  (int64_t)floorf(sinf(legacyAngle) * orbitGame));
    uint32_t seed = WorldHash3D(legacyX, index + 1, legacyZ);
    return seed == 0u ? DEFAULT_WORLD_SEED : seed;
}

static float PlanetProfileHashUnit(uint32_t seed, uint32_t lane)
{
    uint32_t h = seed ^ (lane * 0x9e3779b9u);
    h ^= h >> 16;
    h *= 0x7feb352du;
    h ^= h >> 15;
    h *= 0x846ca68bu;
    h ^= h >> 16;
    return (float)(h & 0x00ffffffu) / 16777215.0f;
}

static PlanetAtmosphereType ClassifyAtmosphere(SolarBodyStyle style, float density,
                                                float temperatureK, float composition)
{
    if (density < 0.12f) return PLANET_ATMOSPHERE_NONE;
    if (density < 0.32f) return PLANET_ATMOSPHERE_THIN;
    if (style == SOLAR_STYLE_LAVA || temperatureK > 355.0f) {
        return PLANET_ATMOSPHERE_CORROSIVE;
    }
    if (style == SOLAR_STYLE_TEMPERATE && temperatureK >= 250.0f &&
        temperatureK <= 310.0f && density <= 0.82f && composition > 0.38f) {
        return PLANET_ATMOSPHERE_BREATHABLE;
    }
    return PLANET_ATMOSPHERE_DENSE;
}

static void DerivePlanetClimateProfile(PlanetProfile *profile)
{
    if (!profile) return;

    float albedoBase = 0.30f;
    switch (profile->style) {
    case SOLAR_STYLE_LAVA:   albedoBase = 0.17f; break;
    case SOLAR_STYLE_ICE:    albedoBase = 0.62f; break;
    case SOLAR_STYLE_DESERT: albedoBase = 0.36f; break;
    case SOLAR_STYLE_GAS:    albedoBase = 0.47f; break;
    case SOLAR_STYLE_CRATER: albedoBase = 0.13f; break;
    case SOLAR_STYLE_TEMPERATE:
    default:                 albedoBase = 0.29f; break;
    }
    profile->albedo = Clamp(albedoBase + profile->oceanCoverage * 0.08f +
                            (PlanetProfileHashUnit(profile->seed, 19u) - 0.5f) * 0.12f,
                            0.04f, 0.82f);

    float atmosphere = Clamp(profile->atmosphereDensity, 0.0f, 1.0f);
    float greenhouse = atmosphere * 0.42f;
    if (profile->atmosphereType == PLANET_ATMOSPHERE_DENSE) greenhouse += 0.16f;
    if (profile->atmosphereType == PLANET_ATMOSPHERE_CORROSIVE) greenhouse += 0.24f;
    if (profile->style == SOLAR_STYLE_LAVA) greenhouse += 0.12f;
    profile->greenhouseEffect = Clamp(greenhouse +
                                      PlanetProfileHashUnit(profile->seed, 20u) * 0.16f,
                                      0.0f, 0.92f);

    profile->axialTilt = (2.5f + PlanetProfileHashUnit(profile->seed, 21u) * 31.0f) * DEG2RAD;
    if (profile->tidallyLocked) profile->axialTilt *= 0.35f;
    profile->seasonPhase = PlanetProfileHashUnit(profile->seed, 22u) * 2.0f * PI;
    if (profile->yearLength <= 0.0f) {
        profile->yearLength = 1800.0f +
                              PlanetProfileHashUnit(profile->seed, 23u) * 6200.0f;
    }
    profile->prevailingWindAngle = PlanetProfileHashUnit(profile->seed, 24u) * 2.0f * PI;

    float volcanicBase = profile->style == SOLAR_STYLE_LAVA ? 0.78f :
                         profile->style == SOLAR_STYLE_CRATER ? 0.20f : 0.08f;
    if (profile->equilibriumTempK > 340.0f) volcanicBase += 0.14f;
    profile->volcanicActivity = Clamp(volcanicBase +
                                      PlanetProfileHashUnit(profile->seed, 25u) * 0.22f,
                                      0.0f, 1.0f);
    float impactBase = profile->style == SOLAR_STYLE_CRATER ? 0.74f :
                       profile->style == SOLAR_STYLE_LAVA ? 0.32f : 0.12f;
    if (profile->atmosphereType == PLANET_ATMOSPHERE_NONE) impactBase += 0.12f;
    profile->impactRate = Clamp(impactBase +
                                PlanetProfileHashUnit(profile->seed, 26u) * 0.20f,
                                0.0f, 1.0f);
}

PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index)
{
    PlanetProfile profile = { 0 };
    if (!sys || index < 0 || index >= sys->planetCount) return profile;

    const SolarPlanetDef *def = &sys->planets[index];
    uint32_t seed = SolarPlanetWorldSeed(sys, index);
    float sizeUnit = PlanetProfileHashUnit(seed, 1u);
    float composition = PlanetProfileHashUnit(seed, 2u);
    float volatileSupply = PlanetProfileHashUnit(seed, 3u);
    double orbitAU = fmax(def->semiMajorAxisKm /
                          SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.18);
    SolarLightSource climateSources[MAX_SOLAR_LIGHTS];
    int climateSourceCount = SolarSystemLightSources(sys, climateSources,
                                                      MAX_SOLAR_LIGHTS);
    double orbitDistanceAuSqr = orbitAU * orbitAU;
    double irradiance = 0.0;
    for (int sourceIndex = 0; sourceIndex < climateSourceCount; sourceIndex++) {
        irradiance += (double)climateSources[sourceIndex].luminosity /
                      orbitDistanceAuSqr;
    }
    if (irradiance <= 0.0001) {
        irradiance = (double)SolarSystemStarLuminosity(sys) /
                     orbitDistanceAuSqr;
    }
    // Use the mean orbital radius here so binary motion does not churn terrain or
    // cloud caches; the live position is still used by frame-by-frame lighting.
    // One unit is the irradiance of a solar-luminosity star at one AU.
    float temperature = 278.5f * (float)pow(fmax(irradiance, 0.0001), 0.25);
    temperature *= 0.96f + PlanetProfileHashUnit(seed, 4u) * 0.08f;

    float solidRadiusEarth = (float)(def->physicalRadiusKm /
                                     SPACE_UNITS_EARTH_RADIUS_KM);
    solidRadiusEarth = Clamp(solidRadiusEarth, 0.62f, 1.55f);
    float atmosphere = solidRadiusEarth * 0.34f + volatileSupply * 0.52f;
    atmosphere -= Clamp((temperature - 330.0f) / 260.0f, 0.0f, 0.45f);
    atmosphere = Clamp(atmosphere, 0.0f, 0.95f);

    bool forcedGasGiant = sys->anchorX == 0 && sys->anchorZ == 0 && index == 3;
    bool gasGiant = forcedGasGiant ||
                    (index > 0 && def->spaceProxyRadius >= 47.0f &&
                     temperature < 430.0f &&
                     PlanetProfileHashUnit(seed, 5u) > 0.52f);

    profile.seed = seed;
    profile.spaceProxyRadius = def->spaceProxyRadius;
    profile.equilibriumTempK = temperature;
    profile.hasSolidSurface = !gasGiant;
    float tidalProximity = Clamp(1.40f - orbitAU, 0.0f, 1.0f);
    profile.tidalLockFactor = Clamp(tidalProximity *
                                    (0.68f + PlanetProfileHashUnit(seed, 13u) * 0.32f),
                                    0.0f, 1.0f);
    profile.tidallyLocked = profile.hasSolidSurface && profile.tidalLockFactor > 0.58f;
    profile.ringTilt = (14.0f + PlanetProfileHashUnit(seed, 14u) * 17.0f) * DEG2RAD;
    if (gasGiant) {
        float gasRadiusEarth = 2.8f + sizeUnit * 1.8f;
        double massEarth = 12.0 + (double)composition * 32.0;
        profile.massKg = SpaceUnitsGameMassToKilograms(massEarth);
        profile.physicalRadiusKm = (double)gasRadiusEarth *
                                   SPACE_UNITS_EARTH_RADIUS_KM;
        double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            profile.massKg, profile.physicalRadiusKm);
        double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
            SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
        profile.surfaceGravity = Clamp((float)(gravity / earthGravity),
                                       0.75f, 2.40f);
        profile.style = SOLAR_STYLE_GAS;
        profile.atmosphereDensity = 1.0f;
        profile.atmosphereType = PLANET_ATMOSPHERE_DENSE;
        profile.oceanCoverage = 0.0f;
        profile.terrainRoughness = 0.0f;
        profile.rotationRate = 5.0f + PlanetProfileHashUnit(seed, 6u) * 3.0f;
        profile.hasRings = forcedGasGiant || PlanetProfileHashUnit(seed, 7u) > 0.34f;
        profile.tidalLockFactor = 0.0f;
        profile.tidallyLocked = false;
        profile.yearLength = (float)SolarSystemPlanetOrbitPeriodGameTime(sys,
                                                                         index);
        DerivePlanetClimateProfile(&profile);
        return profile;
    }

    float density = 0.78f + composition * 0.52f;
    double massEarth = (double)density * solidRadiusEarth * solidRadiusEarth *
                       solidRadiusEarth;
    profile.massKg = SpaceUnitsGameMassToKilograms(massEarth);
    profile.physicalRadiusKm = (double)solidRadiusEarth *
                               SPACE_UNITS_EARTH_RADIUS_KM;
    double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        profile.massKg, profile.physicalRadiusKm);
    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    profile.surfaceGravity = Clamp((float)(gravity / earthGravity),
                                   0.45f, 1.75f);
    profile.atmosphereDensity = atmosphere;
    if (temperature > 365.0f) profile.style = SOLAR_STYLE_LAVA;
    else if (atmosphere < 0.13f) profile.style = SOLAR_STYLE_CRATER;
    else if (temperature > 305.0f) profile.style = SOLAR_STYLE_DESERT;
    else if (temperature < 238.0f) profile.style = SOLAR_STYLE_ICE;
    else profile.style = SOLAR_STYLE_TEMPERATE;

    float liquidWindow = 1.0f - Clamp(fabsf(temperature - 282.0f) / 58.0f, 0.0f, 1.0f);
    profile.oceanCoverage = Clamp(liquidWindow * atmosphere *
                                  (0.22f + PlanetProfileHashUnit(seed, 8u) * 0.72f),
                                  0.0f, 0.78f);
    if (profile.style == SOLAR_STYLE_ICE) profile.oceanCoverage *= 0.65f;
    if (profile.style == SOLAR_STYLE_LAVA) {
        profile.oceanCoverage = 0.08f + PlanetProfileHashUnit(seed, 9u) * 0.22f;
    } else if (profile.style == SOLAR_STYLE_CRATER) {
        profile.oceanCoverage = 0.0f;
    }

    float roughnessBase = profile.style == SOLAR_STYLE_CRATER ? 1.20f :
                          profile.style == SOLAR_STYLE_LAVA ? 1.05f :
                          profile.style == SOLAR_STYLE_DESERT ? 0.72f : 0.88f;
    profile.terrainRoughness = roughnessBase *
                               (0.78f + PlanetProfileHashUnit(seed, 10u) * 0.48f);
    profile.rotationRate = 0.7f + PlanetProfileHashUnit(seed, 11u) * 2.5f;
    profile.hasRings = def->spaceProxyRadius >= 46.0f &&
                       PlanetProfileHashUnit(seed, 12u) > 0.92f;
    profile.atmosphereType = ClassifyAtmosphere(profile.style, atmosphere,
                                                temperature, composition);
    if (profile.tidallyLocked) {
        double orbitPeriod = SolarSystemPlanetOrbitPeriodGameTime(sys, index);
        if (orbitPeriod > 0.0) profile.rotationRate = (float)(360.0 / orbitPeriod);
    }
    profile.yearLength = (float)SolarSystemPlanetOrbitPeriodGameTime(sys, index);
    DerivePlanetClimateProfile(&profile);
    return profile;
}

static PlanetProfile LegacyPlanetProfile(uint32_t seed, SolarBodyStyle style,
                                         float terrainRadius)
{
    PlanetProfile profile = { 0 };
    float legacyProxyRadius = fmaxf(1.0f,
                                    (terrainRadius - 3.6f) / 1.22f);
    float radiusEarth = Clamp(0.72f +
                              (legacyProxyRadius - 40.0f) * 0.095f,
                              0.62f, 1.55f);
    float composition = PlanetProfileHashUnit(seed, 2u);
    float density = 0.78f + composition * 0.52f;
    profile.seed = seed;
    profile.style = style;
    profile.spaceProxyRadius = legacyProxyRadius;
    double massEarth = (double)density * radiusEarth * radiusEarth * radiusEarth;
    profile.massKg = SpaceUnitsGameMassToKilograms(massEarth);
    profile.physicalRadiusKm = (double)radiusEarth *
                               SPACE_UNITS_EARTH_RADIUS_KM;
    double gravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        profile.massKg, profile.physicalRadiusKm);
    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    profile.surfaceGravity = Clamp((float)(gravity / earthGravity),
                                   0.45f, 1.75f);
    profile.equilibriumTempK = style == SOLAR_STYLE_LAVA ? 410.0f :
                               style == SOLAR_STYLE_DESERT ? 325.0f :
                               style == SOLAR_STYLE_ICE ? 220.0f :
                               style == SOLAR_STYLE_CRATER ? 185.0f : 282.0f;
    profile.atmosphereDensity = style == SOLAR_STYLE_CRATER ? 0.08f :
                                style == SOLAR_STYLE_GAS ? 1.0f : 0.55f;
    profile.oceanCoverage = style == SOLAR_STYLE_ICE ? 0.32f :
                            style == SOLAR_STYLE_TEMPERATE ? 0.48f :
                            style == SOLAR_STYLE_LAVA ? 0.16f : 0.0f;
    profile.terrainRoughness = 0.82f + PlanetProfileHashUnit(seed, 10u) * 0.42f;
    profile.rotationRate = style == SOLAR_STYLE_GAS ? 6.0f :
                           0.7f + PlanetProfileHashUnit(seed, 11u) * 2.5f;
    // A loaded legacy save may already be standing on a former gas-style world.
    profile.hasSolidSurface = true;
    profile.hasRings = false;
    profile.tidalLockFactor = style == SOLAR_STYLE_GAS ? 0.0f :
                              PlanetProfileHashUnit(seed, 13u) * 0.35f;
    profile.tidallyLocked = profile.tidalLockFactor > 0.54f;
    profile.atmosphereType = ClassifyAtmosphere(style, profile.atmosphereDensity,
                                                profile.equilibriumTempK, composition);
    DerivePlanetClimateProfile(&profile);
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

bool StarSystemAt(int ax, int az, SolarSystemDef *out)
{
    if (ax == 0 && az == 0) {
        BuildSolSystem(out);
        out->center.x = (float)SpaceGlobalToLocalX(0);
        out->center.z = (float)SpaceGlobalToLocalZ(0);
        for (int i = 0; i < out->planetCount; i++) {
            out->planets[i].style = SolarPlanetProfile(out, i).style;
        }
        return true;
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
        (float)SpaceGlobalToLocalX(SpaceSystemGlobalCoordinate(ax)),
        STAR_SYSTEM_MID_Y + (float)verticalOffset,
        (float)SpaceGlobalToLocalZ(SpaceSystemGlobalCoordinate(az))
    };
    out->planetCount = 2 + (int)((h >> 8) % 4u);

    for (int i = 0; i < out->planetCount; i++) {
        unsigned int ph = WorldHash2D(ax * 53 + i * 7 + 1, az * 29 + i * 3 + 2);
        float orbitGame = (float)(180 + i * 120 + (int)(ph % 5u) * 8);
        out->planets[i].semiMajorAxisKm =
            SpaceUnitsGameDistanceToKilometers(orbitGame);
        // These are visitable planets, not asteroid props. Keep enough volume
        // for a layered surface, caves and a useful landing area.
        out->planets[i].spaceProxyRadius =
            (float)(40 + (int)((ph >> 6) % 9u));
        out->planets[i].physicalRadiusKm =
            SolidPlanetRadiusKilometersForProxy(
                out->planets[i].spaceProxyRadius);
        // The orbit is centered on the star. All planets share a generated
        // system plane, with their own small deviations around it.
        out->planets[i].yOffset = 0;
        out->planets[i].style = SOLAR_STYLE_CRATER;
    }
    for (int i = 0; i < out->planetCount; i++) {
        out->planets[i].style = SolarPlanetProfile(out, i).style;
    }
    return true;
}

Vector3 SolarSystemApparentDirection(const SolarSystemDef *sys, Vector3 observer)
{
    if (!sys) return Vector3Zero();

    Vector3 toStar = Vector3Subtract(sys->center, observer);
    float distance = Vector3Length(toStar);
    if (distance < 0.001f) return Vector3Zero();

    float horizontalDistance = sqrtf(toStar.x * toStar.x + toStar.z * toStar.z);
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
    return (Vector3){
        toStar.x * horizontalScale,
        sinf(latitude),
        toStar.z * horizontalScale
    };
}

Vector3 SolarSystemPlanetPositionAtTime(const SolarSystemDef *sys, int index,
                                        double simulationTime)
{
    const SolarPlanetDef *def = &sys->planets[index];
    unsigned int orbitHash = SolarOrbitHash(sys, index);
    unsigned int planeHash = SolarPlaneHash(sys);
    float phase = (float)(orbitHash % 6283u) / 1000.0f;
    // A planetary system is a thin but truly three-dimensional disk. Keep its
    // vertical extent inside the playable space layer while preserving distinct
    // planes between systems and slight differences between neighboring orbits.
    float systemInclination = ((float)((planeHash >> 6) % 25u) - 12.0f) * 0.0055f;
    float planetInclination = ((float)((orbitHash >> 22) % 9u) - 4.0f) * 0.0020f;
    float inclination = Clamp(systemInclination + planetInclination, -0.074f, 0.074f);
    float systemNode = (float)((planeHash >> 13) % 6283u) / 1000.0f;
    float nodeOffset = ((float)((orbitHash >> 7) % 17u) - 8.0f) * 0.005f;
    float node = systemNode + nodeOffset;
    // Mean motion follows Kepler's third law. Eccentricity and periapsis are
    // derived from stable system hashes, so a body keeps the same orbit across
    // visits without requiring a saved simulation state.
    double angularSpeed = SpaceUnitsKeplerMeanMotionGame(
        def->semiMajorAxisKm, SolarSystemStarMassKg(sys));
    float meanAnomaly = phase + (float)fmod(simulationTime * angularSpeed,
                                            2.0 * PI);
    float eccentricity = 0.015f + (float)((orbitHash >> 17) % 180u) / 1000.0f;
    if (sys->anchorX == 0 && sys->anchorZ == 0) {
        static const float solEccentricities[6] = { 0.08f, 0.04f, 0.02f, 0.11f, 0.15f, 0.06f };
        eccentricity = solEccentricities[index];
    }
    // Surface-world save data historically infers a host system from the
    // stored body center. Keep apoapsis inside that system's anchor cell so
    // outer planets never become ambiguous after a later launch or load.
    float semiMajorAxis = (float)SpaceUnitsKilometersToGameDistance(
        def->semiMajorAxisKm);
    float hostCellLimit = 694.0f / fmaxf(semiMajorAxis, 1.0f) - 1.0f;
    // Compact multi-planet systems remain stable when their orbital shells do
    // not cross. A modest eccentricity still gives visible Keplerian motion
    // without making the large game-scale proxies collide at conjunction.
    eccentricity = Clamp(eccentricity, 0.0f, fminf(0.05f, fmaxf(hostCellLimit, 0.0f)));

    float eccentricAnomaly = meanAnomaly;
    for (int iteration = 0; iteration < 4; iteration++) {
        float residual = eccentricAnomaly - eccentricity * sinf(eccentricAnomaly) - meanAnomaly;
        eccentricAnomaly -= residual / fmaxf(1.0f - eccentricity * cosf(eccentricAnomaly),
                                              0.001f);
    }

    float semiMinorAxis = semiMajorAxis * sqrtf(1.0f - eccentricity * eccentricity);
    float ellipseX = semiMajorAxis * (cosf(eccentricAnomaly) - eccentricity);
    float ellipseZ = semiMinorAxis * sinf(eccentricAnomaly);
    float periapsis = (float)((orbitHash >> 3) % 6283u) / 1000.0f;
    float periCos = cosf(periapsis);
    float periSin = sinf(periapsis);
    float orbitX = ellipseX * periCos - ellipseZ * periSin;
    float orbitZ = ellipseX * periSin + ellipseZ * periCos;
    float planeX = orbitX;
    float planeZ = orbitZ * cosf(inclination);
    float planeY = orbitZ * sinf(inclination);
    float nodeCos = cosf(node);
    float nodeSin = sinf(node);
    return (Vector3){
        sys->center.x + planeX * nodeCos - planeZ * nodeSin,
        sys->center.y + planeY,
        sys->center.z + planeX * nodeSin + planeZ * nodeCos
    };
}

Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index)
{
    return SolarSystemPlanetPositionAtTime(sys, index, solarSimulationTime);
}

static Vector3 SolarSystemPlanetVelocityAtTime(const SolarSystemDef *sys, int index,
                                               double simulationTime)
{
    const double sampleDt = 0.05;
    Vector3 before = SolarSystemPlanetPositionAtTime(sys, index,
                                                     simulationTime - sampleDt);
    Vector3 after = SolarSystemPlanetPositionAtTime(sys, index,
                                                    simulationTime + sampleDt);
    return Vector3Scale(Vector3Subtract(after, before),
                        1.0f / (float)(2.0 * sampleDt));
}

double SolarSystemPlanetOrbitPeriodSeconds(const SolarSystemDef *sys, int index)
{
    if (!sys || index < 0 || index >= sys->planetCount) return 0.0;

    return SpaceUnitsKeplerPeriodSeconds(sys->planets[index].semiMajorAxisKm,
                                         SolarSystemStarMassKg(sys));
}

double SolarSystemPlanetOrbitPeriodGameTime(const SolarSystemDef *sys, int index)
{
    return SpaceUnitsSecondsToGameTime(
        SolarSystemPlanetOrbitPeriodSeconds(sys, index));
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

int SolarSystemLightSources(const SolarSystemDef *sys, SolarLightSource *out,
                            int maxCount)
{
    if (!sys || !out || maxCount <= 0) return 0;

    uint32_t hash = SolarLightHash(sys);
    float primaryMass = fmaxf(sys->star.massSolar, 0.08f);
    unsigned int multipleRoll = hash % 1000u;
    unsigned int binaryThreshold = primaryMass < 0.60f ? 250u :
                                   (primaryMass < 1.40f ? 440u : 680u);
    unsigned int tripleThreshold = primaryMass < 0.60f ? 30u :
                                   (primaryMass < 1.40f ? 80u : 160u);
    int count = multipleRoll < tripleThreshold ? 3 :
                (multipleRoll < binaryThreshold ? 2 : 1);
    if (sys->anchorX == 0 && sys->anchorZ == 0) count = 1;
    if (count > maxCount) count = maxCount;

    out[0] = (SolarLightSource){
        .center = sys->center,
        .stellar = sys->star,
        .spectrum = sys->spectrum,
        .spaceProxyRadius = (float)sys->starProxyRadius,
        .luminosity = SolarSystemStarLuminosity(sys),
        .primary = true
    };
    if (count == 1) return count;

    float separation = 22.0f + (float)((hash >> 8) % 36u);
    StellarProfile companion = SolarCompanionProfile(
        sys, hash ^ 0x94d049bbu, 0.18f, 0.92f);
    double binaryMeanMotion = SpaceUnitsKeplerMeanMotionGame(
        SpaceUnitsGameDistanceToKilometers(separation),
        sys->star.massKg + companion.massKg);
    float binaryPhase = (float)(hash % 6283u) / 1000.0f +
                        (float)fmod(solarSimulationTime * binaryMeanMotion,
                                    2.0 * PI);
    Vector3 offset = {
        cosf(binaryPhase) * separation,
        sinf(binaryPhase * 0.71f) * separation * 0.14f,
        sinf(binaryPhase) * separation
    };
    out[1] = (SolarLightSource){
        .center = Vector3Add(sys->center, offset),
        .stellar = companion,
        .spectrum = companion.spectrum,
        .spaceProxyRadius = (float)StellarVisualRadius(&companion),
        .luminosity = companion.luminositySolar,
        .primary = false
    };
    if (count == 2) return count;

    float tertiarySeparation = separation * 1.35f;
    StellarProfile tertiary = SolarCompanionProfile(
        sys, hash ^ 0x369dea0fu, 0.10f, 0.62f);
    double tertiaryMeanMotion = SpaceUnitsKeplerMeanMotionGame(
        SpaceUnitsGameDistanceToKilometers(tertiarySeparation),
        sys->star.massKg + companion.massKg + tertiary.massKg);
    float tertiaryPhase = (float)((hash >> 4) % 6283u) / 1000.0f +
                          (float)fmod(solarSimulationTime * tertiaryMeanMotion,
                                      2.0 * PI);
    Vector3 tertiaryOffset = {
        cosf(tertiaryPhase) * tertiarySeparation,
        sinf(tertiaryPhase * 0.63f) * tertiarySeparation * 0.11f,
        sinf(tertiaryPhase) * tertiarySeparation
    };
    out[2] = (SolarLightSource){
        .center = Vector3Add(sys->center, tertiaryOffset),
        .stellar = tertiary,
        .spectrum = tertiary.spectrum,
        .spaceProxyRadius = (float)StellarVisualRadius(&tertiary),
        .luminosity = tertiary.luminositySolar,
        .primary = false
    };
    return count;
}

float SolarLightIrradianceAt(const SolarLightSource *source, Vector3 point)
{
    if (!source || source->luminosity <= 0.0f) return 0.0f;
    double distanceKm = SpaceUnitsGameDistanceToKilometers(
        Vector3Distance(source->center, point));
    double minimumDistanceAu = SpaceUnitsGameDistanceToKilometers(1.0) /
                               SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    double distanceAu = fmax(distanceKm / SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
                             minimumDistanceAu);
    return (float)((double)source->luminosity /
                   (distanceAu * distanceAu));
}

float SolarSystemIrradianceAt(const SolarLightSource *sources, int sourceCount,
                              Vector3 point)
{
    if (!sources || sourceCount <= 0) return 0.0f;
    float total = 0.0f;
    for (int i = 0; i < sourceCount; i++) {
        total += SolarLightIrradianceAt(&sources[i], point);
    }
    return total;
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

bool PlanetWorldLightStateAt(Vector3 surfacePosition, PlanetLightState *out)
{
    if (!out) return false;
    *out = (PlanetLightState){ 0 };
    if (!planetWorld.active || !planetWorld.profile.hasSolidSurface) return false;

    SolarSystemDef system = { 0 };
    if (!SurfaceHostSystem(&system)) return false;
    int orbitIndex = planetWorld.planetIndex - 1;
    if (orbitIndex < 0 || orbitIndex >= system.planetCount) return false;

    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    int sourceCount = SolarSystemLightSources(&system, sources, MAX_SOLAR_LIGHTS);
    if (sourceCount <= 0) return false;

    Vector3 planetCenter = SolarSystemPlanetCenter(&system, orbitIndex);
    float spinPhase = (float)(planetWorld.seed & 0xffffu) / 65535.0f * 2.0f * PI +
                      (float)solarSimulationTime * planetWorld.profile.rotationRate * DEG2RAD;
    float totalWeight = 0.0f;
    Vector3 weightedDirection = Vector3Zero();
    float weightedR = 0.0f;
    float weightedG = 0.0f;
    float weightedB = 0.0f;

    for (int i = 0; i < sourceCount; i++) {
        Vector3 toSource = Vector3Subtract(sources[i].center, planetCenter);
        float distance = Vector3Length(toSource);
        if (distance < 0.001f) continue;

        // Convert the inertial star direction into the rotating planet frame.
        // The inverse rotation keeps a tidally locked face pointed at its star.
        Vector3 direction = PlanetWorldSkyDirection(toSource);
        direction = PlanetRotateY(Vector3Normalize(direction), -spinPhase);
        float weight = SolarLightIrradianceAt(&sources[i], planetCenter);
        bool eclipsed = false;
        for (int j = 0; j < sourceCount; j++) {
            if (i == j) continue;
            Vector3 toOther = Vector3Subtract(sources[j].center, planetCenter);
            float otherDistance = Vector3Length(toOther);
            if (otherDistance >= distance || otherDistance < 0.001f) continue;
            Vector3 otherDirection = PlanetRotateY(Vector3Normalize(PlanetWorldSkyDirection(toOther)),
                                                   -spinPhase);
            float sourceAngular = asinf(Clamp(
                sources[i].spaceProxyRadius / distance, 0.0f, 0.98f));
            float otherAngular = asinf(Clamp(
                sources[j].spaceProxyRadius / otherDistance, 0.0f, 0.98f));
            if (Vector3DotProduct(direction, otherDirection) >
                cosf(sourceAngular + otherAngular)) {
                eclipsed = true;
                break;
            }
        }
        if (eclipsed) {
            weight *= 0.12f;
            out->specialEclipse = true;
        }
        Color color = SpectrumColor(sources[i].spectrum);
        out->sourceDirections[i] = direction;
        out->sourceColors[i] = color;
        out->sourceIntensities[i] = weight;
        out->sourceVisibility[i] = eclipsed ? 0.12f : 1.0f;
        totalWeight += weight;
        weightedDirection = Vector3Add(weightedDirection, Vector3Scale(direction, weight));
        weightedR += (float)color.r * weight;
        weightedG += (float)color.g * weight;
        weightedB += (float)color.b * weight;
    }

    if (Vector3LengthSqr(weightedDirection) < 0.000001f || totalWeight <= 0.0f) return false;
    Vector3 sunDirection = Vector3Normalize(weightedDirection);
    Vector3 surfaceNormal = PlanetSurfaceNormalAt(surfacePosition);
    float incidence = Vector3DotProduct(surfaceNormal, sunDirection);
    float incidentIrradiance = fmaxf(incidence, 0.0f) * totalWeight;
    float daylight = 1.0f - expf(-incidentIrradiance * 1.45f);

    float moonAngle = (float)(planetWorld.seed % 6283u) / 1000.0f +
                      (float)solarSimulationTime * 0.018f;
    Vector3 moonReference = Vector3CrossProduct(sunDirection, (Vector3){ 0.0f, 1.0f, 0.0f });
    if (Vector3LengthSqr(moonReference) < 0.001f) {
        moonReference = Vector3CrossProduct(sunDirection, (Vector3){ 1.0f, 0.0f, 0.0f });
    }
    moonReference = Vector3Normalize(moonReference);
    Vector3 moonDirection = Vector3Normalize(Vector3Add(
        Vector3Scale(sunDirection, cosf(moonAngle)),
        Vector3Scale(moonReference, sinf(moonAngle))));
    float moonIllumination = Clamp((1.0f - Vector3DotProduct(moonDirection, sunDirection)) * 0.5f,
                                   0.0f, 1.0f);
    float moonAlignment = Vector3DotProduct(moonDirection, sunDirection);
    if (moonAlignment > 0.994f && incidence > 0.0f) {
        out->eclipse = Clamp((moonAlignment - 0.994f) / 0.006f, 0.0f, 1.0f);
        out->specialEclipse = true;
    }

    out->sunDirection = sunDirection;
    out->moonDirection = moonDirection;
    out->daylight = daylight;
    out->sunset = incidence > 0.0f ?
                  powf(1.0f - Clamp(incidence, 0.0f, 1.0f), 2.0f) *
                  Clamp(sqrtf(totalWeight), 0.0f, 1.0f) : 0.0f;
    out->ringShadow = PlanetRingShadowForPoint(surfacePosition, sunDirection);
    out->daylight *= (1.0f - out->ringShadow * 0.72f);
    out->daylight *= (1.0f - out->eclipse * 0.88f);
    out->daylight = Clamp(out->daylight, 0.0f, 1.0f);
    out->moonIllumination = moonIllumination;
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

static BlockType StarBlock(int bx, int by, int bz, float distSqr, float shellSqr, SpectrumType spectrum)
{
    unsigned int h = WorldHash3D(SpaceLocalToGlobalX(bx), by, SpaceLocalToGlobalZ(bz));
    bool surface = distSqr >= shellSqr;

    switch (spectrum) {
    case SPECTRUM_RED_DWARF:
        if (surface) {
            if (h % 5u == 0u) return BLOCK_LAVA;
            if (h % 9u == 0u) return BLOCK_GLOWSTONE;
            if (h % 11u == 0u) return BLOCK_METEORITE;
            return BLOCK_MOON_ROCK;
        }
        return (h % 7u == 0u) ? BLOCK_GLOWSTONE : BLOCK_MOON_ROCK;
    case SPECTRUM_ORANGE:
        if (surface) {
            if (h % 7u == 0u) return BLOCK_LAVA;
            if (h % 5u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_MOON_SAND;
        }
        return (h % 9u == 0u) ? BLOCK_STAR_MATTER : BLOCK_GLOWSTONE;
    case SPECTRUM_YELLOW:
        if (!surface) return (h % 5u == 0u) ? BLOCK_STAR_MATTER : BLOCK_GLOWSTONE;
        if (h % 9u == 0u) return BLOCK_LAVA;
        if (h % 4u == 0u) return BLOCK_STAR_MATTER;
        return BLOCK_GLOWSTONE;
    case SPECTRUM_BLUE_WHITE:
        if (surface) {
            if (h % 6u == 0u) return BLOCK_ICE;
            if (h % 9u == 0u) return BLOCK_MOON_SAND;
            if (h % 5u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_STAR_MATTER;
        }
        return (h % 7u == 0u) ? BLOCK_GLOWSTONE : BLOCK_STAR_MATTER;
    case SPECTRUM_RED_GIANT:
        if (surface) {
            if (h % 3u == 0u) return BLOCK_LAVA;
            if (h % 8u == 0u) return BLOCK_METEORITE;
            if (h % 7u == 0u) return BLOCK_GLOWSTONE;
            return BLOCK_MOON_ROCK;
        }
        return (h % 5u == 0u) ? BLOCK_LAVA : BLOCK_GLOWSTONE;
    default:
        return BLOCK_GLOWSTONE;
    }
}

static void FillStarBody(SpaceChunk *chunk, int startX, int startZ,
                         int cx, int cy, int cz, int radius, SpectrumType spectrum)
{
    int chunkMinX = startX;
    int chunkMaxX = startX + CHUNK_SIZE - 1;
    int chunkMinZ = startZ;
    int chunkMaxZ = startZ + CHUNK_SIZE - 1;
    if (cx + radius < chunkMinX || cx - radius > chunkMaxX) return;
    if (cz + radius < chunkMinZ || cz - radius > chunkMaxZ) return;

    float radiusSqr = (float)(radius * radius);
    float shellSqr = (float)((radius - 1) * (radius - 1));

    for (int lx = 0; lx < CHUNK_SIZE; lx++) {
        for (int ly = 0; ly < SPACE_LAYER_HEIGHT; ly++) {
            for (int lz = 0; lz < CHUNK_SIZE; lz++) {
                int bx = startX + lx;
                int by = SPACE_LAYER_Y + ly;
                int bz = startZ + lz;
                float dx = (float)(bx - cx);
                float dy = (float)(by - cy);
                float dz = (float)(bz - cz);
                float distSqr = dx * dx + dy * dy + dz * dz;
                if (distSqr >= radiusSqr) continue;
                chunk->blocks[lx][ly][lz] = (unsigned short)StarBlock(bx, by, bz, distSqr, shellSqr, spectrum);
            }
        }
    }
}

static void FillSolarSystemsInChunk(SpaceChunk *chunk, int startX, int startZ)
{
    int minAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX - 900), STAR_SYSTEM_SPACING);
    int maxAnchorX = FloorDivInt(SpaceLocalToGlobalX(startX + CHUNK_SIZE + 900),
                                 STAR_SYSTEM_SPACING);
    int minAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ - 900), STAR_SYSTEM_SPACING);
    int maxAnchorZ = FloorDivInt(SpaceLocalToGlobalZ(startZ + CHUNK_SIZE + 900),
                                 STAR_SYSTEM_SPACING);

    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            FillStarBody(chunk, startX, startZ,
                         (int)sys.center.x, (int)sys.center.y, (int)sys.center.z,
                         sys.starProxyRadius, sys.spectrum);

            // Planets are rendered as distant proxies. Landing switches to a
            // dedicated procedural surface world instead of a finite voxel ball.
        }
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

int StarSystemsNear(Vector3 pos, float maxDist, SolarSystemDef *out, int maxCount)
{
    if (!out || maxCount <= 0) return 0;

    int centerAx = FloorDivInt(SpaceLocalToGlobalX((int)floorf(pos.x)),
                               STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ((int)floorf(pos.z)),
                               STAR_SYSTEM_SPACING);
    int radiusAnchors = (int)(maxDist / (float)STAR_SYSTEM_SPACING) + 1;

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
                if (d < dists[farthest]) {
                    found[farthest] = sys;
                    dists[farthest] = d;
                }
            }
        }
    }

    for (int i = 0; i < foundCount; i++) {
        int best = i;
        for (int j = i + 1; j < foundCount; j++) {
            if (dists[j] < dists[best]) best = j;
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

int SpaceBodiesNear(Vector3 pos, float maxDist, SpaceBodyInfo *out, int maxCount)
{
    int count = 0;
    int centerAx = FloorDivInt(SpaceLocalToGlobalX((int)floorf(pos.x)),
                               STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(SpaceLocalToGlobalZ((int)floorf(pos.z)),
                               STAR_SYSTEM_SPACING);

    for (int ax = centerAx - 1; ax <= centerAx + 1; ax++) {
        for (int az = centerAz - 1; az <= centerAz + 1; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;
            if (count >= maxCount) return count;

            Vector3 star = sys.center;
            float starDist = Vector3Distance(star, pos);
            if (starDist <= maxDist) {
                out[count] = (SpaceBodyInfo){
                    .center = star,
                    .physicalRadiusKm = sys.star.radiusKm,
                    .spaceProxyRadius = (float)sys.starProxyRadius,
                    .dist = starDist,
                    .isStar = true,
                    .index = 0,
                    .systemAnchorX = ax,
                    .systemAnchorZ = az,
                    .hostStar = sys.star,
                    .spectrum = sys.spectrum
                };
                snprintf(out[count].name, sizeof(out[count].name), "%s", sys.name);
                count++;
            }

            for (int i = 0; i < sys.planetCount; i++) {
                if (count >= maxCount) return count;
                Vector3 center = SolarSystemPlanetCenter(&sys, i);
                float dist = Vector3Distance(center, pos);
                if (dist > maxDist) continue;
                PlanetProfile profile = SolarPlanetProfile(&sys, i);
                out[count] = (SpaceBodyInfo){
                    .center = center,
                    .physicalRadiusKm = profile.physicalRadiusKm,
                    .semiMajorAxisKm = sys.planets[i].semiMajorAxisKm,
                    .spaceProxyRadius = profile.spaceProxyRadius,
                    .dist = dist,
                    .isStar = false,
                    .index = i + 1,
                    .systemAnchorX = ax,
                    .systemAnchorZ = az,
                    .worldSeed = profile.seed,
                    .hostStar = sys.star,
                    .spectrum = sys.spectrum,
                    .style = profile.style,
                    .profile = profile
                };
                snprintf(out[count].name, sizeof(out[count].name), "%s", sys.name);
                count++;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            if (out[j].dist < out[i].dist) {
                SpaceBodyInfo tmp = out[i];
                out[i] = out[j];
                out[j] = tmp;
            }
        }
    }
    return count;
}

bool SpaceBodyPick(Vector3 origin, Vector3 direction, SpaceBodyInfo *out)
{
    if (!out || Vector3LengthSqr(direction) < 0.000001f) return false;
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

static float SpaceProxyGravitationalParameter(double massKg,
                                              double physicalRadiusKm,
                                              float proxyRadius)
{
    double surfaceGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        massKg, physicalRadiusKm);
    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    if (!(surfaceGravity > 0.0) || !(earthGravity > 0.0) ||
        !(proxyRadius > 0.0f)) {
        return 0.0f;
    }
    // Encounter spheres are deliberately enlarged. Preserve the generated
    // body's physical surface-gravity ratio across that proxy transform.
    return SPACE_PROXY_EARTH_SURFACE_ACCEL_GAME *
           (float)(surfaceGravity / earthGravity) *
           proxyRadius * proxyRadius;
}

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
    if (HomeWorldSurfaceIsActive() || PlanetWorldIsActive()) return false;

    SpaceGravityCandidate candidates[64];
    int candidateCount = 0;
    float homeRadius = HomeWorldProxyRadius();
    AddSpaceGravityCandidate(
        candidates, &candidateCount, 64,
        (SpacePhysicsGravityBody){
            .center = HomeWorldCenter(),
            .softeningRadiusGame = homeRadius,
            .gravitationalParameterGame = SpaceProxyGravitationalParameter(
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
                Vector3Zero(),
                SPACE_GRAVITY_PRIMARY_STAR, bodies[i].name);
            continue;
        }

        int planetIndex = bodies[i].index - 1;
        if (planetIndex < 0 || bodies[i].semiMajorAxisKm <= 0.0) continue;

        Vector3 orbitalVelocity = Vector3Zero();
        SolarSystemDef parentSystem;
        if (StarSystemAt(bodies[i].systemAnchorX, bodies[i].systemAnchorZ,
                         &parentSystem) &&
            planetIndex < parentSystem.planetCount) {
            orbitalVelocity = SolarSystemPlanetVelocityAtTime(
                &parentSystem, planetIndex, solarSimulationTime);
        }

        float terrainRadius = SolarBodyTerrainProxyRadius(
            bodies[i].spaceProxyRadius);
        float orbitRadius = (float)SpaceUnitsKilometersToGameDistance(
            bodies[i].semiMajorAxisKm);
        float minimumSoi = terrainRadius * 2.20f;
        float maximumSoi = fmaxf(minimumSoi,
                                 fminf(orbitRadius * 0.36f,
                                       SPACE_MAX_PLANET_ENCOUNTER_RADIUS_GAME));
        double physicalSoiKm = SpaceUnitsLaplaceSphereOfInfluenceKm(
            bodies[i].semiMajorAxisKm, bodies[i].profile.massKg,
            bodies[i].hostStar.massKg);
        float physicalSoi = (float)SpaceUnitsKilometersToGameDistance(
            physicalSoiKm);
        float soi = Clamp(physicalSoi, minimumSoi, maximumSoi);
        float mu = SpaceProxyGravitationalParameter(
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
            orbitalVelocity,
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
    *out = (SpaceGravitySample){
        .active = true,
        .kind = primary->kind,
        .center = primary->body.center,
        .primaryVelocity = primary->velocity,
        .acceleration = SpacePhysicsGravityAcceleration(position, &primary->body),
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
    if (!homeWorld.surfaceActive || planetWorld.active ||
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
    if (planetWorld.active || homeWorld.surfaceActive) return false;

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
    if (planetWorld.active || homeWorld.surfaceActive) return false;

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
    SetImportMessage(TextFormat("Landed on %s - %s. Biosphere: %s / %s / %s.",
                                planetWorld.name,
                                PlanetBiomeName(PlanetBiomeAt((int)floorf(player->position.x),
                                                               (int)floorf(player->position.z))),
                                PlanetEcologyBiomassName(), PlanetEcologyChemistryName(),
                                PlanetEcologyBodyPlanName()));
    return true;
}

bool PlanetWorldTryLaunch(Player *player)
{
    if (!planetWorld.active || PlanetWorldAtmosphereFade(player->position) < 1.0f) {
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
        Vector3 currentCenter = SolarSystemPlanetCenter(&system, orbitIndex);
        returnPosition = PlanetReturnPosition(currentCenter,
                                              planetWorld.spaceProxyRadius,
                                              outward);
        launchVelocity = Vector3Add(
            launchVelocity,
            SolarSystemPlanetVelocityAtTime(&system, orbitIndex,
                                            solarSimulationTime));
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

bool PlanetWorldSaveState(FILE *file)
{
    uint8_t active = planetWorld.active ? 1u : 0u;
    uint32_t style = (uint32_t)planetWorld.style;
    int32_t originX = (int32_t)planetWorld.originX;
    int32_t originZ = (int32_t)planetWorld.originZ;
    int32_t planetIndex = (int32_t)planetWorld.planetIndex;
    float bodyCenter[3] = { planetWorld.bodyCenter.x, planetWorld.bodyCenter.y,
                            planetWorld.bodyCenter.z };
    float returnPosition[3] = { planetWorld.returnPosition.x, planetWorld.returnPosition.y,
                                planetWorld.returnPosition.z };

    return fwrite(&active, sizeof(active), 1, file) == 1 &&
           fwrite(&planetWorld.seed, sizeof(planetWorld.seed), 1, file) == 1 &&
           fwrite(&style, sizeof(style), 1, file) == 1 &&
           fwrite(&originX, sizeof(originX), 1, file) == 1 &&
           fwrite(&originZ, sizeof(originZ), 1, file) == 1 &&
           fwrite(&planetIndex, sizeof(planetIndex), 1, file) == 1 &&
           fwrite(bodyCenter, sizeof(bodyCenter), 1, file) == 1 &&
           fwrite(returnPosition, sizeof(returnPosition), 1, file) == 1 &&
           fwrite(&planetWorld.spaceProxyRadius,
                  sizeof(planetWorld.spaceProxyRadius), 1, file) == 1 &&
           fwrite(planetWorld.name, sizeof(planetWorld.name), 1, file) == 1;
}

bool PlanetWorldLoadState(FILE *file)
{
    PlanetWorldContext loaded = { 0 };
    uint8_t active = 0;
    uint32_t style = 0;
    int32_t originX = 0;
    int32_t originZ = 0;
    int32_t planetIndex = 0;
    float bodyCenter[3] = { 0 };
    float returnPosition[3] = { 0 };

    if (fread(&active, sizeof(active), 1, file) != 1 ||
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

    if (active > 1u || style > (uint32_t)SOLAR_STYLE_TEMPERATE ||
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
    loaded.profile = LegacyPlanetProfile(loaded.seed, loaded.style,
                                         loaded.spaceProxyRadius);
    loaded.name[sizeof(loaded.name) - 1] = '\0';
    planetWorld = loaded;
    return true;
}

bool HomeWorldSaveState(FILE *file)
{
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

    chunk->hasStar = false;
    FillSolarSystemsInChunk(chunk, startX, startZ);

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
                chunk->hasStar = job->result.hasStar;
                chunk->starX = job->result.starX;
                chunk->starY = job->result.starY;
                chunk->starZ = job->result.starZ;
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
    uint32_t count = (uint32_t)spaceEditCount;
    fwrite(&count, sizeof(count), 1, file);
    if (spaceEditCount > 0) {
        fwrite(spaceEdits, sizeof(BlockEdit), (size_t)spaceEditCount, file);
    }
}

void SpaceLoadEdits(FILE *file)
{
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

void SpaceUpdateStarGlow(Vector3 playerPosition)
{
    for (int i = 0; i < MAX_SPACE_CHUNKS; i++) {
        SpaceChunk *chunk = &spaceChunks[i];
        if (!chunk->loaded || !chunk->hasStar) continue;

        if (SpaceBlockAt(chunk->starX, chunk->starY, chunk->starZ) != BLOCK_STAR_MATTER) continue;

        Vector3 star = { (float)chunk->starX + 0.5f, (float)chunk->starY + 0.5f, (float)chunk->starZ + 0.5f };
        float dist = Vector3Distance(star, playerPosition);
        if (dist > 18.0f) continue;

        int count = (dist < 9.0f) ? 2 : 1;
        for (int k = 0; k < count; k++) {
            Vector3 offset = {
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f,
                ((float)rand() / (float)RAND_MAX - 0.5f) * 3.0f
            };
            ParticlesEmitOne(Vector3Add(star, offset),
                             (Vector3){ 0.1f, 0.35f, 0.1f },
                             (Color){ 255, 244, 190, 220 },
                             (Vector3){ 0.09f, 0.09f, 0.09f },
                             1.4f, 0.0f);
        }
    }
}

int GetSpaceEditCount(void)
{
    return spaceEditCount;
}

void SpaceUpdateSolarGlow(Vector3 playerPosition)
{
    SolarSystemDef sys;
    float sysDist = 0.0f;
    if (!FindNearestSystem(playerPosition, 60.0f, &sys, &sysDist)) return;

    float dist = Vector3Distance(sys.center, playerPosition);
    int count = (dist < 24.0f) ? 3 : 1;
    Color glow = SpectrumColor(sys.spectrum);
    for (int k = 0; k < count; k++) {
        Vector3 offset = {
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f,
            ((float)rand() / (float)RAND_MAX - 0.5f) * 10.0f
        };
        ParticlesEmitOne(Vector3Add(sys.center, offset),
                         (Vector3){ ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f,
                                    0.2f + (float)rand() / (float)RAND_MAX * 0.5f,
                                    ((float)rand() / (float)RAND_MAX - 0.5f) * 0.8f },
                         glow,
                         (Vector3){ 0.14f, 0.14f, 0.14f },
                         1.8f, 0.0f);
    }
}
