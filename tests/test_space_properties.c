#include "chunks.h"
#include "ecology.h"
#include "space.h"
#include "space_barycenter.h"
#include "space_physics.h"
#include "space_units.h"
#include "terrain.h"
#include "weather.h"
#include "weather_model.h"

#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846

/* The production terrain hash reads the world seed through this small API. */
static uint32_t propertyWorldSeed = DEFAULT_WORLD_SEED;
TerrainMode terrainMode = TERRAIN_VARIED;

uint32_t WorldGetSeed(void)
{
    return propertyWorldSeed;
}

int WorldSurfaceHeightAt(int x, int z)
{
    return PlanetTerrainHeight(x, z);
}

int WorldGetEditCount(void)
{
    return 0;
}

bool WorldGetEditForCurrentDimension(int index, BlockEdit *outEdit)
{
    (void)index;
    (void)outEdit;
    return false;
}

float TorchLightAtBlockNearby(int x, int y, int z,
                              const int *indices, int count)
{
    (void)x;
    (void)y;
    (void)z;
    (void)indices;
    (void)count;
    return 0.0f;
}

bool IsColorBlock(BlockType type)
{
    return type >= BLOCK_COLOR_START && type <= BLOCK_COLOR_END;
}

int ColorBlockIndex(BlockType type)
{
    return IsColorBlock(type) ? (int)type - BLOCK_COLOR_START : -1;
}

Color ColorPalette256(int index)
{
    unsigned char value = (unsigned char)(index & 0xff);
    return (Color){ value, value, value, 255 };
}

bool IsTranslucentBlock(BlockType type)
{
    return type == BLOCK_AIR || type == BLOCK_GLASS ||
           type == BLOCK_WATER || type == BLOCK_ICE ||
           type == BLOCK_FLOWER || type == BLOCK_MUSHROOM ||
           type == BLOCK_GLASS_PANE || type == BLOCK_NETHER_PORTAL;
}

BlockType GetBlockAt(int x, int y, int z)
{
    return GetBlock(x, y, z);
}

void PlanetPoiApplyToChunk(Chunk *chunk, int cx, int cz)
{
    (void)chunk;
    (void)cx;
    (void)cz;
}

void UnloadModel(Model model)
{
    (void)model;
}

static void SetPropertySeed(uint32_t seed)
{
    propertyWorldSeed = seed == 0 ? DEFAULT_WORLD_SEED : seed;
}

static double VectorLength(Vector3 value)
{
    return sqrt((double)value.x * value.x + (double)value.y * value.y +
                (double)value.z * value.z);
}

static Vector3 VectorSubtractTest(Vector3 left, Vector3 right)
{
    return (Vector3){ left.x - right.x, left.y - right.y,
                      left.z - right.z };
}

static Vector3 VectorScaleTest(Vector3 value, double scale)
{
    return (Vector3){ (float)(value.x * scale), (float)(value.y * scale),
                       (float)(value.z * scale) };
}

static void AssertRelative(double actual, double expected, double tolerance)
{
    assert(isfinite(actual));
    assert(isfinite(expected));
    double scale = fmax(fabs(expected), 1.0);
    assert(fabs(actual - expected) <= tolerance * scale);
}

static void AssertStefanBoltzmann(const StellarProfile *star)
{
    double radiusRatio = star->radiusKm / SPACE_UNITS_SOLAR_RADIUS_KM;
    double temperatureRatio = (double)star->temperatureK / 5772.0;
    double expected = radiusRatio * radiusRatio *
                      temperatureRatio * temperatureRatio *
                      temperatureRatio * temperatureRatio;
    AssertRelative(star->luminositySolar, expected, 0.00005);
    AssertRelative(star->radiusSolar, radiusRatio, 0.00001);
    AssertRelative(star->massSolar,
                   star->massKg / SPACE_UNITS_SOLAR_MASS_KG, 0.00001);
}

static void AssertPlanetClimate(const PlanetProfile *profile)
{
    assert(isfinite(profile->receivedIrradiance));
    assert(profile->receivedIrradiance > 0.0);
    assert(isfinite(profile->surfacePressureAtm));
    assert(profile->surfacePressureAtm >= 0.0f);
    assert(profile->atmosphereDensity >= 0.0f &&
           profile->atmosphereDensity <= 1.0f);
    assert(profile->albedo >= 0.03f && profile->albedo <= 0.88f);
    assert(profile->greenhouseEffect >= 0.0f &&
           profile->greenhouseEffect <= 3.0f);
    assert(profile->oceanCoverage >= 0.0f && profile->oceanCoverage <= 1.0f);
    assert(profile->iceCoverage >= 0.0f && profile->iceCoverage <= 1.0f);
    assert(profile->cloudCoverage >= 0.0f && profile->cloudCoverage <= 1.0f);
    assert(profile->windStrength >= 0.0f && profile->windStrength <= 1.0f);

    double expectedDensity = profile->surfacePressureAtm /
                             (profile->surfacePressureAtm + 0.30);
    AssertRelative(profile->atmosphereDensity, expectedDensity, 0.000001);
    double absorbed = profile->receivedIrradiance * (1.0 - profile->albedo);
    double expectedRadiative = 278.5 * pow(absorbed, 0.25);
    double expectedSurface = expectedRadiative *
        pow(1.0 + 0.75 * profile->greenhouseEffect, 0.25);
    AssertRelative(profile->radiativeTempK, expectedRadiative, 0.00001);
    AssertRelative(profile->equilibriumTempK, expectedSurface, 0.00001);
    assert(profile->equilibriumTempK >= profile->radiativeTempK);
    if (profile->style == SOLAR_STYLE_GAS) {
        assert(profile->oceanCoverage == 0.0f);
        assert(profile->iceCoverage == 0.0f);
    }
}

static void AssertBarycenter(const SolarSystemDef *system,
                             const SolarStellarBody *bodies, int count)
{
    double totalMass = 0.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double positionZ = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    for (int i = 0; i < count; i++) {
        Vector3 offset = VectorSubtractTest(bodies[i].center, system->center);
        totalMass += bodies[i].stellar.massKg;
        positionX += (double)offset.x * bodies[i].stellar.massKg;
        positionY += (double)offset.y * bodies[i].stellar.massKg;
        positionZ += (double)offset.z * bodies[i].stellar.massKg;
        velocityX += (double)bodies[i].velocity.x * bodies[i].stellar.massKg;
        velocityY += (double)bodies[i].velocity.y * bodies[i].stellar.massKg;
        velocityZ += (double)bodies[i].velocity.z * bodies[i].stellar.massKg;
    }
    assert(totalMass > 0.0);
    assert(sqrt((positionX * positionX + positionY * positionY +
                 positionZ * positionZ) / (totalMass * totalMass)) < 0.01);
    assert(sqrt((velocityX * velocityX + velocityY * velocityY +
                 velocityZ * velocityZ) / (totalMass * totalMass)) < 0.00001);

    if (count < 2) return;

    double innerPositionX = 0.0;
    double innerPositionY = 0.0;
    double innerPositionZ = 0.0;
    double innerVelocityX = 0.0;
    double innerVelocityY = 0.0;
    double innerVelocityZ = 0.0;
    double innerMass = bodies[0].stellar.massKg + bodies[1].stellar.massKg;
    for (int i = 0; i < 2; i++) {
        Vector3 offset = VectorSubtractTest(bodies[i].center, system->center);
        innerPositionX += (double)offset.x * bodies[i].stellar.massKg;
        innerPositionY += (double)offset.y * bodies[i].stellar.massKg;
        innerPositionZ += (double)offset.z * bodies[i].stellar.massKg;
        innerVelocityX += (double)bodies[i].velocity.x * bodies[i].stellar.massKg;
        innerVelocityY += (double)bodies[i].velocity.y * bodies[i].stellar.massKg;
        innerVelocityZ += (double)bodies[i].velocity.z * bodies[i].stellar.massKg;
    }
    Vector3 innerPosition = {
        (float)(innerPositionX / innerMass),
        (float)(innerPositionY / innerMass),
        (float)(innerPositionZ / innerMass)
    };
    Vector3 innerVelocity = {
        (float)(innerVelocityX / innerMass),
        (float)(innerVelocityY / innerMass),
        (float)(innerVelocityZ / innerMass)
    };

    Vector3 relativePosition = VectorSubtractTest(bodies[1].center,
                                                  bodies[0].center);
    Vector3 relativeVelocity = VectorSubtractTest(bodies[1].velocity,
                                                  bodies[0].velocity);
    double separationGame = VectorLength(relativePosition);
    double speedGame = VectorLength(relativeVelocity);
    double separationKm = SpaceUnitsGameDistanceToKilometers(separationGame);
    double speedKmPerSecond = SpaceUnitsGameVelocityToKilometersPerSecond(
        speedGame);
    double expectedSpeed = SpaceUnitsCircularOrbitVelocityKilometersPerSecond(
        separationKm, innerMass);
    AssertRelative(speedKmPerSecond, expectedSpeed, 0.0002);
    double expectedPeriod = 2.0 * TEST_PI * sqrt(
        separationKm * separationKm * separationKm /
        (SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * innerMass));
    AssertRelative(2.0 * TEST_PI * separationKm / speedKmPerSecond,
                   expectedPeriod, 0.0002);

    if (count != 3) return;
    Vector3 outerPosition = VectorSubtractTest(bodies[2].center, system->center);
    Vector3 outerVelocity = bodies[2].velocity;
    outerPosition = VectorSubtractTest(outerPosition, innerPosition);
    outerVelocity = VectorSubtractTest(outerVelocity, innerVelocity);
    separationGame = VectorLength(outerPosition);
    speedGame = VectorLength(outerVelocity);
    separationKm = SpaceUnitsGameDistanceToKilometers(separationGame);
    speedKmPerSecond = SpaceUnitsGameVelocityToKilometersPerSecond(speedGame);
    expectedSpeed = SpaceUnitsCircularOrbitVelocityKilometersPerSecond(
        separationKm, totalMass);
    AssertRelative(speedKmPerSecond, expectedSpeed, 0.0003);
}

static void AssertPlanetOrbit(const SolarSystemDef *system, int index,
                              const PlanetProfile *profile,
                              const SolarStellarBody *bodies, int bodyCount)
{
    AssertPlanetClimate(profile);
    const SolarPlanetDef *planet = &system->planets[index];
    double scaleTime = SpaceSimulationTime();
    double scaleDt = 0.001;
    Vector3 scaleBefore = SolarSystemPlanetPositionAtTime(
        system, index, scaleTime - scaleDt);
    Vector3 scaleAfter = SolarSystemPlanetPositionAtTime(
        system, index, scaleTime + scaleDt);
    SpaceBodyInfo scaleBody = {
        .center = SolarSystemPlanetCenter(system, index),
        .velocity = VectorScaleTest(VectorSubtractTest(scaleAfter, scaleBefore),
                                    1.0 / (2.0 * scaleDt)),
        .physicalRadiusKm = profile->physicalRadiusKm,
        .semiMajorAxisKm = planet->semiMajorAxisKm,
        .parentMassKg = SolarSystemStellarMassKg(system),
        .spaceProxyRadius = profile->spaceProxyRadius,
        .index = index + 1,
        .profile = *profile
    };
    snprintf(scaleBody.name, sizeof(scaleBody.name), "%s", system->name);
    SpaceScaleDiagnostics scale;
    assert(SpaceBodyScaleDiagnostics(&scaleBody, &scale));
    assert(scale.withinErrorBudget);
    assert(scale.maxRelativeError <= SPACE_UNITS_MAX_RELATIVE_ERROR);
    assert(scale.physicalRadiusKm == profile->physicalRadiusKm);
    assert(scale.physicalRadiusGame > 0.0);
    assert(scale.landingRadiusGame > scale.physicalRadiusGame);
    assert(scale.landingRadiusScale > 100.0);
    assert(scale.physicalGravityMetersPerSecondSquared > 0.0);
    assert(scale.gameplaySurfaceGravity > 0.0);
    assert(scale.orbitalSpeedKilometersPerSecond > 0.0);
    assert(scale.sphereOfInfluenceKm > 0.0);
    assert(scale.hillSphereKm > scale.sphereOfInfluenceKm);
    assert(scale.encounterRadiusGame >= scale.landingRadiusGame * 2.19f);
    assert(scale.climateIrradianceEarth == profile->receivedIrradiance);
    assert(scale.currentIrradianceEarth > 0.0);
    assert(scale.surfaceTemperatureK == profile->equilibriumTempK);
    double orbitAU = fmax(planet->semiMajorAxisKm /
                          SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.18);
    SolarLightSource lights[MAX_SOLAR_LIGHTS];
    int lightCount = SolarSystemLightSources(system, lights, MAX_SOLAR_LIGHTS);
    assert(lightCount > 0);
    double expectedIrradiance = 0.0;
    for (int light = 0; light < lightCount; light++) {
        expectedIrradiance += (double)lights[light].luminosity /
                              (orbitAU * orbitAU);
    }
    assert(expectedIrradiance > 0.0001);
    AssertRelative(profile->receivedIrradiance, expectedIrradiance, 0.000001);
    double centralMass = SolarSystemStellarMassKg(system);
    double semiMajorAxisKm = planet->semiMajorAxisKm;
    double periodSeconds = SolarSystemPlanetOrbitPeriodSeconds(system, index);
    double expectedPeriod = 2.0 * TEST_PI * sqrt(
        semiMajorAxisKm * semiMajorAxisKm * semiMajorAxisKm /
        (SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * centralMass));
    AssertRelative(periodSeconds, expectedPeriod, 0.0000001);
    double periodGame = SpaceUnitsSecondsToGameTime(periodSeconds);
    assert(periodGame > 0.0);

    double minimumStarClearanceKm = HUGE_VAL;
    double minimumRadiusKm = HUGE_VAL;
    double maximumRadiusKm = 0.0;
    for (int sample = 0; sample < 12; sample++) {
        double time = periodGame * (double)sample / 12.0;
        Vector3 position = SolarSystemPlanetPositionAtTime(system, index, time);
        Vector3 relative = VectorSubtractTest(position, system->center);
        double radiusGame = VectorLength(relative);
        double radiusKm = SpaceUnitsGameDistanceToKilometers(radiusGame);
        SpacePhysicsGravityBody gravityBody = {
            .center = system->center,
            .softeningRadiusGame = 0.0f,
            .gravitationalParameterGame =
                (float)SpaceUnitsGravitationalParameterGame(centralMass),
            .encounterRadiusGame = 0.0f,
            .hierarchy = 0
        };
        Vector3 acceleration = SpacePhysicsGravityAcceleration(position,
                                                                &gravityBody);
        double expectedAcceleration = gravityBody.gravitationalParameterGame /
                                      (radiusGame * radiusGame);
        AssertRelative(VectorLength(acceleration), expectedAcceleration, 0.00001);
        assert((double)acceleration.x * relative.x +
               (double)acceleration.y * relative.y +
               (double)acceleration.z * relative.z < 0.0);
        minimumRadiusKm = fmin(minimumRadiusKm, radiusKm);
        maximumRadiusKm = fmax(maximumRadiusKm, radiusKm);
        assert(radiusKm > profile->physicalRadiusKm);
        for (int star = 0; star < bodyCount; star++) {
            double clearance = SpaceUnitsGameDistanceToKilometers(
                VectorLength(VectorSubtractTest(position, bodies[star].center)));
            clearance -= profile->physicalRadiusKm + bodies[star].stellar.radiusKm;
            minimumStarClearanceKm = fmin(minimumStarClearanceKm, clearance);
        }

        double dt = periodGame * 0.0005;
        Vector3 before = SolarSystemPlanetPositionAtTime(system, index, time - dt);
        Vector3 after = SolarSystemPlanetPositionAtTime(system, index, time + dt);
        Vector3 delta = VectorScaleTest(VectorSubtractTest(after, before),
                                        1.0 / (2.0 * dt));
        double speedKmPerSecond = SpaceUnitsGameVelocityToKilometersPerSecond(
            VectorLength(delta));
        double expectedSpeed = sqrt(
            SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * centralMass *
            (2.0 / radiusKm - 1.0 / semiMajorAxisKm));
        AssertRelative(speedKmPerSecond, expectedSpeed, 0.002);
    }
    assert(minimumStarClearanceKm > 0.0);
    assert(minimumRadiusKm < maximumRadiusKm);

    Vector3 initial = SolarSystemPlanetPositionAtTime(system, index, 3.25);
    Vector3 complete = SolarSystemPlanetPositionAtTime(system, index,
                                                       3.25 + periodGame);
    assert(VectorLength(VectorSubtractTest(initial, complete)) < 0.02);

    double earthGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM);
    double expectedGravity = SpaceUnitsSurfaceGravityKmPerSecondSquared(
        profile->massKg, profile->physicalRadiusKm) / earthGravity;
    double gravityMinimum = profile->style == SOLAR_STYLE_GAS ? 0.75 : 0.45;
    double gravityMaximum = profile->style == SOLAR_STYLE_GAS ? 2.40 : 1.75;
    expectedGravity = fmin(fmax(expectedGravity, gravityMinimum), gravityMaximum);
    AssertRelative(profile->surfaceGravity, expectedGravity, 0.00001);

    SpaceSatelliteOrbit satellite;
    if (SolarPlanetSatelliteOrbit(system, index, profile, &satellite)) {
        assert(satellite.exists);
        double hillRadiusKm = semiMajorAxisKm * cbrt(
            profile->massKg / (3.0 * centralMass));
        double periapsis = satellite.semiMajorAxisKm *
                           (1.0 - satellite.eccentricity);
        double apoapsis = satellite.semiMajorAxisKm *
                          (1.0 + satellite.eccentricity);
        assert(periapsis > 5.0 * profile->physicalRadiusKm);
        assert(periapsis > profile->physicalRadiusKm + satellite.radiusKm);
        assert(apoapsis < 0.5 * hillRadiusKm);
        assert(apoapsis < hillRadiusKm);
        assert(satellite.eccentricity >= 0.0 && satellite.eccentricity < 0.1);
        double moonPeriod = SpaceSatelliteOrbitalPeriodSeconds(
            &satellite, profile->massKg);
        double expectedMoonPeriod = 2.0 * TEST_PI * sqrt(
            satellite.semiMajorAxisKm * satellite.semiMajorAxisKm *
            satellite.semiMajorAxisKm /
            (SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 *
             (profile->massKg + satellite.massKg)));
        AssertRelative(moonPeriod, expectedMoonPeriod, 0.0000001);
    }
}

static void TestGeneratedSystems(void)
{
    static const uint32_t seeds[] = {
        DEFAULT_WORLD_SEED, 1u, 0x12345678u, 0xdeadbeefu,
        0x31415926u, 0x9e3779b9u, 0xa5a5a5a5u
    };
    int systemCount = 0;
    int multiplicities[4] = { 0 };
    int climates[SOLAR_STYLE_TEMPERATE + 1] = { 0 };
    int satelliteCount = 0;
    for (size_t seedIndex = 0; seedIndex < sizeof(seeds) / sizeof(seeds[0]);
         seedIndex++) {
        SetPropertySeed(seeds[seedIndex]);
        for (int anchorX = -10; anchorX <= 10; anchorX++) {
            for (int anchorZ = -10; anchorZ <= 10; anchorZ++) {
                SolarSystemDef system;
                if (!StarSystemAt(anchorX, anchorZ, &system)) continue;
                systemCount++;
                assert(system.exists);
                assert(system.planetCount >= 1 && system.planetCount <= 6);
                SolarStellarBody bodies[SPACE_BARYCENTER_MAX_BODIES];
                int bodyCount = SolarSystemStellarBodiesAtTime(
                    &system, 0.0, bodies, SPACE_BARYCENTER_MAX_BODIES);
                assert(bodyCount >= 1 && bodyCount <= 3);
                multiplicities[bodyCount]++;
                for (int star = 0; star < bodyCount; star++) {
                    AssertStefanBoltzmann(&bodies[star].stellar);
                }
                for (int planet = 0; planet < system.planetCount; planet++) {
                    PlanetProfile profile = SolarPlanetProfile(&system, planet);
                    assert(profile.massKg > 0.0 && profile.physicalRadiusKm > 0.0);
                    assert(profile.style >= SOLAR_STYLE_LAVA &&
                           profile.style <= SOLAR_STYLE_TEMPERATE);
                    climates[profile.style]++;
                    SpaceSatelliteOrbit satellite;
                    if (SolarPlanetSatelliteOrbit(&system, planet, &profile,
                                                   &satellite)) {
                        satelliteCount++;
                    }
                    AssertPlanetOrbit(&system, planet, &profile, bodies, bodyCount);
                }
                for (double time = 0.0; time < 180.0; time += 37.5) {
                    SolarStellarBody states[SPACE_BARYCENTER_MAX_BODIES];
                    assert(SolarSystemStellarBodiesAtTime(
                        &system, time, states,
                        SPACE_BARYCENTER_MAX_BODIES) == bodyCount);
                    AssertBarycenter(&system, states, bodyCount);
                }
            }
        }
    }
    assert(systemCount > 1000);
    assert(multiplicities[1] > 0);
    assert(multiplicities[2] > 0);
    assert(multiplicities[3] > 0);
    assert(satelliteCount > 100);
    for (int style = SOLAR_STYLE_LAVA; style <= SOLAR_STYLE_TEMPERATE; style++) {
        assert(climates[style] > 0);
    }
    printf("space properties: %d systems, %d satellites, multiplicity %d/%d/%d, "
           "climates %d/%d/%d/%d/%d/%d\n",
           systemCount, satelliteCount, multiplicities[1], multiplicities[2],
           multiplicities[3], climates[SOLAR_STYLE_LAVA], climates[SOLAR_STYLE_ICE],
           climates[SOLAR_STYLE_DESERT], climates[SOLAR_STYLE_GAS],
           climates[SOLAR_STYLE_CRATER], climates[SOLAR_STYLE_TEMPERATE]);
}

static void TestHomeScaleDiagnostics(void)
{
    SpaceScaleDiagnostics scale;
    assert(SpaceScaleDiagnosticsAt((Vector3){ 0 }, &scale));
    assert(scale.withinErrorBudget);
    assert(scale.physicalRadiusKm == SPACE_UNITS_EARTH_RADIUS_KM);
    assert(scale.visualRadiusGame == 62.0f);
    assert(scale.landingRadiusGame == 62.0f);
    assert(scale.landingRadiusScale > 4200.0);
    AssertRelative(scale.physicalGravityEarth, 1.0, 0.000001);
    AssertRelative(scale.gameplaySurfaceGravity,
                   SPACE_UNITS_EARTH_PROXY_SURFACE_ACCELERATION_GAME,
                   0.000001);
    AssertRelative(scale.orbitalSpeedKilometersPerSecond, 29.7851, 0.0001);
    assert(scale.sphereOfInfluenceKm > 900000.0);
    assert(scale.hillSphereKm > scale.sphereOfInfluenceKm);
    assert(scale.encounterRadiusClamped);
    assert(scale.currentIrradianceEarth == 1.0);
    assert(scale.climateIrradianceEarth == 1.0);
    assert(scale.radiativeTemperatureK == 255.0f);
    assert(scale.surfaceTemperatureK == 288.0f);
}

static void WritePlanetWorldFixture(FILE *file, uint32_t seed,
                                    int originX, int originZ,
                                    SolarBodyStyle planetStyle)
{
    uint8_t active = 1u;
    uint32_t style = (uint32_t)planetStyle;
    int32_t savedOriginX = (int32_t)originX;
    int32_t savedOriginZ = (int32_t)originZ;
    int32_t planetIndex = 1;
    float bodyCenter[3] = { 420.0f, -18.0f, 75.0f };
    float returnPosition[3] = { 486.0f, -18.0f, 75.0f };
    float proxyRadius = 62.0f;
    char name[32] = "Ecology Replay";

    assert(fwrite(&active, sizeof(active), 1, file) == 1);
    assert(fwrite(&seed, sizeof(seed), 1, file) == 1);
    assert(fwrite(&style, sizeof(style), 1, file) == 1);
    assert(fwrite(&savedOriginX, sizeof(savedOriginX), 1, file) == 1);
    assert(fwrite(&savedOriginZ, sizeof(savedOriginZ), 1, file) == 1);
    assert(fwrite(&planetIndex, sizeof(planetIndex), 1, file) == 1);
    assert(fwrite(bodyCenter, sizeof(bodyCenter), 1, file) == 1);
    assert(fwrite(returnPosition, sizeof(returnPosition), 1, file) == 1);
    assert(fwrite(&proxyRadius, sizeof(proxyRadius), 1, file) == 1);
    assert(fwrite(name, sizeof(name), 1, file) == 1);
}

static void ActivateEcologyPlanetStyle(uint32_t seed, int originX, int originZ,
                                       SolarBodyStyle style)
{
    FILE *file = tmpfile();
    assert(file);
    WritePlanetWorldFixture(file, seed, originX, originZ, style);
    rewind(file);
    assert(PlanetWorldLoadState(file));
    fclose(file);
    assert(PlanetWorldIsActive());
    assert(PlanetWorldSeed() == seed);
    assert(PlanetWorldOriginX() == originX);
    assert(PlanetWorldOriginZ() == originZ);
}

static void ActivateEcologyPlanet(uint32_t seed, int originX, int originZ)
{
    ActivateEcologyPlanetStyle(seed, originX, originZ,
                               SOLAR_STYLE_TEMPERATE);
}

static void AssertLocalEcologyEqual(PlanetLocalEcology actual,
                                    PlanetLocalEcology expected)
{
    assert(memcmp(&actual.environment, &expected.environment,
                  sizeof(actual.environment)) == 0);
    assert(memcmp(&actual.suitability, &expected.suitability,
                  sizeof(actual.suitability)) == 0);
    assert(memcmp(&actual.population, &expected.population,
                  sizeof(actual.population)) == 0);

#define ASSERT_POPULATION_UNIT(field)                                      \
    assert(actual.population.field >= 0.0f &&                              \
           actual.population.field <= 1.0f)
    ASSERT_POPULATION_UNIT(floraDensity);
    ASSERT_POPULATION_UNIT(faunaDensity);
    ASSERT_POPULATION_UNIT(floraCarryingCapacity);
    ASSERT_POPULATION_UNIT(faunaCarryingCapacity);
    ASSERT_POPULATION_UNIT(seasonalMemory);
#undef ASSERT_POPULATION_UNIT
}

static bool LocalEcologyDiffers(PlanetLocalEcology left,
                                PlanetLocalEcology right)
{
    return memcmp(&left.environment, &right.environment,
                  sizeof(left.environment)) != 0 ||
           memcmp(&left.suitability, &right.suitability,
                  sizeof(left.suitability)) != 0 ||
           memcmp(&left.population, &right.population,
                  sizeof(left.population)) != 0;
}

static float WeatherSampleDistance(WeatherFieldSample left,
                                   WeatherFieldSample right)
{
    return fabsf(left.cloudCover - right.cloudCover) +
           fabsf(left.precipitation - right.precipitation) +
           fabsf(left.rain - right.rain) +
           fabsf(left.snow - right.snow) +
           fabsf(left.storm - right.storm) +
           fabsf(left.wind - right.wind);
}

static void TestEcologyUsesPositionLocalWeather(void)
{
    const uint32_t seed = 0x6c8e9cf5u;
    SetPropertySeed(seed);
    ActivateEcologyPlanet(seed, 317, -911);
    SpaceAdvanceTime(87.25f);

    int wetX = 0;
    int wetZ = 0;
    WeatherFieldSample wetWeather = { 0 };
    bool foundWetCell = false;
    for (int index = 0; index < 512; index++) {
        int x = index * 37 - 4096;
        int z = ((index * index * 53) % 8192) - 4096;
        WeatherFieldSample sample = WeatherFieldSampleAtWorld(x, z);
        if (sample.precipitation > 0.12f) {
            wetX = x;
            wetZ = z;
            wetWeather = sample;
            foundWetCell = true;
            break;
        }
    }
    assert(foundWetCell);
    assert(WeatherPrecipitationRate() == 0.0f);

    PlanetLocalEcology local = PlanetEcologyLocalAt(wetX, wetZ, 0.84f);
    assert(local.environment.precipitationRate == wetWeather.precipitation);
    assert(local.environment.currentStorm == wetWeather.storm);

    float sky = WeatherFieldSkyFactor(wetWeather);
    float usableDaylight = fmaxf(0.0f, fminf(1.0f,
        0.84f * (1.0f - sky * 0.68f)));
    float expectedLight = fmaxf(0.0f, fminf(1.0f,
        usableDaylight * (float)PlanetWorldProfile()->receivedIrradiance));
    assert(fabsf(local.environment.currentUsableLight - expectedLight) < 0.00001f);

    WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(wetX, wetZ);
    PlanetLocalEcology replay = PlanetEcologyLocalAt(wetX, wetZ, 0.84f);
    assert(memcmp(&wetWeather, &replayWeather, sizeof(wetWeather)) == 0);
    AssertLocalEcologyEqual(replay, local);
}

static void TestEcologyCacheInvalidation(void)
{
    const uint32_t seed = 0x13579bdfu;
    const int sampleCount = 24;
    PlanetLocalEcology firstOrigin[sampleCount];
    PlanetLocalEcology movedOrigin[sampleCount];
    WeatherFieldSample firstWeather[sampleCount];
    SetPropertySeed(seed);
    ActivateEcologyPlanet(seed, 120, -340);

    PlanetEcologyProfile temperate = PlanetEcologyCurrent();
    PlanetEcologyProfile repeated = PlanetEcologyCurrent();
    assert(memcmp(&temperate, &repeated, sizeof(temperate)) == 0);
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        firstWeather[index] = WeatherFieldSampleAtWorld(x, z);
        firstOrigin[index] = PlanetEcologyLocalAt(x, z, 0.74f);
        AssertLocalEcologyEqual(
            PlanetEcologyLocalAt(x, z, 0.74f), firstOrigin[index]);
    }

    ActivateEcologyPlanetStyle(seed, 120, -340, SOLAR_STYLE_ICE);
    PlanetEcologyProfile ice = PlanetEcologyCurrent();
    assert(PlanetWorldProfile()->style == SOLAR_STYLE_ICE);
    assert(memcmp(&temperate, &ice, sizeof(temperate)) != 0);

    ActivateEcologyPlanet(seed, 4100, -3700);
    int originChanges = 0;
    int weatherChanges = 0;
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        WeatherFieldSample movedWeather = WeatherFieldSampleAtWorld(x, z);
        PlanetLocalEcology moved = PlanetEcologyLocalAt(x, z, 0.74f);
        movedOrigin[index] = moved;
        if (LocalEcologyDiffers(moved, firstOrigin[index])) {
            originChanges++;
        }
        if (WeatherSampleDistance(movedWeather, firstWeather[index]) > 0.0001f) {
            weatherChanges++;
        }
    }
    assert(originChanges > sampleCount / 2);
    assert(weatherChanges > sampleCount / 2);

    SpaceAdvanceTime(97.0f);
    int timeChanges = 0;
    for (int index = 0; index < sampleCount; index++) {
        int x = index * 83 - 900;
        int z = index * index * 19 - 700;
        PlanetLocalEcology advanced = PlanetEcologyLocalAt(x, z, 0.74f);
        PlanetLocalEcology replay = PlanetEcologyLocalAt(x, z, 0.74f);
        AssertLocalEcologyEqual(replay, advanced);
        if (LocalEcologyDiffers(advanced, movedOrigin[index])) {
            timeChanges++;
        }
    }
    assert(timeChanges > sampleCount / 2);
}

static void TestEcologyCrossSeedReplay(void)
{
    WeatherFieldSample previousWeather = { 0 };
    int distinctWeatherCount = 0;
    for (int index = 0; index < 64; index++) {
        uint32_t seed = 0x9e3779b9u * (uint32_t)(index + 1) ^ 0x61c88647u;
        int originX = index * 113 - 3500;
        int originZ = 2800 - index * 89;
        int sampleX = ((index * 997) % 7000) - 3500;
        int sampleZ = ((index * index * 131) % 7000) - 3500;
        SetPropertySeed(seed);
        ActivateEcologyPlanet(seed, originX, originZ);

        WeatherFieldSample firstWeather = WeatherFieldSampleAtWorld(
            sampleX, sampleZ);
        PlanetLocalEcology firstEcology = PlanetEcologyLocalAt(
            sampleX, sampleZ, 0.66f);
        ActivateEcologyPlanet(seed, originX, originZ);
        WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(
            sampleX, sampleZ);
        PlanetLocalEcology replayEcology = PlanetEcologyLocalAt(
            sampleX, sampleZ, 0.66f);

        assert(memcmp(&firstWeather, &replayWeather,
                      sizeof(firstWeather)) == 0);
        AssertLocalEcologyEqual(replayEcology, firstEcology);
        assert(firstEcology.environment.precipitationRate ==
               firstWeather.precipitation);
        assert(firstEcology.environment.currentStorm == firstWeather.storm);
        if (index > 0 &&
            WeatherSampleDistance(previousWeather, firstWeather) > 0.001f) {
            distinctWeatherCount++;
        }
        previousWeather = firstWeather;
    }
    assert(distinctWeatherCount > 48);
}

static void TestEcologySaveLoadReplay(void)
{
    const uint32_t seed = 0x2468ace0u;
    const int sampleX = 725;
    const int sampleZ = -1384;
    SetPropertySeed(seed);
    PlanetEcologyResetState();
    ActivateEcologyPlanet(seed, -2048, 1024);
    SpaceAdvanceTime(163.5f);

    WeatherFieldSample beforeWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float beforeWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology beforeEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);

    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(&seed, sizeof(seed), 1, file) == 1);
    assert(SpaceSaveState(file));
    assert(PlanetWorldSaveState(file));
    assert(PlanetEcologySaveState(file));

    SetPropertySeed(0xdeadbeefu);
    ActivateEcologyPlanet(0xdeadbeefu, 99, -77);
    SpaceAdvanceTime(41.0f);
    rewind(file);
    uint32_t loadedSeed = 0;
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    SetPropertySeed(loadedSeed);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));

    WeatherFieldSample afterWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float afterWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology afterEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);
    assert(memcmp(&beforeWeather, &afterWeather, sizeof(beforeWeather)) == 0);
    assert(beforeWindAngle == afterWindAngle);
    AssertLocalEcologyEqual(afterEcology, beforeEcology);

    SpaceAdvanceTime(19.75f);
    WeatherFieldSample continuedWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology continuedEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);

    rewind(file);
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    SetPropertySeed(loadedSeed);
    assert(SpaceLoadState(file));
    assert(PlanetWorldLoadState(file));
    assert(PlanetEcologyLoadState(file));
    SpaceAdvanceTime(19.75f);
    WeatherFieldSample replayWeather = WeatherFieldSampleAtWorld(sampleX, sampleZ);
    float replayWindAngle = WeatherWindAngleAtWorld(sampleX, sampleZ);
    PlanetLocalEcology replayEcology = PlanetEcologyLocalAt(sampleX, sampleZ, 0.72f);
    assert(memcmp(&continuedWeather, &replayWeather,
                  sizeof(continuedWeather)) == 0);
    assert(beforeWindAngle == replayWindAngle);
    AssertLocalEcologyEqual(replayEcology, continuedEcology);
    fclose(file);
}

typedef struct ChunkBlockSnapshot {
    int cx;
    int cz;
    FloraStructureInstance floraStructures[MAX_CHUNK_FLORA_STRUCTURES];
    int floraStructureCount;
    unsigned short blocks[CHUNK_SIZE][WORLD_HEIGHT][CHUNK_SIZE];
} ChunkBlockSnapshot;

static void FreeCpuMesh(Mesh *mesh)
{
    free(mesh->vertices);
    free(mesh->texcoords);
    free(mesh->normals);
    free(mesh->colors);
    *mesh = (Mesh){ 0 };
}

static void AssertMeshEqual(const Mesh *actual, const Mesh *expected)
{
    assert(actual->vertexCount == expected->vertexCount);
    assert(actual->triangleCount == expected->triangleCount);
    size_t vertexCount = (size_t)actual->vertexCount;
    assert(memcmp(actual->vertices, expected->vertices,
                  vertexCount * 3u * sizeof(float)) == 0);
    assert(memcmp(actual->texcoords, expected->texcoords,
                  vertexCount * 2u * sizeof(float)) == 0);
    assert(memcmp(actual->normals, expected->normals,
                  vertexCount * 3u * sizeof(float)) == 0);
    assert(memcmp(actual->colors, expected->colors,
                  vertexCount * 4u * sizeof(unsigned char)) == 0);
}

static bool BuildChunkFloraMesh(
    const Chunk *chunk, Mesh *outMesh,
    FloraVisualInstance **outInstances, int *outInstanceCount)
{
    static const int faces[6][3] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 },
        { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }
    };
    return BuildChunkFloraMeshData(
        chunk, faces, NULL, 0, outMesh, outInstances, outInstanceCount);
}

static Vector3 FindFloraGenerationCenter(
    uint32_t *outSeed, FloraStructureInstance *outStructure)
{
    for (uint32_t seedIndex = 0; seedIndex < 512u; seedIndex++) {
        uint32_t seed = 0x51a7e5edu + seedIndex * 0x9e3779b9u;
        SetPropertySeed(seed);
        PlanetEcologyResetState();
        ActivateEcologyPlanet(seed, 317, -911);
        PlanetEcologyProfile ecology = PlanetEcologyCurrent();
        if (ecology.floraDensity <= 0.08f) continue;

        Chunk probe = { 0 };
        for (int radius = 0; radius <= 12; radius++) {
            for (int cz = -radius; cz <= radius; cz++) {
                for (int cx = -radius; cx <= radius; cx++) {
                    if (radius > 0 && abs(cx) != radius &&
                        abs(cz) != radius) {
                        continue;
                    }
                    GenerateChunkTerrain(&probe, cx, cz, terrainMode);
                    int chunkMinX = cx * CHUNK_SIZE;
                    int chunkMinZ = cz * CHUNK_SIZE;
                    int chunkMaxX = chunkMinX + CHUNK_SIZE - 1;
                    int chunkMaxZ = chunkMinZ + CHUNK_SIZE - 1;
                    for (int index = 0;
                         index < probe.floraStructureCount; index++) {
                        FloraStructureInstance structure =
                            probe.floraStructures[index];
                        bool crossesBoundary =
                            structure.minX < chunkMinX ||
                            structure.maxX > chunkMaxX ||
                            structure.minZ < chunkMinZ ||
                            structure.maxZ > chunkMaxZ;
                        if (!crossesBoundary) continue;
                        *outSeed = seed;
                        *outStructure = structure;
                        return (Vector3){
                            (float)chunkMinX + 0.5f,
                            18.0f,
                            (float)chunkMinZ + 0.5f
                        };
                    }
                }
            }
        }
    }
    assert(false);
    return (Vector3){ 0 };
}

static void TestFloraMeshDeformationProperties(void)
{
    enum { vertexCount = 64 };
    for (int sample = 0; sample < 512; sample++) {
        float baseVertices[vertexCount * 3];
        float vertices[vertexCount * 3];
        for (int vertex = 0; vertex < vertexCount; vertex++) {
            baseVertices[vertex * 3] = (float)(vertex - 24) * 0.25f;
            baseVertices[vertex * 3 + 1] = 10.0f +
                (float)((vertex * 7 + sample * 3) % 9 + 1) * 0.5f;
            baseVertices[vertex * 3 + 2] =
                (float)(17 - vertex) * 0.125f;
        }
        memcpy(vertices, baseVertices, sizeof(vertices));

        int firstVertex = sample % 19;
        int available = vertexCount - firstVertex;
        int rangeCount = 1 + (sample * 29) % available;
        if (sample % 7 == 0) rangeCount += vertexCount;
        FloraVisualInstance instance = {
            .firstVertex = firstVertex,
            .vertexCount = rangeCount,
            .anchor = { 3.5f, 10.0f, -8.5f },
            .height = 5.0f,
            .windResponse = 1.0f
        };
        float targetScale = 0.15f + (float)(sample % 13) * 0.06f;
        float sway = (float)(sample % 17 - 8) * 0.015f;
        float windAngle = (float)sample * 0.13f;
        float appliedScale = 0.0f;
        bool changed = false;
        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            targetScale, 1.0f, sway, windAngle,
            &appliedScale, &changed));
        assert(changed);
        assert(fabsf(appliedScale - targetScale) < 0.000001f);

        int lastVertex = firstVertex + rangeCount;
        if (lastVertex > vertexCount) lastVertex = vertexCount;
        for (int vertex = 0; vertex < vertexCount; vertex++) {
            const float *base = &baseVertices[vertex * 3];
            const float *current = &vertices[vertex * 3];
            if (vertex < firstVertex || vertex >= lastVertex) {
                assert(memcmp(current, base, 3u * sizeof(float)) == 0);
                continue;
            }
            float heightFraction = fminf(fmaxf(
                (base[1] - instance.anchor.y) / instance.height,
                0.0f), 1.0f);
            float expectedX = base[0] +
                cosf(windAngle) * sway * heightFraction;
            float expectedY = instance.anchor.y +
                (base[1] - instance.anchor.y) * targetScale;
            float expectedZ = base[2] +
                sinf(windAngle) * sway * heightFraction;
            assert(fabsf(current[0] - expectedX) < 0.000001f);
            assert(fabsf(current[1] - expectedY) < 0.000001f);
            assert(fabsf(current[2] - expectedZ) < 0.000001f);
        }

        float stableVertices[vertexCount * 3];
        memcpy(stableVertices, vertices, sizeof(stableVertices));
        changed = true;
        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            targetScale, 1.0f, sway, windAngle,
            &appliedScale, &changed));
        assert(!changed);
        assert(memcmp(vertices, stableVertices, sizeof(vertices)) == 0);

        assert(DeformFloraMeshInstance(
            vertices, baseVertices, vertexCount, &instance,
            1.0f, 1.0f, 0.0f, 0.0f,
            &appliedScale, &changed));
        assert(memcmp(vertices, baseVertices, sizeof(vertices)) == 0);
    }

    float fragmentABase[4 * 3] = {
        -100.0f, -100.0f, -100.0f,
        2.0f, 11.0f, 7.0f,
        2.0f, 12.0f, 7.0f,
        -100.0f, -100.0f, -100.0f
    };
    float fragmentBBase[5 * 3] = {
        -100.0f, -100.0f, -100.0f,
        -100.0f, -100.0f, -100.0f,
        2.0f, 12.0f, 7.0f,
        2.0f, 14.0f, 7.0f,
        -100.0f, -100.0f, -100.0f
    };
    float fragmentA[4 * 3];
    float fragmentB[5 * 3];
    memcpy(fragmentA, fragmentABase, sizeof(fragmentA));
    memcpy(fragmentB, fragmentBBase, sizeof(fragmentB));
    FloraVisualInstance instanceA = {
        .firstVertex = 1, .vertexCount = 2,
        .anchor = { 2.0f, 10.0f, 7.0f }, .height = 4.0f
    };
    FloraVisualInstance instanceB = instanceA;
    instanceB.firstVertex = 2;
    float appliedScale = 0.0f;
    bool changed = false;
    assert(DeformFloraMeshInstance(
        fragmentA, fragmentABase, 4, &instanceA,
        0.65f, 1.0f, 0.18f, 0.7f, &appliedScale, &changed));
    assert(DeformFloraMeshInstance(
        fragmentB, fragmentBBase, 5, &instanceB,
        0.65f, 1.0f, 0.18f, 0.7f, &appliedScale, &changed));
    assert(memcmp(&fragmentA[2 * 3], &fragmentB[2 * 3],
                  3u * sizeof(float)) == 0);

    float guarded[4 * 3];
    memcpy(guarded, fragmentABase, sizeof(guarded));
    FloraVisualInstance invalid = instanceA;
    invalid.firstVertex = -1;
    assert(!DeformFloraMeshInstance(
        guarded, fragmentABase, 4, &invalid,
        0.5f, 1.0f, 0.1f, 0.0f, &appliedScale, &changed));
    assert(memcmp(guarded, fragmentABase, sizeof(guarded)) == 0);
    invalid.firstVertex = 2;
    invalid.vertexCount = INT_MAX;
    assert(DeformFloraMeshInstance(
        guarded, fragmentABase, 4, &invalid,
        0.5f, 1.0f, 0.1f, 0.0f, &appliedScale, &changed));
    assert(memcmp(guarded, fragmentABase,
                  2u * 3u * sizeof(float)) == 0);
}

static void TestChunkUnloadReloadDeterminism(void)
{
    const int expectedChunkCount =
        (MIN_RENDER_DISTANCE_CHUNKS * 2 + 1) *
        (MIN_RENDER_DISTANCE_CHUNKS * 2 + 1);
    uint32_t seed = 0;
    FloraStructureInstance crossingStructure = { 0 };
    Vector3 playerPosition = FindFloraGenerationCenter(
        &seed, &crossingStructure);
    assert(seed != 0u);
    assert(WorldGetSeed() == seed);
    assert(ChunksStartGenThread());

    UpdateChunks(playerPosition, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    assert(GetActiveChunkCount() == expectedChunkCount);

    ChunkBlockSnapshot *snapshots = calloc(
        (size_t)expectedChunkCount, sizeof(*snapshots));
    assert(snapshots);
    int snapshotCount = 0;
    int crossingFragmentCount = 0;
    Chunk *crossingChunks[expectedChunkCount];
    int crossingChunkCount = 0;
    Chunk *floraChunk = NULL;
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        Chunk *chunk = &chunks[index];
        if (!chunk->loaded) continue;
        assert(!chunk->generating);
        assert(!chunk->hasModel && !chunk->hasWaterModel &&
               !chunk->hasFloraModel);
        assert(chunk->floraTargetScales == NULL);
        assert(chunk->floraTargetWind == NULL);
        assert(chunk->floraTargetWindAngle == NULL);
        assert(chunk->floraTargetPresence == NULL);
        assert(chunk->floraBaseVertices == NULL);
        assert(chunk->floraBaseColors == NULL);
        assert(chunk->floraVisualInstances == NULL);
        ChunkBlockSnapshot *snapshot = &snapshots[snapshotCount++];
        snapshot->cx = chunk->cx;
        snapshot->cz = chunk->cz;
        snapshot->floraStructureCount = chunk->floraStructureCount;
        memcpy(snapshot->floraStructures, chunk->floraStructures,
               (size_t)chunk->floraStructureCount *
               sizeof(FloraStructureInstance));
        memcpy(snapshot->blocks, chunk->blocks, sizeof(snapshot->blocks));
        bool containsCrossingStructure = false;
        for (int structureIndex = 0;
             structureIndex < chunk->floraStructureCount; structureIndex++) {
            const FloraStructureInstance *structure =
                &chunk->floraStructures[structureIndex];
            if (structure->kind == crossingStructure.kind &&
                structure->shapeHash == crossingStructure.shapeHash &&
                structure->rootX == crossingStructure.rootX &&
                structure->rootZ == crossingStructure.rootZ) {
                crossingFragmentCount++;
                containsCrossingStructure = true;
                if (!floraChunk) floraChunk = chunk;
            }
        }
        if (containsCrossingStructure) {
            assert(crossingChunkCount < expectedChunkCount);
            crossingChunks[crossingChunkCount++] = chunk;
        }
    }
    assert(snapshotCount == expectedChunkCount);
    assert(crossingFragmentCount >= 2);
    assert(crossingChunkCount == crossingFragmentCount);
    assert(floraChunk);

    int heightOwner[WORLD_HEIGHT + 1];
    float displacementX[WORLD_HEIGHT + 1] = { 0 };
    float displacementZ[WORLD_HEIGHT + 1] = { 0 };
    for (int y = 0; y <= WORLD_HEIGHT; y++) heightOwner[y] = -1;
    int matchedFragmentMeshCount = 0;
    int sharedHeightComparisons = 0;
    for (int fragment = 0; fragment < crossingChunkCount; fragment++) {
        Mesh mesh = { 0 };
        FloraVisualInstance *instances = NULL;
        int instanceCount = 0;
        assert(BuildChunkFloraMesh(
            crossingChunks[fragment], &mesh, &instances, &instanceCount));

        int matchingInstance = -1;
        for (int instanceIndex = 0; instanceIndex < instanceCount;
             instanceIndex++) {
            FloraVisualInstance *instance = &instances[instanceIndex];
            if (instance->anchor.x !=
                    (float)crossingStructure.rootX + 0.5f ||
                instance->anchor.y !=
                    (float)crossingStructure.groundY + 1.0f ||
                instance->anchor.z !=
                    (float)crossingStructure.rootZ + 0.5f) {
                continue;
            }
            assert(matchingInstance < 0);
            matchingInstance = instanceIndex;
        }
        assert(matchingInstance >= 0);
        FloraVisualInstance *instance = &instances[matchingInstance];
        assert(instance->height ==
               (float)(crossingStructure.maxY - crossingStructure.groundY));
        assert(instance->windResponse == crossingStructure.windResponse);

        size_t coordinateCount = (size_t)mesh.vertexCount * 3u;
        float *baseVertices = malloc(coordinateCount * sizeof(float));
        assert(baseVertices);
        memcpy(baseVertices, mesh.vertices,
               coordinateCount * sizeof(float));
        float appliedScale = 0.0f;
        bool changed = false;
        assert(DeformFloraMeshInstance(
            mesh.vertices, baseVertices, mesh.vertexCount, instance,
            0.63f, 1.0f, 0.14f, 0.79f,
            &appliedScale, &changed));
        assert(changed);
        assert(fabsf(appliedScale - 0.63f) < 0.000001f);
        matchedFragmentMeshCount++;

        int firstVertex = instance->firstVertex;
        int lastVertex = firstVertex + instance->vertexCount;
        assert(firstVertex >= 0 && lastVertex <= mesh.vertexCount);
        for (int vertex = firstVertex; vertex < lastVertex; vertex++) {
            const float *base = &baseVertices[vertex * 3];
            const float *current = &mesh.vertices[vertex * 3];
            float heightFraction = fminf(fmaxf(
                (base[1] - instance->anchor.y) / instance->height,
                0.0f), 1.0f);
            float expectedY = instance->anchor.y +
                (base[1] - instance->anchor.y) * 0.63f;
            assert(fabsf(current[1] - expectedY) < 0.000001f);
            if (heightFraction <= 0.001f) continue;

            int layer = (int)lroundf(base[1]);
            if (layer < 0 || layer > WORLD_HEIGHT ||
                fabsf(base[1] - (float)layer) >= 0.0001f) {
                continue;
            }
            float dx = current[0] - base[0];
            float dz = current[2] - base[2];
            if (heightOwner[layer] < 0) {
                heightOwner[layer] = fragment;
                displacementX[layer] = dx;
                displacementZ[layer] = dz;
            } else if (heightOwner[layer] != fragment) {
                assert(fabsf(dx - displacementX[layer]) < 0.000001f);
                assert(fabsf(dz - displacementZ[layer]) < 0.000001f);
                sharedHeightComparisons++;
            }
        }
        free(baseVertices);
        free(instances);
        FreeCpuMesh(&mesh);
    }
    assert(matchedFragmentMeshCount == crossingChunkCount);
    assert(sharedHeightComparisons > 0);

    int floraCx = floraChunk->cx;
    int floraCz = floraChunk->cz;
    Mesh firstFloraMesh = { 0 };
    FloraVisualInstance *firstInstances = NULL;
    int firstInstanceCount = 0;
    assert(BuildChunkFloraMesh(
        floraChunk, &firstFloraMesh, &firstInstances, &firstInstanceCount));
    assert(firstFloraMesh.vertexCount > 0);
    assert(firstInstances);
    assert(firstInstanceCount > 0);
    bool hasVariableStructureRange = false;
    bool hasCrossingStructureLayout = false;
    for (int index = 0; index < firstInstanceCount; index++) {
        const FloraVisualInstance *instance = &firstInstances[index];
        assert(instance->firstVertex >= 0);
        assert(instance->vertexCount > 0);
        assert(instance->firstVertex + instance->vertexCount <=
               firstFloraMesh.vertexCount);
        if (instance->vertexCount != 12) hasVariableStructureRange = true;
        if (instance->anchor.x == (float)crossingStructure.rootX + 0.5f &&
            instance->anchor.y == (float)crossingStructure.groundY + 1.0f &&
            instance->anchor.z == (float)crossingStructure.rootZ + 0.5f) {
            assert(instance->height ==
                   (float)(crossingStructure.maxY -
                           crossingStructure.groundY));
            assert(instance->windResponse == crossingStructure.windResponse);
            hasCrossingStructureLayout = true;
        }
    }
    assert(hasVariableStructureRange);
    assert(hasCrossingStructureLayout);

    floraChunk->floraTargetScales = malloc(sizeof(float));
    floraChunk->floraTargetWind = malloc(sizeof(float));
    floraChunk->floraTargetWindAngle = malloc(sizeof(float));
    floraChunk->floraTargetPresence = malloc(sizeof(float));
    floraChunk->floraBaseVertices = malloc(3u * sizeof(float));
    floraChunk->floraBaseColors = malloc(4u);
    floraChunk->floraVisualInstances = malloc(sizeof(FloraVisualInstance));
    floraChunk->floraTargetScaleCount = 1;
    assert(floraChunk->floraTargetScales && floraChunk->floraTargetWind &&
           floraChunk->floraTargetWindAngle &&
           floraChunk->floraTargetPresence && floraChunk->floraBaseVertices &&
           floraChunk->floraBaseColors && floraChunk->floraVisualInstances);

    UnloadAllChunks();
    assert(GetActiveChunkCount() == 0);
    for (int index = 0; index < MAX_ACTIVE_CHUNKS; index++) {
        assert(!chunks[index].loaded);
        assert(!chunks[index].dirty);
        assert(chunks[index].floraTargetScales == NULL);
        assert(chunks[index].floraTargetWind == NULL);
        assert(chunks[index].floraTargetWindAngle == NULL);
        assert(chunks[index].floraTargetPresence == NULL);
        assert(chunks[index].floraBaseVertices == NULL);
        assert(chunks[index].floraBaseColors == NULL);
        assert(chunks[index].floraVisualInstances == NULL);
        assert(chunks[index].floraTargetScaleCount == 0);
        chunks[index].floraStructureCount = MAX_CHUNK_FLORA_STRUCTURES;
        memset(chunks[index].floraStructures, 0xa5,
               sizeof(chunks[index].floraStructures));
        memset(chunks[index].blocks, 0xa5, sizeof(chunks[index].blocks));
    }

    UpdateChunks(playerPosition, MIN_RENDER_DISTANCE_CHUNKS);
    DrainChunkGen();
    assert(GetActiveChunkCount() == expectedChunkCount);
    for (int index = 0; index < snapshotCount; index++) {
        Chunk *chunk = FindChunk(snapshots[index].cx, snapshots[index].cz);
        assert(chunk);
        assert(!chunk->generating);
        assert(memcmp(chunk->blocks, snapshots[index].blocks,
                      sizeof(chunk->blocks)) == 0);
        assert(chunk->floraStructureCount ==
               snapshots[index].floraStructureCount);
        assert(memcmp(chunk->floraStructures,
                      snapshots[index].floraStructures,
                      (size_t)chunk->floraStructureCount *
                      sizeof(FloraStructureInstance)) == 0);
    }

    Chunk *reloadedFloraChunk = FindChunk(floraCx, floraCz);
    assert(reloadedFloraChunk);
    Mesh secondFloraMesh = { 0 };
    FloraVisualInstance *secondInstances = NULL;
    int secondInstanceCount = 0;
    assert(BuildChunkFloraMesh(
        reloadedFloraChunk, &secondFloraMesh,
        &secondInstances, &secondInstanceCount));
    AssertMeshEqual(&secondFloraMesh, &firstFloraMesh);
    assert(secondInstanceCount == firstInstanceCount);
    assert(memcmp(secondInstances, firstInstances,
                  (size_t)firstInstanceCount *
                  sizeof(FloraVisualInstance)) == 0);

    free(secondInstances);
    free(firstInstances);
    FreeCpuMesh(&secondFloraMesh);
    FreeCpuMesh(&firstFloraMesh);
    free(snapshots);
    UnloadAllChunks();
    ChunksShutdownGenThread();
}

static void TestSaveLoadTimeDeterminism(void)
{
    const uint32_t seed = 0x2468ace0u;
    SetPropertySeed(seed);
    SpaceAdvanceTime(123.5f);
    SolarSystemDef beforeSystem;
    assert(StarSystemAt(3, -4, &beforeSystem));
    Vector3 beforePosition = SolarSystemPlanetPositionAtTime(&beforeSystem, 0,
                                                              SpaceSimulationTime());
    PlanetProfile beforeProfile = SolarPlanetProfile(&beforeSystem, 0);
    WeatherFieldInput beforeWeatherInput = {
        .seed = beforeProfile.seed,
        .simulationTime = SpaceSimulationTime(),
        .worldX = 318.0f,
        .worldZ = -741.0f,
        .temperatureK = beforeProfile.equilibriumTempK,
        .moisture = beforeProfile.oceanCoverage,
        .cloudPotential = beforeProfile.cloudCoverage,
        .windStrength = beforeProfile.windStrength,
        .prevailingWindAngle = beforeProfile.prevailingWindAngle
    };
    WeatherFieldSample beforeWeather = WeatherFieldSampleAt(&beforeWeatherInput);

    FILE *file = tmpfile();
    assert(file);
    assert(fwrite(&seed, sizeof(seed), 1, file) == 1);
    assert(SpaceSaveState(file));

    SetPropertySeed(0xabcdef01u);
    SpaceAdvanceTime(91.25f);
    rewind(file);
    uint32_t loadedSeed = 0;
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    SetPropertySeed(loadedSeed);
    assert(SpaceLoadState(file));
    assert(SpaceSimulationTime() == 123.5);

    SolarSystemDef afterSystem;
    assert(StarSystemAt(3, -4, &afterSystem));
    Vector3 afterPosition = SolarSystemPlanetPositionAtTime(
        &afterSystem, 0, SpaceSimulationTime());
    PlanetProfile afterProfile = SolarPlanetProfile(&afterSystem, 0);
    assert(afterSystem.anchorX == beforeSystem.anchorX);
    assert(afterSystem.anchorZ == beforeSystem.anchorZ);
    assert(afterSystem.planetCount == beforeSystem.planetCount);
    AssertRelative(afterSystem.star.massKg, beforeSystem.star.massKg, 0.0);
    AssertRelative(afterSystem.star.radiusKm, beforeSystem.star.radiusKm, 0.0);
    AssertRelative(afterSystem.star.temperatureK, beforeSystem.star.temperatureK, 0.0);
    assert(VectorLength(VectorSubtractTest(afterPosition, beforePosition)) == 0.0);
    AssertRelative(afterProfile.massKg, beforeProfile.massKg, 0.0);
    AssertRelative(afterProfile.physicalRadiusKm, beforeProfile.physicalRadiusKm, 0.0);
    AssertRelative(afterProfile.receivedIrradiance,
                   beforeProfile.receivedIrradiance, 0.0);
    AssertRelative(afterProfile.radiativeTempK, beforeProfile.radiativeTempK, 0.0);
    AssertRelative(afterProfile.equilibriumTempK,
                   beforeProfile.equilibriumTempK, 0.0);
    AssertRelative(afterProfile.surfacePressureAtm,
                   beforeProfile.surfacePressureAtm, 0.0);
    AssertRelative(afterProfile.greenhouseEffect,
                   beforeProfile.greenhouseEffect, 0.0);
    AssertRelative(afterProfile.oceanCoverage, beforeProfile.oceanCoverage, 0.0);
    AssertRelative(afterProfile.iceCoverage, beforeProfile.iceCoverage, 0.0);
    AssertRelative(afterProfile.cloudCoverage, beforeProfile.cloudCoverage, 0.0);
    AssertRelative(afterProfile.windStrength, beforeProfile.windStrength, 0.0);
    WeatherFieldInput afterWeatherInput = beforeWeatherInput;
    afterWeatherInput.seed = afterProfile.seed;
    afterWeatherInput.simulationTime = SpaceSimulationTime();
    WeatherFieldSample afterWeather = WeatherFieldSampleAt(&afterWeatherInput);
    assert(memcmp(&afterWeather, &beforeWeather, sizeof(beforeWeather)) == 0);

    SpaceAdvanceTime(17.25f);
    Vector3 continued = SolarSystemPlanetPositionAtTime(
        &afterSystem, 0, SpaceSimulationTime());
    afterWeatherInput.simulationTime = SpaceSimulationTime();
    WeatherFieldSample continuedWeather = WeatherFieldSampleAt(&afterWeatherInput);
    rewind(file);
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    SetPropertySeed(loadedSeed);
    assert(SpaceLoadState(file));
    SpaceAdvanceTime(17.25f);
    SolarSystemDef replaySystem;
    assert(StarSystemAt(3, -4, &replaySystem));
    Vector3 replay = SolarSystemPlanetPositionAtTime(
        &replaySystem, 0, SpaceSimulationTime());
    assert(VectorLength(VectorSubtractTest(continued, replay)) == 0.0);
    WeatherFieldInput replayWeatherInput = afterWeatherInput;
    replayWeatherInput.simulationTime = SpaceSimulationTime();
    WeatherFieldSample replayWeather = WeatherFieldSampleAt(&replayWeatherInput);
    assert(memcmp(&continuedWeather, &replayWeather,
                  sizeof(continuedWeather)) == 0);
    fclose(file);
}

int main(void)
{
    TestHomeScaleDiagnostics();
    TestGeneratedSystems();
    TestSaveLoadTimeDeterminism();
    TestEcologyUsesPositionLocalWeather();
    TestEcologyCacheInvalidation();
    TestEcologyCrossSeedReplay();
    TestEcologySaveLoadReplay();
    TestFloraMeshDeformationProperties();
    TestChunkUnloadReloadDeterminism();
    puts("space properties tests passed");
    return 0;
}
