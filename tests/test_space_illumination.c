#include "space_illumination.h"
#include "space_units.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance * fmax(fabs(expected), 1.0));
}

static void TestIrradiance(void)
{
    double au = SPACE_UNITS_ASTRONOMICAL_UNIT_KM;
    AssertNear(SpaceIlluminationIrradianceEarth(1.0, au), 1.0, 1e-12);
    AssertNear(SpaceIlluminationIrradianceEarth(4.0, 2.0 * au), 1.0, 1e-12);
    AssertNear(SpaceIlluminationIrradianceEarth(0.0, au), 0.0, 1e-12);
    AssertNear(SpaceIlluminationIrradianceEarth(1.0, -au), 0.0, 1e-12);

    AssertNear(SpaceIlluminationOrbitMeanIrradianceEarth(1.0, au, 0.0),
               1.0, 1e-12);
    AssertNear(SpaceIlluminationOrbitMeanIrradianceEarth(1.0, au, 0.6),
               1.25, 1e-12);
    assert(SpaceIlluminationOrbitMeanIrradianceEarth(1.0, au, 1.0) == 0.0);
}

static void TestCircleCoverage(void)
{
    assert(SpaceIlluminationCircleCoverage(0.1, 0.1, 0.0) == 1.0);
    assert(SpaceIlluminationCircleCoverage(0.1, 0.1, 0.21) == 0.0);
    AssertNear(SpaceIlluminationCircleCoverage(0.1, 0.05, 0.0),
               0.25, 1e-12);
    double partial = SpaceIlluminationCircleCoverage(0.1, 0.1, 0.1);
    assert(partial > 0.0 && partial < 1.0);
}

static void TestOccultation(void)
{
    SpaceIlluminationBody foreground = {
        .positionKm = { 1.0, 0.0, 0.0 },
        .radiusKm = 0.12
    };
    SpaceIlluminationBody background = {
        .positionKm = { 2.0, 0.0, 0.0 },
        .radiusKm = 0.08
    };
    assert(SpaceIlluminationOccultationFraction(foreground, background) == 1.0);

    foreground.radiusKm = 0.03;
    double partial = SpaceIlluminationOccultationFraction(
        foreground, background);
    assert(partial > 0.0 && partial < 1.0);

    foreground.positionKm.y = 0.20;
    assert(SpaceIlluminationOccultationFraction(foreground, background) == 0.0);
    foreground.positionKm = background.positionKm;
    assert(SpaceIlluminationOccultationFraction(foreground, background) == 0.0);
}

int main(void)
{
    TestIrradiance();
    TestCircleCoverage();
    TestOccultation();
    puts("space illumination tests passed");
    return 0;
}
