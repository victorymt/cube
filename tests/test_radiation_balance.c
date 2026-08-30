#include "world/radiation_balance.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_PI 3.14159265358979323846

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance * fmax(fabs(expected), 1.0));
}

static RadiationBalanceInput EarthInput(void)
{
    return (RadiationBalanceInput){
        .stellarLuminositySolar = 1.0,
        .semiMajorAxisAu = 1.0,
        .orbitalEccentricity = 0.0167,
        .orbitalMeanAnomalyRad = 0.0,
        .seasonPhaseRad = 0.0,
        .axialTiltRad = 23.44 * TEST_PI / 180.0,
        .latitudeRad = 0.0,
        .localHourAngleRad = 0.0,
        .albedo = 0.30,
        .longwaveEmissivity = 1.0,
        .greenhouseOpticalDepth = 0.0,
        .surfaceTemperatureK = 255.0
    };
}

static void TestKeplerAndOrbit(void)
{
    AssertNear(RadiationSolveKepler(0.0, 0.0), 0.0, 1e-14);
    AssertNear(RadiationSolveKepler(TEST_PI * 0.5, 0.0), TEST_PI * 0.5, 1e-14);
    double eccentricity = 0.2;
    double periapsis = RadiationOrbitalDistanceAu(
        2.0, eccentricity, RadiationSolveKepler(0.0, eccentricity));
    double apoapsis = RadiationOrbitalDistanceAu(
        2.0, eccentricity, RadiationSolveKepler(TEST_PI, eccentricity));
    AssertNear(periapsis, 1.6, 1e-12);
    AssertNear(apoapsis, 2.4, 1e-12);
    assert(isnan(RadiationSolveKepler(0.0, 1.0)));
    assert(RadiationOrbitalDistanceAu(0.0, 0.0, 0.0) == 0.0);
}

static void TestSolarGeometry(void)
{
    double equinoxDeclination = RadiationSolarDeclinationRad(0.4, 0.0);
    AssertNear(equinoxDeclination, 0.0, 1e-14);
    AssertNear(RadiationSolarZenithCosine(0.0, 0.0, 0.0), 1.0, 1e-14);
    AssertNear(RadiationSolarZenithCosine(0.0, 0.0, TEST_PI), -1.0, 1e-14);
    double equatorDaily = RadiationDailyMeanInsolationWm2(
        RADIATION_SOLAR_CONSTANT_WM2, 0.0, 0.0);
    AssertNear(equatorDaily, RADIATION_SOLAR_CONSTANT_WM2 / TEST_PI, 1e-12);
    assert(RadiationDailyMeanInsolationWm2(
               1000.0, 80.0 * TEST_PI / 180.0, -23.0 * TEST_PI / 180.0) ==
           0.0);
    double polarDay = RadiationDailyMeanInsolationWm2(
        1000.0, 80.0 * TEST_PI / 180.0, 23.0 * TEST_PI / 180.0);
    assert(polarDay > 0.0);
}

static void TestRadiativeBalance(void)
{
    RadiationBalanceInput input = EarthInput();
    RadiationBalanceOutput output;
    assert(RadiationBalanceEvaluate(&input, &output));
    AssertNear(output.solarIrradianceWm2,
               1361.0 / ((1.0 - 0.0167) * (1.0 - 0.0167)), 1e-12);
    AssertNear(output.absorbedShortwaveWm2,
               output.dailyMeanInsolationWm2 * 0.70, 1e-12);
    assert(output.outgoingLongwaveWm2 > 0.0);
    input.greenhouseOpticalDepth = 2.0;
    assert(RadiationBalanceEvaluate(&input, &output));
    assert(output.effectiveLongwaveEmissivity < 1.0);
    assert(output.equilibriumTemperatureK > 0.0);
    input.localHourAngleRad = TEST_PI;
    assert(RadiationBalanceEvaluate(&input, &output));
    assert(output.instantaneousInsolationWm2 == 0.0);
}

static void TestColumnConservesEnergy(void)
{
    RadiationColumnState state;
    assert(RadiationColumnStateInit(&state, 280.0, 10000.0));
    RadiationBalanceInput input = EarthInput();
    RadiationBalanceOutput output;
    double energyBefore = state.energyJm2;
    assert(RadiationColumnStateStep(&state, &input, 60.0, &output));
    double expectedEnergy = energyBefore + output.netRadiativeFluxWm2 * 60.0;
    AssertNear(state.energyJm2, expectedEnergy, 1e-12);
    AssertNear(state.temperatureK, state.energyJm2 / state.heatCapacityJm2K,
               1e-14);
    input.albedo = 0.0;
    assert(RadiationColumnStateStep(&state, &input, 60.0, &output));
    assert(state.energyJm2 > expectedEnergy);
}

static void TestInvalidInputClearsOutputs(void)
{
    RadiationBalanceInput input = EarthInput();
    RadiationBalanceOutput output;
    memset(&output, 0xa5, sizeof(output));
    input.albedo = 1.1;
    assert(!RadiationBalanceEvaluate(&input, &output));
    RadiationBalanceOutput cleared = { 0 };
    assert(memcmp(&output, &cleared, sizeof(output)) == 0);
    RadiationColumnState state;
    memset(&state, 0xa5, sizeof(state));
    assert(!RadiationColumnStateInit(&state, 0.0, 100.0));
    assert(memcmp(&state, &(RadiationColumnState){ 0 }, sizeof(state)) == 0);
}

int main(void)
{
    TestKeplerAndOrbit();
    TestSolarGeometry();
    TestRadiativeBalance();
    TestColumnConservesEnergy();
    TestInvalidInputClearsOutputs();
    puts("radiation balance tests passed");
    return 0;
}
