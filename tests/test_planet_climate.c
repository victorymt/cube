#include "planet_climate.h"

#include <assert.h>
#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static PlanetClimateInput EarthLikeInput(void)
{
    return (PlanetClimateInput){
        .stellarIrradianceEarth = 1.0,
        .volatileInventory = 0.68f,
        .greenhouseGasFraction = 0.42f,
        .surfaceReflectivity = 0.16f,
        .surfaceGravityEarth = 1.0f,
        .rotationRate = 1.0f,
        .tidalLockFactor = 0.0f,
        .gasGiant = false
    };
}

static PlanetClimateState Solve(PlanetClimateInput input)
{
    PlanetClimateState state;
    assert(PlanetClimateSolve(&input, &state));
    return state;
}

static void AssertUnit(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f && value <= 1.0f);
}

static void AssertValid(PlanetClimateInput input, PlanetClimateState state)
{
    assert(isfinite(state.surfacePressureAtm) && state.surfacePressureAtm >= 0.0f);
    AssertUnit(state.atmosphereDensity);
    assert(isfinite(state.albedo) && state.albedo >= 0.03f && state.albedo <= 0.88f);
    assert(isfinite(state.greenhouseOpticalDepth));
    assert(state.greenhouseOpticalDepth >= 0.0f &&
           state.greenhouseOpticalDepth <= 3.0f);
    assert(isfinite(state.radiativeTemperatureK) && state.radiativeTemperatureK > 0.0f);
    assert(isfinite(state.surfaceTemperatureK));
    assert(state.surfaceTemperatureK >= state.radiativeTemperatureK);
    AssertUnit(state.liquidWaterCoverage);
    AssertUnit(state.iceCoverage);
    AssertUnit(state.cloudCoverage);
    AssertUnit(state.windStrength);
    double expectedAbsorbed = input.stellarIrradianceEarth * (1.0 - state.albedo);
    assert(fabs(state.absorbedIrradianceEarth - expectedAbsorbed) < 1e-6 *
           fmax(expectedAbsorbed, 1.0));
}

static void TestGreenhouseWarmsSurface(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.greenhouseGasFraction = 0.05f;
    PlanetClimateState weak = Solve(input);
    input.greenhouseGasFraction = 0.90f;
    PlanetClimateState strong = Solve(input);
    assert(strong.greenhouseOpticalDepth > weak.greenhouseOpticalDepth);
    assert(strong.surfaceTemperatureK > weak.surfaceTemperatureK + 8.0f);
}

static void TestIrradianceControlsTemperature(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.stellarIrradianceEarth = 0.25;
    PlanetClimateState cold = Solve(input);
    input.stellarIrradianceEarth = 1.0;
    PlanetClimateState temperate = Solve(input);
    input.stellarIrradianceEarth = 4.0;
    PlanetClimateState hot = Solve(input);
    assert(cold.radiativeTemperatureK < temperate.radiativeTemperatureK);
    assert(temperate.radiativeTemperatureK < hot.radiativeTemperatureK);
    assert(cold.surfaceTemperatureK < temperate.surfaceTemperatureK);
    assert(temperate.surfaceTemperatureK < hot.surfaceTemperatureK);
    assert(cold.iceCoverage > cold.liquidWaterCoverage);
}

static void TestAlbedoControlsAbsorption(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.surfaceReflectivity = 0.06f;
    PlanetClimateState dark = Solve(input);
    input.surfaceReflectivity = 0.62f;
    PlanetClimateState bright = Solve(input);
    assert(bright.albedo > dark.albedo);
    assert(bright.absorbedIrradianceEarth < dark.absorbedIrradianceEarth);
    assert(bright.radiativeTemperatureK < dark.radiativeTemperatureK);
    assert(bright.surfaceTemperatureK < dark.surfaceTemperatureK);
}

static void TestVolatilesCauseAtmosphereAndWater(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.volatileInventory = 0.0f;
    PlanetClimateState dry = Solve(input);
    assert(dry.surfacePressureAtm == 0.0f);
    assert(dry.atmosphereDensity == 0.0f);
    assert(dry.greenhouseOpticalDepth == 0.0f);
    assert(dry.liquidWaterCoverage == 0.0f);
    assert(dry.iceCoverage == 0.0f);
    assert(dry.cloudCoverage == 0.0f);

    input.volatileInventory = 0.80f;
    PlanetClimateState wet = Solve(input);
    assert(wet.surfacePressureAtm > dry.surfacePressureAtm);
    assert(wet.liquidWaterCoverage + wet.iceCoverage > 0.10f);
    assert(wet.cloudCoverage > 0.0f);
}

static void TestGravityRetainsAtmosphere(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.surfaceGravityEarth = 0.18f;
    PlanetClimateState lowGravity = Solve(input);
    input.surfaceGravityEarth = 1.60f;
    PlanetClimateState highGravity = Solve(input);
    assert(highGravity.surfacePressureAtm > lowGravity.surfacePressureAtm);
    assert(highGravity.atmosphereDensity > lowGravity.atmosphereDensity);
    assert(highGravity.greenhouseOpticalDepth > lowGravity.greenhouseOpticalDepth);
    assert(highGravity.surfaceTemperatureK > lowGravity.surfaceTemperatureK);
}

static void TestTiltDrivesSeasonalAmplitude(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.axialTiltRad = 0.0f;
    PlanetClimateState flat = Solve(input);
    input.axialTiltRad = 0.41f;
    PlanetClimateState tilted = Solve(input);
    assert(flat.seasonalTemperatureAmplitudeK < 0.001f);
    assert(tilted.seasonalTemperatureAmplitudeK >
           flat.seasonalTemperatureAmplitudeK + 8.0f);
    assert(tilted.polarIceVariability >= flat.polarIceVariability);
}

static void TestEccentricityDrivesOrbitalAmplitude(void)
{
    PlanetClimateInput input = EarthLikeInput();
    input.orbitalEccentricity = 0.0f;
    PlanetClimateState circular = Solve(input);
    input.orbitalEccentricity = 0.24f;
    PlanetClimateState eccentric = Solve(input);
    assert(circular.orbitalTemperatureAmplitudeK < 0.001f);
    assert(eccentric.orbitalTemperatureAmplitudeK >
           circular.orbitalTemperatureAmplitudeK + 4.0f);
}

static void TestGeneratedDomain(void)
{
    uint32_t state = 0x12345678u;
    for (int sample = 0; sample < 10000; sample++) {
        state = state * 1664525u + 1013904223u;
        PlanetClimateInput input = {
            .stellarIrradianceEarth = 0.01 + (double)(state & 0xffffu) / 4096.0,
            .volatileInventory = (float)((state >> 8) & 255u) / 255.0f,
            .greenhouseGasFraction = (float)((state >> 16) & 255u) / 255.0f,
            .surfaceReflectivity = 0.04f +
                (float)((state >> 4) & 255u) / 255.0f * 0.58f,
            .surfaceGravityEarth = 0.08f +
                (float)((state >> 12) & 255u) / 255.0f * 3.5f,
            .rotationRate = (float)((state >> 20) & 255u) / 32.0f,
            .tidalLockFactor = (float)((state >> 24) & 255u) / 255.0f,
            .gasGiant = (state % 13u) == 0u
        };
        PlanetClimateState first = Solve(input);
        PlanetClimateState second = Solve(input);
        AssertValid(input, first);
        assert(first.surfacePressureAtm == second.surfacePressureAtm);
        assert(first.albedo == second.albedo);
        assert(first.surfaceTemperatureK == second.surfaceTemperatureK);
        assert(first.cloudCoverage == second.cloudCoverage);
    }
}

static void TestInvalidInputClearsOutput(void)
{
    PlanetClimateInput input = EarthLikeInput();
    PlanetClimateState state;
    const PlanetClimateState cleared = { 0 };
    memset(&state, 0xa5, sizeof(state));
    assert(!PlanetClimateSolve(NULL, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);

    input.stellarIrradianceEarth = NAN;
    memset(&state, 0xa5, sizeof(state));
    assert(!PlanetClimateSolve(&input, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);

    input = EarthLikeInput();
    input.stellarIrradianceEarth = 0.0;
    memset(&state, 0xa5, sizeof(state));
    assert(!PlanetClimateSolve(&input, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);

    input = EarthLikeInput();
    input.stellarIrradianceEarth = DBL_MAX;
    memset(&state, 0xa5, sizeof(state));
    assert(!PlanetClimateSolve(&input, &state));
    assert(memcmp(&state, &cleared, sizeof(state)) == 0);
    assert(!PlanetClimateSolve(&input, NULL));
}

int main(void)
{
    TestGreenhouseWarmsSurface();
    TestIrradianceControlsTemperature();
    TestAlbedoControlsAbsorption();
    TestVolatilesCauseAtmosphereAndWater();
    TestGravityRetainsAtmosphere();
    TestTiltDrivesSeasonalAmplitude();
    TestEccentricityDrivesOrbitalAmplitude();
    TestGeneratedDomain();
    TestInvalidInputClearsOutput();
    puts("planet_climate tests passed");
    return 0;
}
