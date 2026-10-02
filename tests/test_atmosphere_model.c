#include "world/atmosphere_model.h"
#include "space/planet_atmosphere.h"
#include "core/config.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static AtmosphereProfile Earth(void)
{
    AtmosphereInput input = AtmosphereEarthInput();
    AtmosphereProfile profile;
    assert(AtmosphereProfileBuild(&input, &profile));
    return profile;
}

static void Near(double actual, double expected, double relative)
{
    assert(isfinite(actual));
    assert(fabs(actual - expected) <= fmax(fabs(expected) * relative, 1e-10));
}

static void TestStandardAtmosphere(void)
{
    AtmosphereProfile earth = Earth();
    const double altitude[] = { 0, 11000, 20000, 32000, 47000, 51000, 71000 };
    const double temperature[] = { 288.15, 216.65, 216.65, 228.65, 270.65, 270.65, 214.65 };
    const double pressurePa[] = { 101325, 22632.1, 5474.89, 868.019, 110.906, 66.9389, 3.95642 };
    for (int i = 0; i < 7; i++) {
        AtmosphereSample air = AtmosphereSampleAt(&earth, altitude[i]);
        assert(air.valid);
        Near(air.temperatureK, temperature[i], 1e-6);
        Near(air.pressureAtm * 101325.0, pressurePa[i], 0.001);
        Near(air.densityKgM3, air.pressureAtm * 101325 * air.molarMassKgMol /
                              (8.314462618 * air.temperatureK), 1e-10);
    }
    Near(earth.surfaceDensityKgM3, 1.225, 0.001);
}

static void TestContinuityAndDecay(void)
{
    AtmosphereProfile earth = Earth();
    for (int i = 1; i < ATMOSPHERE_LEVEL_COUNT - 1; i++) {
        AtmosphereSample below = AtmosphereSampleAt(&earth, earth.altitudeM[i] - 0.001);
        AtmosphereSample above = AtmosphereSampleAt(&earth, earth.altitudeM[i] + 0.001);
        Near(below.temperatureK, above.temperatureK, 1e-5);
        Near(below.pressureAtm, above.pressureAtm, 1e-5);
        Near(below.densityKgM3, above.densityKgM3, 1e-5);
        Near(below.molarMassKgMol, above.molarMassKgMol, 1e-5);
    }
    double lastPressure = 2.0, lastDensity = 2.0;
    for (int h = 0; h <= 1100000; h += 500) {
        AtmosphereSample air = AtmosphereSampleAt(&earth, h);
        assert(air.valid && isfinite(air.temperatureK));
        assert(air.pressureAtm >= 0 && air.pressureAtm <= lastPressure);
        assert(air.densityKgM3 >= 0 && air.densityKgM3 <= lastDensity);
        lastPressure = air.pressureAtm;
        lastDensity = air.densityKgM3;
    }
    assert(lastPressure == 0 && lastDensity == 0);
    AtmosphereSample tail = AtmosphereSampleAt(&earth, 999999.0);
    assert(tail.densityKgM3 < 1e-15);
}

static void TestLayersAndPlanetComposition(void)
{
    AtmosphereProfile earth = Earth();
    assert(AtmosphereSampleAt(&earth, 5000).layer == ATMOSPHERE_LAYER_TROPOSPHERE);
    assert(AtmosphereSampleAt(&earth, 25000).layer == ATMOSPHERE_LAYER_STRATOSPHERE);
    assert(AtmosphereSampleAt(&earth, 75000).layer == ATMOSPHERE_LAYER_MESOSPHERE);
    assert(AtmosphereSampleAt(&earth, 150000).layer == ATMOSPHERE_LAYER_THERMOSPHERE);
    assert(AtmosphereSampleAt(&earth, 600000).layer == ATMOSPHERE_LAYER_EXOSPHERE);
    assert(AtmosphereSampleAt(&earth, 1000000).layer == ATMOSPHERE_LAYER_VACUUM);
    assert(AtmosphereSampleAt(&earth, 25000).ozone > 0.99);
    assert(AtmosphereSampleAt(&earth, 150000).ionization > 0);
    assert(AtmosphereSampleAt(&earth, 400000).temperatureK > 900);

    AtmosphereInput input = AtmosphereEarthInput();
    input.ozoneStrength = 0.0;
    input.molarMassKgMol = 0.04401;
    input.gravityMps2 = 3.71;
    input.surfacePressureAtm = 0.006;
    AtmosphereProfile mars;
    assert(AtmosphereProfileBuild(&input, &mars));
    assert(mars.altitudeM[1] > earth.altitudeM[1]);
    AtmosphereSample lower = AtmosphereSampleAt(&mars, mars.altitudeM[2]);
    AtmosphereSample upper = AtmosphereSampleAt(&mars, mars.altitudeM[4]);
    Near(lower.temperatureK, upper.temperatureK, 1e-6);
    assert(upper.ozone == 0.0 && upper.pressureAtm < lower.pressureAtm);

    input = AtmosphereEarthInput();
    input.gravityMps2 *= 2.0;
    AtmosphereProfile heavy;
    assert(AtmosphereProfileBuild(&input, &heavy));
    assert(heavy.altitudeM[1] < earth.altitudeM[1]);
    assert(AtmosphereSampleAt(&heavy, 5000).pressureAtm <
           AtmosphereSampleAt(&earth, 5000).pressureAtm);
    assert(strcmp(AtmosphereLayerName(ATMOSPHERE_LAYER_EXOSPHERE), "Exosphere") == 0);
}

static void TestVacuumAndInvalidInputs(void)
{
    AtmosphereInput input = AtmosphereEarthInput();
    input.surfacePressureAtm = 0;
    AtmosphereProfile vacuum;
    assert(AtmosphereProfileBuild(&input, &vacuum));
    AtmosphereSample air = AtmosphereSampleAt(&vacuum, 0);
    assert(air.valid && air.layer == ATMOSPHERE_LAYER_VACUUM);
    assert(air.pressureAtm == 0 && air.densityKgM3 == 0);
    assert(PlanetAtmosphereFlightDensity(air) == 0);
    assert(PlanetAtmosphereSkyVisibility(air) == 0);
    AtmosphereProfile earth = Earth();
    Near(AtmosphereSampleAt(&earth, -200).pressureAtm, 1.0, 1e-9);
    assert(!AtmosphereSampleAt(&earth, NAN).valid);
    assert(!AtmosphereSampleAt(NULL, 0).valid);
    assert(!AtmosphereProfileBuild(NULL, &vacuum));
    assert(!vacuum.valid);
    input.gravityMps2 = NAN;
    assert(!AtmosphereProfileBuild(&input, &vacuum));
    input = AtmosphereEarthInput();
    input.molarMassKgMol = 0;
    assert(!AtmosphereProfileBuild(&input, &vacuum));
}

static void TestGameAltitudeMapping(void)
{
    const float start = SURFACE_GENERATION_MAX_Y_EXCLUSIVE + 18.0f;
    const float top = PlanetAtmosphereTopHeight(NULL);
    Near(PlanetAtmosphereAltitudeM(NULL, 112), 100, 1e-9);
    const float fractions[] = { 0.10f, 0.30f, 0.48f, 0.70f, 0.92f };
    for (int i = 0; i < 5; i++) {
        float y = start + fractions[i] * (top - start);
        AtmosphereSample air = PlanetAtmosphereAtHeight(NULL, y);
        assert(air.valid && air.layer == (AtmosphereLayer)(i + 1));
    }
    assert(PlanetAtmosphereAtHeight(NULL, top).layer == ATMOSPHERE_LAYER_VACUUM);
    double last = 0.0;
    float lastDensity = 1.0f;
    for (float y = 12; y <= top + 2; y += 0.125f) {
        AtmosphereSample air = PlanetAtmosphereAtHeight(NULL, y);
        assert(air.altitudeM >= last);
        float density = PlanetAtmosphereFlightDensity(air);
        assert(density <= lastDensity);
        last = air.altitudeM;
        lastDensity = density;
    }
    AtmosphereSample left = PlanetAtmosphereAtHeight(NULL, start - 0.001f);
    AtmosphereSample right = PlanetAtmosphereAtHeight(NULL, start + 0.001f);
    Near(left.pressureAtm, right.pressureAtm, 0.001);
    PlanetProfile moon = { .equilibriumTempK = 220, .atmosphereType = PLANET_ATMOSPHERE_NONE };
    assert(PlanetAtmosphereAtHeight(&moon, 30).layer == ATMOSPHERE_LAYER_VACUUM);
    PlanetProfile venus = { .equilibriumTempK = 737, .surfacePressureAtm = 92,
        .surfaceGravity = 0.904f, .atmosphereDensity = 1,
        .atmosphereType = PLANET_ATMOSPHERE_CORROSIVE };
    AtmosphereSample thick = PlanetAtmosphereAtHeight(&venus, 12);
    assert(thick.pressureAtm > 90 && thick.densityKgM3 > 50 && thick.ozone == 0);
    assert(!PlanetAtmosphereAtHeight(NULL, INFINITY).valid);
}

static void TestExtremeColumnsRemainFinite(void)
{
    const double temperatures[] = { 80, 288.15, 737, 3000 };
    const double gravities[] = { 0.1, 3.71, 9.80665, 100 };
    const double masses[] = { 0.0023, 0.0289644, 0.04401 };
    for (int t = 0; t < 4; t++) for (int g = 0; g < 4; g++) {
        for (int m = 0; m < 3; m++) {
            AtmosphereInput input = AtmosphereEarthInput();
            input.surfaceTemperatureK = temperatures[t];
            input.gravityMps2 = gravities[g];
            input.molarMassKgMol = masses[m];
            input.surfacePressureAtm = 92;
            AtmosphereProfile column;
            assert(AtmosphereProfileBuild(&input, &column));
            double pressure = input.surfacePressureAtm;
            double density = column.surfaceDensityKgM3;
            for (int step = 1; step <= 200; step++) {
                AtmosphereSample air = AtmosphereSampleAt(
                    &column, column.altitudeM[11] * step / 200.0);
                assert(air.valid && isfinite(air.temperatureK));
                assert(isfinite(air.densityRatio));
                assert(air.pressureAtm >= 0 && air.pressureAtm <= pressure);
                assert(air.densityKgM3 >= 0 && air.densityKgM3 <= density);
                pressure = air.pressureAtm;
                density = air.densityKgM3;
            }
        }
    }
    AtmosphereInput trace = AtmosphereEarthInput();
    trace.surfacePressureAtm = 1e-320;
    trace.surfaceTemperatureK = 3000;
    trace.molarMassKgMol = 0.0023;
    AtmosphereProfile column;
    assert(AtmosphereProfileBuild(&trace, &column));
    assert(isfinite(AtmosphereSampleAt(&column, 0).densityRatio));
}

int main(void)
{
    TestStandardAtmosphere();
    TestContinuityAndDecay();
    TestLayersAndPlanetComposition();
    TestVacuumAndInvalidInputs();
    TestGameAltitudeMapping();
    TestExtremeColumnsRemainFinite();
    puts("atmosphere_model tests passed");
    return 0;
}
