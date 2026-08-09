#include "space_satellite.h"
#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance);
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
        assert(orbit.semiMajorAxisKm >= 6.0 * SPACE_UNITS_EARTH_RADIUS_KM);
        assert(orbit.semiMajorAxisKm <= 0.35 * hillRadius);
        assert(orbit.eccentricity >= 0.0 && orbit.eccentricity < 0.1);
        assert(orbit.inclinationRad > 0.0);
    }
    assert(moonCount > sampleCount * 20 / 100);
    assert(moonCount < sampleCount * 28 / 100);
}

static void TestKeplerOrbit(void)
{
    SpaceSatelliteOrbit orbit = {
        .exists = true,
        .semiMajorAxisKm = 384400.0,
        .eccentricity = 0.0549,
        .inclinationRad = 5.145 * 3.14159265358979323846 / 180.0,
        .radiusKm = 1737.4,
        .massKg = 7.342e22
    };
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
}

int main(void)
{
    TestMoonOccurrenceAndStability();
    TestKeplerOrbit();
    TestSolarOccultation();
    TestPlanetUmbra();
    puts("space_satellite tests passed");
    return 0;
}
