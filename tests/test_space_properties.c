#include "space.h"
#include "space_barycenter.h"
#include "space_physics.h"
#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

#define TEST_PI 3.14159265358979323846

/* The production terrain hash reads the world seed through this small API. */
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
    const SolarPlanetDef *planet = &system->planets[index];
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
    printf("space properties: %d systems, %d satellites, multiplicity %d/%d/%d\n",
           systemCount, satelliteCount, multiplicities[1], multiplicities[2],
           multiplicities[3]);
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

    SpaceAdvanceTime(17.25f);
    Vector3 continued = SolarSystemPlanetPositionAtTime(
        &afterSystem, 0, SpaceSimulationTime());
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
    fclose(file);
}

int main(void)
{
    TestGeneratedSystems();
    TestSaveLoadTimeDeterminism();
    puts("space properties tests passed");
    return 0;
}
