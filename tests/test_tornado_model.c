#include "world/tornado_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static WeatherFieldSample Supercell(void)
{
    WeatherFieldSample weather = {
        .cloudGenera = WEATHER_CLOUD_GENUS_FLAG(
            WEATHER_CLOUD_GENUS_CUMULONIMBUS),
        .dominantCloudGenus = WEATHER_CLOUD_GENUS_CUMULONIMBUS,
        .temperatureK = 299.0f,
        .pressureAtm = 0.98f,
        .pressureAnomaly = -0.042f,
        .relativeHumidity = 0.91f,
        .instability = 0.92f,
        .precipitation = 0.82f,
        .storm = 0.91f,
        .wind = 0.56f,
        .gust = 0.79f,
        .windAngle = 0.35f
    };
    weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_CUMULONIMBUS] = 0.88f;
    return weather;
}

static void TestFormationPotential(void)
{
    TornadoFormationInput input = {
        .weather = Supercell(),
        .atmosphereActive = true,
        .supportsWaterCycle = true
    };
    float first = TornadoFormationPotential(&input);
    float second = TornadoFormationPotential(&input);
    assert(first == second);
    assert(first > 0.70f && first <= 1.0f);

    input.supportsWaterCycle = false;
    assert(TornadoFormationPotential(&input) == 0.0f);
    input.supportsWaterCycle = true;
    input.weather.relativeHumidity = 0.40f;
    assert(TornadoFormationPotential(&input) == 0.0f);
    input.weather = Supercell();
    input.weather.gust = input.weather.wind + 0.01f;
    assert(TornadoFormationPotential(&input) == 0.0f);
    input.weather = Supercell();
    input.weather.cloudGenusCoverage[
        WEATHER_CLOUD_GENUS_CUMULONIMBUS] = 0.0f;
    assert(TornadoFormationPotential(&input) == 0.0f);
}

static void TestLifecycleAndPath(void)
{
    WeatherFieldSample weather = Supercell();
    TornadoState first = TornadoModelCreate(
        0x1235u, 7u, (Vector3){ 10.0f, 20.0f, -4.0f }, 0.86f, 40.0f,
        false);
    TornadoState second = first;
    assert(first.active);
    assert(first.phase == TORNADO_PHASE_FORMING);
    assert(first.radius > 2.5f);

    bool sawForming = false;
    bool sawIntensifying = false;
    bool sawMature = false;
    bool sawDissipating = false;
    Vector3 previous = first.center;
    for (int index = 0; index < 401; index++) {
        TornadoModelAdvance(&first, 0.1f, weather, 20.0f);
        TornadoModelAdvance(&second, 0.1f, weather, 20.0f);
        assert(first.active == second.active);
        assert(first.phase == second.phase);
        assert(first.center.x == second.center.x);
        assert(first.center.z == second.center.z);
        assert(first.intensity == second.intensity);
        float displacement = hypotf(first.center.x - previous.x,
                                    first.center.z - previous.z);
        assert(displacement < 0.60f);
        previous = first.center;
        sawForming |= first.phase == TORNADO_PHASE_FORMING;
        sawIntensifying |= first.phase == TORNADO_PHASE_INTENSIFYING;
        sawMature |= first.phase == TORNADO_PHASE_MATURE;
        sawDissipating |= first.phase == TORNADO_PHASE_DISSIPATING;
    }
    assert(sawForming && sawIntensifying && sawMature && sawDissipating);
    assert(!first.active);
    assert(first.phase == TORNADO_PHASE_INACTIVE);
    assert(first.intensity == 0.0f);
}

static void TestForceField(void)
{
    WeatherFieldSample weather = Supercell();
    TornadoState state = TornadoModelCreate(
        1u, 1u, (Vector3){ 0.0f, 10.0f, 0.0f }, 1.0f, 60.0f, true);
    for (int index = 0; index < 90; index++) {
        TornadoModelAdvance(&state, 0.1f, weather, 10.0f);
    }
    assert(state.phase == TORNADO_PHASE_MATURE);
    Vector3 east = {
        state.center.x + state.radius,
        state.center.y + 1.0f,
        state.center.z
    };
    TornadoForceSample force = TornadoModelForceAt(&state, east);
    assert(force.exposure > 0.90f);
    assert(force.localWindMps > 70.0f);
    assert(force.acceleration.x < 0.0f);
    assert(force.acceleration.z > 0.0f);
    assert(force.acceleration.y > 0.0f);
    float magnitude = sqrtf(
        force.acceleration.x * force.acceleration.x +
        force.acceleration.y * force.acceleration.y +
        force.acceleration.z * force.acceleration.z);
    assert(magnitude <= 28.0001f);

    TornadoForceSample far = TornadoModelForceAt(
        &state, (Vector3){ state.center.x + state.influenceRadius + 0.1f,
                           state.center.y, state.center.z });
    assert(far.exposure == 0.0f);
    assert(far.acceleration.x == 0.0f);
    TornadoForceSample above = TornadoModelForceAt(
        &state, (Vector3){ state.center.x, state.center.y +
                           state.funnelHeight * 1.2f, state.center.z });
    assert(above.exposure == 0.0f);
}

int main(void)
{
    TestFormationPotential();
    TestLifecycleAndPath();
    TestForceField();
    assert(strcmp(TornadoPhaseName(TORNADO_PHASE_MATURE), "mature") == 0);
    puts("tornado model tests passed");
    return 0;
}
