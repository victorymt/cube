#include "space_satellite.h"
#include "space_units.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance);
}

static double NormalizeAngle(double angle)
{
    angle = fmod(angle, 2.0 * 3.14159265358979323846);
    if (angle > 3.14159265358979323846) {
        angle -= 2.0 * 3.14159265358979323846;
    } else if (angle < -3.14159265358979323846) {
        angle += 2.0 * 3.14159265358979323846;
    }
    return angle;
}

static SpaceSatelliteOrbit TestMoonOrbit(void)
{
    return (SpaceSatelliteOrbit){
        .exists = true,
        .semiMajorAxisKm = 384400.0,
        .eccentricity = 0.0549,
        .inclinationRad = 5.145 * 3.14159265358979323846 / 180.0,
        .longitudeAscendingNodeRad = 0.3,
        .argumentPeriapsisRad = 0.7,
        .meanAnomalyAtEpochRad = 0.0,
        .radiusKm = 1737.4,
        .massKg = 7.342e22
    };
}

static void AssertStateZero(const SpaceSatelliteState *state)
{
    assert(state->positionKm.x == 0.0);
    assert(state->positionKm.y == 0.0);
    assert(state->positionKm.z == 0.0);
    assert(state->velocityKmPerSecond.x == 0.0);
    assert(state->velocityKmPerSecond.y == 0.0);
    assert(state->velocityKmPerSecond.z == 0.0);
}

static void AssertOrbitZero(const SpaceSatelliteOrbit *orbit)
{
    assert(!orbit->exists);
    assert(orbit->semiMajorAxisKm == 0.0);
    assert(orbit->eccentricity == 0.0);
    assert(orbit->inclinationRad == 0.0);
    assert(orbit->longitudeAscendingNodeRad == 0.0);
    assert(orbit->argumentPeriapsisRad == 0.0);
    assert(orbit->meanAnomalyAtEpochRad == 0.0);
    assert(orbit->radiusKm == 0.0);
    assert(orbit->massKg == 0.0);
}

static void AssertInvalidOrbit(const SpaceSatelliteOrbit *orbit,
                               double planetMassKg)
{
    SpaceSatelliteState state = {
        .positionKm = { 1.0, 2.0, 3.0 },
        .velocityKmPerSecond = { 4.0, 5.0, 6.0 }
    };
    assert(!SpaceSatelliteStateAtSeconds(orbit, planetMassKg, 10.0, &state));
    AssertStateZero(&state);

    SpaceSatelliteVector3 position = SpaceSatellitePositionAtSeconds(
        orbit, planetMassKg, 10.0);
    assert(position.x == 0.0 && position.y == 0.0 && position.z == 0.0);
    assert(SpaceSatelliteOrbitalPeriodSeconds(orbit, planetMassKg) == 0.0);
}

static void TestInvalidGenerationInputs(void)
{
    const double planetMassKg = SPACE_UNITS_EARTH_MASS_KG;
    const double planetRadiusKm = SPACE_UNITS_EARTH_RADIUS_KM;
    const double planetOrbitKm = SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    const double starMassKg = SPACE_UNITS_SOLAR_MASS_KG;
    SpaceSatelliteOrbit orbit = {
        .exists = true,
        .semiMajorAxisKm = 1.0,
        .radiusKm = 2.0,
        .massKg = 3.0
    };

#define ASSERT_INVALID_GENERATION(mass, radius, orbitKm, star, probability) \
    do {                                                                   \
        assert(!SpaceSatelliteGenerate(                                \
            7u, (mass), (radius), (orbitKm), (star), (probability),     \
            false, &orbit));                                            \
        AssertOrbitZero(&orbit);                                         \
    } while (0)
    ASSERT_INVALID_GENERATION(0.0, planetRadiusKm, planetOrbitKm,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(-planetMassKg, planetRadiusKm, planetOrbitKm,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(NAN, planetRadiusKm, planetOrbitKm,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, 0.0, planetOrbitKm,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, NAN, planetOrbitKm,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, planetRadiusKm, 0.0,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, planetRadiusKm, INFINITY,
                              starMassKg, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, planetRadiusKm, planetOrbitKm,
                              0.0, 0.24);
    ASSERT_INVALID_GENERATION(planetMassKg, planetRadiusKm, planetOrbitKm,
                              starMassKg, NAN);
    ASSERT_INVALID_GENERATION(planetMassKg, planetRadiusKm, planetOrbitKm,
                              starMassKg, INFINITY);
#undef ASSERT_INVALID_GENERATION

    assert(!SpaceSatelliteGenerate(
        7u, DBL_MAX, planetRadiusKm, planetOrbitKm, starMassKg, 1.0,
        true, &orbit));
    AssertOrbitZero(&orbit);
    assert(!SpaceSatelliteGenerate(
        7u, planetMassKg, planetRadiusKm, planetOrbitKm, starMassKg, 0.24,
        false, NULL));
}

static void TestMoonOccurrenceAndStability(void)
{
    int moonCount = 0;
    const int sampleCount = 10000;
    for (uint32_t seed = 0; seed < (uint32_t)sampleCount; seed++) {
        SpaceSatelliteOrbit orbit;
        assert(SpaceSatelliteGenerate(
            seed, SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM,
            SPACE_UNITS_ASTRONOMICAL_UNIT_KM, SPACE_UNITS_SOLAR_MASS_KG,
            0.24, false, &orbit));
        if (!orbit.exists) continue;
        moonCount++;
        double hillRadius = SPACE_UNITS_ASTRONOMICAL_UNIT_KM *
                            cbrt(SPACE_UNITS_EARTH_MASS_KG /
                                 (3.0 * SPACE_UNITS_SOLAR_MASS_KG));
        double periapsisKm = orbit.semiMajorAxisKm *
                             (1.0 - orbit.eccentricity);
        double apoapsisKm = orbit.semiMajorAxisKm *
                            (1.0 + orbit.eccentricity);
        double rocheLimitKm = SpaceSatelliteFluidRocheLimitKm(
            SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM,
            orbit.massKg, orbit.radiusKm);
        assert(periapsisKm >= 6.0 * SPACE_UNITS_EARTH_RADIUS_KM);
        assert(periapsisKm > rocheLimitKm);
        assert(apoapsisKm <= 0.35 * hillRadius);
        assert(orbit.eccentricity >= 0.0 && orbit.eccentricity < 0.1);
        assert(orbit.inclinationRad > 0.0);
    }
    assert(moonCount > sampleCount * 20 / 100);
    assert(moonCount < sampleCount * 28 / 100);
}

static void TestRocheLimit(void)
{
    double rocheLimitKm = SpaceSatelliteFluidRocheLimitKm(
        SPACE_UNITS_EARTH_MASS_KG, SPACE_UNITS_EARTH_RADIUS_KM,
        7.342e22, 1737.4);
    AssertNear(rocheLimitKm, 18350.0, 250.0);
    assert(SpaceSatelliteFluidRocheLimitKm(
               0.0, SPACE_UNITS_EARTH_RADIUS_KM, 7.342e22, 1737.4) == 0.0);
    assert(SpaceSatelliteFluidRocheLimitKm(
               DBL_MAX, 1.0, DBL_MIN, 1.0) == 0.0);
    assert(SpaceSatelliteFluidRocheLimitKm(
               NAN, 1.0, 1.0, 1.0) == 0.0);
}

static void TestKeplerOrbit(void)
{
    SpaceSatelliteOrbit orbit = TestMoonOrbit();
    double period = SpaceSatelliteOrbitalPeriodSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG);
    AssertNear(period / 86400.0, 27.285, 0.05);

    SpaceSatelliteVector3 periapsis = SpaceSatellitePositionAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, 0.0);
    double distance = sqrt(periapsis.x * periapsis.x +
                           periapsis.y * periapsis.y +
                           periapsis.z * periapsis.z);
    AssertNear(distance, orbit.semiMajorAxisKm * (1.0 - orbit.eccentricity),
               0.01);

    SpaceSatelliteState state;
    assert(SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, 0.0, &state));
    assert(state.positionKm.x == periapsis.x &&
           state.positionKm.y == periapsis.y &&
           state.positionKm.z == periapsis.z);
    double speed = sqrt(
        state.velocityKmPerSecond.x * state.velocityKmPerSecond.x +
        state.velocityKmPerSecond.y * state.velocityKmPerSecond.y +
        state.velocityKmPerSecond.z * state.velocityKmPerSecond.z);
    double mu = SpaceUnitsGravitationalParameterKm(
        SPACE_UNITS_EARTH_MASS_KG + orbit.massKg);
    double expectedSpeed = sqrt(mu *
        (2.0 / distance - 1.0 / orbit.semiMajorAxisKm));
    AssertNear(speed, expectedSpeed, 1e-12);

    SpaceSatelliteState before;
    SpaceSatelliteState after;
    assert(SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, -10.0, &before));
    assert(SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, 10.0, &after));
    AssertNear((after.positionKm.x - before.positionKm.x) / 20.0,
               state.velocityKmPerSecond.x, 1e-9);
    AssertNear((after.positionKm.y - before.positionKm.y) / 20.0,
               state.velocityKmPerSecond.y, 1e-9);
    AssertNear((after.positionKm.z - before.positionKm.z) / 20.0,
               state.velocityKmPerSecond.z, 1e-9);
}

static void TestHighEccentricityKeplerSolve(void)
{
    const double semiMajorAxisKm = 100000.0;
    const double planetMassKg = SPACE_UNITS_EARTH_MASS_KG;
    const double satelliteMassKg = 1.0e20;
    const double mu = SpaceUnitsGravitationalParameterKm(
        planetMassKg + satelliteMassKg);
    const double meanMotion = sqrt(mu /
        (semiMajorAxisKm * semiMajorAxisKm * semiMajorAxisKm));
    const double targetMeanAnomalies[] = {
        -3.0, -1.5, -0.1, 0.0, 0.1, 1.5, 3.0
    };
    const double eccentricities[] = { 0.95, 0.999 };

    for (unsigned eccentricityIndex = 0;
         eccentricityIndex < sizeof(eccentricities) / sizeof(eccentricities[0]);
         eccentricityIndex++) {
        SpaceSatelliteOrbit orbit = {
            .exists = true,
            .semiMajorAxisKm = semiMajorAxisKm,
            .eccentricity = eccentricities[eccentricityIndex],
            .radiusKm = 1000.0,
            .massKg = satelliteMassKg
        };
        for (unsigned anomalyIndex = 0;
             anomalyIndex < sizeof(targetMeanAnomalies) /
                             sizeof(targetMeanAnomalies[0]);
             anomalyIndex++) {
            double targetMeanAnomaly = targetMeanAnomalies[anomalyIndex];
            double physicalTimeSeconds = targetMeanAnomaly / meanMotion;
            SpaceSatelliteState state;
            assert(SpaceSatelliteStateAtSeconds(
                &orbit, planetMassKg, physicalTimeSeconds, &state));

            double eccentricityScale = sqrt(
                1.0 - orbit.eccentricity * orbit.eccentricity);
            double eccentricCosine = state.positionKm.x /
                                     orbit.semiMajorAxisKm +
                                     orbit.eccentricity;
            double eccentricSine = state.positionKm.z /
                                   (orbit.semiMajorAxisKm *
                                    eccentricityScale);
            double eccentricAnomaly = atan2(eccentricSine, eccentricCosine);
            double recoveredMeanAnomaly = NormalizeAngle(
                eccentricAnomaly - orbit.eccentricity *
                                   sin(eccentricAnomaly));
            AssertNear(NormalizeAngle(recoveredMeanAnomaly -
                                      targetMeanAnomaly), 0.0, 1e-11);
        }
    }
}

static void TestInvalidKeplerOrbitInputs(void)
{
    SpaceSatelliteOrbit orbit = TestMoonOrbit();
    AssertInvalidOrbit(NULL, SPACE_UNITS_EARTH_MASS_KG);

    SpaceSatelliteOrbit invalid = orbit;
    invalid.exists = false;
    AssertInvalidOrbit(&invalid, SPACE_UNITS_EARTH_MASS_KG);

#define ASSERT_INVALID_ORBIT_FIELD(field, value)                           \
    do {                                                                  \
        invalid = orbit;                                                  \
        invalid.field = (value);                                         \
        AssertInvalidOrbit(&invalid, SPACE_UNITS_EARTH_MASS_KG);         \
    } while (0)
    ASSERT_INVALID_ORBIT_FIELD(semiMajorAxisKm, 0.0);
    ASSERT_INVALID_ORBIT_FIELD(semiMajorAxisKm, -1.0);
    ASSERT_INVALID_ORBIT_FIELD(semiMajorAxisKm, NAN);
    ASSERT_INVALID_ORBIT_FIELD(semiMajorAxisKm, INFINITY);
    ASSERT_INVALID_ORBIT_FIELD(eccentricity, -0.01);
    ASSERT_INVALID_ORBIT_FIELD(eccentricity, 1.0);
    ASSERT_INVALID_ORBIT_FIELD(eccentricity, NAN);
    ASSERT_INVALID_ORBIT_FIELD(eccentricity, INFINITY);
    ASSERT_INVALID_ORBIT_FIELD(inclinationRad, NAN);
    ASSERT_INVALID_ORBIT_FIELD(longitudeAscendingNodeRad, INFINITY);
    ASSERT_INVALID_ORBIT_FIELD(argumentPeriapsisRad, NAN);
    ASSERT_INVALID_ORBIT_FIELD(meanAnomalyAtEpochRad, INFINITY);
    ASSERT_INVALID_ORBIT_FIELD(radiusKm, 0.0);
    ASSERT_INVALID_ORBIT_FIELD(radiusKm, NAN);
    ASSERT_INVALID_ORBIT_FIELD(massKg, 0.0);
    ASSERT_INVALID_ORBIT_FIELD(massKg, -1.0);
    ASSERT_INVALID_ORBIT_FIELD(massKg, NAN);
#undef ASSERT_INVALID_ORBIT_FIELD

    AssertInvalidOrbit(&orbit, 0.0);
    AssertInvalidOrbit(&orbit, -SPACE_UNITS_EARTH_MASS_KG);
    AssertInvalidOrbit(&orbit, NAN);
    AssertInvalidOrbit(&orbit, INFINITY);

    invalid = orbit;
    invalid.semiMajorAxisKm = 1.0e103;
    assert(SpaceSatelliteOrbitalPeriodSeconds(
               &invalid, SPACE_UNITS_EARTH_MASS_KG) == 0.0);

    SpaceSatelliteState state = {
        .positionKm = { 1.0, 2.0, 3.0 },
        .velocityKmPerSecond = { 4.0, 5.0, 6.0 }
    };
    assert(!SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, NAN, &state));
    AssertStateZero(&state);
    state.positionKm.x = 1.0;
    assert(!SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, INFINITY, &state));
    AssertStateZero(&state);
    assert(!SpaceSatelliteStateAtSeconds(
        &orbit, SPACE_UNITS_EARTH_MASS_KG, 0.0, NULL));
}

static void TestSolarOccultation(void)
{
    SpaceSatelliteVector3 observer = { 0.0, 0.0, 0.0 };
    SpaceSatelliteVector3 moon = { 384400.0, 0.0, 0.0 };
    SpaceSatelliteVector3 sun = { SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.0, 0.0 };
    double aligned = SpaceSatelliteSolarOccultationFraction(
        observer, moon, 1737.4, sun, SPACE_UNITS_SOLAR_RADIUS_KM);
    assert(aligned > 0.90 && aligned < 1.0);

    // A conjunction at the planet center does not eclipse every surface point.
    observer.y = SPACE_UNITS_EARTH_RADIUS_KM;
    double differentSurfacePoint = SpaceSatelliteSolarOccultationFraction(
        observer, moon, 1737.4, sun, SPACE_UNITS_SOLAR_RADIUS_KM);
    AssertNear(differentSurfacePoint, 0.0, 1e-12);

    observer.y = 0.0;
    moon.y = 10000.0;
    double missed = SpaceSatelliteSolarOccultationFraction(
        observer, moon, 1737.4, sun, SPACE_UNITS_SOLAR_RADIUS_KM);
    AssertNear(missed, 0.0, 1e-12);

    observer.x = NAN;
    assert(SpaceSatelliteSolarOccultationFraction(
               observer, moon, 1737.4, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
    observer.x = 0.0;
    assert(SpaceSatelliteSolarOccultationFraction(
               observer, moon, INFINITY, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
    sun.x = INFINITY;
    assert(SpaceSatelliteSolarOccultationFraction(
               observer, moon, 1737.4, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
}

static void TestPlanetUmbra(void)
{
    SpaceSatelliteVector3 sun = {
        SPACE_UNITS_ASTRONOMICAL_UNIT_KM, 0.0, 0.0
    };
    SpaceSatelliteVector3 moon = { -384400.0, 0.0, 0.0 };
    double eclipse = SpaceSatellitePlanetUmbraFraction(
        moon, 1737.4, SPACE_UNITS_EARTH_RADIUS_KM, sun,
        SPACE_UNITS_SOLAR_RADIUS_KM);
    AssertNear(eclipse, 1.0, 1e-12);

    moon.x = 384400.0;
    eclipse = SpaceSatellitePlanetUmbraFraction(
        moon, 1737.4, SPACE_UNITS_EARTH_RADIUS_KM, sun,
        SPACE_UNITS_SOLAR_RADIUS_KM);
    AssertNear(eclipse, 0.0, 1e-12);

    moon.x = NAN;
    assert(SpaceSatellitePlanetUmbraFraction(
               moon, 1737.4, SPACE_UNITS_EARTH_RADIUS_KM, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
    moon.x = -384400.0;
    sun.x = INFINITY;
    assert(SpaceSatellitePlanetUmbraFraction(
               moon, 1737.4, SPACE_UNITS_EARTH_RADIUS_KM, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
    sun.x = SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    assert(SpaceSatellitePlanetUmbraFraction(
               moon, INFINITY, SPACE_UNITS_EARTH_RADIUS_KM, sun,
               SPACE_UNITS_SOLAR_RADIUS_KM) == 0.0);
}

int main(void)
{
    TestInvalidGenerationInputs();
    TestMoonOccurrenceAndStability();
    TestRocheLimit();
    TestKeplerOrbit();
    TestHighEccentricityKeplerSolve();
    TestInvalidKeplerOrbitInputs();
    TestSolarOccultation();
    TestPlanetUmbra();
    puts("space_satellite tests passed");
    return 0;
}
