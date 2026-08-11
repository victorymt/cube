#include "space.h"
#include "space_barycenter.h"
#include "space_physics.h"
#include "space_query_cache.h"
#include "space_system.h"
#include "space_system_physics.h"
#include "space_units.h"
#include "weather_model.h"

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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
        .orbitalEccentricity =
            system->physicalSnapshot.planetOrbits[index].eccentricity,
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

static void AssertSystemCenterOfMass(const SolarSystemDef *system,
                                     const SolarStellarBody *bodies,
                                     int count)
{
    double totalMass = 0.0;
    double positionX = 0.0;
    double positionY = 0.0;
    double positionZ = 0.0;
    double velocityX = 0.0;
    double velocityY = 0.0;
    double velocityZ = 0.0;
    double maximumPosition = 0.0;
    double maximumVelocity = 0.0;
    for (int i = 0; i < count; i++) {
        Vector3 offset = VectorSubtractTest(bodies[i].center, system->center);
        maximumPosition = fmax(maximumPosition, VectorLength(offset));
        maximumVelocity = fmax(maximumVelocity,
                               VectorLength(bodies[i].velocity));
        totalMass += bodies[i].stellar.massKg;
        positionX += (double)offset.x * bodies[i].stellar.massKg;
        positionY += (double)offset.y * bodies[i].stellar.massKg;
        positionZ += (double)offset.z * bodies[i].stellar.massKg;
        velocityX += (double)bodies[i].velocity.x * bodies[i].stellar.massKg;
        velocityY += (double)bodies[i].velocity.y * bodies[i].stellar.massKg;
        velocityZ += (double)bodies[i].velocity.z * bodies[i].stellar.massKg;
    }
    assert(totalMass > 0.0);
    double positionError = sqrt(
        (positionX * positionX + positionY * positionY +
         positionZ * positionZ) / (totalMass * totalMass));
    double velocityError = sqrt(
        (velocityX * velocityX + velocityY * velocityY +
         velocityZ * velocityZ) / (totalMass * totalMass));
    assert(positionError <= fmax(
        0.01, maximumPosition * SPACE_UNITS_MAX_RELATIVE_ERROR));
    assert(velocityError <= fmax(
        0.00001, maximumVelocity * SPACE_UNITS_MAX_RELATIVE_ERROR));
}

static void AssertBarycenter(const SolarSystemDef *system,
                             const SolarStellarBody *bodies, int count)
{
    AssertSystemCenterOfMass(system, bodies, count);

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
    double totalMass = innerMass + bodies[2].stellar.massKg;
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
    SolarPlanetOrbitalState scaleState;
    assert(SolarSystemPlanetStateAtTime(system, index, scaleTime,
                                        &scaleState));
    SpaceBodyInfo scaleBody = {
        .center = scaleState.center,
        .velocity = scaleState.velocity,
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
    const SpaceKeplerOrbit *orbit =
        &system->physicalSnapshot.planetOrbits[index];
    assert(SpaceKeplerOrbitIsValid(orbit));
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
    expectedIrradiance /= sqrt(1.0 - orbit->eccentricity *
                                     orbit->eccentricity);
    assert(expectedIrradiance > 0.0001);
    AssertRelative(profile->receivedIrradiance, expectedIrradiance, 0.000001);
    double centralMass = SolarSystemStellarMassKg(system);
    double semiMajorAxisKm = planet->semiMajorAxisKm;
    assert(orbit->semiMajorAxisKm == semiMajorAxisKm);
    assert(orbit->centralMassKg == centralMass);
    assert(orbit->eccentricity >= 0.0 && orbit->eccentricity <= 0.05);
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
        SolarPlanetOrbitalState orbitalState;
        assert(SolarSystemPlanetStateAtTime(system, index, time,
                                            &orbitalState));
        Vector3 position = orbitalState.center;
        Vector3 compatibilityPosition = SolarSystemPlanetPositionAtTime(
            system, index, time);
        assert(memcmp(&position, &compatibilityPosition,
                      sizeof(position)) == 0);
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
            VectorLength(orbitalState.velocity));
        double expectedSpeed = sqrt(
            SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 * centralMass *
            (2.0 / radiusKm - 1.0 / semiMajorAxisKm));
        AssertRelative(speedKmPerSecond, expectedSpeed, 0.00001);
        double sampledSpeed = VectorLength(delta);
        double analyticSpeed = VectorLength(orbitalState.velocity);
        assert(VectorLength(VectorSubtractTest(delta, orbitalState.velocity)) /
               fmax(analyticSpeed, 0.000001) < 0.002);
        AssertRelative(sampledSpeed, analyticSpeed, 0.002);
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
        double rocheLimitKm = SpaceSatelliteFluidRocheLimitKm(
            profile->massKg, profile->physicalRadiusKm,
            satellite.massKg, satellite.radiusKm);
        assert(periapsis >= 6.0 * profile->physicalRadiusKm);
        assert(periapsis > rocheLimitKm);
        assert(periapsis > profile->physicalRadiusKm + satellite.radiusKm);
        assert(apoapsis <= 0.35 * hillRadiusKm);
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

static void AssertRuntimeState(const SolarSystemDef *system,
                               const SolarStellarBody *stellarBodies,
                               int stellarCount)
{
    SolarSystemRuntimeState runtime;
    assert(SolarSystemEvaluateAtTime(system, 0.0, &runtime));
    assert(runtime.valid);
    assert(runtime.simulationTime == 0.0);
    assert(runtime.stellarCount == stellarCount);
    assert(runtime.planetCount == system->planetCount);
    assert(runtime.totalStellarMassKg == SolarSystemStellarMassKg(system));
    for (int star = 0; star < stellarCount; star++) {
        assert(memcmp(&runtime.stars[star], &stellarBodies[star],
                      sizeof(stellarBodies[star])) == 0);
    }

    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    assert(SolarSystemRuntimeLightSources(&runtime, sources,
                                          MAX_SOLAR_LIGHTS) == stellarCount);
    for (int planet = 0; planet < system->planetCount; planet++) {
        assert(runtime.planets[planet].valid);
        assert(runtime.planets[planet].dynamicalStatus ==
               SOLAR_PLANET_STABLE);
        SolarPlanetOrbitalState orbitalState;
        assert(SolarSystemPlanetStateAtTime(system, planet, 0.0,
                                            &orbitalState));
        assert(memcmp(&runtime.planets[planet].center,
                      &orbitalState.center, sizeof(orbitalState.center)) == 0);
        assert(memcmp(&runtime.planets[planet].velocity,
                      &orbitalState.velocity,
                      sizeof(orbitalState.velocity)) == 0);
        PlanetProfile profile = SolarPlanetProfile(system, planet);
        assert(memcmp(&runtime.planets[planet].profile, &profile,
                      sizeof(profile)) == 0);
        SpaceSatelliteOrbit satellite;
        bool hasSatellite = SolarPlanetSatelliteOrbit(
            system, planet, &profile, &satellite);
        assert(hasSatellite ==
               system->physicalSnapshot.satelliteOrbits[planet].exists);
        assert(memcmp(&runtime.planets[planet].satelliteOrbit,
                      &system->physicalSnapshot.satelliteOrbits[planet],
                      sizeof(satellite)) == 0);
        if (hasSatellite) {
            assert(memcmp(&satellite,
                          &runtime.planets[planet].satelliteOrbit,
                          sizeof(satellite)) == 0);
            SpaceSatelliteState satelliteState;
            assert(SpaceSatelliteStateAtSeconds(
                &satellite, profile.massKg, 0.0, &satelliteState));
            assert(memcmp(&satelliteState,
                          &runtime.planets[planet].satelliteState,
                          sizeof(satelliteState)) == 0);
            Vector3 expectedCenter = {
                orbitalState.center.x +
                    (float)SpaceUnitsKilometersToGameDistance(
                        satelliteState.positionKm.x),
                orbitalState.center.y +
                    (float)SpaceUnitsKilometersToGameDistance(
                        satelliteState.positionKm.y),
                orbitalState.center.z +
                    (float)SpaceUnitsKilometersToGameDistance(
                        satelliteState.positionKm.z)
            };
            Vector3 expectedVelocity = {
                orbitalState.velocity.x +
                    (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                        satelliteState.velocityKmPerSecond.x),
                orbitalState.velocity.y +
                    (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                        satelliteState.velocityKmPerSecond.y),
                orbitalState.velocity.z +
                    (float)SpaceUnitsKilometersPerSecondToGameVelocity(
                        satelliteState.velocityKmPerSecond.z)
            };
            assert(memcmp(&expectedCenter,
                          &runtime.planets[planet].satelliteCenter,
                          sizeof(expectedCenter)) == 0);
            assert(memcmp(&expectedVelocity,
                          &runtime.planets[planet].satelliteVelocity,
                          sizeof(expectedVelocity)) == 0);
        }
        float irradiance = SolarSystemIrradianceAt(
            sources, stellarCount, orbitalState.center);
        assert(runtime.planets[planet].currentIrradianceEarth == irradiance);
        assert(irradiance > 0.0f);
    }

    SolarSystemRuntimeState repeated;
    assert(SolarSystemEvaluateAtTime(system, 0.0, &repeated));
    assert(memcmp(&runtime, &repeated, sizeof(runtime)) == 0);
}

static void TestRuntimeInputContracts(void)
{
    SolarSystemRuntimeState runtime;
    const SolarSystemRuntimeState cleared = { 0 };

    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SolarSystemEvaluateAtTime(NULL, 0.0, &runtime));
    assert(memcmp(&runtime, &cleared, sizeof(runtime)) == 0);

    SolarSystemDef system;
    SetPropertySeed(DEFAULT_WORLD_SEED);
    assert(StarSystemAt(0, 0, &system));
    assert(!SolarSystemEvaluateAtTime(&system, 0.0, NULL));

    SolarSystemPhysicalSummary profileSummary;
    assert(SolarSystemPhysicalSummaryForSystem(&system, &profileSummary));
    PlanetProfile sourceProfile = SolarPlanetProfile(&system, 0);
    PlanetProfileGenerationInput profileInput = ProfileGenerationInputFor(
        &system, 0, &profileSummary, &sourceProfile);
    PlanetProfile generatedProfile;
    const PlanetProfile clearedGeneratedProfile = { 0 };
    memset(&generatedProfile, 0xa5, sizeof(generatedProfile));
    assert(!PlanetProfileGenerate(NULL, &generatedProfile));
    assert(memcmp(&generatedProfile, &clearedGeneratedProfile,
                  sizeof(generatedProfile)) == 0);
    profileInput.physicalRadiusKm = NAN;
    memset(&generatedProfile, 0xa5, sizeof(generatedProfile));
    assert(!PlanetProfileGenerate(&profileInput, &generatedProfile));
    assert(memcmp(&generatedProfile, &clearedGeneratedProfile,
                  sizeof(generatedProfile)) == 0);
    assert(!PlanetProfileGenerate(&profileInput, NULL));

    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SolarSystemEvaluateAtTime(&system, NAN, &runtime));
    assert(memcmp(&runtime, &cleared, sizeof(runtime)) == 0);

    assert(SolarSystemEvaluateAtTime(&system, 0.0, &runtime));
    Vector3 originalCenter = system.center;
    system.center.x = NAN;
    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SolarSystemEvaluateAtTime(&system, 0.0, &runtime));
    assert(memcmp(&runtime, &cleared, sizeof(runtime)) == 0);
    system.center = originalCenter;

    assert(SolarSystemEvaluateAtTime(&system, 0.0, &runtime));
    SolarLightSource sources[MAX_SOLAR_LIGHTS];
    const SolarLightSource clearedSources[MAX_SOLAR_LIGHTS] = { 0 };
    runtime.stars[0].center.x = NAN;
    memset(sources, 0xa5, sizeof(sources));
    assert(SolarSystemRuntimeLightSources(&runtime, sources,
                                          MAX_SOLAR_LIGHTS) == 0);
    assert(memcmp(sources, clearedSources, sizeof(sources)) == 0);

    memset(sources, 0xa5, sizeof(sources));
    assert(SolarSystemLightSources(NULL, sources, MAX_SOLAR_LIGHTS) == 0);
    assert(memcmp(sources, clearedSources, sizeof(sources)) == 0);
    system.center.x = NAN;
    memset(sources, 0xa5, sizeof(sources));
    assert(SolarSystemLightSources(
               &system, sources, MAX_SOLAR_LIGHTS) == 0);
    assert(memcmp(sources, clearedSources, sizeof(sources)) == 0);
    system.center = originalCenter;

    SolarStellarBody stellarBodies[MAX_SOLAR_LIGHTS];
    const SolarStellarBody clearedBodies[MAX_SOLAR_LIGHTS] = { 0 };
    memset(stellarBodies, 0xa5, sizeof(stellarBodies));
    assert(SolarSystemStellarBodiesAtTime(
               &system, NAN, stellarBodies, MAX_SOLAR_LIGHTS) == 0);
    assert(memcmp(stellarBodies, clearedBodies, sizeof(stellarBodies)) == 0);

    system.center.x = NAN;
    memset(stellarBodies, 0xa5, sizeof(stellarBodies));
    assert(SolarSystemStellarBodiesAtTime(
               &system, 0.0, stellarBodies, MAX_SOLAR_LIGHTS) == 0);
    assert(memcmp(stellarBodies, clearedBodies, sizeof(stellarBodies)) == 0);
    system.center = originalCenter;

    SolarStellarBody expectedBodies[MAX_SOLAR_LIGHTS];
    int expectedBodyCount = SolarSystemStellarBodiesAtTime(
        &system, 0.0, expectedBodies, MAX_SOLAR_LIGHTS);
    assert(expectedBodyCount > 0);
    double originalStellarMassKg =
        system.physicalSnapshot.stellarProfiles[0].massKg;
    system.physicalSnapshot.stellarProfiles[0].massKg = NAN;
    memset(stellarBodies, 0xa5, sizeof(stellarBodies));
    assert(SolarSystemStellarBodiesAtTime(
               &system, 0.0, stellarBodies, MAX_SOLAR_LIGHTS) ==
           expectedBodyCount);
    assert(memcmp(stellarBodies, expectedBodies, sizeof(stellarBodies)) == 0);
    system.physicalSnapshot.stellarProfiles[0].massKg = originalStellarMassKg;

    SolarSystemPhysicalSummary summary;
    const SolarSystemPhysicalSummary clearedSummary = { 0 };
    memset(&summary, 0xa5, sizeof(summary));
    assert(!SolarSystemPhysicalSummaryForSystem(NULL, &summary));
    assert(memcmp(&summary, &clearedSummary, sizeof(summary)) == 0);
    assert(SolarSystemStellarMassKg(NULL) == 0.0);
    SolarSystemPhysicalSummary originalSummary = system.physicalSnapshot.summary;
    system.physicalSnapshot.summary.totalMassKg = NAN;
    memset(&summary, 0xa5, sizeof(summary));
    assert(SolarSystemPhysicalSummaryForSystem(&system, &summary));
    assert(memcmp(&summary, &originalSummary, sizeof(summary)) == 0);
    assert(SolarSystemStellarMassKg(&system) == originalSummary.totalMassKg);
    system.physicalSnapshot.summary = originalSummary;
    system.physicalSnapshot.summary.stellarLuminositiesSolar[0] = INFINITY;
    memset(&summary, 0xa5, sizeof(summary));
    assert(SolarSystemPhysicalSummaryForSystem(&system, &summary));
    assert(memcmp(&summary, &originalSummary, sizeof(summary)) == 0);
    system.physicalSnapshot.summary = originalSummary;

    SolarSystemPhysicalSnapshot snapshot;
    const SolarSystemPhysicalSnapshot clearedSnapshot = { 0 };
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(NULL, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    assert(!SolarSystemPhysicalSnapshotBuild(&system, NULL));
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotEvolve(NULL, 1.0, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotEvolve(&system, NAN, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotEvolve(&system, -1.0, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    assert(!SolarSystemPhysicalSnapshotEvolve(&system, 1.0, NULL));

    int originalPlanetCount = system.planetCount;
    system.planetCount = MAX_SOLAR_PLANETS + 1;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    system.planetCount = originalPlanetCount;

    double originalPrimaryMassKg = system.star.massKg;
    system.star.massKg = NAN;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    assert(SolarSystemStellarMassKg(&system) == 0.0);
    system.star.massKg = originalPrimaryMassKg;

    double originalSemiMajorAxisKm = system.planets[0].semiMajorAxisKm;
    system.planets[0].semiMajorAxisKm = NAN;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    system.planets[0].semiMajorAxisKm = originalSemiMajorAxisKm;

    double originalPhysicalRadiusKm = system.planets[0].physicalRadiusKm;
    bool originalSnapshotValid = system.physicalSnapshot.valid;
    system.planets[0].physicalRadiusKm = NAN;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    system.physicalSnapshot.valid = false;
    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SolarSystemEvaluateAtTime(&system, 0.0, &runtime));
    assert(memcmp(&runtime, &cleared, sizeof(runtime)) == 0);
    system.physicalSnapshot.valid = originalSnapshotValid;
    system.planets[0].physicalRadiusKm = originalPhysicalRadiusKm;

    float originalFormationMassEarth =
        system.planets[0].formationMassEarth;
    system.planets[0].formationMassEarth = NAN;
    memset(&snapshot, 0xa5, sizeof(snapshot));
    assert(!SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    assert(memcmp(&snapshot, &clearedSnapshot, sizeof(snapshot)) == 0);
    system.planets[0].formationMassEarth = originalFormationMassEarth;

    assert(SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    const SpaceSatelliteOrbit clearedSatelliteOrbits[MAX_SOLAR_PLANETS] = {
        0
    };
    snapshot.satellitesBuilt = true;
    memset(snapshot.satelliteOrbits, 0xa5, sizeof(snapshot.satelliteOrbits));
    snapshot.summary.totalMassKg = NAN;
    assert(!SolarSystemPhysicalSnapshotBuildSatellites(&system, &snapshot));
    assert(!snapshot.satellitesBuilt);
    assert(memcmp(snapshot.satelliteOrbits, clearedSatelliteOrbits,
                  sizeof(snapshot.satelliteOrbits)) == 0);

    assert(SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    snapshot.satellitesBuilt = true;
    memset(snapshot.satelliteOrbits, 0xa5, sizeof(snapshot.satelliteOrbits));
    system.planets[0].semiMajorAxisKm = NAN;
    assert(!SolarSystemPhysicalSnapshotBuildSatellites(&system, &snapshot));
    assert(!snapshot.satellitesBuilt);
    assert(memcmp(snapshot.satelliteOrbits, clearedSatelliteOrbits,
                  sizeof(snapshot.satelliteOrbits)) == 0);
    system.planets[0].semiMajorAxisKm = originalSemiMajorAxisKm;

    assert(SolarSystemPhysicalSnapshotBuild(&system, &snapshot));
    snapshot.planetStatuses[0] = (SolarPlanetDynamicalStatus)99;
    snapshot.satellitesBuilt = true;
    memset(snapshot.satelliteOrbits, 0xa5, sizeof(snapshot.satelliteOrbits));
    assert(!SolarSystemPhysicalSnapshotBuildSatellites(&system, &snapshot));
    assert(!snapshot.satellitesBuilt);
    assert(memcmp(snapshot.satelliteOrbits, clearedSatelliteOrbits,
                  sizeof(snapshot.satelliteOrbits)) == 0);

    SolarPlanetOrbitalState orbitalState;
    assert(SolarSystemPlanetStateAtTime(&system, 0, 0.0, &orbitalState));
    SolarPlanetOrbitalState expectedOrbitalState = orbitalState;
    double originalCentralMassKg =
        system.physicalSnapshot.planetOrbits[0].centralMassKg;
    system.physicalSnapshot.planetOrbits[0].centralMassKg = NAN;
    memset(&orbitalState, 0xa5, sizeof(orbitalState));
    assert(SolarSystemPlanetStateAtTime(&system, 0, 0.0, &orbitalState));
    assert(memcmp(&orbitalState, &expectedOrbitalState,
                  sizeof(orbitalState)) == 0);
    system.physicalSnapshot.planetOrbits[0].centralMassKg =
        originalCentralMassKg;

    system.center.x = NAN;
    assert(!SolarSystemPlanetStateAtTime(&system, 0, 0.0, &orbitalState));
    assert(orbitalState.center.x == 0.0f && orbitalState.velocity.x == 0.0f);
    system.center = originalCenter;
    system.planetCount = MAX_SOLAR_PLANETS + 1;
    assert(!SolarSystemPlanetStateAtTime(&system, MAX_SOLAR_PLANETS, 0.0,
                                         &orbitalState));
    const PlanetProfile clearedProfile = { 0 };
    PlanetProfile invalidProfile = SolarPlanetProfile(
        &system, MAX_SOLAR_PLANETS);
    assert(memcmp(&invalidProfile, &clearedProfile,
                  sizeof(invalidProfile)) == 0);
    assert(SolarSystemPlanetOrbitPeriodSeconds(
               &system, MAX_SOLAR_PLANETS) == 0.0);
    assert(SolarSystemPlanetOrbitPeriodGameTime(
               &system, MAX_SOLAR_PLANETS) == 0.0);

    SpaceSatelliteOrbit satelliteOrbit;
    const SpaceSatelliteOrbit clearedSatelliteOrbit = { 0 };
    PlanetProfile profile = { 0 };
    memset(&satelliteOrbit, 0xa5, sizeof(satelliteOrbit));
    assert(!SolarPlanetSatelliteOrbit(
        &system, MAX_SOLAR_PLANETS, &profile, &satelliteOrbit));
    assert(memcmp(&satelliteOrbit, &clearedSatelliteOrbit,
                  sizeof(satelliteOrbit)) == 0);
    system.planetCount = originalPlanetCount;

    invalidProfile = SolarPlanetProfile(NULL, 0);
    assert(memcmp(&invalidProfile, &clearedProfile,
                  sizeof(invalidProfile)) == 0);
    assert(SolarSystemPlanetOrbitPeriodSeconds(NULL, 0) == 0.0);
    system.planets[0].semiMajorAxisKm = NAN;
    invalidProfile = SolarPlanetProfile(&system, 0);
    assert(memcmp(&invalidProfile, &clearedProfile,
                  sizeof(invalidProfile)) == 0);
    assert(SolarSystemPlanetOrbitPeriodSeconds(&system, 0) == 0.0);
    system.planets[0].semiMajorAxisKm = originalSemiMajorAxisKm;
    float originalProxyRadius = system.planets[0].spaceProxyRadius;
    system.planets[0].spaceProxyRadius = NAN;
    invalidProfile = SolarPlanetProfile(&system, 0);
    assert(memcmp(&invalidProfile, &clearedProfile,
                  sizeof(invalidProfile)) == 0);
    system.planets[0].spaceProxyRadius = originalProxyRadius;

    memset(&satelliteOrbit, 0xa5, sizeof(satelliteOrbit));
    assert(!SolarPlanetSatelliteOrbit(&system, 0, NULL, &satelliteOrbit));
    assert(memcmp(&satelliteOrbit, &clearedSatelliteOrbit,
                  sizeof(satelliteOrbit)) == 0);
    assert(!SolarPlanetSatelliteOrbit(&system, 0, &profile, NULL));

    const int satelliteIndex = 2;
    profile = SolarPlanetProfile(&system, satelliteIndex);
    SpaceSatelliteOrbit expectedSatelliteOrbit;
    assert(SolarPlanetSatelliteOrbit(
        &system, satelliteIndex, &profile, &expectedSatelliteOrbit));
    bool originalSatellitesBuilt = system.physicalSnapshot.satellitesBuilt;
    SpaceSatelliteOrbit originalSatelliteOrbit =
        system.physicalSnapshot.satelliteOrbits[satelliteIndex];
    system.physicalSnapshot.satellitesBuilt = true;
    system.physicalSnapshot.satelliteOrbits[satelliteIndex] =
        (SpaceSatelliteOrbit){
            .exists = true,
            .semiMajorAxisKm = NAN,
            .eccentricity = 0.1,
            .radiusKm = 1000.0,
            .massKg = 1.0e20
        };
    memset(&satelliteOrbit, 0xa5, sizeof(satelliteOrbit));
    assert(SolarPlanetSatelliteOrbit(
        &system, satelliteIndex, &profile, &satelliteOrbit));
    assert(memcmp(&satelliteOrbit, &expectedSatelliteOrbit,
                  sizeof(satelliteOrbit)) == 0);
    system.physicalSnapshot.satellitesBuilt = originalSatellitesBuilt;
    system.physicalSnapshot.satelliteOrbits[satelliteIndex] =
        originalSatelliteOrbit;
    assert(!SolarSystemPlanetStateAtTime(&system, 0, NAN, &orbitalState));

    Vector3 direction = SolarSystemApparentDirection(
        &system, (Vector3){ NAN, 0.0f, 0.0f });
    assert(direction.x == 0.0f && direction.y == 0.0f &&
           direction.z == 0.0f);
    system.center.x = INFINITY;
    direction = SolarSystemApparentDirection(&system, (Vector3){ 0 });
    assert(direction.x == 0.0f && direction.y == 0.0f &&
           direction.z == 0.0f);
}

static void TestQueryCacheInputContracts(void)
{
    SpaceQueryCacheClear();

    SolarSystemDef definition;
    const SolarSystemDef clearedDefinition = { 0 };
    memset(&definition, 0xa5, sizeof(definition));
    assert(!SpaceQueryDefinitionCacheGet(
        0x71c35a9du, 12345, -67890, &definition));
    assert(memcmp(&definition, &clearedDefinition, sizeof(definition)) == 0);

    SolarSystemRuntimeState runtime;
    const SolarSystemRuntimeState clearedRuntime = { 0 };
    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SpaceQueryRuntimeCacheGet(
        0x71c35a9du, 12345, -67890, UINT64_C(0x4f2319a781c53d6b),
        18.0, &runtime));
    assert(memcmp(&runtime, &clearedRuntime, sizeof(runtime)) == 0);

    memset(&runtime, 0xa5, sizeof(runtime));
    assert(!SpaceQueryRuntimeCacheGet(
        0x71c35a9du, 12345, -67890, UINT64_C(0x4f2319a781c53d6b),
        NAN, &runtime));
    assert(memcmp(&runtime, &clearedRuntime, sizeof(runtime)) == 0);
    SpaceQueryRuntimeCachePut(
        0x71c35a9du, 12345, -67890, UINT64_C(0x4f2319a781c53d6b),
        NAN, &clearedRuntime);

    SpaceQueryCacheClear();
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
                AssertRuntimeState(&system, bodies, bodyCount);
                SolarSystemPhysicalSnapshot rebuiltSnapshot;
                assert(SolarSystemPhysicalSnapshotBuild(&system,
                                                        &rebuiltSnapshot));
                assert(SolarSystemPhysicalSnapshotBuildSatellites(
                    &system, &rebuiltSnapshot));
                assert(memcmp(&system.physicalSnapshot, &rebuiltSnapshot,
                              sizeof(rebuiltSnapshot)) == 0);
                multiplicities[bodyCount]++;
                for (int star = 0; star < bodyCount; star++) {
                    AssertStefanBoltzmann(&bodies[star].stellar);
                    assert(summary.stellarLuminositiesSolar[star] ==
                           bodies[star].stellar.luminositySolar);
                }
                SolarStellarBody snapshotBodies[SPACE_BARYCENTER_MAX_BODIES];
                assert(SolarSystemPhysicalSnapshotStellarBodiesAtTime(
                    &system, &system.physicalSnapshot, 0.0, snapshotBodies,
                    SPACE_BARYCENTER_MAX_BODIES) == bodyCount);
                for (int star = 0; star < bodyCount; star++) {
                    assert(memcmp(&bodies[star], &snapshotBodies[star],
                                  sizeof(bodies[star])) == 0);
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

static void TestExtremeAnchorDeterminism(void)
{
    SetPropertySeed(DEFAULT_WORLD_SEED);
    int generatedCount = 0;
    for (int sample = 0; sample < 512; sample++) {
        int anchorX = INT_MAX - sample;
        int anchorZ = INT_MIN + sample;
        SolarSystemDef first;
        SolarSystemDef second;
        SpaceQueryCacheClear();
        bool firstExists = StarSystemAt(anchorX, anchorZ, &first);
        SpaceQueryCacheClear();
        bool secondExists = StarSystemAt(anchorX, anchorZ, &second);
        assert(firstExists == secondExists);
        assert(memcmp(&first, &second, sizeof(first)) == 0);
        assert(first.anchorX == anchorX && first.anchorZ == anchorZ);
        if (!firstExists) continue;
        assert(first.physicalSnapshot.valid);
        assert(first.planetCount > 0);
        generatedCount++;
    }
    assert(generatedCount > 0);
}

static void TestStellarAgeClimateCausality(void)
{
    StellarProfile youngStar;
    StellarProfile oldStar;
    assert(StellarProfileAtAge(1.0f, 0.5f, 0x13579bdu, &youngStar));
    assert(StellarProfileAtAge(1.0f, 9.5f, 0x13579bdu, &oldStar));
    assert(oldStar.luminositySolar > youngStar.luminositySolar);

    SpaceSystemFormationInput formationInput = {
        .seed = 0x2468aceu,
        .stellarMassSolar = 1.0f,
        .stellarAgeGyr = youngStar.ageGyr,
        .stellarCount = 1,
        .innerStabilityLimitGame = 180.0f,
        .outerLimitGame = 650.0f
    };
    SpaceSystemFormation youngFormation;
    SpaceSystemFormation oldFormation;
    formationInput.stellarLuminositySolar = youngStar.luminositySolar;
    assert(SpaceSystemFormationGenerate(&formationInput, &youngFormation));
    formationInput.stellarAgeGyr = oldStar.ageGyr;
    formationInput.stellarLuminositySolar = oldStar.luminositySolar;
    assert(SpaceSystemFormationGenerate(&formationInput, &oldFormation));
    assert(oldFormation.snowLineGame > youngFormation.snowLineGame);
    assert(oldFormation.habitableInnerGame >
           youngFormation.habitableInnerGame);
    assert(oldFormation.habitableOuterGame >
           youngFormation.habitableOuterGame);

    PlanetProfileGenerationInput planetInput = {
        .seed = 0x10203040u,
        .semiMajorAxisKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
        .formationMassEarth = 1.0f,
        .spaceProxyRadius = 44.0f,
        .stellarAgeGyr = youngStar.ageGyr,
        .orbitalEccentricity = 0.02,
        .orbitalPeriodGameTime = 365.25f,
        .stellarCount = 1,
        .planetIndex = 0
    };
    PlanetProfile youngPlanet;
    PlanetProfile oldPlanet;
    planetInput.stellarLuminositiesSolar[0] = youngStar.luminositySolar;
    assert(PlanetProfileGenerate(&planetInput, &youngPlanet));
    planetInput.stellarAgeGyr = oldStar.ageGyr;
    planetInput.stellarLuminositiesSolar[0] = oldStar.luminositySolar;
    assert(PlanetProfileGenerate(&planetInput, &oldPlanet));
    assert(oldPlanet.receivedIrradiance > youngPlanet.receivedIrradiance);
    assert(oldPlanet.radiativeTempK > youngPlanet.radiativeTempK);
    assert(oldPlanet.equilibriumTempK > youngPlanet.equilibriumTempK);
}

static void TestRuntimeStellarEvolution(void)
{
    SolarSystemDef system;
    assert(StarSystemAt(0, 0, &system));

    SpaceQueryCacheClear();
    SolarSystemRuntimeState baseline;
    assert(SolarSystemEvaluateAtElapsedTime(&system, 0.0, &baseline));
    SpaceQueryCacheStats baselineStats = SpaceQueryCacheGetStats();
    SolarSystemRuntimeState repeated;
    assert(SolarSystemEvaluateAtElapsedTime(&system, 0.0, &repeated));
    assert(memcmp(&baseline, &repeated, sizeof(baseline)) == 0);
    SpaceQueryCacheStats repeatedStats = SpaceQueryCacheGetStats();
    assert(repeatedStats.runtimeHits > baselineStats.runtimeHits);

    double oneGyr = SpaceUnitsGigayearsToGameTime(1.0);
    SolarSystemRuntimeState aged;
    assert(SolarSystemEvaluateAtElapsedTime(&system, oneGyr, &aged));
    SpaceQueryCacheStats agedStats = SpaceQueryCacheGetStats();
    assert(agedStats.runtimeMisses > repeatedStats.runtimeMisses);
    assert(aged.simulationTime == baseline.simulationTime);
    assert(aged.stars[0].stellar.ageGyr >
           baseline.stars[0].stellar.ageGyr);
    assert(aged.stars[0].stellar.luminositySolar >
           baseline.stars[0].stellar.luminositySolar);
    assert(aged.stars[0].stellar.radiusSolar >
           baseline.stars[0].stellar.radiusSolar);
    assert(memcmp(&aged.planets[0].center, &baseline.planets[0].center,
                  sizeof(aged.planets[0].center)) == 0);
    assert(aged.planets[0].profile.receivedIrradiance >
           baseline.planets[0].profile.receivedIrradiance);
    assert(aged.planets[0].profile.radiativeTempK >
           baseline.planets[0].profile.radiativeTempK);
    assert(aged.planets[0].profile.equilibriumTempK >
           baseline.planets[0].profile.equilibriumTempK);

    SolarSystemPhysicalSnapshot remnant;
    SolarSystemPhysicalSnapshot later;
    assert(SolarSystemPhysicalSnapshotEvolve(&system, 20.0, &remnant));
    assert(SolarSystemPhysicalSnapshotEvolve(&system, 30.0, &later));
    assert(memcmp(&remnant, &later, sizeof(remnant)) != 0);
    for (int star = 0; star < remnant.summary.stellarCount; star++) {
        assert(remnant.stellarProfiles[star].stage >=
               STELLAR_STAGE_WHITE_DWARF);
        assert(remnant.stellarProfiles[star].ageGyr >
               remnant.stellarProfiles[star].luminousLifetimeGyr);
        assert(later.stellarProfiles[star].ageGyr >
               remnant.stellarProfiles[star].ageGyr);
        assert(later.stellarProfiles[star].massKg ==
               remnant.stellarProfiles[star].massKg);
        assert(later.stellarProfiles[star].luminositySolar <=
               remnant.stellarProfiles[star].luminositySolar);
    }
    assert(remnant.summary.ageGyr == system.physicalSnapshot.summary.ageGyr +
                                     20.0f);
    assert(later.summary.ageGyr == system.physicalSnapshot.summary.ageGyr +
                                   30.0f);
    assert(remnant.planetOrbits[0].semiMajorAxisKm >
           system.physicalSnapshot.planetOrbits[0].semiMajorAxisKm);
    SolarSystemRuntimeState finalRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        &system, SpaceUnitsGigayearsToGameTime(20.0), &finalRuntime));
    assert(finalRuntime.valid);
    assert(finalRuntime.planets[0].semiMajorAxisKm ==
           remnant.planetOrbits[0].semiMajorAxisKm);

    finalRuntime.valid = true;
    assert(!SolarSystemEvaluateAtElapsedTime(&system, NAN, &finalRuntime));
    assert(!finalRuntime.valid);
}

static void TestCrossSystemRuntimeStellarEvolution(void)
{
    const double ageOffsetGyr = 2000.0;
    const double elapsed = SpaceUnitsGigayearsToGameTime(ageOffsetGyr);
    int checked = 0;
    int multiplicities[3] = { 0 };
    int stablePlanets = 0;
    int inactivePlanets = 0;
    for (int ax = -24; ax <= 24 && checked < 128; ax++) {
        for (int az = -24; az <= 24 && checked < 128; az++) {
            SolarSystemDef system;
            if (!StarSystemAt(ax, az, &system)) continue;
            const SolarSystemPhysicalSnapshot *base =
                &system.physicalSnapshot;
            SolarSystemPhysicalSnapshot evolved;
            SolarSystemPhysicalSnapshot repeated;
            assert(SolarSystemPhysicalSnapshotEvolve(
                &system, ageOffsetGyr, &evolved));
            assert(SolarSystemPhysicalSnapshotEvolve(
                &system, ageOffsetGyr, &repeated));
            assert(memcmp(&evolved, &repeated, sizeof(evolved)) == 0);
            assert(evolved.summary.stellarCount ==
                   base->summary.stellarCount);
            multiplicities[evolved.summary.stellarCount - 1]++;

            double totalMass = 0.0;
            float totalLuminosity = 0.0f;
            for (int star = 0; star < evolved.summary.stellarCount; star++) {
                const StellarProfile *before = &base->stellarProfiles[star];
                const StellarProfile *after = &evolved.stellarProfiles[star];
                assert(after->ageGyr >= before->ageGyr);
                assert(after->stage >= STELLAR_STAGE_WHITE_DWARF);
                assert(after->ageGyr >= after->luminousLifetimeGyr);
                float temperatureRatio = after->temperatureK / 5772.0f;
                float expectedLuminosity =
                    after->radiusSolar * after->radiusSolar *
                    powf(temperatureRatio, 4.0f);
                AssertRelative(after->luminositySolar,
                               expectedLuminosity, 0.0003);
                assert(evolved.stellarOrbit.massKg[star] == after->massKg);
                totalMass += after->massKg;
                totalLuminosity += after->luminositySolar;
            }
            assert(evolved.summary.totalMassKg == totalMass);
            assert(evolved.summary.totalLuminositySolar == totalLuminosity);
            assert(totalMass < base->summary.totalMassKg);
            double orbitScale = base->summary.totalMassKg / totalMass;
            assert(orbitScale > 1.0);
            if (evolved.summary.stellarCount > 1 &&
                evolved.stellarOrbit.motion == SPACE_BARYCENTER_BOUND &&
                evolved.stellarOrbit.innerEccentricity == 0.0) {
                double initialInnerMass =
                    base->stellarOrbit.massKg[0] +
                    base->stellarOrbit.massKg[1];
                double evolvedInnerMass =
                    evolved.stellarOrbit.massKg[0] +
                    evolved.stellarOrbit.massKg[1];
                AssertRelative(
                    evolved.stellarOrbit.innerSeparationKm,
                    base->stellarOrbit.innerSeparationKm *
                        initialInnerMass / evolvedInnerMass,
                    0.0000001);
            }
            if (evolved.summary.stellarCount == 3 &&
                evolved.stellarOrbit.motion == SPACE_BARYCENTER_BOUND &&
                evolved.stellarOrbit.outerEccentricity == 0.0) {
                AssertRelative(
                    evolved.stellarOrbit.outerSeparationKm,
                    base->stellarOrbit.outerSeparationKm * orbitScale,
                    0.0000001);
            }
            for (int planet = 0; planet < system.planetCount; planet++) {
                SolarPlanetDynamicalStatus status =
                    evolved.planetStatuses[planet];
                assert(status >= SOLAR_PLANET_STABLE &&
                       status <= SOLAR_PLANET_EJECTED);
                if (status != SOLAR_PLANET_STABLE) {
                    const SpaceKeplerOrbit clearedOrbit = { 0 };
                    assert(memcmp(&evolved.planetOrbits[planet],
                                  &clearedOrbit,
                                  sizeof(clearedOrbit)) == 0);
                    assert(!evolved.satelliteOrbits[planet].exists);
                    inactivePlanets++;
                    continue;
                }
                stablePlanets++;
                assert(SpaceKeplerOrbitIsValid(
                    &evolved.planetOrbits[planet]));
                assert(evolved.planetOrbits[planet].centralMassKg ==
                       totalMass);
                assert(evolved.planetOrbits[planet].semiMajorAxisKm >=
                       base->planetOrbits[planet].semiMajorAxisKm);
                assert(evolved.planetOrbits[planet].eccentricity >=
                       base->planetOrbits[planet].eccentricity);
                double expectedPeriod = 2.0 * TEST_PI * sqrt(
                    evolved.planetOrbits[planet].semiMajorAxisKm *
                    evolved.planetOrbits[planet].semiMajorAxisKm *
                    evolved.planetOrbits[planet].semiMajorAxisKm /
                    (SPACE_UNITS_GRAVITATIONAL_CONSTANT_KM3_KG_S2 *
                     totalMass));
                AssertRelative(
                    SpaceUnitsKeplerPeriodSeconds(
                        evolved.planetOrbits[planet].semiMajorAxisKm,
                        evolved.planetOrbits[planet].centralMassKg),
                    expectedPeriod, 0.0000001);
            }

            SolarStellarBody evolvedBodies[MAX_SOLAR_LIGHTS];
            assert(SolarSystemPhysicalSnapshotStellarBodiesAtTime(
                &system, &evolved, 37.25, evolvedBodies,
                MAX_SOLAR_LIGHTS) == evolved.summary.stellarCount);
            AssertSystemCenterOfMass(&system, evolvedBodies,
                                     evolved.summary.stellarCount);

            SolarSystemRuntimeState runtime;
            SolarSystemRuntimeState replay;
            assert(SolarSystemEvaluateAtElapsedTime(
                &system, elapsed, &runtime));
            assert(SolarSystemEvaluateAtElapsedTime(
                &system, elapsed, &replay));
            assert(memcmp(&runtime, &replay, sizeof(runtime)) == 0);
            assert(runtime.totalStellarMassKg == totalMass);
            AssertSystemCenterOfMass(&system, runtime.stars,
                                     runtime.stellarCount);
            for (int planet = 0; planet < runtime.planetCount; planet++) {
                assert(runtime.planets[planet].dynamicalStatus ==
                       evolved.planetStatuses[planet]);
                if (evolved.planetStatuses[planet] !=
                        SOLAR_PLANET_STABLE) {
                    const PlanetProfile clearedProfile = { 0 };
                    assert(!runtime.planets[planet].valid);
                    assert(memcmp(&runtime.planets[planet].profile,
                                  &clearedProfile,
                                  sizeof(clearedProfile)) == 0);
                    assert(!runtime.planets[planet].satelliteOrbit.exists);
                    continue;
                }
                assert(runtime.planets[planet].valid);
                assert(runtime.planets[planet].semiMajorAxisKm ==
                       evolved.planetOrbits[planet].semiMajorAxisKm);
                if (runtime.planets[planet].satelliteOrbit.exists) {
                    const SpaceSatelliteOrbit *moon =
                        &runtime.planets[planet].satelliteOrbit;
                    double hillRadiusKm = SpaceUnitsHillSphereKm(
                        runtime.planets[planet].semiMajorAxisKm,
                        runtime.planets[planet].profile.massKg,
                        runtime.totalStellarMassKg);
                    double apoapsis = moon->semiMajorAxisKm *
                                       (1.0 + moon->eccentricity);
                    assert(apoapsis <= 0.35 * hillRadiusKm);
                }
            }
            checked++;
        }
    }
    assert(checked == 128);
    assert(multiplicities[0] > 0);
    assert(multiplicities[1] > 0);
    assert(multiplicities[2] > 0);
    assert(stablePlanets > 0);
    assert(stablePlanets + inactivePlanets > 0);
}

static void TestSupernovaPlanetFates(void)
{
    SolarSystemDef system;
    assert(StarSystemAt(0, 0, &system));
    assert(StellarProfileAtAge(12.0f, 0.0, 0x6d2b79f5u,
                               &system.star));
    system.spectrum = system.star.spectrum;
    system.starProxyRadius = SolarSystemStellarVisualRadius(&system.star);
    assert(SolarSystemPhysicalSnapshotBuild(
        &system, &system.physicalSnapshot));
    assert(SolarSystemPhysicalSnapshotBuildSatellites(
        &system, &system.physicalSnapshot));

    double ageOffsetGyr = (double)system.star.luminousLifetimeGyr + 0.25;
    SolarSystemPhysicalSnapshot evolved;
    SolarSystemPhysicalSnapshot repeated;
    assert(SolarSystemPhysicalSnapshotEvolve(
        &system, ageOffsetGyr, &evolved));
    assert(SolarSystemPhysicalSnapshotEvolve(
        &system, ageOffsetGyr, &repeated));
    assert(memcmp(&evolved, &repeated, sizeof(evolved)) == 0);
    assert(evolved.stellarProfiles[0].stage ==
           STELLAR_STAGE_NEUTRON_STAR);

    StellarProfile terminalGiant;
    assert(StellarProfileAtAge(
        system.star.initialMassSolar,
        (double)system.star.luminousLifetimeGyr,
        system.star.evolutionSeed, &terminalGiant));
    assert(terminalGiant.stage == STELLAR_STAGE_RED_GIANT);
    int engulfed = 0;
    int ejected = 0;
    for (int planet = 0; planet < system.planetCount; planet++) {
        const SpaceKeplerOrbit *initial =
            &system.physicalSnapshot.planetOrbits[planet];
        double periapsisKm = initial->semiMajorAxisKm *
                             (1.0 - initial->eccentricity);
        bool expectedEngulfment =
            periapsisKm <= terminalGiant.radiusKm +
                            system.planets[planet].physicalRadiusKm;
        SolarPlanetDynamicalStatus expected = expectedEngulfment
            ? SOLAR_PLANET_ENGULFED : SOLAR_PLANET_EJECTED;
        assert(evolved.planetStatuses[planet] == expected);
        if (expectedEngulfment) engulfed++;
        else ejected++;
        const SpaceKeplerOrbit clearedOrbit = { 0 };
        assert(memcmp(&evolved.planetOrbits[planet], &clearedOrbit,
                      sizeof(clearedOrbit)) == 0);
    }
    assert(engulfed > 0);
    assert(ejected > 0);

    double elapsed = SpaceUnitsGigayearsToGameTime(ageOffsetGyr);
    SolarSystemRuntimeState runtime;
    SolarSystemRuntimeState replay;
    assert(SolarSystemEvaluateAtElapsedTime(&system, elapsed, &runtime));
    assert(SolarSystemEvaluateAtElapsedTime(&system, elapsed, &replay));
    assert(memcmp(&runtime, &replay, sizeof(runtime)) == 0);
    assert(runtime.valid);
    assert(runtime.planetCount == system.planetCount);
    for (int planet = 0; planet < runtime.planetCount; planet++) {
        const PlanetProfile clearedProfile = { 0 };
        assert(runtime.planets[planet].dynamicalStatus ==
               evolved.planetStatuses[planet]);
        assert(!runtime.planets[planet].valid);
        assert(memcmp(&runtime.planets[planet].profile, &clearedProfile,
                      sizeof(clearedProfile)) == 0);
        assert(!runtime.planets[planet].satelliteOrbit.exists);
    }
}

static void TestBoundSupernovaOrbitResponse(void)
{
    SolarSystemDef templateSystem;
    assert(StarSystemAt(0, 0, &templateSystem));
    assert(StellarProfileAtAge(12.0f, 0.0, 0xa511e9b3u,
                               &templateSystem.star));
    templateSystem.spectrum = templateSystem.star.spectrum;
    templateSystem.starProxyRadius = SolarSystemStellarVisualRadius(
        &templateSystem.star);

    bool found = false;
    for (int ax = -12; ax <= 12 && !found; ax++) {
        for (int az = -12; az <= 12 && !found; az++) {
            if (ax == 0 && az == 0) continue;
            SolarSystemDef system = templateSystem;
            system.anchorX = ax;
            system.anchorZ = az;
            assert(SolarSystemPhysicalSnapshotBuild(
                &system, &system.physicalSnapshot));
            const SolarSystemPhysicalSnapshot *base =
                &system.physicalSnapshot;
            if (base->summary.stellarCount <= 1) continue;

            double eventAgeGyr =
                (double)base->stellarProfiles[0].luminousLifetimeGyr;
            double beforeMassKg = 0.0;
            double afterMassKg = 0.0;
            for (int star = 0; star < base->summary.stellarCount; star++) {
                const StellarProfile *initial = &base->stellarProfiles[star];
                StellarProfile before;
                StellarProfile after;
                assert(StellarProfileAtAge(
                    initial->initialMassSolar, eventAgeGyr,
                    initial->evolutionSeed, &before));
                assert(StellarProfileAtAge(
                    initial->initialMassSolar,
                    nextafter(eventAgeGyr, INFINITY),
                    initial->evolutionSeed, &after));
                beforeMassKg += before.massKg;
                afterMassKg += after.massKg;
            }
            if (2.0 * afterMassKg <= beforeMassKg) continue;

            double requestedAgeGyr = eventAgeGyr + 0.000001;
            SolarSystemPhysicalSnapshot evolved;
            assert(SolarSystemPhysicalSnapshotEvolve(
                &system, requestedAgeGyr, &evolved));
            assert(evolved.stellarProfiles[0].stage ==
                   STELLAR_STAGE_NEUTRON_STAR);
            if (evolved.stellarOrbit.motion != SPACE_BARYCENTER_BOUND) {
                continue;
            }
            for (int planet = system.planetCount - 1;
                 planet >= 0; planet--) {
                if (evolved.planetStatuses[planet] !=
                    SOLAR_PLANET_STABLE) {
                    continue;
                }
                const SpaceKeplerOrbit *initial =
                    &base->planetOrbits[planet];
                const SpaceKeplerOrbit *orbit =
                    &evolved.planetOrbits[planet];
                double expectedSemiMajorAxisKm =
                    initial->semiMajorAxisKm *
                    base->summary.totalMassKg / beforeMassKg *
                    afterMassKg / (2.0 * afterMassKg - beforeMassKg) *
                    afterMassKg / evolved.summary.totalMassKg;
                double impulseEccentricity =
                    (beforeMassKg - afterMassKg) / afterMassKg;
                double expectedEccentricity =
                    initial->eccentricity + impulseEccentricity *
                    (1.0 - initial->eccentricity);
                AssertRelative(orbit->semiMajorAxisKm,
                               expectedSemiMajorAxisKm, 0.0000001);
                AssertRelative(orbit->eccentricity,
                               expectedEccentricity, 0.0000001);
                assert(orbit->semiMajorAxisKm > initial->semiMajorAxisKm);
                assert(orbit->eccentricity > initial->eccentricity);

                SolarSystemRuntimeState runtime;
                assert(SolarSystemEvaluateAtElapsedTime(
                    &system,
                    SpaceUnitsGigayearsToGameTime(requestedAgeGyr),
                    &runtime));
                assert(runtime.planets[planet].valid);
                assert(runtime.planets[planet].dynamicalStatus ==
                       SOLAR_PLANET_STABLE);
                assert(runtime.planets[planet].semiMajorAxisKm ==
                       orbit->semiMajorAxisKm);
                found = true;
                break;
            }
        }
    }
    assert(found);
}

static double StellarSeparationSum(const SolarStellarBody *bodies,
                                   int count)
{
    double sum = 0.0;
    for (int left = 0; left < count; left++) {
        for (int right = left + 1; right < count; right++) {
            sum += VectorLength(VectorSubtractTest(bodies[right].center,
                                                   bodies[left].center));
        }
    }
    return sum;
}

static void AssertDisruptedSystem(
    const SolarSystemDef *system,
    const SolarSystemPhysicalSnapshot *evolved,
    double elapsed)
{
    assert(system && evolved);
    assert(evolved->summary.stellarCount > 1);
    assert(evolved->stellarOrbit.motion != SPACE_BARYCENTER_BOUND);
    if (evolved->summary.stellarCount == 2) {
        assert(evolved->stellarOrbit.motion == SPACE_BARYCENTER_FREE_FLIGHT);
    }

    SolarStellarBody initial[MAX_SOLAR_LIGHTS];
    SolarStellarBody later[MAX_SOLAR_LIGHTS];
    int count = evolved->summary.stellarCount;
    assert(SolarSystemPhysicalSnapshotStellarBodiesAtTime(
               system, evolved, 0.0, initial, MAX_SOLAR_LIGHTS) == count);
    assert(SolarSystemPhysicalSnapshotStellarBodiesAtTime(
               system, evolved, 7.25, later, MAX_SOLAR_LIGHTS) == count);
    AssertSystemCenterOfMass(system, initial, count);
    AssertSystemCenterOfMass(system, later, count);
    assert(fabs(StellarSeparationSum(later, count) -
                StellarSeparationSum(initial, count)) > 0.001);

    SolarSystemRuntimeState runtime;
    SolarSystemRuntimeState replay;
    assert(SolarSystemEvaluateAtElapsedTime(system, elapsed, &runtime));
    assert(SolarSystemEvaluateAtElapsedTime(system, elapsed, &replay));
    assert(memcmp(&runtime, &replay, sizeof(runtime)) == 0);
    assert(runtime.stellarCount == count);
    AssertSystemCenterOfMass(system, runtime.stars, runtime.stellarCount);
    SpaceRemnantEnvironment coreEnvironment;
    assert(SolarSystemRemnantEnvironmentAt(
        &runtime, runtime.stars[0].center, &coreEnvironment));
    assert(coreEnvironment.active && coreEnvironment.remnantCount > 0);
    assert(coreEnvironment.radiationHazard > 0.0f);
    Vector3 farFromRemnant = runtime.stars[0].center;
    farFromRemnant.x += runtime.stars[0].remnant.proxyShockRadiusGame * 8.0f;
    SpaceRemnantEnvironment farEnvironment;
    assert(SolarSystemRemnantEnvironmentAt(
        &runtime, farFromRemnant, &farEnvironment));
    assert(farEnvironment.radiationHazard <
           coreEnvironment.radiationHazard);
    assert(coreEnvironment.nearestShellDistanceGame > 0.0f);
    assert(farEnvironment.nearestShellDistanceGame >
           coreEnvironment.nearestShellDistanceGame);

    int ejected = 0;
    for (int planet = 0; planet < system->planetCount; planet++) {
        SolarPlanetDynamicalStatus status = evolved->planetStatuses[planet];
        assert(status == SOLAR_PLANET_ENGULFED ||
               status == SOLAR_PLANET_EJECTED);
        if (status == SOLAR_PLANET_EJECTED) ejected++;
        const SpaceKeplerOrbit clearedOrbit = { 0 };
        assert(memcmp(&evolved->planetOrbits[planet], &clearedOrbit,
                      sizeof(clearedOrbit)) == 0);
        assert(runtime.planets[planet].dynamicalStatus == status);
        assert(!runtime.planets[planet].valid);
        assert(!runtime.planets[planet].satelliteOrbit.exists);
    }
    assert(ejected > 0);
}

static void AssertDisruptionSaveLoadDeterminism(
    const SolarSystemDef *system, double elapsed)
{
    FILE *restore = tmpfile();
    FILE *target = tmpfile();
    FILE *checkpoint = tmpfile();
    assert(restore && target && checkpoint);
    assert(SpaceSaveState(restore));

    int originX = SpaceOriginX();
    int originZ = SpaceOriginZ();
    assert(fwrite(&elapsed, sizeof(elapsed), 1, target) == 1);
    assert(fwrite(&originX, sizeof(originX), 1, target) == 1);
    assert(fwrite(&originZ, sizeof(originZ), 1, target) == 1);
    rewind(target);
    assert(SpaceLoadState(target));
    assert(SpaceElapsedSimulationTime() == elapsed);

    SolarSystemRuntimeState savedRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        system, SpaceElapsedSimulationTime(), &savedRuntime));
    assert(SpaceSaveState(checkpoint));

    SpaceAdvanceTime(17.25f);
    SolarSystemRuntimeState continuedRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        system, SpaceElapsedSimulationTime(), &continuedRuntime));

    rewind(checkpoint);
    assert(SpaceLoadState(checkpoint));
    assert(SpaceElapsedSimulationTime() == elapsed);
    SolarSystemRuntimeState loadedRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        system, SpaceElapsedSimulationTime(), &loadedRuntime));
    assert(memcmp(&savedRuntime, &loadedRuntime,
                  sizeof(savedRuntime)) == 0);

    SpaceAdvanceTime(17.25f);
    SolarSystemRuntimeState replayRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        system, SpaceElapsedSimulationTime(), &replayRuntime));
    assert(memcmp(&continuedRuntime, &replayRuntime,
                  sizeof(continuedRuntime)) == 0);

    rewind(restore);
    assert(SpaceLoadState(restore));
    fclose(checkpoint);
    fclose(target);
    fclose(restore);
}

static void TestNatalKickSystemDisruption(void)
{
    SolarSystemDef templateSystem;
    assert(StarSystemAt(0, 0, &templateSystem));
    assert(StellarProfileAtAge(20.0f, 0.0, 0x37b9e2d1u,
                               &templateSystem.star));
    templateSystem.spectrum = templateSystem.star.spectrum;
    templateSystem.starProxyRadius = SolarSystemStellarVisualRadius(
        &templateSystem.star);
    for (int planet = 0; planet < templateSystem.planetCount; planet++) {
        templateSystem.planets[planet].semiMajorAxisKm *= 12.0;
    }

    SolarSystemDef disruptedSystems[2] = { 0 };
    SolarSystemPhysicalSnapshot disruptedSnapshots[2] = { 0 };
    double disruptedElapsed[2] = { 0.0, 0.0 };
    bool found[2] = { false, false };
    for (int ax = -32; ax <= 32 && (!found[0] || !found[1]); ax++) {
        for (int az = -32; az <= 32 && (!found[0] || !found[1]); az++) {
            if (ax == 0 && az == 0) continue;
            SolarSystemDef system = templateSystem;
            system.anchorX = ax;
            system.anchorZ = az;
            assert(SolarSystemPhysicalSnapshotBuild(
                &system, &system.physicalSnapshot));
            int count = system.physicalSnapshot.summary.stellarCount;
            if (count < 2 || count > 3 || found[count - 2]) continue;

            double ageOffsetGyr =
                (double)system.physicalSnapshot.stellarProfiles[0]
                    .luminousLifetimeGyr + 0.000001;
            SolarSystemPhysicalSnapshot evolved;
            SolarSystemPhysicalSnapshot repeated;
            assert(SolarSystemPhysicalSnapshotEvolve(
                &system, ageOffsetGyr, &evolved));
            assert(SolarSystemPhysicalSnapshotEvolve(
                &system, ageOffsetGyr, &repeated));
            assert(memcmp(&evolved, &repeated, sizeof(evolved)) == 0);
            assert(evolved.stellarProfiles[0].stage ==
                   STELLAR_STAGE_NEUTRON_STAR);
            if (evolved.stellarOrbit.motion == SPACE_BARYCENTER_BOUND) {
                continue;
            }
            disruptedSystems[count - 2] = system;
            disruptedSnapshots[count - 2] = evolved;
            disruptedElapsed[count - 2] =
                SpaceUnitsGigayearsToGameTime(ageOffsetGyr);
            found[count - 2] = true;
        }
    }
    assert(found[0] && found[1]);

    for (int index = 0; index < 2; index++) {
        AssertDisruptedSystem(&disruptedSystems[index],
                              &disruptedSnapshots[index],
                              disruptedElapsed[index]);
    }
    AssertDisruptionSaveLoadDeterminism(&disruptedSystems[1],
                                        disruptedElapsed[1]);
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

static void TestScaleDiagnosticsInputContracts(void)
{
    SpaceBodyInfo body = {
        .center = { 0 },
        .velocity = { 1.0f, 0.0f, 0.0f },
        .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
        .semiMajorAxisKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM,
        .parentMassKg = SPACE_UNITS_SOLAR_MASS_KG,
        .spaceProxyRadius = 62.0f,
        .profile = {
            .physicalRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM,
            .massKg = SPACE_UNITS_EARTH_MASS_KG,
            .receivedIrradiance = 1.0,
            .radiativeTempK = 255.0f,
            .equilibriumTempK = 288.0f
        }
    };
    SpaceScaleDiagnostics diagnostics;
    const SpaceScaleDiagnostics cleared = { 0 };
    assert(SpaceBodyScaleDiagnostics(&body, &diagnostics));

    body.velocity.x = NAN;
    memset(&diagnostics, 0xa5, sizeof(diagnostics));
    assert(!SpaceBodyScaleDiagnostics(&body, &diagnostics));
    assert(memcmp(&diagnostics, &cleared, sizeof(diagnostics)) == 0);

    body.velocity.x = 1.0f;
    body.spaceProxyRadius = INFINITY;
    memset(&diagnostics, 0xa5, sizeof(diagnostics));
    assert(!SpaceBodyScaleDiagnostics(&body, &diagnostics));
    assert(memcmp(&diagnostics, &cleared, sizeof(diagnostics)) == 0);

    body.spaceProxyRadius = 62.0f;
    body.profile.massKg = NAN;
    memset(&diagnostics, 0xa5, sizeof(diagnostics));
    assert(!SpaceBodyScaleDiagnostics(&body, &diagnostics));
    assert(memcmp(&diagnostics, &cleared, sizeof(diagnostics)) == 0);

    body.profile.massKg = SPACE_UNITS_EARTH_MASS_KG;
    body.parentMassKg = NAN;
    body.hostStar.massKg = 0.0;
    memset(&diagnostics, 0xa5, sizeof(diagnostics));
    assert(!SpaceBodyScaleDiagnostics(&body, &diagnostics));
    assert(memcmp(&diagnostics, &cleared, sizeof(diagnostics)) == 0);
}

static void TestSpaceQueryInputContracts(void)
{
    SolarSystemDef systems[2];
    SpaceBodyInfo bodies[2];
    SpaceSatelliteInfo satellites[2];
    Vector3 invalidPositions[] = {
        { NAN, 0.0f, 0.0f },
        { INFINITY, 0.0f, 0.0f },
        { FLT_MAX, 0.0f, 0.0f }
    };
    for (size_t i = 0; i < sizeof(invalidPositions) /
                            sizeof(invalidPositions[0]); i++) {
        assert(StarSystemsNear(invalidPositions[i], 100.0f, systems, 2) == 0);
        assert(SpaceBodiesNear(invalidPositions[i], 100.0f, bodies, 2) == 0);
        assert(SpaceSatellitesNear(invalidPositions[i], 100.0f,
                                   satellites, 2) == 0);
        SolarSystemDef nearest;
        float nearestDistance = 123.0f;
        assert(!FindNearestSystem(invalidPositions[i], 100.0f,
                                   &nearest, &nearestDistance));
        assert(!nearest.exists && nearestDistance == 0.0f);
    }
    SpaceRemnantEnvironment remnantEnvironment;
    const SpaceRemnantEnvironment clearedRemnantEnvironment = { 0 };
    for (size_t i = 0; i < sizeof(invalidPositions) /
                            sizeof(invalidPositions[0]); i++) {
        memset(&remnantEnvironment, 0xa5, sizeof(remnantEnvironment));
        assert(!SpaceRemnantEnvironmentAt(
            invalidPositions[i], &remnantEnvironment));
        assert(memcmp(&remnantEnvironment, &clearedRemnantEnvironment,
                      sizeof(remnantEnvironment)) == 0);
    }
    SolarSystemRuntimeState invalidRuntime = { 0 };
    memset(&remnantEnvironment, 0xa5, sizeof(remnantEnvironment));
    assert(!SolarSystemRemnantEnvironmentAt(
        &invalidRuntime, (Vector3){ 0 }, &remnantEnvironment));
    assert(memcmp(&remnantEnvironment, &clearedRemnantEnvironment,
                  sizeof(remnantEnvironment)) == 0);
    assert(StarSystemsNear((Vector3){ 0 }, FLT_MAX, systems, 2) == 0);
    assert(SpaceBodiesNear((Vector3){ 0 }, FLT_MAX, bodies, 2) == 0);
    assert(SpaceSatellitesNear((Vector3){ 0 }, FLT_MAX,
                               satellites, 2) == 0);
    assert(!FindNearestSystem((Vector3){ 0 }, 100.0f, NULL, NULL));
    assert(HomeWorldSpaceFade(
               (Vector3){ NAN, 0.0f, 0.0f }) == 0.0f);
    assert(PlanetWorldAtmosphereFade(
               (Vector3){ 0.0f, INFINITY, 0.0f }) == 0.0f);
    assert(!HomeWorldCanEnter((Vector3){ 0.0f, 0.0f, NAN }));
    Vector3 skyDirection = PlanetWorldSkyDirection(
        (Vector3){ NAN, 0.0f, 0.0f });
    assert(skyDirection.x == 0.0f && skyDirection.y == 0.0f &&
           skyDirection.z == 0.0f);

    SpaceBodyInfo picked;
    memset(&picked, 0xa5, sizeof(picked));
    assert(!SpaceBodyPick((Vector3){ 0 },
                          (Vector3){ NAN, 0.0f, 0.0f }, &picked));
    assert(picked.physicalRadiusKm == 0.0 && picked.spaceProxyRadius == 0.0f);

    SolarSystemDef host;
    const SolarSystemDef clearedHost = { 0 };
    FILE *homeState = tmpfile();
    FILE *inactiveHomeState = tmpfile();
    assert(homeState && inactiveHomeState);
    assert(HomeWorldSaveState(homeState));
    uint8_t inactive = 0;
    const float homeReturnPosition[3] = { 0.5f, 12.0f, 0.5f };
    assert(fwrite(&inactive, sizeof(inactive), 1, inactiveHomeState) == 1);
    assert(fwrite(homeReturnPosition, sizeof(homeReturnPosition), 1,
                  inactiveHomeState) == 1);
    rewind(inactiveHomeState);
    assert(HomeWorldLoadState(inactiveHomeState));
    memset(&host, 0xa5, sizeof(host));
    assert(!SurfaceHostSystem(&host));
    assert(memcmp(&host, &clearedHost, sizeof(host)) == 0);
    assert(!SurfaceHostSystem(NULL));
    rewind(homeState);
    assert(HomeWorldLoadState(homeState));
    fclose(inactiveHomeState);
    fclose(homeState);

    SpaceBodyInfo landingTarget;
    const SpaceBodyInfo clearedLandingTarget = { 0 };
    memset(&landingTarget, 0xa5, sizeof(landingTarget));
    assert(!PlanetWorldLandingTarget(
        (Vector3){ NAN, 0.0f, 0.0f }, &landingTarget));
    assert(memcmp(&landingTarget, &clearedLandingTarget,
                  sizeof(landingTarget)) == 0);
    assert(!PlanetWorldLandingTarget((Vector3){ 0 }, NULL));

    Vector3 gravityDirection = { 1.0f, 2.0f, 3.0f };
    float surfaceDistance = 123.0f;
    float gravityScale = 456.0f;
    assert(!PlanetSurfaceAt((Vector3){ NAN, 0.0f, 0.0f },
                            &gravityDirection, &surfaceDistance,
                            &gravityScale));
    assert(gravityDirection.x == 0.0f && gravityDirection.y == 0.0f &&
           gravityDirection.z == 0.0f);
    assert(surfaceDistance == 0.0f && gravityScale == 0.0f);
    assert(!PlanetSurfaceAt((Vector3){ 0 }, NULL, &surfaceDistance,
                            &gravityScale));
    assert(!PlanetSurfaceAt((Vector3){ 0 }, &gravityDirection, NULL,
                            &gravityScale));

    SpaceScaleDiagnostics scale;
    memset(&scale, 0xa5, sizeof(scale));
    assert(!SpaceScaleDiagnosticsAt((Vector3){ NAN, 0.0f, 0.0f }, &scale));
    assert(scale.physicalRadiusKm == 0.0);

    SpaceSatelliteScaleDiagnostics satelliteScale;
    memset(&satelliteScale, 0xa5, sizeof(satelliteScale));
    assert(!SpaceSatelliteScaleDiagnosticsAt(
        (Vector3){ INFINITY, 0.0f, 0.0f }, &satelliteScale));
    assert(satelliteScale.physicalRadiusKm == 0.0);

    SpaceGravitySample gravity;
    memset(&gravity, 0xa5, sizeof(gravity));
    assert(!SpaceGravityAt((Vector3){ NAN, 0.0f, 0.0f }, &gravity));
    assert(!gravity.active && gravity.distance == 0.0f &&
           gravity.gravitationalParameterGame == 0.0f);
}

static void TestIrradianceInputContracts(void)
{
    SolarLightSource source = {
        .center = { 0.0f, 0.0f, 0.0f },
        .luminosity = 1.0f
    };
    assert(isfinite(SolarLightIrradianceAt(&source, (Vector3){ 0 })));
    assert(SolarLightIrradianceAt(NULL, (Vector3){ 0 }) == 0.0f);

    source.luminosity = NAN;
    assert(SolarLightIrradianceAt(&source, (Vector3){ 0 }) == 0.0f);
    source.luminosity = 1.0f;
    source.center.x = NAN;
    assert(SolarLightIrradianceAt(&source, (Vector3){ 0 }) == 0.0f);
    source.center.x = 0.0f;
    assert(SolarLightIrradianceAt(&source,
                                  (Vector3){ INFINITY, 0.0f, 0.0f }) == 0.0f);

    source.luminosity = FLT_MAX;
    assert(SolarLightIrradianceAt(&source, (Vector3){ 0 }) == 0.0f);

    source.luminosity = 1.0f;
    assert(SolarSystemIrradianceAt(&source, 1, (Vector3){ 0 }) > 0.0f);
    assert(SolarSystemIrradianceAt(&source, MAX_SOLAR_LIGHTS + 1,
                                   (Vector3){ 0 }) == 0.0f);
    assert(SolarSystemIrradianceAt(&source, 1,
                                   (Vector3){ NAN, 0.0f, 0.0f }) == 0.0f);
}

static void TestSaveLoadTimeDeterminism(void)
{
    SpaceSaveOrigin(NULL);
    assert(!SpaceSaveState(NULL));
    assert(!HomeWorldSaveState(NULL));
    assert(!SpaceLoadOrigin(NULL));
    assert(!SpaceLoadState(NULL));
    assert(!PlanetWorldLoadState(NULL));
    assert(!HomeWorldLoadState(NULL));

    const uint32_t seed = 0x2468ace0u;
    SetPropertySeed(seed);
    SpaceAdvanceTime(123.5f);
    SolarSystemDef beforeSystem;
    assert(StarSystemAt(3, -4, &beforeSystem));
    SolarPlanetOrbitalState beforeOrbitalState;
    assert(SolarSystemPlanetStateAtTime(&beforeSystem, 0,
                                        SpaceSimulationTime(),
                                        &beforeOrbitalState));
    SolarSystemRuntimeState beforeRuntime;
    assert(SolarSystemEvaluateAtTime(&beforeSystem, SpaceSimulationTime(),
                                     &beforeRuntime));
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
    assert(SpaceElapsedSimulationTime() == 123.5);

    SolarSystemDef afterSystem;
    assert(StarSystemAt(3, -4, &afterSystem));
    SolarPlanetOrbitalState afterOrbitalState;
    assert(SolarSystemPlanetStateAtTime(&afterSystem, 0,
                                        SpaceSimulationTime(),
                                        &afterOrbitalState));
    SolarSystemRuntimeState afterRuntime;
    assert(SolarSystemEvaluateAtTime(&afterSystem, SpaceSimulationTime(),
                                     &afterRuntime));
    PlanetProfile afterProfile = SolarPlanetProfile(&afterSystem, 0);
    assert(afterSystem.anchorX == beforeSystem.anchorX);
    assert(afterSystem.anchorZ == beforeSystem.anchorZ);
    assert(afterSystem.planetCount == beforeSystem.planetCount);
    assert(memcmp(&afterSystem, &beforeSystem, sizeof(afterSystem)) == 0);
    AssertRelative(afterSystem.star.massKg, beforeSystem.star.massKg, 0.0);
    AssertRelative(afterSystem.star.radiusKm, beforeSystem.star.radiusKm, 0.0);
    AssertRelative(afterSystem.star.temperatureK, beforeSystem.star.temperatureK, 0.0);
    assert(memcmp(&afterOrbitalState, &beforeOrbitalState,
                  sizeof(afterOrbitalState)) == 0);
    assert(memcmp(&afterRuntime, &beforeRuntime,
                  sizeof(afterRuntime)) == 0);
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
    SolarPlanetOrbitalState continuedState;
    assert(SolarSystemPlanetStateAtTime(&afterSystem, 0,
                                        SpaceSimulationTime(),
                                        &continuedState));
    SolarSystemRuntimeState continuedRuntime;
    assert(SolarSystemEvaluateAtTime(&afterSystem, SpaceSimulationTime(),
                                     &continuedRuntime));
    afterWeatherInput.simulationTime = SpaceSimulationTime();
    WeatherFieldSample continuedWeather = WeatherFieldSampleAt(&afterWeatherInput);
    rewind(file);
    assert(fread(&loadedSeed, sizeof(loadedSeed), 1, file) == 1);
    SetPropertySeed(loadedSeed);
    assert(SpaceLoadState(file));
    SpaceAdvanceTime(17.25f);
    SolarSystemDef replaySystem;
    assert(StarSystemAt(3, -4, &replaySystem));
    SolarPlanetOrbitalState replayState;
    assert(SolarSystemPlanetStateAtTime(&replaySystem, 0,
                                        SpaceSimulationTime(),
                                        &replayState));
    SolarSystemRuntimeState replayRuntime;
    assert(SolarSystemEvaluateAtTime(&replaySystem, SpaceSimulationTime(),
                                     &replayRuntime));
    assert(memcmp(&continuedState, &replayState,
                  sizeof(continuedState)) == 0);
    assert(memcmp(&continuedRuntime, &replayRuntime,
                  sizeof(continuedRuntime)) == 0);
    WeatherFieldInput replayWeatherInput = afterWeatherInput;
    replayWeatherInput.simulationTime = SpaceSimulationTime();
    WeatherFieldSample replayWeather = WeatherFieldSampleAt(&replayWeatherInput);
    assert(memcmp(&continuedWeather, &replayWeather,
                  sizeof(continuedWeather)) == 0);
    fclose(file);
}

static void TestLongTermTimeClock(void)
{
    FILE *original = tmpfile();
    assert(original);
    assert(SpaceSaveState(original));

    FILE *future = tmpfile();
    assert(future);
    double elapsed = 20.0 * SPACE_UNITS_GAME_TIME_PER_GIGAYEAR + 123.75;
    int originX = 17;
    int originZ = -29;
    assert(fwrite(&elapsed, sizeof(elapsed), 1, future) == 1);
    assert(fwrite(&originX, sizeof(originX), 1, future) == 1);
    assert(fwrite(&originZ, sizeof(originZ), 1, future) == 1);
    rewind(future);
    assert(SpaceLoadState(future));
    assert(SpaceElapsedSimulationTime() == elapsed);
    assert(SpaceSimulationTime() == 123.75);

    SolarSystemDef evolvedSystem;
    assert(StarSystemAt(0, 0, &evolvedSystem));
    SolarSystemRuntimeState evolvedRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        &evolvedSystem, SpaceElapsedSimulationTime(), &evolvedRuntime));
    assert(evolvedRuntime.stars[0].stellar.stage ==
           STELLAR_STAGE_WHITE_DWARF);
    assert(evolvedRuntime.stars[0].stellar.ageGyr >
           evolvedSystem.star.ageGyr);
    SolarSystemPhysicalSnapshot evolvedSnapshot;
    assert(SolarSystemPhysicalSnapshotEvolve(
        &evolvedSystem, 20.0, &evolvedSnapshot));
    int stablePlanet = -1;
    for (int planet = 0; planet < evolvedRuntime.planetCount; planet++) {
        assert(evolvedRuntime.planets[planet].dynamicalStatus ==
               evolvedSnapshot.planetStatuses[planet]);
        if (evolvedSnapshot.planetStatuses[planet] ==
                SOLAR_PLANET_STABLE) {
            assert(evolvedRuntime.planets[planet].valid);
            stablePlanet = planet;
        } else {
            assert(!evolvedRuntime.planets[planet].valid);
        }
    }
    assert(stablePlanet >= 0);
    assert(evolvedRuntime.planets[stablePlanet].semiMajorAxisKm >
           evolvedSystem.planets[stablePlanet].semiMajorAxisKm);

    SpaceAdvanceTime(10.25f);
    assert(SpaceElapsedSimulationTime() ==
           20.0 * SPACE_UNITS_GAME_TIME_PER_GIGAYEAR + 134.0);
    assert(SpaceSimulationTime() == 134.0);

    FILE *replay = tmpfile();
    assert(replay);
    assert(SpaceSaveState(replay));
    SpaceAdvanceTime(91.0f);
    rewind(replay);
    assert(SpaceLoadState(replay));
    assert(SpaceElapsedSimulationTime() ==
           20.0 * SPACE_UNITS_GAME_TIME_PER_GIGAYEAR + 134.0);
    assert(SpaceSimulationTime() == 134.0);
    assert(SpaceOriginX() == originX);
    assert(SpaceOriginZ() == originZ);
    SolarSystemRuntimeState replayRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        &evolvedSystem, SpaceElapsedSimulationTime(), &replayRuntime));
    assert(memcmp(&evolvedRuntime, &replayRuntime,
                  sizeof(evolvedRuntime)) != 0);

    rewind(replay);
    assert(SpaceLoadState(replay));
    SolarSystemRuntimeState deterministicRuntime;
    assert(SolarSystemEvaluateAtElapsedTime(
        &evolvedSystem, SpaceElapsedSimulationTime(),
        &deterministicRuntime));
    assert(memcmp(&replayRuntime, &deterministicRuntime,
                  sizeof(replayRuntime)) == 0);

    rewind(original);
    assert(SpaceLoadState(original));
    fclose(replay);
    fclose(future);
    fclose(original);
}

static void TestSpaceLoadFailureAtomicity(void)
{
    FILE *original = tmpfile();
    assert(original);
    assert(SpaceSaveState(original));

    FILE *baseline = tmpfile();
    assert(baseline);
    double baselineTime = 731.25;
    int baselineX = 18421;
    int baselineZ = -9375;
    assert(fwrite(&baselineTime, sizeof(baselineTime), 1, baseline) == 1);
    assert(fwrite(&baselineX, sizeof(baselineX), 1, baseline) == 1);
    assert(fwrite(&baselineZ, sizeof(baselineZ), 1, baseline) == 1);
    rewind(baseline);
    assert(SpaceLoadState(baseline));
    assert(SpaceSimulationTime() == baselineTime);
    assert(SpaceElapsedSimulationTime() == baselineTime);
    assert(SpaceOriginX() == baselineX);
    assert(SpaceOriginZ() == baselineZ);

    FILE *truncatedState = tmpfile();
    assert(truncatedState);
    double replacementTime = 19.0;
    assert(fwrite(&replacementTime, sizeof(replacementTime), 1,
                  truncatedState) == 1);
    rewind(truncatedState);
    assert(!SpaceLoadState(truncatedState));
    assert(SpaceSimulationTime() == baselineTime);
    assert(SpaceElapsedSimulationTime() == baselineTime);
    assert(SpaceOriginX() == baselineX);
    assert(SpaceOriginZ() == baselineZ);

    FILE *invalidState = tmpfile();
    assert(invalidState);
    double invalidTime = NAN;
    int replacementX = -11;
    int replacementZ = 29;
    assert(fwrite(&invalidTime, sizeof(invalidTime), 1, invalidState) == 1);
    assert(fwrite(&replacementX, sizeof(replacementX), 1, invalidState) == 1);
    assert(fwrite(&replacementZ, sizeof(replacementZ), 1, invalidState) == 1);
    rewind(invalidState);
    assert(!SpaceLoadState(invalidState));
    assert(SpaceSimulationTime() == baselineTime);
    assert(SpaceElapsedSimulationTime() == baselineTime);
    assert(SpaceOriginX() == baselineX);
    assert(SpaceOriginZ() == baselineZ);

    FILE *truncatedOrigin = tmpfile();
    assert(truncatedOrigin);
    assert(fwrite(&replacementX, sizeof(replacementX), 1,
                  truncatedOrigin) == 1);
    rewind(truncatedOrigin);
    assert(!SpaceLoadOrigin(truncatedOrigin));
    assert(SpaceSimulationTime() == baselineTime);
    assert(SpaceElapsedSimulationTime() == baselineTime);
    assert(SpaceOriginX() == baselineX);
    assert(SpaceOriginZ() == baselineZ);

    rewind(original);
    assert(SpaceLoadState(original));
    fclose(truncatedOrigin);
    fclose(invalidState);
    fclose(truncatedState);
    fclose(baseline);
    fclose(original);
}

static void TestDeterministicSpaceQueries(void)
{
    const Vector3 observer = { 120.0f, 0.0f, -220.0f };
    const float systemRange = 8000.0f;
    const float bodyRange = 700.0f;
    SetPropertySeed(DEFAULT_WORLD_SEED);
    SpaceResetOrigin();
    SpaceQueryCacheClear();

    SolarSystemDef systems[STAR_NAVIGATION_MAX_SYSTEMS];
    int systemCount = StarSystemsNear(observer, systemRange, systems,
                                       STAR_NAVIGATION_MAX_SYSTEMS);
    assert(systemCount > 0);
    SpaceQueryCacheStats coldStats = SpaceQueryCacheGetStats();
    assert(coldStats.definitionMisses > 0);

    SolarSystemDef systemsPrefix[16];
    int prefixCount = StarSystemsNear(observer, systemRange, systemsPrefix,
                                      16);
    assert(prefixCount == (systemCount < 16 ? systemCount : 16));
    for (int i = 0; i < prefixCount; i++) {
        assert(memcmp(&systems[i], &systemsPrefix[i], sizeof(systems[i])) == 0);
    }
    SolarSystemDef systemsRepeat[STAR_NAVIGATION_MAX_SYSTEMS];
    int repeatedCount = StarSystemsNear(observer, systemRange,
                                         systemsRepeat,
                                         STAR_NAVIGATION_MAX_SYSTEMS);
    assert(repeatedCount == systemCount);
    assert(memcmp(systems, systemsRepeat,
                  sizeof(systems[0]) * (size_t)systemCount) == 0);
    SpaceQueryCacheStats repeatedStats = SpaceQueryCacheGetStats();
    assert(repeatedStats.definitionHits > coldStats.definitionHits);

    SolarSystemRuntimeState runtimeFirst;
    SolarSystemRuntimeState runtimeSecond;
    SpaceQueryCacheClear();
    assert(SolarSystemEvaluateAtTime(&systems[0], 42.5, &runtimeFirst));
    SpaceQueryCacheStats runtimeColdStats = SpaceQueryCacheGetStats();
    assert(SolarSystemEvaluateAtTime(&systems[0], 42.5, &runtimeSecond));
    assert(memcmp(&runtimeFirst, &runtimeSecond,
                  sizeof(runtimeFirst)) == 0);
    SpaceQueryCacheStats runtimeRepeatStats = SpaceQueryCacheGetStats();
    assert(runtimeRepeatStats.runtimeHits > runtimeColdStats.runtimeHits);

    float originalProxyRadius = systems[0].planets[0].spaceProxyRadius;
    systems[0].planets[0].spaceProxyRadius = originalProxyRadius + 2.0f;
    SolarSystemRuntimeState runtimeChanged;
    assert(SolarSystemEvaluateAtTime(&systems[0], 42.5, &runtimeChanged));
    assert(runtimeChanged.planets[0].profile.spaceProxyRadius ==
           originalProxyRadius + 2.0f);
    assert(memcmp(&runtimeFirst, &runtimeChanged,
                  sizeof(runtimeFirst)) != 0);
    SpaceQueryCacheStats changedStats = SpaceQueryCacheGetStats();
    assert(changedStats.runtimeMisses > runtimeRepeatStats.runtimeMisses);

    systems[0].planets[0].spaceProxyRadius = originalProxyRadius;
    SolarSystemRuntimeState runtimeRestored;
    assert(SolarSystemEvaluateAtTime(&systems[0], 42.5, &runtimeRestored));
    assert(memcmp(&runtimeFirst, &runtimeRestored,
                  sizeof(runtimeFirst)) == 0);
    SpaceQueryCacheStats restoredStats = SpaceQueryCacheGetStats();
    assert(restoredStats.runtimeHits > changedStats.runtimeHits);

    SpaceBodyInfo bodies[64];
    SpaceQueryCacheClear();
    int bodyCount = SpaceBodiesNear(observer, bodyRange, bodies, 64);
    assert(bodyCount > 0);
    SpaceQueryCacheStats bodyColdStats = SpaceQueryCacheGetStats();
    SpaceBodyInfo bodyPrefix[8];
    int bodyPrefixCount = SpaceBodiesNear(observer, bodyRange, bodyPrefix, 8);
    assert(bodyPrefixCount == (bodyCount < 8 ? bodyCount : 8));
    for (int i = 0; i < bodyPrefixCount; i++) {
        assert(memcmp(&bodies[i], &bodyPrefix[i], sizeof(bodies[i])) == 0);
    }
    SpaceQueryCacheStats bodyRepeatedStats = SpaceQueryCacheGetStats();
    assert(bodyRepeatedStats.runtimeHits > bodyColdStats.runtimeHits);

    SpaceQueryCacheClear();
    SpaceSatelliteInfo satellites[8];
    int satelliteCount = SpaceSatellitesNear(
        (Vector3){ 0.0f, 0.0f, 0.0f }, 900.0f, satellites, 8);
    assert(satelliteCount > 0);
    SpaceSatelliteInfo satellitesRepeat[8];
    int repeatedSatelliteCount = SpaceSatellitesNear(
        (Vector3){ 0.0f, 0.0f, 0.0f }, 900.0f, satellitesRepeat, 8);
    assert(repeatedSatelliteCount == satelliteCount);
    assert(memcmp(satellites, satellitesRepeat,
                  sizeof(satellites[0]) * (size_t)satelliteCount) == 0);
    for (int i = 0; i < satelliteCount; i++) {
        const SpaceSatelliteInfo *satellite = &satellites[i];
        SolarSystemDef system;
        assert(StarSystemAt(satellite->systemAnchorX,
                            satellite->systemAnchorZ, &system));
        SolarSystemRuntimeState runtime;
        assert(SolarSystemEvaluateAtTime(&system, SpaceSimulationTime(),
                                         &runtime));
        assert(satellite->parentPlanetIndex >= 0 &&
               satellite->parentPlanetIndex < runtime.planetCount);
        const SolarPlanetRuntimeState *parent =
            &runtime.planets[satellite->parentPlanetIndex];
        assert(memcmp(&satellite->center, &parent->satelliteCenter,
                      sizeof(satellite->center)) == 0);
        assert(memcmp(&satellite->velocity, &parent->satelliteVelocity,
                      sizeof(satellite->velocity)) == 0);
        double periapsis = satellite->orbit.semiMajorAxisKm *
                           (1.0 - satellite->orbit.eccentricity);
        double apoapsis = satellite->orbit.semiMajorAxisKm *
                          (1.0 + satellite->orbit.eccentricity);
        double rocheLimit = SpaceSatelliteFluidRocheLimitKm(
            parent->profile.massKg, parent->profile.physicalRadiusKm,
            satellite->massKg, satellite->physicalRadiusKm);
        double hillSphere = SpaceUnitsHillSphereKm(
            system.planets[satellite->parentPlanetIndex].semiMajorAxisKm,
            parent->profile.massKg, runtime.totalStellarMassKg);
        assert(periapsis > rocheLimit);
        assert(apoapsis <= 0.35 * hillSphere);
        assert(satellite->encounterRadiusGame > 0.0f);
    }
    SpaceSatelliteScaleDiagnostics satelliteScale;
    assert(SpaceSatelliteScaleDiagnosticsAt(
        (Vector3){ 0.0f, 0.0f, 0.0f }, &satelliteScale));
    assert(satelliteScale.withinErrorBudget);
    assert(satelliteScale.physicalRadiusKm > 0.0);
    assert(satelliteScale.physicalRadiusGame > 0.0);
    assert(satelliteScale.orbitalSpeedKilometersPerSecond > 0.0);
    assert(satelliteScale.hillSphereKm > 0.0);
    assert(satelliteScale.sphereOfInfluenceKm > 0.0);

    SpaceQueryCacheClear();
    SolarSystemDef systemsFirst[32];
    SpaceBodyInfo bodiesFirst[32];
    int systemsFirstCount = StarSystemsNear(observer, systemRange,
                                            systemsFirst, 32);
    int bodiesFirstCount = SpaceBodiesNear(observer, bodyRange,
                                           bodiesFirst, 32);
    SpaceQueryCacheClear();
    SpaceBodyInfo bodiesSecond[32];
    SolarSystemDef systemsSecond[32];
    int bodiesSecondCount = SpaceBodiesNear(observer, bodyRange,
                                            bodiesSecond, 32);
    int systemsSecondCount = StarSystemsNear(observer, systemRange,
                                             systemsSecond, 32);
    assert(systemsFirstCount == systemsSecondCount);
    assert(bodiesFirstCount == bodiesSecondCount);
    assert(memcmp(systemsFirst, systemsSecond,
                  sizeof(systemsFirst[0]) * (size_t)systemsFirstCount) == 0);
    assert(memcmp(bodiesFirst, bodiesSecond,
                  sizeof(bodiesFirst[0]) * (size_t)bodiesFirstCount) == 0);

    SpaceQueryCacheClear();
    SpaceResetOrigin();
    SolarSystemDef beforeRebase[32];
    int beforeCount = StarSystemsNear(observer, systemRange, beforeRebase, 32);
    int rebasedOriginX = STAR_SYSTEM_SPACING * 4;
    int rebasedOriginZ = -STAR_SYSTEM_SPACING * 3;
    FILE *originFile = tmpfile();
    assert(originFile);
    assert(fwrite(&rebasedOriginX, sizeof(rebasedOriginX), 1, originFile) == 1);
    assert(fwrite(&rebasedOriginZ, sizeof(rebasedOriginZ), 1, originFile) == 1);
    rewind(originFile);
    assert(SpaceLoadOrigin(originFile));
    fclose(originFile);
    Vector3 rebasedObserver = {
        observer.x - (float)rebasedOriginX, observer.y,
        observer.z - (float)rebasedOriginZ
    };
    SolarSystemDef afterRebase[32];
    int afterCount = StarSystemsNear(rebasedObserver, systemRange,
                                     afterRebase, 32);
    assert(afterCount == beforeCount);
    for (int i = 0; i < beforeCount; i++) {
        SolarSystemDef before = beforeRebase[i];
        SolarSystemDef after = afterRebase[i];
        assert(before.anchorX == after.anchorX);
        assert(before.anchorZ == after.anchorZ);
        assert(before.center.y == after.center.y);
        assert(before.center.x == after.center.x + rebasedOriginX);
        assert(before.center.z == after.center.z + rebasedOriginZ);
        before.center.x = before.center.z = 0.0f;
        after.center.x = after.center.z = 0.0f;
        assert(memcmp(&before, &after, sizeof(before)) == 0);
    }
    SpaceQueryCacheStats rebaseStats = SpaceQueryCacheGetStats();
    assert(rebaseStats.definitionHits > 0);
    SpaceResetOrigin();

    SpaceQueryCacheClear();
    SpaceBodyInfo benchmarkBodies[48];
    clock_t benchmarkStart = clock();
    for (int iteration = 0; iteration < 128; iteration++) {
        assert(SpaceBodiesNear(observer, bodyRange, benchmarkBodies, 48) >= 0);
    }
    clock_t benchmarkEnd = clock();
    SpaceQueryCacheStats benchmarkStats = SpaceQueryCacheGetStats();
    double elapsedMs = (double)(benchmarkEnd - benchmarkStart) * 1000.0 /
                       (double)CLOCKS_PER_SEC;
    assert(benchmarkStats.runtimeHits > benchmarkStats.runtimeMisses);
    printf("space query cache: systems=%d bodies=%d def=%llu/%llu runtime=%llu/%llu benchmark=%.2fms\n",
           systemCount, bodyCount,
           (unsigned long long)benchmarkStats.definitionHits,
           (unsigned long long)benchmarkStats.definitionMisses,
           (unsigned long long)benchmarkStats.runtimeHits,
           (unsigned long long)benchmarkStats.runtimeMisses, elapsedMs);
}

typedef struct SpaceQueryWorker {
    Vector3 observer;
    SolarSystemDef systems[16];
    SpaceBodyInfo bodies[16];
    SpaceSatelliteInfo satellites[8];
    int systemCount;
    int bodyCount;
    int satelliteCount;
    bool ok;
} SpaceQueryWorker;

static void *RunConcurrentSpaceQueries(void *opaque)
{
    SpaceQueryWorker *worker = opaque;
    worker->ok = true;
    for (int iteration = 0; iteration < 16; iteration++) {
        SolarSystemDef systems[16];
        SpaceBodyInfo bodies[16];
        SpaceSatelliteInfo satellites[8];
        int systemCount = StarSystemsNear(worker->observer, 2800.0f,
                                           systems, 16);
        int bodyCount = SpaceBodiesNear(worker->observer, 700.0f,
                                        bodies, 16);
        int satelliteCount = SpaceSatellitesNear(
            worker->observer, 900.0f, satellites, 8);
        if (iteration == 0) {
            worker->systemCount = systemCount;
            worker->bodyCount = bodyCount;
            worker->satelliteCount = satelliteCount;
            memcpy(worker->systems, systems,
                   sizeof(systems[0]) * (size_t)systemCount);
            memcpy(worker->bodies, bodies,
                   sizeof(bodies[0]) * (size_t)bodyCount);
            memcpy(worker->satellites, satellites,
                   sizeof(satellites[0]) * (size_t)satelliteCount);
        } else if (systemCount != worker->systemCount ||
                   bodyCount != worker->bodyCount ||
                   satelliteCount != worker->satelliteCount ||
                   memcmp(worker->systems, systems,
                          sizeof(systems[0]) * (size_t)systemCount) != 0 ||
                   memcmp(worker->bodies, bodies,
                          sizeof(bodies[0]) * (size_t)bodyCount) != 0 ||
                   memcmp(worker->satellites, satellites,
                          sizeof(satellites[0]) *
                              (size_t)satelliteCount) != 0) {
            worker->ok = false;
            break;
        }
    }
    return NULL;
}

static void TestConcurrentSpaceQueries(void)
{
    static const Vector3 observers[8] = {
        { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f },
        { 1400.0f, 0.0f, 1400.0f }, { 1400.0f, 0.0f, 1400.0f },
        { -2800.0f, 0.0f, 700.0f }, { -2800.0f, 0.0f, 700.0f },
        { 4200.0f, 0.0f, -2100.0f }, { 4200.0f, 0.0f, -2100.0f }
    };
    SpaceResetOrigin();
    SetPropertySeed(0x31415926u);
    SpaceQueryCacheClear();
    SpaceQueryWorker workers[8] = { 0 };
    pthread_t threads[8];
    for (int i = 0; i < 8; i++) {
        workers[i].observer = observers[i];
        assert(pthread_create(&threads[i], NULL, RunConcurrentSpaceQueries,
                              &workers[i]) == 0);
    }
    for (int i = 0; i < 8; i++) {
        assert(pthread_join(threads[i], NULL) == 0);
        assert(workers[i].ok);
    }
    for (int i = 0; i < 8; i += 2) {
        assert(workers[i].systemCount == workers[i + 1].systemCount);
        assert(workers[i].bodyCount == workers[i + 1].bodyCount);
        assert(workers[i].satelliteCount == workers[i + 1].satelliteCount);
        assert(memcmp(workers[i].systems, workers[i + 1].systems,
                      sizeof(workers[i].systems[0]) *
                          (size_t)workers[i].systemCount) == 0);
        assert(memcmp(workers[i].bodies, workers[i + 1].bodies,
                      sizeof(workers[i].bodies[0]) *
                          (size_t)workers[i].bodyCount) == 0);
        assert(memcmp(workers[i].satellites, workers[i + 1].satellites,
                      sizeof(workers[i].satellites[0]) *
                          (size_t)workers[i].satelliteCount) == 0);
    }
    SpaceQueryCacheStats stats = SpaceQueryCacheGetStats();
    assert(stats.definitionHits > 0);
    assert(stats.runtimeHits > 0);
    SetPropertySeed(DEFAULT_WORLD_SEED);
}

int main(void)
{
    TestHomeScaleDiagnostics();
    TestScaleDiagnosticsInputContracts();
    TestSpaceQueryInputContracts();
    TestIrradianceInputContracts();
    TestRuntimeInputContracts();
    TestQueryCacheInputContracts();
    TestGeneratedSystems();
    TestExtremeAnchorDeterminism();
    TestStellarAgeClimateCausality();
    TestRuntimeStellarEvolution();
    TestCrossSystemRuntimeStellarEvolution();
    TestSupernovaPlanetFates();
    TestBoundSupernovaOrbitResponse();
    TestNatalKickSystemDisruption();
    TestSaveLoadTimeDeterminism();
    TestLongTermTimeClock();
    TestSpaceLoadFailureAtomicity();
    TestDeterministicSpaceQueries();
    TestConcurrentSpaceQueries();
    puts("space properties tests passed");
    return 0;
}
