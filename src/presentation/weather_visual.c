#include "presentation/weather_visual.h"

#include <math.h>

static float WeatherVisualClamp(float value)
{
    if (!isfinite(value) || value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static float WeatherVisualSmoothStep(float edge0, float edge1, float value)
{
    float t = WeatherVisualClamp((value - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

float WeatherCloudVerticalDensity(float normalizedHeight)
{
    if (!isfinite(normalizedHeight) || normalizedHeight <= 0.0f ||
        normalizedHeight >= 1.0f) {
        return 0.0f;
    }
    float bottom = WeatherVisualSmoothStep(0.0f, 0.16f, normalizedHeight);
    float top = 1.0f - WeatherVisualSmoothStep(0.64f, 1.0f,
                                               normalizedHeight);
    float baseWeight = 0.72f + (1.0f - normalizedHeight) * 0.36f;
    return WeatherVisualClamp(bottom * top * baseWeight);
}

static bool WeatherVisualSampleIsFinite(WeatherFieldSample sample)
{
    if (!isfinite(sample.cloudCover) || !isfinite(sample.precipitation) ||
        !isfinite(sample.rain) || !isfinite(sample.snow) ||
        !isfinite(sample.sleet) || !isfinite(sample.freezingRain) ||
        !isfinite(sample.hail) || !isfinite(sample.storm) ||
        !isfinite(sample.lightning) || !isfinite(sample.fog) ||
        !isfinite(sample.frost) || !isfinite(sample.dust) ||
        !isfinite(sample.wind) || !isfinite(sample.gust) ||
        !isfinite(sample.visibility) || !isfinite(sample.rainbow) ||
        !isfinite(sample.aurora) ||
        !isfinite(sample.temperatureAnomalyK)) {
        return false;
    }
    for (int genus = WEATHER_CLOUD_GENUS_NONE;
         genus < WEATHER_CLOUD_GENUS_COUNT; genus++) {
        if (!isfinite(sample.cloudGenusCoverage[genus]) ||
            !isfinite(sample.cloudGenusBaseHeight[genus]) ||
            !isfinite(sample.cloudGenusThickness[genus])) {
            return false;
        }
    }
    return true;
}

static float WeatherCloudRenderHeight(float meters)
{
    meters = fmaxf(meters, 0.0f);
    if (meters <= 2000.0f) return 14.0f + meters * (32.0f / 2000.0f);
    if (meters <= 6000.0f) {
        return 46.0f + (meters - 2000.0f) * (32.0f / 4000.0f);
    }
    return fminf(78.0f + (meters - 6000.0f) * (34.0f / 5000.0f),
                 116.0f);
}

static float WeatherCloudRenderThickness(float meters)
{
    return 4.0f + WeatherVisualClamp(meters / 12000.0f) * 86.0f;
}

static WeatherCloudVisualLayer WeatherCloudLayerProfile(
    WeatherCloudGenus genus, float coverage, float physicalBase,
    float physicalThickness, float aggregateOpacity)
{
    WeatherCloudVisualLayer layer = {
        .genus = genus,
        .coverage = WeatherVisualClamp(coverage),
        .baseHeight = WeatherCloudRenderHeight(physicalBase),
        .thickness = WeatherCloudRenderThickness(physicalThickness),
        .opacity = aggregateOpacity,
        .noiseScale = 1.0f,
        .stretch = 0.0f,
        .cellularity = 0.0f,
        .verticalDevelopment = 0.12f,
        .anvil = 0.0f,
        .driftScale = 1.0f
    };
    float opacityScale = 0.72f;
    switch (genus) {
    case WEATHER_CLOUD_GENUS_CIRRUS:
        layer.noiseScale = 0.76f; layer.stretch = 0.92f;
        layer.verticalDevelopment = 0.03f; layer.driftScale = 1.42f;
        opacityScale = 0.34f;
        break;
    case WEATHER_CLOUD_GENUS_CIRROCUMULUS:
        layer.noiseScale = 1.85f; layer.stretch = 0.28f;
        layer.cellularity = 0.92f; layer.verticalDevelopment = 0.08f;
        layer.driftScale = 1.34f; opacityScale = 0.46f;
        break;
    case WEATHER_CLOUD_GENUS_CIRROSTRATUS:
        layer.noiseScale = 0.46f; layer.stretch = 0.48f;
        layer.verticalDevelopment = 0.02f; layer.driftScale = 1.28f;
        opacityScale = 0.38f;
        break;
    case WEATHER_CLOUD_GENUS_ALTOCUMULUS:
        layer.noiseScale = 1.38f; layer.stretch = 0.18f;
        layer.cellularity = 0.68f; layer.verticalDevelopment = 0.20f;
        layer.driftScale = 1.16f; opacityScale = 0.66f;
        break;
    case WEATHER_CLOUD_GENUS_ALTOSTRATUS:
        layer.noiseScale = 0.58f; layer.stretch = 0.30f;
        layer.verticalDevelopment = 0.05f; layer.driftScale = 1.08f;
        opacityScale = 0.68f;
        break;
    case WEATHER_CLOUD_GENUS_NIMBOSTRATUS:
        layer.noiseScale = 0.48f; layer.stretch = 0.20f;
        layer.verticalDevelopment = 0.18f; layer.driftScale = 0.92f;
        opacityScale = 1.0f;
        break;
    case WEATHER_CLOUD_GENUS_STRATOCUMULUS:
        layer.noiseScale = 0.92f; layer.stretch = 0.10f;
        layer.cellularity = 0.42f; layer.verticalDevelopment = 0.20f;
        layer.driftScale = 0.86f; opacityScale = 0.78f;
        break;
    case WEATHER_CLOUD_GENUS_STRATUS:
        layer.noiseScale = 0.38f; layer.stretch = 0.24f;
        layer.verticalDevelopment = 0.01f; layer.driftScale = 0.72f;
        opacityScale = 0.74f;
        break;
    case WEATHER_CLOUD_GENUS_CUMULUS:
        layer.noiseScale = 0.84f; layer.cellularity = 0.18f;
        layer.verticalDevelopment = 0.62f; layer.driftScale = 0.82f;
        opacityScale = 0.86f;
        break;
    case WEATHER_CLOUD_GENUS_CUMULONIMBUS:
        layer.noiseScale = 0.62f; layer.cellularity = 0.12f;
        layer.verticalDevelopment = 1.0f; layer.anvil = 1.0f;
        layer.driftScale = 0.76f; opacityScale = 1.0f;
        break;
    case WEATHER_CLOUD_GENUS_NONE:
    case WEATHER_CLOUD_GENUS_COUNT:
        break;
    }
    layer.opacity = WeatherVisualClamp(
        aggregateOpacity * opacityScale * (0.52f + layer.coverage * 0.68f));
    return layer;
}

static WeatherCloudGenus WeatherCloudBestInRange(
    const WeatherFieldSample *sample, WeatherCloudGenus first,
    WeatherCloudGenus last)
{
    WeatherCloudGenus best = WEATHER_CLOUD_GENUS_NONE;
    float bestCoverage = 0.069f;
    for (int index = (int)first; index <= (int)last; index++) {
        float coverage = sample->cloudGenusCoverage[index];
        if (coverage > bestCoverage) {
            bestCoverage = coverage;
            best = (WeatherCloudGenus)index;
        }
    }
    return best;
}

static void WeatherCloudAddLayer(WeatherVisualState *state,
                                 const WeatherFieldSample *sample,
                                 WeatherCloudGenus genus)
{
    if (genus <= WEATHER_CLOUD_GENUS_NONE ||
        genus >= WEATHER_CLOUD_GENUS_COUNT ||
        state->cloudLayerCount >= WEATHER_VISUAL_CLOUD_LAYER_CAPACITY) {
        return;
    }
    float coverage = sample->cloudGenusCoverage[genus];
    if (coverage < 0.07f) return;
    WeatherCloudVisualLayer layer = WeatherCloudLayerProfile(
        genus, coverage, sample->cloudGenusBaseHeight[genus],
        sample->cloudGenusThickness[genus], state->cloudOpacity);
    unsigned insert = state->cloudLayerCount;
    while (insert > 0u &&
           state->cloudLayers[insert - 1u].baseHeight > layer.baseHeight) {
        state->cloudLayers[insert] = state->cloudLayers[insert - 1u];
        insert--;
    }
    state->cloudLayers[insert] = layer;
    state->cloudLayerCount++;
}

static void WeatherCloudBuildLayers(WeatherVisualState *state,
                                    const WeatherFieldSample *sample)
{
    state->dominantCloudGenus = sample->dominantCloudGenus;
    WeatherCloudGenus high = WeatherCloudBestInRange(
        sample, WEATHER_CLOUD_GENUS_CIRRUS,
        WEATHER_CLOUD_GENUS_CIRROSTRATUS);
    WeatherCloudGenus middle = WeatherCloudBestInRange(
        sample, WEATHER_CLOUD_GENUS_ALTOCUMULUS,
        WEATHER_CLOUD_GENUS_ALTOSTRATUS);
    WeatherCloudGenus low = WeatherCloudBestInRange(
        sample, WEATHER_CLOUD_GENUS_NIMBOSTRATUS,
        WEATHER_CLOUD_GENUS_CUMULONIMBUS);
    if (low == WEATHER_CLOUD_GENUS_CUMULONIMBUS) {
        float coverage = sample->cloudGenusCoverage[low];
        WeatherCloudVisualLayer cumulonimbus = WeatherCloudLayerProfile(
            low, coverage, sample->cloudGenusBaseHeight[low],
            sample->cloudGenusThickness[low], state->cloudOpacity);
        if (cumulonimbus.opacity >= 0.72f) {
            WeatherCloudAddLayer(state, sample, low);
            return;
        }
    }
    WeatherCloudAddLayer(state, sample, high);
    WeatherCloudAddLayer(state, sample, middle);
    WeatherCloudAddLayer(state, sample, low);

    if (state->cloudLayerCount == 0u && state->cloudCover >= 0.03f) {
        WeatherCloudGenus fallback = sample->storm >= 0.30f ?
            WEATHER_CLOUD_GENUS_CUMULONIMBUS :
            (sample->precipitation >= 0.30f ?
                 WEATHER_CLOUD_GENUS_NIMBOSTRATUS :
                 WEATHER_CLOUD_GENUS_STRATOCUMULUS);
        float physicalBase = sample->cloudBaseHeight > 0.0f ?
            sample->cloudBaseHeight : 1200.0f;
        float physicalThickness = fallback == WEATHER_CLOUD_GENUS_CUMULONIMBUS ?
            9000.0f : (fallback == WEATHER_CLOUD_GENUS_NIMBOSTRATUS ?
                           4300.0f : 1250.0f);
        state->dominantCloudGenus = fallback;
        state->cloudLayers[0] = WeatherCloudLayerProfile(
            fallback, state->cloudCover, physicalBase, physicalThickness,
            state->cloudOpacity);
        state->cloudLayerCount = 1u;
    }
}

WeatherVisualState WeatherVisualStateEvaluate(const WeatherVisualInput *input)
{
    WeatherVisualState state = { .visibility = 1.0f };
    if (!input || !input->atmosphereActive ||
        !WeatherVisualSampleIsFinite(input->weather) ||
        !isfinite(input->atmosphereDensity) || !isfinite(input->daylight) ||
        !isfinite(input->windAngle)) {
        return state;
    }

    float density = WeatherVisualClamp(input->atmosphereDensity);
    if (density <= 0.01f) return state;

    float cloud = WeatherVisualClamp(input->weather.cloudCover);
    float precipitation = WeatherVisualClamp(input->weather.precipitation);
    float storm = WeatherVisualClamp(input->weather.storm);
    float wind = WeatherVisualClamp(input->weather.wind);
    float daylight = WeatherVisualClamp(input->daylight);
    float snow = WeatherVisualClamp(input->weather.snow);
    float sleet = WeatherVisualClamp(input->weather.sleet);
    float freezingRain = WeatherVisualClamp(input->weather.freezingRain);
    float hail = WeatherVisualClamp(input->weather.hail);

    state.active = true;
    state.atmosphereDensity = density;
    state.daylight = daylight;
    state.cloudCover = WeatherVisualClamp(
        cloud * (0.44f + density * 0.56f) + precipitation * 0.12f);
    float physicalCloudBase = input->weather.cloudBaseHeight > 0.0f ?
        14.0f + WeatherVisualClamp(input->weather.cloudBaseHeight / 2500.0f) *
        38.0f : 46.0f;
    state.cloudBaseHeight = physicalCloudBase - state.cloudCover * 8.0f -
                            precipitation * 7.0f - storm * 5.0f;
    if (state.cloudBaseHeight < 14.0f) state.cloudBaseHeight = 14.0f;
    state.cloudThickness = 5.0f + state.cloudCover * 12.0f +
                           precipitation * 7.0f + storm * 5.0f;
    state.cloudOpacity = WeatherVisualClamp(
        state.cloudCover * (0.42f + density * 0.42f) + precipitation * 0.18f);
    WeatherCloudBuildLayers(&state, &input->weather);
    state.precipitationVeil = WeatherVisualClamp(
        precipitation * (0.42f + storm * 0.36f) * density);
    state.dustDensity = WeatherVisualClamp(input->weather.dust * density);
    state.fogDensity = WeatherVisualClamp(
        (input->weather.fog * 0.68f + state.cloudCover * 0.07f +
         precipitation * 0.24f + storm * 0.16f) *
        density * (0.80f + (1.0f - daylight) * 0.20f));
    state.stormDarkening = WeatherVisualClamp(
        state.cloudCover * 0.20f + precipitation * 0.34f + storm * 0.42f);
    state.visibility = fminf(WeatherVisualClamp(input->weather.visibility),
        WeatherVisualClamp(1.0f - state.fogDensity * 0.76f -
                           state.precipitationVeil * 0.28f -
                           state.dustDensity * 0.70f));
    state.windDrift = WeatherVisualClamp(wind * (0.42f + storm * 0.58f));
    state.windAngle = input->windAngle;
    state.snowFraction = precipitation > 0.001f ?
                             WeatherVisualClamp(snow / precipitation) : 0.0f;
    state.sleetFraction = precipitation > 0.001f ?
        WeatherVisualClamp(sleet / precipitation) : 0.0f;
    state.freezingRainFraction = precipitation > 0.001f ?
        WeatherVisualClamp(freezingRain / precipitation) : 0.0f;
    state.hailFraction = precipitation > 0.001f ?
        WeatherVisualClamp(hail / precipitation) : 0.0f;
    state.frost = WeatherVisualClamp(input->weather.frost);
    state.lightningIntensity = WeatherVisualClamp(input->weather.lightning);
    state.rainbowStrength = WeatherVisualClamp(
        input->weather.rainbow * daylight * density);
    state.auroraStrength = WeatherVisualClamp(
        input->weather.aurora * (1.0f - daylight) * density);
    state.temperatureAnomalyK = input->weather.temperatureAnomalyK;
    return state;
}
