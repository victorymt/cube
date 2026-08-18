#include "world/weather_model.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static WeatherFieldInput TemperateInput(void)
{
    WeatherFieldInput input = { 0 };
    input.seed = 0x71ac39e5u;
    input.simulationTime = 135.25;
    input.worldX = 218.0f;
    input.worldZ = -473.0f;
    input.temperatureK = 288.0f;
    input.moisture = 0.68f;
    input.cloudPotential = 0.54f;
    input.windStrength = 0.46f;
    input.prevailingWindAngle = 0.73f;
    input.surfacePressureAtm = 1.0f;
    input.atmosphereDensity = 0.72f;
    input.relativeHumidity = 0.68f;
    input.dewPointK = 282.0f;
    input.wetBulbK = 284.0f;
    input.instability = 0.46f;
    input.orographicLift = 0.12f;
    input.aridity = 0.28f;
    input.dustAvailability = 0.20f;
    input.latitude = 0.48f;
    input.magneticLatitude = 0.62f;
    input.magneticFieldStrength = 0.54f;
    input.daylight = 0.72f;
    input.solarElevation = 0.42f;
    input.atmosphereActive = true;
    input.supportsWaterCycle = true;
    return input;
}

static void AssertUnit(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f);
    assert(value <= 1.0f);
}

static void AssertValid(WeatherFieldSample sample)
{
    AssertUnit(sample.cloudCover);
    AssertUnit(sample.precipitation);
    AssertUnit(sample.rain);
    AssertUnit(sample.snow);
    AssertUnit(sample.storm);
    AssertUnit(sample.wind);
    AssertUnit(sample.drizzle);
    AssertUnit(sample.sleet);
    AssertUnit(sample.freezingRain);
    AssertUnit(sample.hail);
    AssertUnit(sample.lightning);
    AssertUnit(sample.fog);
    AssertUnit(sample.frost);
    AssertUnit(sample.dust);
    AssertUnit(sample.gust);
    AssertUnit(sample.visibility);
    AssertUnit(sample.heatWave);
    AssertUnit(sample.coldSnap);
    AssertUnit(sample.rainbow);
    AssertUnit(sample.aurora);
    assert(isfinite(sample.temperatureK));
    assert(isfinite(sample.pressureAtm));
    assert(isfinite(sample.dewPointK));
    assert(isfinite(sample.wetBulbK));
    assert(fabsf(sample.rain + sample.snow + sample.sleet +
                 sample.freezingRain + sample.hail - sample.precipitation) <
           0.00001f);
    assert(sample.storm <= sample.precipitation + 0.00001f);
    assert(sample.drizzle <= sample.rain + 0.00001f);
    assert(sample.phenomena != 0u);
}

static float SampleDistance(WeatherFieldSample left, WeatherFieldSample right)
{
    return fabsf(left.cloudCover - right.cloudCover) +
           fabsf(left.precipitation - right.precipitation) +
           fabsf(left.rain - right.rain) + fabsf(left.snow - right.snow) +
           fabsf(left.storm - right.storm) + fabsf(left.wind - right.wind);
}

static void TestDeterministicSampling(void)
{
    WeatherFieldInput input = TemperateInput();
    WeatherFieldSample first = WeatherFieldSampleAt(&input);
    WeatherFieldSample second = WeatherFieldSampleAt(&input);
    assert(memcmp(&first, &second, sizeof(first)) == 0);
    AssertValid(first);
}

static void TestSkyFactorUsesLocalWeather(void)
{
    WeatherFieldSample clear = { 0 };
    clear.cloudCover = 0.20f;
    WeatherFieldSample storm = clear;
    storm.cloudCover = 0.90f;
    storm.precipitation = 0.80f;
    storm.storm = 0.70f;

    float clearFactor = WeatherFieldSkyFactor(clear);
    float stormFactor = WeatherFieldSkyFactor(storm);
    AssertUnit(clearFactor);
    AssertUnit(stormFactor);
    assert(fabsf(clearFactor - 0.11f) < 0.00001f);
    assert(stormFactor > clearFactor + 0.65f);

    WeatherFieldSample invalidRange = { 0 };
    invalidRange.cloudCover = 4.0f;
    invalidRange.precipitation = 4.0f;
    invalidRange.storm = 4.0f;
    assert(WeatherFieldSkyFactor(invalidRange) == 1.0f);
}

static void TestSpatialAndTemporalContinuity(void)
{
    WeatherFieldInput input = TemperateInput();
    WeatherFieldSample baseline = WeatherFieldSampleAt(&input);
    input.worldX += 0.10f;
    input.worldZ -= 0.10f;
    WeatherFieldSample nearby = WeatherFieldSampleAt(&input);
    assert(SampleDistance(baseline, nearby) < 0.02f);

    input = TemperateInput();
    input.simulationTime += 0.01;
    WeatherFieldSample momentLater = WeatherFieldSampleAt(&input);
    assert(SampleDistance(baseline, momentLater) < 0.02f);
}

static void TestWeatherVariesAcrossSpaceAndTime(void)
{
    WeatherFieldInput input = TemperateInput();
    input.moisture = 0.78f;
    float cloudMin = 1.0f;
    float cloudMax = 0.0f;
    float precipitationMin = 1.0f;
    float precipitationMax = 0.0f;
    for (int index = 0; index < 200; index++) {
        input.worldX = (float)(index * 311 - 12000);
        input.worldZ = (float)(index * index * 17 - 5000);
        input.simulationTime = 20.0 + (double)index * 13.7;
        WeatherFieldSample sample = WeatherFieldSampleAt(&input);
        cloudMin = fminf(cloudMin, sample.cloudCover);
        cloudMax = fmaxf(cloudMax, sample.cloudCover);
        precipitationMin = fminf(precipitationMin, sample.precipitation);
        precipitationMax = fmaxf(precipitationMax, sample.precipitation);
    }
    assert(cloudMax - cloudMin > 0.20f);
    assert(precipitationMax - precipitationMin > 0.60f);
}

static void TestTemperatureSelectsRainOrSnow(void)
{
    WeatherFieldInput warm = TemperateInput();
    warm.moisture = 1.0f;
    warm.cloudPotential = 1.0f;
    warm.temperatureK = 305.0f;
    WeatherFieldInput cold = warm;
    cold.temperatureK = 250.0f;
    WeatherFieldSample rain = WeatherFieldSampleAt(&warm);
    WeatherFieldSample snow = WeatherFieldSampleAt(&cold);
    assert(rain.precipitation > 0.50f);
    assert(snow.precipitation > 0.50f);
    assert(rain.rain > rain.snow);
    assert(snow.snow > snow.rain);
}

static void TestMixedPrecipitationAndPhysicalGates(void)
{
    WeatherFieldInput input = TemperateInput();
    input.moisture = 1.0f;
    input.cloudPotential = 1.0f;
    input.relativeHumidity = 0.98f;
    input.temperatureK = 278.0f;
    WeatherFieldSample mixed = WeatherFieldSampleAt(&input);
    assert(mixed.precipitation > 0.40f);
    assert(mixed.sleet + mixed.freezingRain > 0.02f);

    input.supportsWaterCycle = false;
    WeatherFieldSample dryAtmosphere = WeatherFieldSampleAt(&input);
    assert(dryAtmosphere.precipitation == 0.0f);
    assert(dryAtmosphere.rain == 0.0f);
    assert(dryAtmosphere.snow == 0.0f);

    input.atmosphereActive = false;
    WeatherFieldSample vacuum = WeatherFieldSampleAt(&input);
    assert(vacuum.visibility == 1.0f);
    assert(WeatherSampleHasPhenomenon(
        vacuum, WEATHER_PHENOMENON_CLEAR));
}

static void TestPhenomenonNames(void)
{
    WeatherPhenomenon phenomenon = WEATHER_PHENOMENON_CLEAR;
    assert(WeatherPhenomenonFromName("freezing-rain", &phenomenon));
    assert(phenomenon == WEATHER_PHENOMENON_FREEZING_RAIN);
    assert(WeatherPhenomenonFromName("DUST_STORM", &phenomenon));
    assert(phenomenon == WEATHER_PHENOMENON_DUST_STORM);
    assert(!WeatherPhenomenonFromName("not-weather", &phenomenon));
    assert(strcmp(WeatherPhenomenonName(WEATHER_PHENOMENON_AURORA),
                  "Aurora") == 0);
}

static void TestClimateControlsPrecipitationAndStorms(void)
{
    float dryPrecipitation = 0.0f;
    float wetPrecipitation = 0.0f;
    float calmStorm = 0.0f;
    float windyStorm = 0.0f;
    const int count = 1000;
    for (int index = 0; index < count; index++) {
        WeatherFieldInput dry = TemperateInput();
        dry.seed += (uint32_t)index * 0x9e3779b9u;
        dry.worldX = (float)(index * 193);
        dry.worldZ = (float)(index * -127);
        dry.simulationTime = (double)index * 7.25;
        dry.moisture = 0.08f;
        dry.cloudPotential = 0.10f;
        WeatherFieldInput wet = dry;
        wet.moisture = 0.92f;
        wet.cloudPotential = 0.86f;
        WeatherFieldInput calm = wet;
        calm.windStrength = 0.05f;
        WeatherFieldInput windy = wet;
        windy.windStrength = 0.95f;
        dryPrecipitation += WeatherFieldSampleAt(&dry).precipitation;
        wetPrecipitation += WeatherFieldSampleAt(&wet).precipitation;
        calmStorm += WeatherFieldSampleAt(&calm).storm;
        windyStorm += WeatherFieldSampleAt(&windy).storm;
    }
    dryPrecipitation /= (float)count;
    wetPrecipitation /= (float)count;
    calmStorm /= (float)count;
    windyStorm /= (float)count;
    assert(wetPrecipitation > dryPrecipitation + 0.45f);
    assert(windyStorm > calmStorm + 0.05f);
}

static void TestRandomizedBoundsAndReplay(void)
{
    uint32_t state = 0x43e21bafu;
    for (int index = 0; index < 20000; index++) {
        state = state * 1664525u + 1013904223u;
        WeatherFieldInput input = TemperateInput();
        input.seed = state;
        input.simulationTime = (double)(state % 10000000u) * 0.125;
        input.worldX = (float)(int32_t)(state ^ (state << 13));
        state = state * 1664525u + 1013904223u;
        input.worldZ = (float)(int32_t)(state ^ (state >> 7));
        input.temperatureK = 160.0f + (float)(state & 1023u) * 0.36f;
        input.moisture = (float)(state & 255u) / 255.0f;
        input.cloudPotential = (float)((state >> 8) & 255u) / 255.0f;
        input.windStrength = (float)((state >> 16) & 255u) / 255.0f;
        input.prevailingWindAngle = (float)(state & 4095u) * 0.0015f;
        WeatherFieldSample first = WeatherFieldSampleAt(&input);
        WeatherFieldSample replay = WeatherFieldSampleAt(&input);
        AssertValid(first);
        assert(memcmp(&first, &replay, sizeof(first)) == 0);
    }
}

static void TestInvalidInputReturnsClearWeather(void)
{
    WeatherFieldInput input = TemperateInput();
    input.prevailingWindAngle = NAN;
    WeatherFieldSample sample = WeatherFieldSampleAt(&input);
    assert(sample.cloudCover == 0.0f);
    assert(sample.precipitation == 0.0f);
    assert(sample.rain == 0.0f);
    assert(sample.snow == 0.0f);
    assert(sample.storm == 0.0f);
    assert(sample.wind == 0.0f);
}

int main(void)
{
    TestDeterministicSampling();
    TestSkyFactorUsesLocalWeather();
    TestSpatialAndTemporalContinuity();
    TestWeatherVariesAcrossSpaceAndTime();
    TestTemperatureSelectsRainOrSnow();
    TestMixedPrecipitationAndPhysicalGates();
    TestPhenomenonNames();
    TestClimateControlsPrecipitationAndStorms();
    TestRandomizedBoundsAndReplay();
    TestInvalidInputReturnsClearWeather();
    puts("weather_model tests passed");
    return 0;
}
