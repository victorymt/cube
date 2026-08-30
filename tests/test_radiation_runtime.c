#include "world/radiation_runtime.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void AssertNear(double actual, double expected, double tolerance)
{
    assert(fabs(actual - expected) <= tolerance * fmax(fabs(expected), 1.0));
}

static PlanetProfile TestProfile(void)
{
    return (PlanetProfile){
        .receivedIrradiance = 1.0,
        .equilibriumTempK = 288.0f,
        .albedo = 0.30f,
        .greenhouseEffect = 0.40f,
        .orbitalEccentricity = 0.0167f,
        .orbitalMeanAnomalyAtEpoch = 0.0f,
        .axialTilt = 23.44f * 3.14159265358979323846f / 180.0f,
        .seasonPhase = 0.0f,
        .yearLength = 365.25f,
        .rotationRate = 360.0f / 1.0f
    };
}

static RadiationRuntimeLocationTime TestLocation(double time)
{
    return (RadiationRuntimeLocationTime){
        .latitudeRad = 0.4,
        .longitudeRad = -0.6,
        .simulationTimeGameTime = time
    };
}

static void TestDefaultsAndState(void)
{
    RadiationRuntime runtime;
    assert(RadiationRuntimeInit(&runtime, NULL));
    assert(RadiationRuntimeStateIsValid(&runtime));
    assert(runtime.version == RADIATION_RUNTIME_STATE_VERSION);
    assert(runtime.config.fixedStepSeconds > 0.0);
    assert(runtime.column.temperatureK ==
           RADIATION_RUNTIME_DEFAULT_INITIAL_TEMPERATURE_K);
    RadiationRuntimeSnapshot snapshot;
    assert(!RadiationRuntimeGetSnapshot(&runtime, &snapshot));
}

static void TestBuildForcingAndDeterminism(void)
{
    PlanetProfile profile = TestProfile();
    RadiationRuntimeConfig config = RadiationRuntimeDefaultConfig();
    RadiationRuntimeLocationTime location = TestLocation(12.0);
    RadiationBalanceInput first;
    RadiationBalanceInput second;
    assert(RadiationRuntimeBuildForcing(&profile, &location, &config,
                                        &first));
    assert(RadiationRuntimeBuildForcing(&profile, &location, &config,
                                        &second));
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    assert(RadiationRuntimeBuildForcingAtTemperature(
        &profile, &location, &config, 301.0, &second));
    assert(second.surfaceTemperatureK == 301.0);
    assert(first.latitudeRad == location.latitudeRad);
    assert(first.greenhouseOpticalDepth == profile.greenhouseEffect);
    assert(first.localHourAngleRad >= -3.14159265358979323846 &&
           first.localHourAngleRad <= 3.14159265358979323846);
}

static void TestFixedStepAndEnergy(void)
{
    PlanetProfile profile = TestProfile();
    RadiationRuntime runtime;
    assert(RadiationRuntimeInit(&runtime, NULL));
    RadiationRuntimeLocationTime location = TestLocation(0.0);
    double before = runtime.column.energyJm2;
    double dt = runtime.config.fixedStepSeconds;
    assert(RadiationRuntimeStep(&runtime, &profile, &location, dt));
    RadiationRuntimeSnapshot snapshot;
    assert(RadiationRuntimeGetSnapshot(&runtime, &snapshot));
    AssertNear(runtime.column.energyJm2,
               before + snapshot.balance.netRadiativeFluxWm2 * dt, 1e-12);
    assert(snapshot.stepCount == 1u);
    assert(!RadiationRuntimeStep(&runtime, &profile, &location, dt * 2.0));
    assert(RadiationRuntimeStateIsValid(&runtime));
}

static void TestAdvanceAccumulatorAndReplay(void)
{
    PlanetProfile profile = TestProfile();
    RadiationRuntime first;
    RadiationRuntime second;
    assert(RadiationRuntimeInit(&first, NULL));
    assert(RadiationRuntimeInit(&second, NULL));
    RadiationRuntimeLocationTime location = TestLocation(10.0);
    unsigned steps = 0;
    assert(RadiationRuntimeAdvance(&first, &profile, &location, 0.010,
                                   &steps));
    assert(steps == 0u);
    assert(RadiationRuntimeAdvance(&first, &profile, &location, 0.050,
                                   &steps));
    assert(steps == 3u);
    assert(RadiationRuntimeAdvance(&second, &profile, &location, 0.060,
                                   &steps));
    assert(steps == 3u);
    assert(memcmp(&first.column, &second.column, sizeof(first.column)) == 0);
    assert(first.snapshot.stepCount == second.snapshot.stepCount);
}

static void TestInvalidInputsClearAndPreserve(void)
{
    RadiationRuntimeConfig config = RadiationRuntimeDefaultConfig();
    config.fixedStepSeconds = NAN;
    assert(!RadiationRuntimeConfigIsValid(&config));
    assert(RadiationRuntimeConfigNormalize(&config));
    RadiationRuntime runtime;
    assert(RadiationRuntimeInit(&runtime, NULL));
    RadiationRuntime before = runtime;
    PlanetProfile profile = TestProfile();
    RadiationRuntimeLocationTime location = TestLocation(0.0);
    location.latitudeRad = NAN;
    assert(!RadiationRuntimeStep(&runtime, &profile, &location,
                                 runtime.config.fixedStepSeconds));
    assert(memcmp(&runtime, &before, sizeof(runtime)) == 0);
    location = TestLocation(0.0);
    profile.albedo = 2.0f;
    assert(!RadiationRuntimeStep(&runtime, &profile, &location,
                                 runtime.config.fixedStepSeconds));
    assert(RadiationRuntimeStateIsValid(&runtime));
    runtime.version++;
    assert(!RadiationRuntimeStateIsValid(&runtime));
}

static void TestGlobalStateRoundTrip(void)
{
    PlanetProfile profile = TestProfile();
    RadiationRuntimeLocationTime location = TestLocation(4.0);
    unsigned steps = 0;
    assert(RadiationRuntimeGlobalReset(NULL));
    assert(RadiationRuntimeGlobalBind(42u, -1, -128, 288.0));
    assert(RadiationRuntimeGlobalAdvance(&profile, &location, 0.1, &steps));
    assert(steps > 0u);
    double offset = 0.0;
    assert(RadiationRuntimeGlobalTemperatureOffsetAt(42u, -127, -1,
                                                      &offset));
    assert(isfinite(offset));
    assert(!RadiationRuntimeGlobalTemperatureOffsetAt(42u, 0, -1,
                                                       &offset));
    assert(offset == 0.0);
    RadiationRuntimeSnapshot before;
    assert(RadiationRuntimeGlobalSnapshot(&before));
    FILE *file = tmpfile();
    assert(file);
    assert(RadiationRuntimeGlobalSaveState(file));
    assert(fseek(file, 0, SEEK_SET) == 0);
    assert(RadiationRuntimeGlobalReset(NULL));
    assert(RadiationRuntimeGlobalLoadState(file));
    RadiationRuntimeSnapshot after;
    assert(RadiationRuntimeGlobalSnapshot(&after));
    assert(memcmp(&before, &after, sizeof(before)) == 0);
    fclose(file);

    file = tmpfile();
    assert(file);
    uint32_t wrongMagic[3] = { 0u, RADIATION_RUNTIME_STATE_VERSION, 3u };
    assert(fwrite(wrongMagic, sizeof(wrongMagic), 1u, file) == 1u);
    assert(fseek(file, 0, SEEK_SET) == 0);
    assert(!RadiationRuntimeGlobalLoadState(file));
    RadiationRuntimeSnapshot preserved;
    assert(RadiationRuntimeGlobalSnapshot(&preserved));
    assert(memcmp(&after, &preserved, sizeof(after)) == 0);
    fclose(file);

    assert(RadiationRuntimeGlobalBind(42u, 0, -1, 275.0));
    assert(!RadiationRuntimeGlobalSnapshot(&preserved));
}

int main(void)
{
    TestDefaultsAndState();
    TestBuildForcingAndDeterminism();
    TestFixedStepAndEnergy();
    TestAdvanceAccumulatorAndReplay();
    TestInvalidInputsClearAndPreserve();
    TestGlobalStateRoundTrip();
    puts("radiation runtime tests passed");
    return 0;
}
