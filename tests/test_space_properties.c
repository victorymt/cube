#include "space.h"
#include "space_barycenter.h"
#include "space_physics.h"
#include "space_units.h"
#include "weather_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846

static uint32_t propertyWorldSeed = DEFAULT_WORLD_SEED;

uint32_t WorldGetSeed(void)
{
    return propertyWorldSeed;
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

static PlanetProfileGenerationInput ProfileGenerationInputFor(
    const SolarSystemDef *system, int index,
    const SolarSystemPhysicalSummary *summary, const PlanetProfile *profile)
{
    const SolarPlanetDef *planet = &system->planets[index];
    PlanetProfileGenerationInput input = {
        .seed = profile->seed,
        .semiMajorAxisKm = planet->semiMajorAxisKm,
        .physicalRadiusKm = planet->physicalRadiusKm,
        .formationMassEarth = planet->formationMassEarth,
        .spaceProxyRadius = planet->spaceProxyRadius,
        .stellarAgeGyr = summary->ageGyr,
        .orbitalPeriodGameTime =
            (float)SolarSystemPlanetOrbitPeriodGameTime(system, index),
        .stellarCount = summary->stellarCount,
        .planetIndex = index,
        .formationGasGiant = planet->formationGasGiant,
        .forcedGasGiant = system->anchorX == 0 && system->anchorZ == 0 &&
                          index == 3
    };
    memcpy(input.stellarLuminositiesSolar,
           summary->stellarLuminositiesSolar,
           sizeof(input.stellarLuminositiesSolar));
    return input;
}

static void AssertSystemFormation(const SolarSystemDef *system)
{
    assert(system);
    if (system->anchorX == 0 && system->anchorZ == 0) return;
    assert(isfinite(system->formationMetallicity));
    assert(system->formationMetallicity >= 0.05f &&
           system->formationMetallicity <= 1.0f);
    assert(isfinite(system->formationDiskMassEarth));
    assert(system->formationDiskMassEarth >= 0.08f);
    assert(isfinite(system->snowLineKm) && system->snowLineKm > 0.0);
    assert(isfinite(system->habitableZoneInnerKm));
    assert(isfinite(system->habitableZoneOuterKm));
    assert(system->habitableZoneInnerKm > 0.0);
    assert(system->habitableZoneInnerKm < system->habitableZoneOuterKm);

    double previousOrbitKm = 0.0;
    double planetaryMassEarth = 0.0;
    for (int index = 0; index < system->planetCount; index++) {
        const SolarPlanetDef *planet = &system->planets[index];
        assert(planet->formationMassEarth >= 0.08f);
        assert(planet->physicalRadiusKm >=
               0.35 * SPACE_UNITS_EARTH_RADIUS_KM);
        assert(planet->semiMajorAxisKm > previousOrbitKm);
        if (planet->formationGasGiant) {
            assert(planet->formationMassEarth >= 10.0f);
            assert(planet->physicalRadiusKm >=
                   2.6 * SPACE_UNITS_EARTH_RADIUS_KM);
            assert(planet->semiMajorAxisKm >= system->snowLineKm * 0.78);
        }
        planetaryMassEarth += planet->formationMassEarth;
        previousOrbitKm = planet->semiMajorAxisKm;
    }
    assert(planetaryMassEarth <=
           (double)system->formationDiskMassEarth + 0.001);
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
    SolarSystemPhysicalSummary summary;
    assert(SolarSystemPhysicalSummaryForSystem(system, &summary));
    assert(summary.stellarCount == bodyCount);
    assert(summary.stellarCount > 0);
    double expectedIrradiance = 0.0;
    for (int light = 0; light < summary.stellarCount; light++) {
        expectedIrradiance +=
            (double)summary.stellarLuminositiesSolar[light] /
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
                SolarSystemDef repeatedSystem;
                memset(&repeatedSystem, 0xa5, sizeof(repeatedSystem));
                assert(StarSystemAt(anchorX, anchorZ, &repeatedSystem));
                assert(memcmp(&system, &repeatedSystem,
                              sizeof(system)) == 0);
                systemCount++;
                assert(system.exists);
                assert(system.planetCount >= 1 && system.planetCount <= 6);
                AssertSystemFormation(&system);
                SolarStellarBody bodies[SPACE_BARYCENTER_MAX_BODIES];
                int bodyCount = SolarSystemStellarBodiesAtTime(
                    &system, 0.0, bodies, SPACE_BARYCENTER_MAX_BODIES);
                assert(bodyCount >= 1 && bodyCount <= 3);
                SolarSystemPhysicalSummary summary;
                memset(&summary, 0xa5, sizeof(summary));
                assert(SolarSystemPhysicalSummaryForSystem(&system, &summary));
                assert(summary.stellarCount == bodyCount);
                assert(summary.totalMassKg == SolarSystemStellarMassKg(&system));
                assert(summary.ageGyr == system.star.ageGyr);
                multiplicities[bodyCount]++;
                for (int star = 0; star < bodyCount; star++) {
                    AssertStefanBoltzmann(&bodies[star].stellar);
                    assert(summary.stellarLuminositiesSolar[star] ==
                           bodies[star].stellar.luminositySolar);
                }
                for (int planet = 0; planet < system.planetCount; planet++) {
                    PlanetProfile profile = SolarPlanetProfile(&system, planet);
                    PlanetProfileGenerationInput input =
                        ProfileGenerationInputFor(&system, planet, &summary,
                                                  &profile);
                    PlanetProfile directProfile;
                    memset(&directProfile, 0xa5, sizeof(directProfile));
                    assert(PlanetProfileGenerate(&input, &directProfile));
                    assert(memcmp(&profile, &directProfile,
                                  sizeof(profile)) == 0);
                    assert(profile.massKg > 0.0 && profile.physicalRadiusKm > 0.0);
                    if (system.anchorX != 0 || system.anchorZ != 0) {
                        AssertRelative(
                            profile.massKg / SPACE_UNITS_EARTH_MASS_KG,
                            system.planets[planet].formationMassEarth,
                            0.000001);
                        assert(profile.hasSolidSurface !=
                               system.planets[planet].formationGasGiant);
                    }
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
    SolarSystemPhysicalSummary beforeSummary;
    assert(SolarSystemPhysicalSummaryForSystem(&beforeSystem, &beforeSummary));
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
    SetPropertySeed(seed);
    PlanetProfile advancedProfile = SolarPlanetProfile(&beforeSystem, 0);
    SolarSystemPhysicalSummary advancedSummary;
    assert(SolarSystemPhysicalSummaryForSystem(&beforeSystem,
                                               &advancedSummary));
    assert(memcmp(&beforeProfile, &advancedProfile,
                  sizeof(beforeProfile)) == 0);
    assert(memcmp(&beforeSummary, &advancedSummary,
                  sizeof(beforeSummary)) == 0);
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
    puts("space properties tests passed");
    return 0;
}
