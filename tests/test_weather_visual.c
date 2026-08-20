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
            .wind = 0.42f,
            .gust = 0.48f,
            .visibility = 0.88f
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
    AssertUnit(state.dustDensity);
    AssertUnit(state.visibility);
    AssertUnit(state.precipitationVeil);
    AssertUnit(state.stormDarkening);
    AssertUnit(state.windDrift);
    AssertUnit(state.snowFraction);
    AssertUnit(state.sleetFraction);
    AssertUnit(state.freezingRainFraction);
    AssertUnit(state.hailFraction);
    AssertUnit(state.frost);
    AssertUnit(state.lightningIntensity);
    AssertUnit(state.rainbowStrength);
    AssertUnit(state.auroraStrength);
    assert(state.cloudLayerCount <= WEATHER_VISUAL_CLOUD_LAYER_CAPACITY);
    for (unsigned index = 0u; index < state.cloudLayerCount; index++) {
        WeatherCloudVisualLayer layer = state.cloudLayers[index];
        assert(layer.genus > WEATHER_CLOUD_GENUS_NONE);
        assert(layer.genus < WEATHER_CLOUD_GENUS_COUNT);
        AssertUnit(layer.coverage);
        AssertUnit(layer.opacity);
        AssertUnit(layer.stretch);
        AssertUnit(layer.cellularity);
        AssertUnit(layer.verticalDevelopment);
        AssertUnit(layer.anvil);
        assert(isfinite(layer.baseHeight) && layer.baseHeight >= 0.0f);
        assert(isfinite(layer.thickness) && layer.thickness >= 4.0f);
        assert(isfinite(layer.noiseScale) && layer.noiseScale > 0.0f);
        assert(isfinite(layer.driftScale) && layer.driftScale > 0.0f);
        if (index > 0u) {
            assert(state.cloudLayers[index - 1u].baseHeight <= layer.baseHeight);
        }
    }
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
    clearInput.weather = (WeatherFieldSample){
        .cloudCover = 0.12f, .wind = 0.18f, .visibility = 1.0f
    };
    WeatherVisualInput stormInput = clearInput;
    stormInput.weather = (WeatherFieldSample){
        .cloudCover = 0.94f,
        .precipitation = 0.86f,
        .rain = 0.86f,
        .storm = 0.78f,
        .wind = 0.92f,
        .gust = 1.0f,
        .visibility = 0.42f
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
    assert(WeatherCloudVerticalDensity(0.72f) >
           WeatherCloudVerticalDensity(0.92f));
}

static void TestCloudMotionDoesNotReprojectHistory(void)
{
    WeatherCloudMotionState recent = { 0 };
    WeatherCloudMotionState old = { 0 };
    WeatherCloudMotionAdvance(&recent, 3.0, 0.4f, 3.0f);
    WeatherCloudMotionAdvance(&old, 100000.0, 0.4f, 3.0f);
    WeatherCloudMotionAdvance(&recent, 3.5, 0.4f, 3.0f);
    WeatherCloudMotionAdvance(&old, 100000.5, 0.4f, 3.0f);
    assert(fabs(recent.offsetX - old.offsetX) < 0.000001);
    assert(fabs(recent.offsetZ - old.offsetZ) < 0.000001);

    double beforeX = old.offsetX;
    double beforeZ = old.offsetZ;
    WeatherCloudMotionAdvance(&old, 100000.5, 0.4025f, 3.0f);
    assert(old.offsetX == beforeX);
    assert(old.offsetZ == beforeZ);
    WeatherCloudMotionAdvance(&old, 100000.5 + 1.0 / 60.0, 0.4025f, 3.0f);
    assert(hypot(old.offsetX - beforeX, old.offsetZ - beforeZ) < 0.051);

    beforeX = old.offsetX;
    beforeZ = old.offsetZ;
    WeatherCloudMotionAdvance(&old, 2.0, -3.13f, 3.0f);
    assert(old.offsetX == beforeX);
    assert(old.offsetZ == beforeZ);
}

static void TestCloudAltitudeReferenceIsContinuous(void)
{
    float left = WeatherCloudAltitudeReference(
        12.0f, 10.0f, 30.0f, 18.0f, 38.0f, 0.999f, 0.25f);
    float right = WeatherCloudAltitudeReference(
        12.0f, 30.0f, 50.0f, 38.0f, 58.0f, 0.001f, 0.25f);
    assert(fabsf(right - left) < 0.05f);
    assert(WeatherCloudAltitudeReference(
        20.0f, 4.0f, 6.0f, 8.0f, 10.0f, 0.5f, 0.5f) == 20.0f);
    assert(WeatherCloudAltitudeReference(
        -1.0f, 4.0f, 6.0f, 8.0f, 10.0f, 0.5f, 0.5f) == 7.0f);
}

static void TestLayerSelectionAndProfiles(void)
{
    WeatherVisualInput input = TemperateInput();
    input.weather.cloudCover = 0.82f;
    input.weather.dominantCloudGenus = WEATHER_CLOUD_GENUS_CUMULUS;
    input.weather.cloudGenera =
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_CIRRUS) |
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_ALTOCUMULUS) |
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_CUMULUS);
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_CIRRUS] = 0.34f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_CIRRUS] = 7600.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_CIRRUS] = 1350.0f;
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_ALTOCUMULUS] = 0.48f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_ALTOCUMULUS] = 3200.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_ALTOCUMULUS] = 1500.0f;
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_CUMULUS] = 0.72f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_CUMULUS] = 1100.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_CUMULUS] = 3200.0f;

    WeatherVisualState state = WeatherVisualStateEvaluate(&input);
    AssertValid(state);
    assert(state.cloudLayerCount == 3u);
    assert(state.cloudLayers[0].genus == WEATHER_CLOUD_GENUS_CUMULUS);
    assert(state.cloudLayers[1].genus == WEATHER_CLOUD_GENUS_ALTOCUMULUS);
    assert(state.cloudLayers[2].genus == WEATHER_CLOUD_GENUS_CIRRUS);
    assert(state.cloudLayers[0].verticalDevelopment >
           state.cloudLayers[1].verticalDevelopment);
    assert(state.cloudLayers[0].verticalDevelopment >= 0.70f);
    assert(state.cloudLayers[0].cellularity >= 0.35f);
    assert(state.cloudLayers[1].cellularity >
           state.cloudLayers[0].cellularity);
    assert(state.cloudLayers[2].stretch > state.cloudLayers[1].stretch);
    assert(state.cloudLayers[2].opacity < state.cloudLayers[0].opacity);
}

static void TestAllCloudGenusProfiles(void)
{
    for (int index = WEATHER_CLOUD_GENUS_CIRRUS;
         index < WEATHER_CLOUD_GENUS_COUNT; index++) {
        WeatherCloudGenus genus = (WeatherCloudGenus)index;
        WeatherVisualInput input = TemperateInput();
        input.weather.cloudCover = 0.74f;
        input.weather.dominantCloudGenus = genus;
        input.weather.cloudGenera = WEATHER_CLOUD_GENUS_FLAG(genus);
        input.weather.cloudGenusCoverage[genus] = 0.74f;
        input.weather.cloudGenusBaseHeight[genus] =
            genus <= WEATHER_CLOUD_GENUS_CIRROSTRATUS ? 7000.0f :
            (genus <= WEATHER_CLOUD_GENUS_ALTOSTRATUS ? 3000.0f : 1000.0f);
        input.weather.cloudGenusThickness[genus] =
            genus == WEATHER_CLOUD_GENUS_CUMULONIMBUS ? 10000.0f : 1800.0f;
        WeatherVisualState state = WeatherVisualStateEvaluate(&input);
        AssertValid(state);
        assert(state.cloudLayerCount == 1u);
        assert(state.cloudLayers[0].genus == genus);
    }

    WeatherVisualInput storm = TemperateInput();
    storm.weather.cloudCover = 0.94f;
    storm.weather.storm = 0.82f;
    storm.weather.precipitation = 0.84f;
    storm.weather.rain = 0.84f;
    WeatherVisualState fallback = WeatherVisualStateEvaluate(&storm);
    assert(fallback.cloudLayerCount == 1u);
    assert(fallback.dominantCloudGenus == WEATHER_CLOUD_GENUS_CUMULONIMBUS);
    assert(fallback.cloudLayers[0].anvil == 1.0f);
}

static void TestOpaqueCumulonimbusSuppressesOtherLayers(void)
{
    WeatherVisualInput input = TemperateInput();
    input.weather.cloudCover = 0.94f;
    input.weather.precipitation = 0.84f;
    input.weather.rain = 0.84f;
    input.weather.storm = 0.82f;
    input.weather.dominantCloudGenus = WEATHER_CLOUD_GENUS_CUMULONIMBUS;
    input.weather.cloudGenera =
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_CIRRUS) |
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_ALTOSTRATUS) |
        WEATHER_CLOUD_GENUS_FLAG(WEATHER_CLOUD_GENUS_CUMULONIMBUS);
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_CIRRUS] = 0.40f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_CIRRUS] = 7600.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_CIRRUS] = 1350.0f;
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_ALTOSTRATUS] = 0.54f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_ALTOSTRATUS] =
        2600.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_ALTOSTRATUS] =
        2600.0f;
    input.weather.cloudGenusCoverage[WEATHER_CLOUD_GENUS_CUMULONIMBUS] =
        0.92f;
    input.weather.cloudGenusBaseHeight[WEATHER_CLOUD_GENUS_CUMULONIMBUS] =
        800.0f;
    input.weather.cloudGenusThickness[WEATHER_CLOUD_GENUS_CUMULONIMBUS] =
        10000.0f;

    WeatherVisualState state = WeatherVisualStateEvaluate(&input);
    AssertValid(state);
    assert(state.cloudLayerCount == 1u);
    assert(state.cloudLayers[0].genus == WEATHER_CLOUD_GENUS_CUMULONIMBUS);
    assert(state.cloudLayers[0].opacity >= 0.72f);
}

int main(void)
{
    TestDeterministicAndBounded();
    TestStormReducesVisibility();
    TestSnowAndAtmosphereFallbacks();
    TestCloudVerticalDensity();
    TestCloudMotionDoesNotReprojectHistory();
    TestCloudAltitudeReferenceIsContinuous();
    TestLayerSelectionAndProfiles();
    TestAllCloudGenusProfiles();
    TestOpaqueCumulonimbusSuppressesOtherLayers();
    puts("weather visual tests passed");
    return 0;
}
