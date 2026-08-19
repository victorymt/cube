#include "world/wildfire_model.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static WildfireEnvironment DryWind(void)
{
    return (WildfireEnvironment){
        .temperatureK = 309.0f,
        .relativeHumidity = 0.18f,
        .wind = 0.72f,
        .gust = 0.88f
    };
}

static void AssertUnit(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f && value <= 1.0f);
}

static void TestMoistureAndIgnition(void)
{
    WildfireEnvironment dry = DryWind();
    WildfireEnvironment wet = dry;
    wet.relativeHumidity = 1.0f;
    wet.rain = 1.0f;
    wet.waterExposure = 1.0f;
    float dryMoisture = WildfireEquilibriumMoisture(dry);
    float wetMoisture = WildfireEquilibriumMoisture(wet);
    AssertUnit(dryMoisture);
    AssertUnit(wetMoisture);
    assert(wetMoisture > dryMoisture);
    assert(WildfireCanIgnite(0.9f, dryMoisture, 1.0f));
    assert(!WildfireCanIgnite(0.9f, 0.9f, 1.0f));
    assert(!WildfireCanIgnite(0.0f, 0.0f, 1.0f));
}

static void TestLifecycleAndSuppression(void)
{
    WildfireEnvironment dry = DryWind();
    WildfireState fire = WildfireModelCreate(0.92f, 1.8f, 0.12f, 0.8f);
    assert(fire.phase != WILDFIRE_PHASE_INACTIVE);
    float previousFuel = fire.fuel;
    for (int step = 0; step < 12; step++) {
        WildfireModelAdvance(&fire, 0.5f, 0.92f, dry);
        assert(fire.fuel <= previousFuel);
        previousFuel = fire.fuel;
        AssertUnit(fire.intensity);
        AssertUnit(fire.moisture);
        AssertUnit(fire.heatOutput);
        AssertUnit(fire.smokeOutput);
    }
    assert(fire.phase == WILDFIRE_PHASE_FLAMING);
    WildfireModelApplySuppression(&fire, 0.8f);
    assert(fire.phase == WILDFIRE_PHASE_SMOLDERING ||
           fire.phase == WILDFIRE_PHASE_INACTIVE);
    assert(fire.moisture > 0.4f);
    WildfireEnvironment wet = dry;
    wet.rain = 1.0f;
    wet.relativeHumidity = 1.0f;
    wet.waterExposure = 1.0f;
    for (int step = 0; step < 80; step++) {
        WildfireModelAdvance(&fire, 0.5f, 0.92f, wet);
    }
    assert(fire.phase == WILDFIRE_PHASE_INACTIVE);
}

static void TestDirectionalSpread(void)
{
    WildfireSpreadInput input = {
        .sourceIntensity = 0.9f,
        .targetFlammability = 0.9f,
        .targetMoisture = 0.1f,
        .wind = 0.8f,
        .gust = 0.9f,
        .windAngle = 0.0f,
        .offsetX = 1.0f,
        .slope = 0.5f
    };
    float downwindUphill = WildfireSpreadProbability(input);
    input.offsetX = -1.0f;
    input.slope = -0.5f;
    float backingDownhill = WildfireSpreadProbability(input);
    AssertUnit(downwindUphill);
    AssertUnit(backingDownhill);
    assert(downwindUphill > backingDownhill);
    input.targetMoisture = 1.0f;
    assert(WildfireSpreadProbability(input) == 0.0f);
}

static void TestExposureAndProperties(void)
{
    WildfireState fire = WildfireModelCreate(1.0f, 1.0f, 0.05f, 1.0f);
    float nearHeat = WildfireHeatExposure(&fire, 0.0f, 0.0f, 0.0f);
    float farHeat = WildfireHeatExposure(&fire, 20.0f, 0.0f, 0.0f);
    assert(nearHeat > farHeat);
    assert(WildfireHeatExposure(&fire, 0.0f, 1.0f, 1.0f) < nearHeat);
    assert(WildfireSmokeExposure(&fire, 0.0f, 0.0f, 1.0f) <
           WildfireSmokeExposure(&fire, 0.0f, 0.0f, 0.0f));

    WildfireState restored = fire;
    restored.heatOutput = -100.0f;
    restored.smokeOutput = NAN;
    assert(WildfireModelNormalize(&restored));
    AssertUnit(restored.heatOutput);
    AssertUnit(restored.smokeOutput);
    restored.fuel = NAN;
    assert(!WildfireModelNormalize(&restored));

    for (int sample = 0; sample < 20000; sample++) {
        float u = (float)(sample % 101) / 100.0f;
        float v = (float)((sample * 37) % 101) / 100.0f;
        WildfireEnvironment environment = {
            .temperatureK = 180.0f + u * 260.0f,
            .relativeHumidity = v,
            .rain = u,
            .wind = v,
            .gust = fmaxf(u, v),
            .waterExposure = (float)((sample * 13) % 101) / 100.0f,
            .suppression = (float)((sample * 17) % 101) / 100.0f
        };
        AssertUnit(WildfireEquilibriumMoisture(environment));
        WildfireSpreadInput spread = {
            .sourceIntensity = u,
            .targetFlammability = v,
            .targetMoisture = 1.0f - u,
            .wind = v,
            .gust = fmaxf(u, v),
            .windAngle = u * 6.283185307f,
            .offsetX = sample % 2 ? 1.0f : -1.0f,
            .offsetZ = sample % 3 ? 0.5f : -0.5f,
            .slope = u * 2.0f - 1.0f
        };
        AssertUnit(WildfireSpreadProbability(spread));
    }
}

int main(void)
{
    TestMoistureAndIgnition();
    TestLifecycleAndSuppression();
    TestDirectionalSpread();
    TestExposureAndProperties();
    puts("wildfire model tests passed");
    return 0;
}
