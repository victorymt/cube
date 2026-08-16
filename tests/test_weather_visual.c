#include "presentation/weather_visual.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

static WeatherVisualInput TemperateInput(void)
{
    return (WeatherVisualInput){
        .weather = {
            .cloudCover = 0.56f,
            .precipitation = 0.24f,
            .rain = 0.24f,
            .storm = 0.10f,
            .wind = 0.42f
        },
        .atmosphereDensity = 0.68f,
        .daylight = 0.72f,
        .windAngle = 0.73f,
        .atmosphereActive = true
    };
}

static void AssertUnit(float value)
{
    assert(isfinite(value));
    assert(value >= 0.0f);
    assert(value <= 1.0f);
}

static void AssertValid(WeatherVisualState state)
{
    assert(isfinite(state.atmosphereDensity));
    AssertUnit(state.daylight);
    assert(isfinite(state.cloudCover));
    assert(isfinite(state.cloudBaseHeight));
    assert(isfinite(state.cloudThickness));
    assert(isfinite(state.windAngle));
    AssertUnit(state.cloudOpacity);
    AssertUnit(state.fogDensity);
    AssertUnit(state.visibility);
    AssertUnit(state.precipitationVeil);
    AssertUnit(state.stormDarkening);
    AssertUnit(state.windDrift);
    AssertUnit(state.snowFraction);
}

static void TestDeterministicAndBounded(void)
{
    WeatherVisualInput input = TemperateInput();
    WeatherVisualState first = WeatherVisualStateEvaluate(&input);
    WeatherVisualState replay = WeatherVisualStateEvaluate(&input);
    assert(first.active == replay.active);
    assert(first.atmosphereDensity == replay.atmosphereDensity);
    assert(first.daylight == replay.daylight);
    assert(first.cloudCover == replay.cloudCover);
    assert(first.cloudBaseHeight == replay.cloudBaseHeight);
    assert(first.cloudThickness == replay.cloudThickness);
    assert(first.cloudOpacity == replay.cloudOpacity);
    assert(first.fogDensity == replay.fogDensity);
    assert(first.visibility == replay.visibility);
    assert(first.precipitationVeil == replay.precipitationVeil);
    assert(first.stormDarkening == replay.stormDarkening);
    assert(first.windDrift == replay.windDrift);
    assert(first.windAngle == replay.windAngle);
    assert(first.snowFraction == replay.snowFraction);
    assert(first.active);
    AssertValid(first);
}

static void TestStormReducesVisibility(void)
{
    WeatherVisualInput clearInput = TemperateInput();
    clearInput.weather = (WeatherFieldSample){ .cloudCover = 0.12f, .wind = 0.18f };
    WeatherVisualInput stormInput = clearInput;
    stormInput.weather = (WeatherFieldSample){
        .cloudCover = 0.94f,
        .precipitation = 0.86f,
        .rain = 0.86f,
        .storm = 0.78f,
        .wind = 0.92f
    };
    WeatherVisualState clear = WeatherVisualStateEvaluate(&clearInput);
    WeatherVisualState storm = WeatherVisualStateEvaluate(&stormInput);
    assert(storm.cloudOpacity > clear.cloudOpacity);
    assert(storm.fogDensity > clear.fogDensity);
    assert(storm.precipitationVeil > clear.precipitationVeil);
    assert(storm.visibility < clear.visibility);
    assert(storm.cloudBaseHeight < clear.cloudBaseHeight);
}

static void TestSnowAndAtmosphereFallbacks(void)
{
    WeatherVisualInput input = TemperateInput();
    input.weather.precipitation = 0.72f;
    input.weather.rain = 0.0f;
    input.weather.snow = 0.72f;
    WeatherVisualState snow = WeatherVisualStateEvaluate(&input);
    assert(fabsf(snow.snowFraction - 1.0f) < 0.00001f);

    input.atmosphereActive = false;
    WeatherVisualState noAtmosphere = WeatherVisualStateEvaluate(&input);
    assert(!noAtmosphere.active);
    assert(noAtmosphere.visibility == 1.0f);
    assert(noAtmosphere.fogDensity == 0.0f);

    input = TemperateInput();
    input.weather.wind = NAN;
    WeatherVisualState invalid = WeatherVisualStateEvaluate(&input);
    assert(!invalid.active);
    assert(invalid.visibility == 1.0f);
}

static void TestCloudVerticalDensity(void)
{
    assert(WeatherCloudVerticalDensity(-0.1f) == 0.0f);
    assert(WeatherCloudVerticalDensity(0.0f) == 0.0f);
    assert(WeatherCloudVerticalDensity(1.0f) == 0.0f);
    assert(WeatherCloudVerticalDensity(NAN) == 0.0f);
    float lowerEdge = WeatherCloudVerticalDensity(0.04f);
    float body = WeatherCloudVerticalDensity(0.32f);
    float upperEdge = WeatherCloudVerticalDensity(0.92f);
    assert(lowerEdge > 0.0f && lowerEdge < body);
    assert(upperEdge > 0.0f && upperEdge < body);
    AssertUnit(body);
}

int main(void)
{
    TestDeterministicAndBounded();
    TestStormReducesVisibility();
    TestSnowAndAtmosphereFallbacks();
    TestCloudVerticalDensity();
    puts("weather visual tests passed");
    return 0;
}
