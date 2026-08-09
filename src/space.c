#include "space.h"

#include "raymath.h"
#include "chunks.h"
#include "terrain.h"
#include "particles.h"
#include "world.h"

#include <math.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ASTEROID_SPACING 26
#define ASTEROID_PROBABILITY 55u
#define SPACE_MESH_REBUILDS_PER_FRAME 2
#define SOLAR_ORBIT_BASE_SPEED 0.040f

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
    float bodyRadius;
    char name[32];
} PlanetWorldContext;

typedef struct HomeWorldContext {
    bool surfaceActive;
    Vector3 returnPosition;
} HomeWorldContext;

#define HOME_WORLD_RADIUS 62.0f
#define HOME_WORLD_CENTER_Y (-30.0f)
#define HOME_WORLD_LANDING_MARGIN 20.0f

static PlanetWorldContext planetWorld = { 0 };
static HomeWorldContext homeWorld = {
    .surfaceActive = true,
    .returnPosition = { 0.5f, 12.0f, 0.5f }
};
static double solarSimulationTime = 0.0;

bool HomeWorldSurfaceIsActive(void)
{
    return homeWorld.surfaceActive && !planetWorld.active;
}

Vector3 HomeWorldCenter(void)
{
    return (Vector3){ 0.0f, HOME_WORLD_CENTER_Y, 0.0f };
}

float HomeWorldRadius(void)
{
    return HOME_WORLD_RADIUS;
}

float HomeWorldSpaceFade(Vector3 position)
{
    if (planetWorld.active) return 0.0f;
    if (!homeWorld.surfaceActive) return 1.0f;
    return Clamp((position.y - SPACE_EXIT_Y) / (SPACE_ENTER_Y - SPACE_EXIT_Y),
                 0.0f, 1.0f);
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

static float SolarBodyAmplitude(float radius)
{
    return 1.6f + radius * 0.22f;
}

float SolarBodyTerrainRadius(float radius)
{
    return radius + SolarBodyAmplitude(radius) + 2.0f;
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

static void BuildSolSystem(SolarSystemDef *out)
{
    out->exists = true;
    out->anchorX = 0;
    out->anchorZ = 0;
    snprintf(out->name, sizeof(out->name), "Sol");
    out->spectrum = SPECTRUM_YELLOW;
    out->starRadius = 13;
    out->center = (Vector3){ 0.0f, (float)SPACE_LAYER_Y + 48.0f, 0.0f };
    out->planetCount = 6;
    static const SolarPlanetDef solPlanets[6] = {
        { 180, 44, 0, SOLAR_STYLE_LAVA },
        { 260, 42, 0, SOLAR_STYLE_ICE },
        { 340, 46, 0, SOLAR_STYLE_DESERT },
        { 430, 48, 0, SOLAR_STYLE_GAS },
        { 520, 45, 0, SOLAR_STYLE_CRATER },
        { 650, 40, 0, SOLAR_STYLE_LAVA }
    };
    for (int i = 0; i < 6; i++) out->planets[i] = solPlanets[i];
}

static float SolarSpectrumMass(SpectrumType spectrum)
{
    switch (spectrum) {
    case SPECTRUM_RED_DWARF: return 0.45f;
    case SPECTRUM_ORANGE:    return 0.75f;
    case SPECTRUM_YELLOW:    return 1.00f;
    case SPECTRUM_BLUE_WHITE:return 2.00f;
    case SPECTRUM_RED_GIANT: return 3.50f;
    default:                 return 1.00f;
    }
}

static float SolarSpectrumLuminosity(SpectrumType spectrum)
{
    switch (spectrum) {
    case SPECTRUM_RED_DWARF: return 0.06f;
    case SPECTRUM_ORANGE:    return 0.45f;
    case SPECTRUM_YELLOW:    return 1.00f;
    case SPECTRUM_BLUE_WHITE:return 8.00f;
    case SPECTRUM_RED_GIANT: return 35.0f;
    default:                 return 1.00f;
    }
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
    float legacyAngle = (float)(SolarOrbitHash(sys, index) % 6283u) / 1000.0f;
    int legacyX = (int)floorf(sys->center.x + cosf(legacyAngle) * (float)def->orbit);
    int legacyZ = (int)floorf(sys->center.z + sinf(legacyAngle) * (float)def->orbit);
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

PlanetProfile SolarPlanetProfile(const SolarSystemDef *sys, int index)
{
    PlanetProfile profile = { 0 };
    if (!sys || index < 0 || index >= sys->planetCount) return profile;

    const SolarPlanetDef *def = &sys->planets[index];
    uint32_t seed = SolarPlanetWorldSeed(sys, index);
    float sizeUnit = PlanetProfileHashUnit(seed, 1u);
    float composition = PlanetProfileHashUnit(seed, 2u);
    float volatileSupply = PlanetProfileHashUnit(seed, 3u);
    float orbitAU = fmaxf((float)def->orbit / 340.0f, 0.18f);
    // 340 blocks is the game's one-AU reference. This is a game-scale
    // blackbody equilibrium estimate, so the same orbit also drives climate.
    float temperature = 278.5f * powf(SolarSpectrumLuminosity(sys->spectrum), 0.25f) /
                        sqrtf(orbitAU);
    temperature *= 0.96f + PlanetProfileHashUnit(seed, 4u) * 0.08f;

    float solidRadiusEarth = 0.72f + ((float)def->size - 40.0f) * 0.095f;
    solidRadiusEarth = Clamp(solidRadiusEarth, 0.62f, 1.55f);
    float atmosphere = solidRadiusEarth * 0.34f + volatileSupply * 0.52f;
    atmosphere -= Clamp((temperature - 330.0f) / 260.0f, 0.0f, 0.45f);
    atmosphere = Clamp(atmosphere, 0.0f, 0.95f);

    bool forcedGasGiant = sys->anchorX == 0 && sys->anchorZ == 0 && index == 3;
    bool gasGiant = forcedGasGiant ||
                    (index > 0 && def->size >= 47 && temperature < 430.0f &&
                     PlanetProfileHashUnit(seed, 5u) > 0.52f);

    profile.seed = seed;
    profile.bodyRadius = (float)def->size;
    profile.equilibriumTempK = temperature;
    profile.hasSolidSurface = !gasGiant;
    if (gasGiant) {
        float gasRadiusEarth = 2.8f + sizeUnit * 1.8f;
        profile.massEarth = 12.0f + composition * 32.0f;
        profile.surfaceGravity = Clamp(profile.massEarth /
                                       (gasRadiusEarth * gasRadiusEarth), 0.75f, 2.40f);
        profile.style = SOLAR_STYLE_GAS;
        profile.atmosphereDensity = 1.0f;
        profile.atmosphereType = PLANET_ATMOSPHERE_DENSE;
        profile.oceanCoverage = 0.0f;
        profile.terrainRoughness = 0.0f;
        profile.rotationRate = 5.0f + PlanetProfileHashUnit(seed, 6u) * 3.0f;
        profile.hasRings = forcedGasGiant || PlanetProfileHashUnit(seed, 7u) > 0.34f;
        return profile;
    }

    float density = 0.78f + composition * 0.52f;
    profile.massEarth = density * solidRadiusEarth * solidRadiusEarth * solidRadiusEarth;
    profile.surfaceGravity = Clamp(profile.massEarth /
                                   (solidRadiusEarth * solidRadiusEarth), 0.45f, 1.75f);
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
    profile.hasRings = def->size >= 46 && PlanetProfileHashUnit(seed, 12u) > 0.92f;
    profile.atmosphereType = ClassifyAtmosphere(profile.style, atmosphere,
                                                temperature, composition);
    return profile;
}

static PlanetProfile LegacyPlanetProfile(uint32_t seed, SolarBodyStyle style,
                                         float terrainRadius)
{
    PlanetProfile profile = { 0 };
    float bodyRadius = fmaxf(1.0f, (terrainRadius - 3.6f) / 1.22f);
    float radiusEarth = Clamp(0.72f + (bodyRadius - 40.0f) * 0.095f, 0.62f, 1.55f);
    float composition = PlanetProfileHashUnit(seed, 2u);
    float density = 0.78f + composition * 0.52f;
    profile.seed = seed;
    profile.style = style;
    profile.bodyRadius = bodyRadius;
    profile.massEarth = density * radiusEarth * radiusEarth * radiusEarth;
    profile.surfaceGravity = Clamp(profile.massEarth / (radiusEarth * radiusEarth),
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
    profile.atmosphereType = ClassifyAtmosphere(style, profile.atmosphereDensity,
                                                profile.equilibriumTempK, composition);
    return profile;
}

void SpaceAdvanceTime(float dt)
{
    if (!(dt > 0.0f) || !isfinite(dt)) return;
    solarSimulationTime += (double)dt;
    if (solarSimulationTime > 100000000.0) {
        solarSimulationTime = fmod(solarSimulationTime, 1000000.0);
    }
}

double SpaceSimulationTime(void)
{
    return solarSimulationTime;
}

bool StarSystemAt(int ax, int az, SolarSystemDef *out)
{
    if (ax == 0 && az == 0) {
        BuildSolSystem(out);
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
    out->spectrum = (SpectrumType)(h % 5u);
    out->starRadius = (out->spectrum == SPECTRUM_RED_GIANT) ? 10 + (int)(h % 6u) : 9 + (int)(h % 6u);
    out->center = (Vector3){
        (float)ax * (float)STAR_SYSTEM_SPACING,
        (float)SPACE_LAYER_Y + 40.0f + (float)(WorldHash2D(ax + 7, az + 13) % 40u),
        (float)az * (float)STAR_SYSTEM_SPACING
    };
    out->planetCount = 2 + (int)((h >> 8) % 4u);

    for (int i = 0; i < out->planetCount; i++) {
        unsigned int ph = WorldHash2D(ax * 53 + i * 7 + 1, az * 29 + i * 3 + 2);
        out->planets[i].orbit = 180 + i * 120 + (int)(ph % 5u) * 8;
        // These are visitable planets, not asteroid props. Keep enough volume
        // for a layered surface, caves and a useful landing area.
        out->planets[i].size = 40 + (int)((ph >> 6) % 9u);
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

Vector3 SolarSystemPlanetCenter(const SolarSystemDef *sys, int index)
{
    const SolarPlanetDef *def = &sys->planets[index];
    unsigned int orbitHash = SolarOrbitHash(sys, index);
    unsigned int planeHash = SolarPlaneHash(sys);
    float phase = (float)(orbitHash % 6283u) / 1000.0f;
    // Systems occupy genuinely different 3D planes (up to about 18 degrees
    // from the reference plane), while neighboring planets remain mostly
    // coplanar as expected from a shared protoplanetary disk.
    float systemInclination = ((float)((planeHash >> 6) % 25u) - 12.0f) * 0.026f;
    float planetInclination = ((float)((orbitHash >> 22) % 9u) - 4.0f) * 0.010f;
    float inclination = Clamp(systemInclination + planetInclination, -0.36f, 0.36f);
    float systemNode = (float)((planeHash >> 13) % 6283u) / 1000.0f;
    float nodeOffset = ((float)((orbitHash >> 7) % 17u) - 8.0f) * 0.005f;
    float node = systemNode + nodeOffset;
    float orbitRatio = fmaxf((float)def->orbit / 180.0f, 0.1f);
    // Circular Kepler orbit: mean motion is proportional to sqrt(M / a^3).
    float angularSpeed = SOLAR_ORBIT_BASE_SPEED * sqrtf(SolarSpectrumMass(sys->spectrum)) /
                         (orbitRatio * sqrtf(orbitRatio));
    float angle = phase + (float)(solarSimulationTime * (double)angularSpeed);
    float planeX = cosf(angle) * (float)def->orbit;
    float planeZ = sinf(angle) * (float)def->orbit * cosf(inclination);
    float planeY = sinf(angle) * (float)def->orbit * sinf(inclination);
    float nodeCos = cosf(node);
    float nodeSin = sinf(node);
    return (Vector3){
        sys->center.x + planeX * nodeCos - planeZ * nodeSin,
        sys->center.y + planeY,
        sys->center.z + planeX * nodeSin + planeZ * nodeCos
    };
}

Vector3 PlanetWorldSpaceReference(void)
{
    if (!planetWorld.active) return Vector3Zero();

    int systemAx = (int)roundf(planetWorld.bodyCenter.x / (float)STAR_SYSTEM_SPACING);
    int systemAz = (int)roundf(planetWorld.bodyCenter.z / (float)STAR_SYSTEM_SPACING);
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

    int systemAx = (int)roundf(planetWorld.bodyCenter.x / (float)STAR_SYSTEM_SPACING);
    int systemAz = (int)roundf(planetWorld.bodyCenter.z / (float)STAR_SYSTEM_SPACING);
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
    unsigned int h = WorldHash3D(bx, by, bz);
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
    int minAnchorX = FloorDivInt(startX - 900, STAR_SYSTEM_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 900, STAR_SYSTEM_SPACING);
    int minAnchorZ = FloorDivInt(startZ - 900, STAR_SYSTEM_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 900, STAR_SYSTEM_SPACING);

    for (int ax = minAnchorX; ax <= maxAnchorX; ax++) {
        for (int az = minAnchorZ; az <= maxAnchorZ; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;

            FillStarBody(chunk, startX, startZ,
                         (int)sys.center.x, (int)sys.center.y, (int)sys.center.z,
                         sys.starRadius, sys.spectrum);

            // Planets are rendered as distant proxies. Landing switches to a
            // dedicated procedural surface world instead of a finite voxel ball.
        }
    }
}

static bool SpacePointInSolarSystemBubble(int x, int z)
{
    int centerAx = FloorDivInt(x, STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt(z, STAR_SYSTEM_SPACING);
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
    int count = 0;
    int centerAx = FloorDivInt((int)floorf(pos.x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt((int)floorf(pos.z), STAR_SYSTEM_SPACING);
    int radiusAnchors = (int)(maxDist / (float)STAR_SYSTEM_SPACING) + 1;

    SolarSystemDef found[256];
    float dists[256];
    int foundCount = 0;

    for (int ax = centerAx - radiusAnchors; ax <= centerAx + radiusAnchors; ax++) {
        for (int az = centerAz - radiusAnchors; az <= centerAz + radiusAnchors; az++) {
            SolarSystemDef sys;
            if (!StarSystemAt(ax, az, &sys)) continue;
            float dx = sys.center.x - pos.x;
            float dz = sys.center.z - pos.z;
            float d = sqrtf(dx * dx + dz * dz);
            if (d > maxDist) continue;
            if (foundCount < 256) {
                found[foundCount] = sys;
                dists[foundCount] = d;
                foundCount++;
            }
        }
    }

    for (int i = 0; i < foundCount && count < maxCount; i++) {
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
        out[count++] = found[i];
    }
    return count;
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
    int centerAx = FloorDivInt((int)floorf(pos.x), STAR_SYSTEM_SPACING);
    int centerAz = FloorDivInt((int)floorf(pos.z), STAR_SYSTEM_SPACING);

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
                    .radius = (float)sys.starRadius,
                    .dist = starDist,
                    .isStar = true,
                    .index = 0,
                    .systemAnchorX = ax,
                    .systemAnchorZ = az,
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
                    .radius = profile.bodyRadius,
                    .dist = dist,
                    .isStar = false,
                    .index = i + 1,
                    .systemAnchorX = ax,
                    .systemAnchorZ = az,
                    .worldSeed = profile.seed,
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
            float radius = SolarBodyTerrainRadius(bodies[i].radius);
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
        float alignment = Vector3DotProduct(Vector3Scale(toStar, 1.0f / distance),
                                            starDirection);
        if (alignment <= bestAlignment) continue;
        bestAlignment = alignment;
        bestSystem = i;
    }

    if (bestSystem < 0) return false;
    const SolarSystemDef *system = &systems[bestSystem];
    *out = (SpaceBodyInfo){
        .center = system->center,
        .radius = (float)system->starRadius,
        .dist = Vector3Distance(starOrigin, system->center),
        .isStar = true,
        .index = 0,
        .systemAnchorX = system->anchorX,
        .systemAnchorZ = system->anchorZ,
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
        if (homeDistance <= HOME_WORLD_RADIUS + 25.0f) {
            best = homeDistance;
            *gravityDir = homeDistance > 0.001f
                              ? Vector3Scale(Vector3Subtract(homeCenter, position),
                                             1.0f / homeDistance)
                              : (Vector3){ 0.0f, -1.0f, 0.0f };
            *surfaceDist = homeDistance - HOME_WORLD_RADIUS;
            if (gravityScale) *gravityScale = 1.0f;
            found = true;
        }
    }
    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        float terrainR = SolarBodyTerrainRadius(bodies[i].radius);
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

static int PlanetRegionOrigin(uint32_t hash)
{
    int cell = (int)(hash % 768u) - 384;
    if (cell >= -64 && cell <= 64) cell += cell < 0 ? -128 : 128;
    return cell * 2048;
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

static bool FindPlanetForLanding(Vector3 position, SpaceBodyInfo *out)
{
    SpaceBodyInfo bodies[48];
    int count = SpaceBodiesNear(position, 160.0f, bodies, 48);
    float bestGap = 1e30f;
    bool found = false;

    for (int i = 0; i < count; i++) {
        if (bodies[i].isStar) continue;
        if (!bodies[i].profile.hasSolidSurface) continue;
        float radius = SolarBodyTerrainRadius(bodies[i].radius);
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

    player->position = (Vector3){ 0.0f, SPACE_ENTER_Y + 2.0f, 0.0f };
    player->floating = false;
    player->onGround = false;
    SetImportMessage("Left Homeworld atmosphere. Spaceflight is now three-dimensional.");
    return true;
}

bool HomeWorldTryEnter(Player *player)
{
    if (homeWorld.surfaceActive || planetWorld.active) return false;

    Vector3 center = HomeWorldCenter();
    float surfaceGap = fabsf(Vector3Distance(player->position, center) - HOME_WORLD_RADIUS);
    if (surfaceGap > HOME_WORLD_LANDING_MARGIN) return false;

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    homeWorld.surfaceActive = true;
    RebuildTorchList();
    ClearUndoHistory();

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

bool PlanetWorldTryEnter(Player *player)
{
    if (planetWorld.active || homeWorld.surfaceActive) return false;

    SpaceBodyInfo body;
    if (!FindPlanetForLanding(player->position, &body)) return false;

    PlanetWorldContext next = { 0 };
    next.active = true;
    next.profile = body.profile;
    next.style = body.profile.style;
    next.planetIndex = body.index;
    next.bodyCenter = body.center;
    next.bodyRadius = SolarBodyTerrainRadius(body.radius);
    next.seed = body.worldSeed;
    next.originX = PlanetRegionOrigin(next.seed ^ 0x68bc21ebu);
    next.originZ = PlanetRegionOrigin(next.seed ^ 0x02e5be93u);
    snprintf(next.name, sizeof(next.name), "%.28s %c", body.name,
             'a' + (body.index > 0 ? body.index - 1 : 0));

    Vector3 outward = Vector3Subtract(player->position, body.center);
    if (Vector3LengthSqr(outward) < 0.001f) outward = (Vector3){ 0.0f, 1.0f, 0.0f };
    else outward = Vector3Normalize(outward);
    next.returnPosition = PlanetReturnPosition(body.center, next.bodyRadius, outward);

    DrainChunkGen();
    UnloadAllChunks();
    UnloadAllSpaceChunks();
    planetWorld = next;
    RebuildTorchList();
    ClearUndoHistory();

    int shipX = 0;
    int shipZ = 0;
    int shipGround = PlanetTerrainHeight(shipX, shipZ);
    int playerX = shipX + 3;
    int playerZ = shipZ;
    int playerGround = PlanetTerrainHeight(playerX, playerZ);
    player->position = (Vector3){ (float)playerX + 0.5f, (float)playerGround + 2.0f,
                                  (float)playerZ + 0.5f };
    player->velocity = Vector3Zero();
    player->floating = false;
    player->onGround = false;

    UpdateChunks(player->position, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    SetBlock(shipX, shipGround + 1, shipZ, BLOCK_SPACESHIP);
    SetImportMessage(TextFormat("Landed on %s - this planet has a continuous surface.",
                                planetWorld.name));
    return true;
}

bool PlanetWorldTryLaunch(Player *player)
{
    if (!planetWorld.active || player->position.y < (float)WORLD_HEIGHT + 12.0f) return false;

    Vector3 returnPosition = planetWorld.returnPosition;
    Vector3 outward = Vector3Subtract(planetWorld.returnPosition, planetWorld.bodyCenter);
    if (Vector3LengthSqr(outward) < 0.001f) outward = (Vector3){ 1.0f, 0.0f, 0.0f };
    else outward = Vector3Normalize(outward);

    int systemAx = (int)roundf(planetWorld.bodyCenter.x / (float)STAR_SYSTEM_SPACING);
    int systemAz = (int)roundf(planetWorld.bodyCenter.z / (float)STAR_SYSTEM_SPACING);
    SolarSystemDef system;
    int orbitIndex = planetWorld.planetIndex - 1;
    if (StarSystemAt(systemAx, systemAz, &system) &&
        orbitIndex >= 0 && orbitIndex < system.planetCount) {
        Vector3 currentCenter = SolarSystemPlanetCenter(&system, orbitIndex);
        returnPosition = PlanetReturnPosition(currentCenter, planetWorld.bodyRadius, outward);
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
    player->velocity = Vector3Zero();
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
           fwrite(&planetWorld.bodyRadius, sizeof(planetWorld.bodyRadius), 1, file) == 1 &&
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
        fread(&loaded.bodyRadius, sizeof(loaded.bodyRadius), 1, file) != 1 ||
        fread(loaded.name, sizeof(loaded.name), 1, file) != 1) {
        return false;
    }

    if (active > 1u || style > (uint32_t)SOLAR_STYLE_TEMPERATE ||
        planetIndex < 0 || !isfinite(loaded.bodyRadius) || loaded.bodyRadius < 0.0f ||
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
    loaded.profile = LegacyPlanetProfile(loaded.seed, loaded.style, loaded.bodyRadius);
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
        int editCx = 0;
        int editCz = 0;
        int editLx = 0;
        int editLz = 0;
        WorldToChunkLocal(edit->x, edit->z, &editCx, &editCz, &editLx, &editLz);
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
    int minAnchorX = FloorDivInt(startX - 8, ASTEROID_SPACING);
    int maxAnchorX = FloorDivInt(startX + CHUNK_SIZE + 8, ASTEROID_SPACING);
    int minAnchorZ = FloorDivInt(startZ - 8, ASTEROID_SPACING);
    int maxAnchorZ = FloorDivInt(startZ + CHUNK_SIZE + 8, ASTEROID_SPACING);

    for (int anchorX = minAnchorX; anchorX <= maxAnchorX; anchorX++) {
        for (int anchorZ = minAnchorZ; anchorZ <= maxAnchorZ; anchorZ++) {
            if (WorldHash2D(anchorX, anchorZ) % 100u >= ASTEROID_PROBABILITY) continue;

            int wx = anchorX * ASTEROID_SPACING;
            int wz = anchorZ * ASTEROID_SPACING;
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
                        if (WorldHash3D(bx, by, bz) % 89u == 0u) type = BLOCK_METEORITE;
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
    for (int i = 0; i < spaceEditCount; i++) {
        if (spaceEdits[i].x == x && spaceEdits[i].y == y && spaceEdits[i].z == z) {
            spaceEdits[i].type = type;
            return;
        }
    }
    if (spaceEditCount < MAX_SPACE_EDITS) {
        spaceEdits[spaceEditCount++] = (BlockEdit){ x, y, z, type };
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
    PlanetWorldReset();
    HomeWorldReset();
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
            TorchLightAdd(spaceEdits[i].x, spaceEdits[i].y, spaceEdits[i].z);
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
